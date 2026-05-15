# cpp-latency

A minimal TCP ping/pong tool for measuring round-trip latency between two hosts. The original goal: client on a Raspberry Pi 2, server on a DigitalOcean droplet.

The client opens a TCP connection, sends `PING <ms>\n`, the server replies with `PONG <ms>\n`, and the client records the wall-clock RTT measured against a monotonic clock. After a configurable warmup, it runs N timed iterations and prints min, max, mean, median, stddev, p95, and p99.

## Layout

```
.
├── CMakeLists.txt       # C++23, builds two executables
├── build.sh             # cmake + build wrapper
├── run-server.sh        # one-shot bootstrap for the server host
├── run-client.sh        # one-shot bootstrap for the client host
├── src/
│   ├── server.cpp       # TCP server, one thread per client
│   └── client.cpp       # TCP client + stats
└── .gitignore
```

## Local build

Requires `clang++`, `cmake` 3.20+, and a libstdc++ or libc++ that supports C++23 (or C++20 if you bump `CMAKE_CXX_STANDARD` in `CMakeLists.txt`).

```sh
./build.sh
```

Produces `build/server` and `build/client`.

## Local smoke test

```sh
./build/server 9000 &
./build/client --host 127.0.0.1 --port 9000 --iterations 200 --warmup 20
```

## Deploy

The bootstrap scripts install dependencies, fetch the repo, build, and start the binary. Pin to a commit SHA in the URL if you want a specific revision.

### Server (DigitalOcean droplet)

```sh
curl -fsSL https://raw.githubusercontent.com/hirebarend/cpp-latency/main/run-server.sh | bash
```

This installs `clang`, `cmake`, `build-essential`, `unzip`, and `ufw`; opens TCP 9000 in ufw; builds; and runs `./build/server 9000` in the foreground. To survive SSH disconnect, run the server inside `tmux` or via `nohup`.

If the droplet has a DigitalOcean Cloud Firewall attached, add an inbound TCP rule for port 9000 in the control panel. The script cannot touch cloud firewall rules.

Override the port:

```sh
curl -fsSL https://raw.githubusercontent.com/hirebarend/cpp-latency/main/run-server.sh | PORT=9001 bash
```

### Client (Raspberry Pi 2)

```sh
curl -fsSL https://raw.githubusercontent.com/hirebarend/cpp-latency/main/run-client.sh | SERVER_HOST=<droplet-ip> bash
```

Tune the run:

```sh
curl -fsSL https://raw.githubusercontent.com/hirebarend/cpp-latency/main/run-client.sh | \
  SERVER_HOST=1.2.3.4 SERVER_PORT=9000 ITERATIONS=1000 WARMUP=100 INTERVAL_MS=5 bash
```

Do not pipe to `sudo bash`. The script calls `sudo` internally for `apt` and `ufw`.

## Client options

```
./build/client [--host H] [--port P] [--iterations N] [--warmup N] [--interval-ms N]
```

| Flag            | Default     | Description                                           |
|-----------------|-------------|-------------------------------------------------------|
| `--host`        | `127.0.0.1` | Server hostname or IP                                 |
| `--port`        | `9000`      | Server TCP port                                       |
| `--iterations`  | `500`       | Measured iterations after warmup                      |
| `--warmup`      | `50`        | Iterations discarded before measurement starts        |
| `--interval-ms` | `10`        | Sleep between iterations (ms)                         |

## Sample output

```
=== Latency statistics (500 samples) ===
  min     : 0.062 ms
  max     : 0.180 ms
  mean    : 0.110 ms
  median  : 0.108 ms
  stddev  : 0.021 ms
  p95     : 0.148 ms
  p99     : 0.163 ms
```

## Wire protocol

Line-delimited ASCII over a long-lived TCP connection. `TCP_NODELAY` is set on both sides so each PING is flushed immediately.

```
client -> server : PING <client_send_ms>\n
server -> client : PONG <server_recv_ms>\n
```

The PONG carries the server's local receive timestamp for visibility, but the client computes RTT from its own `steady_clock` delta. The two host clocks are not assumed to be synchronized.

## Notes

- **Architecture and OS matter.** A binary built on macOS will not run on Linux, and an arm64 binary will not run on the Pi 2's 32-bit CPU. Build on each target.
- **Raspberry Pi OS reports `armv6l`.** The 32-bit Raspberry Pi OS image is built against an ARMv6 hard-float userspace for compatibility with the original Pi, even when running on a Pi 2/3/4. `uname -m` reports `armv6l` and the system libraries are ARMv6. `CMakeLists.txt` detects this and compiles with `-march=armv6 -mfpu=vfp -mfloat-abi=hard`, which runs everywhere from the Pi 1 up. On a system that genuinely advertises `armv7l` (some Debian armhf setups), it targets ARMv7-A with VFPv3. Compiling with ARMv7/NEON flags against an ARMv6 userspace is the classic cause of `Illegal instruction` at runtime, so leave these flags alone.
- **clang vs g++ on the Pi.** Raspbian's `g++` is preconfigured for the system; the Debian `clang` package occasionally drifts. The build picks `clang++` by default. If that produces a binary that won't run, fall back via `CXX=g++ ./build.sh` (or pass `CXX=g++` to `run-client.sh`).
- **C++ standard.** The code is C++20-compatible. `CMakeLists.txt` requests C++23 because nothing in the code relies on C++23-only features but newer standards are cheap to ask for. If your toolchain rejects it (e.g. g++ 10 on Bullseye), drop `CMAKE_CXX_STANDARD` to `20`.
- **The server is intentionally small.** One thread per connection, blocking I/O, no auth. It is meant for latency probing on a trusted network path, not as a production service. Do not expose it on the open internet without a firewall scope.
