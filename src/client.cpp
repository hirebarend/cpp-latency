#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::size_t iterations = 500;
    std::size_t warmup = 50;
    std::uint32_t interval_ms = 10;
};

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool set_tcp_nodelay(int fd) {
    int flag = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
}

int connect_to(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    int gai = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        std::fprintf(stderr, "[client] getaddrinfo(%s): %s\n", host.c_str(), ::gai_strerror(gai));
        return -1;
    }

    int fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        std::fprintf(stderr, "[client] failed to connect to %s:%u: %s\n",
                     host.c_str(), port, std::strerror(errno));
        return -1;
    }

    set_tcp_nodelay(fd);
    return fd;
}

bool write_all(int fd, std::string_view data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

bool read_line(int fd, std::string& buf, std::string& line) {
    while (true) {
        auto newline = buf.find('\n');
        if (newline != std::string::npos) {
            line.assign(buf, 0, newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, newline + 1);
            return true;
        }
        char tmp[1024];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        buf.append(tmp, static_cast<std::size_t>(n));
    }
}

struct Stats {
    double min;
    double max;
    double mean;
    double median;
    double stddev;
    double p95;
    double p99;
};

double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted.front();
    double rank = p * static_cast<double>(sorted.size() - 1);
    auto lo = static_cast<std::size_t>(std::floor(rank));
    auto hi = static_cast<std::size_t>(std::ceil(rank));
    double frac = rank - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

Stats compute_stats(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    Stats s{};
    s.min = samples.front();
    s.max = samples.back();
    double sum = 0.0;
    for (double v : samples) sum += v;
    s.mean = sum / static_cast<double>(samples.size());

    double sq_sum = 0.0;
    for (double v : samples) {
        double d = v - s.mean;
        sq_sum += d * d;
    }
    s.stddev = std::sqrt(sq_sum / static_cast<double>(samples.size()));
    s.median = percentile(samples, 0.50);
    s.p95 = percentile(samples, 0.95);
    s.p99 = percentile(samples, 0.99);
    return s;
}

void print_stats(const Stats& s, std::size_t count) {
    std::printf("\n=== Latency statistics (%zu samples) ===\n", count);
    std::printf("  min     : %.3f ms\n", s.min);
    std::printf("  max     : %.3f ms\n", s.max);
    std::printf("  mean    : %.3f ms\n", s.mean);
    std::printf("  median  : %.3f ms\n", s.median);
    std::printf("  stddev  : %.3f ms\n", s.stddev);
    std::printf("  p95     : %.3f ms\n", s.p95);
    std::printf("  p99     : %.3f ms\n", s.p99);
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s [--host H] [--port P] [--iterations N] [--warmup N] [--interval-ms N]\n",
        prog);
}

bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto need = [&](std::string_view name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[client] missing value for %.*s\n",
                             static_cast<int>(name.size()), name.data());
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--host") {
            const char* v = need(a); if (!v) return false;
            opts.host = v;
        } else if (a == "--port") {
            const char* v = need(a); if (!v) return false;
            opts.port = static_cast<std::uint16_t>(std::atoi(v));
        } else if (a == "--iterations") {
            const char* v = need(a); if (!v) return false;
            opts.iterations = static_cast<std::size_t>(std::atoll(v));
        } else if (a == "--warmup") {
            const char* v = need(a); if (!v) return false;
            opts.warmup = static_cast<std::size_t>(std::atoll(v));
        } else if (a == "--interval-ms") {
            const char* v = need(a); if (!v) return false;
            opts.interval_ms = static_cast<std::uint32_t>(std::atoi(v));
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return false;
        } else {
            std::fprintf(stderr, "[client] unknown arg: %.*s\n",
                         static_cast<int>(a.size()), a.data());
            usage(argv[0]);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) return 2;
    if (opts.iterations == 0) {
        std::fprintf(stderr, "[client] iterations must be > 0\n");
        return 2;
    }

    std::printf("[client] connecting to %s:%u\n", opts.host.c_str(), opts.port);
    int fd = connect_to(opts.host, opts.port);
    if (fd < 0) return 1;

    std::printf("[client] warmup=%zu iterations=%zu interval=%ums\n",
                opts.warmup, opts.iterations, opts.interval_ms);
    std::fflush(stdout);

    std::vector<double> rtt_samples;
    rtt_samples.reserve(opts.iterations);

    std::string recv_buf;
    std::string line;

    const std::size_t total = opts.warmup + opts.iterations;
    for (std::size_t i = 0; i < total; ++i) {
        auto t_send = std::chrono::steady_clock::now();
        std::int64_t ts_ms = now_ms();
        std::string msg = "PING " + std::to_string(ts_ms) + "\n";
        if (!write_all(fd, msg)) {
            std::fprintf(stderr, "[client] send failed at iter %zu: %s\n", i, std::strerror(errno));
            ::close(fd);
            return 1;
        }
        if (!read_line(fd, recv_buf, line)) {
            std::fprintf(stderr, "[client] connection closed at iter %zu\n", i);
            ::close(fd);
            return 1;
        }
        auto t_recv = std::chrono::steady_clock::now();

        if (line.substr(0, 4) != "PONG") {
            std::fprintf(stderr, "[client] unexpected response: '%s'\n", line.c_str());
            ::close(fd);
            return 1;
        }

        double rtt_ms = std::chrono::duration<double, std::milli>(t_recv - t_send).count();

        if (i < opts.warmup) {
            if (i == 0) { std::printf("[client] warmup...\n"); std::fflush(stdout); }
        } else {
            rtt_samples.push_back(rtt_ms);
            std::size_t measured = i - opts.warmup + 1;
            if (measured == 1 || measured % 50 == 0 || measured == opts.iterations) {
                std::printf("[client] %zu/%zu rtt=%.3f ms\n", measured, opts.iterations, rtt_ms);
                std::fflush(stdout);
            }
        }

        if (opts.interval_ms > 0 && i + 1 < total) {
            std::this_thread::sleep_for(std::chrono::milliseconds(opts.interval_ms));
        }
    }

    ::close(fd);

    auto stats = compute_stats(std::move(rtt_samples));
    print_stats(stats, opts.iterations);
    return 0;
}
