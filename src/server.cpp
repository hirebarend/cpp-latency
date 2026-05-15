#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::uint16_t kDefaultPort = 9000;
constexpr std::size_t kReadBufferSize = 4096;

std::atomic<bool> g_shutdown_requested{false};

void handle_signal(int /*signum*/) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool set_tcp_nodelay(int fd) {
    int flag = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
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

void handle_client(int client_fd, sockaddr_in peer) {
    set_tcp_nodelay(client_fd);

    char addr_buf[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &peer.sin_addr, addr_buf, sizeof(addr_buf));
    std::printf("[server] client connected: %s:%u\n", addr_buf, ntohs(peer.sin_port));
    std::fflush(stdout);

    std::string pending;
    pending.reserve(kReadBufferSize);
    char buf[kReadBufferSize];

    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n == 0) {
            std::printf("[server] client %s:%u disconnected\n", addr_buf, ntohs(peer.sin_port));
            std::fflush(stdout);
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[server] recv error: %s\n", std::strerror(errno));
            break;
        }

        pending.append(buf, static_cast<std::size_t>(n));

        while (true) {
            auto newline = pending.find('\n');
            if (newline == std::string::npos) break;

            std::string_view line(pending.data(), newline);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }

            if (line.substr(0, 4) == "PING") {
                std::string response = "PONG " + std::to_string(now_ms()) + "\n";
                if (!write_all(client_fd, response)) {
                    std::fprintf(stderr, "[server] send failed: %s\n", std::strerror(errno));
                    pending.clear();
                    goto done;
                }
            } else if (!line.empty()) {
                std::fprintf(stderr, "[server] unexpected message: '%.*s'\n",
                             static_cast<int>(line.size()), line.data());
            }

            pending.erase(0, newline + 1);
        }
    }
done:
    ::close(client_fd);
}

int run_server(std::uint16_t port) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::fprintf(stderr, "[server] socket() failed: %s\n", std::strerror(errno));
        return 1;
    }

    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[server] bind() failed on port %u: %s\n",
                     port, std::strerror(errno));
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 16) < 0) {
        std::fprintf(stderr, "[server] listen() failed: %s\n", std::strerror(errno));
        ::close(listen_fd);
        return 1;
    }

    std::printf("[server] listening on 0.0.0.0:%u\n", port);
    std::fflush(stdout);

    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[server] accept() failed: %s\n", std::strerror(errno));
            continue;
        }
        std::thread(handle_client, client_fd, peer).detach();
    }

    ::close(listen_fd);
    std::printf("[server] shutdown complete\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = kDefaultPort;
    if (argc >= 2) {
        port = static_cast<std::uint16_t>(std::atoi(argv[1]));
        if (port == 0) {
            std::fprintf(stderr, "usage: %s [port]\n", argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    return run_server(port);
}
