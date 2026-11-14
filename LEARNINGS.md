# LEARNINGS

## Task 1: Linux C++17 Redis Server with Non-blocking Epoll & Streaming RESP Parser

### What Was Built (Plain Words)
1. Built a Redis-compatible in-memory key-value server in modern C++17 for Linux.
2. Created a TCP socket server on port 6379 configured with non-blocking mode (`O_NONBLOCK`).
3. Used the Linux `epoll` event loop (`epoll_create1`, `epoll_ctl`, `epoll_wait`) to serve multiple clients concurrently.
4. Created a streaming RESP parser for arrays of bulk strings that handles TCP packet fragmentation across reads.
5. Implemented an in-memory key-value store using `std::unordered_map` for $O(1)$ operations.
6. Supported core Redis commands: `PING`, `ECHO`, `SET`, and `GET` (case-insensitive command parsing).
7. Serialized RESP replies: Simple Strings (`+OK`), Bulk Strings (`$<len>`), Null Bulk String (`$-1`), and Errors (`-ERR`).
8. Handled signals: ignored `SIGPIPE` to prevent process termination on broken pipes and caught `SIGINT` for clean shutdown.
9. Added a C++ unit test suite and an automated Python integration test script testing split packets and pipelining.

---

### Interview Questions & Answers

#### Q1: Why do we use non-blocking I/O with `epoll` instead of allocating one thread per client connection?
**Answer:**
In a traditional thread-per-connection architecture, each connected client consumes dedicated OS resources (a thread stack typically 2MB–8MB in size, plus kernel thread scheduling overhead). When scaling to tens of thousands of concurrent connections (the C10K problem), this causes severe memory exhaustion and heavy CPU overhead due to continuous context switching.

By setting sockets to non-blocking mode (`O_NONBLOCK`) and using `epoll`, a single operating system thread can monitor thousands of sockets simultaneously. `epoll` is $O(1)$ per ready event (unlike `select()` or `poll()` which are $O(N)$ because the kernel must scan every file descriptor). The single thread is only woken up when a socket is actually ready for reading or writing. This matches Redis's real-world single-threaded event loop design, eliminating multithreading race conditions, lock contention, and cache thrashing while maximizing throughput.

---

#### Q2: Why is TCP considered a stream-oriented protocol, and how does our parser handle commands split across multiple `read()` calls?
**Answer:**
TCP does not preserve application message boundaries; it delivers an unbroken, ordered stream of raw bytes. Due to Network Maximum Transmission Unit (MTU) limits, TCP window scaling, or OS socket buffering, an application payload like `*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n` can arrive split across multiple distinct `read()` calls, or conversely, multiple commands can arrive bundled together in a single `read()` call (pipelining).

To solve this, each client maintains an incoming buffer (`std::string in_buffer`). When `read()` returns bytes, they are appended to this buffer. The parser checks if full delimiter lines (`\r\n`) and the expected byte count for each bulk string are present:
- If all bytes for the full command are available, the parser extracts the command, returns `ParseStatus::Ok`, and reports `bytes_consumed`. The server erases exactly `bytes_consumed` from the front of the client's buffer and continues to check for pipelined commands.
- If data is incomplete (e.g. bulk string length exceeds remaining bytes), the parser returns `ParseStatus::Incomplete` without consuming any bytes. The server retains the buffer, returns control to `epoll_wait`, and resumes parsing when more bytes arrive.

---

#### Q3: Why is `SO_REUSEADDR` set on the listening socket, and why must `SIGPIPE` be ignored?
**Answer:**
- **`SO_REUSEADDR`**: When a server terminates or restarts while clients were connected, the listening socket's port transitions into the kernel `TIME_WAIT` state (typically lasting 30 to 120 seconds according to RFC 793) to catch stray lingering packets. Without `SO_REUSEADDR`, calling `bind()` immediately upon restarting fails with `EADDRINUSE` ("Address already in use"). Setting `SO_REUSEADDR` allows the new server process to rebind to port 6379 immediately.
- **`SIGPIPE`**: In network programming, if a client abruptly closes its end of the connection (or crashes) and the server attempts to write to that socket with `send()` or `write()`, the operating system sends a `SIGPIPE` signal to the server process. The default OS action for `SIGPIPE` is to terminate the process immediately. By calling `signal(SIGPIPE, SIG_IGN)`, the kernel instead causes `send()` to return `-1` with `errno` set to `EPIPE`, allowing our server to catch the error, close the dead socket descriptor gracefully, and continue serving all other active clients without crashing.
