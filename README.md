# C++17 Redis-Compatible In-Memory Server

A lightweight, high-performance Redis clone implemented in modern C++17 for Linux. It features a non-blocking TCP server driven by an `epoll` event loop, a streaming RESP (Redis Serialization Protocol) parser that handles fragmented TCP reads, and an in-memory key-value store.

---

## Features

- **Non-blocking TCP Server**: Listens on port `6379`, multiplexing multiple client connections using the Linux `epoll` API (`EPOLLIN`).
- **Streaming RESP Parser**: Parses arrays of bulk strings (`*<count>\r\n$<len>\r\n...`) and properly handles commands split across multiple TCP reads/segments without blocking or corrupting state.
- **In-Memory Key-Value Store**: Built on `std::unordered_map` for average $O(1)$ SET and GET operations.
- **Supported Redis Commands**:
  - `PING [message]` — Responds with `+PONG\r\n` or bulk string payload.
  - `ECHO <message>` — Echoes back the message as a bulk string.
  - `SET <key> <value>` — Stores or overwrites the key-value pair and responds with `+OK\r\n`.
  - `GET <key>` — Returns the bulk string value, or null bulk string `$-1\r\n` (nil) if not found.

---

## Project Structure

```text
.
├── CMakeLists.txt          # CMake build configuration (C++17)
├── README.md               # Build and test guide
├── LEARNINGS.md            # Plain-English concepts & interview Q&As
├── src/
│   ├── store.h / .cpp      # KVStore class (unordered_map wrapper)
│   ├── resp.h / .cpp       # RESP parser and response serializers
│   └── server.cpp          # TCP server and epoll event loop
└── tests/
    ├── test_resp.cpp       # Unit tests (RESP parser, store, split packet test)
    └── test_client.py      # End-to-end integration test client
```

---

## Prerequisites (Linux / WSL)

Ensure you have a C++17 compliant compiler, CMake, and `redis-cli` installed:

```bash
# Ubuntu / Debian / WSL
sudo apt update
sudo apt install -y build-essential cmake redis-tools python3
```

---

## Build Steps

Build the project using standard CMake:

```bash
# 1. Generate build directory
cmake -B build -S .

# 2. Compile binaries (redis-server and test_resp)
cmake --build build
```

This generates:
- `build/redis-server` (the main Redis server binary)
- `build/test_resp` (the unit test executable)

---

## Running the Server

Start the server on default port `6379`:

```bash
./build/redis-server
```

Output:
```text
Redis server listening on port 6379...
```

To stop the server cleanly, press `Ctrl+C` (handles `SIGINT` gracefully).

---

## Testing

### 1. Manual Testing with `redis-cli`

In a separate terminal, connect to your server using the official Redis CLI:

```bash
redis-cli -p 6379
```

Run test commands:
```text
127.0.0.1:6379> PING
PONG
127.0.0.1:6379> PING "Hello Antigravity"
"Hello Antigravity"
127.0.0.1:6379> ECHO "Testing Redis"
"Testing Redis"
127.0.0.1:6379> SET user "Alex"
OK
127.0.0.1:6379> GET user
"Alex"
127.0.0.1:6379> GET non_existing_key
(nil)
```

### 2. Run Unit Tests (C++)

Run the compiled unit tests to verify the parser, buffer slicing, and store:

```bash
./build/test_resp
```

### 3. Run Integration Tests (Python)

Run the automated integration test script while `./build/redis-server` is running:

```bash
python3 tests/test_client.py
```
This tests standard commands, byte-by-byte split packet streaming over TCP, and pipelining.
