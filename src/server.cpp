#include "resp.h"
#include "store.h"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <atomic>

// Linux networking headers
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

constexpr int PORT = 6379;
constexpr int MAX_EVENTS = 64;
constexpr size_t READ_BUFFER_SIZE = 4096;

// Flag to control graceful shutdown when SIGINT (Ctrl+C) or SIGTERM is caught.
static std::atomic<bool> g_running{true};

static void handle_signal(int sig) {
    (void)sig;
    g_running = false;
}

// Helper to set a file descriptor to non-blocking mode.
// In an interview: Explain that in a single-threaded epoll server,
// a blocking read(), write(), or accept() on any slow socket would stall
// the entire event loop, preventing all other clients from being served.
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Represents the state of an active client connection.
struct ClientState {
    int fd{-1};
    std::string in_buffer; // Accumulates incoming bytes across read() calls
};

// Converts a string to uppercase (used to match commands case-insensitively).
static std::string to_upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

// Executes a parsed Redis command against the in-memory store and returns RESP response.
// Supported commands: PING, ECHO, SET, GET.
static std::string dispatch_command(const std::vector<std::string>& args, KVStore& store) {
    if (args.empty()) {
        return serialize_error("empty command");
    }

    std::string cmd = to_upper(args[0]);

    if (cmd == "PING") {
        if (args.size() == 1) {
            // PING without args returns simple string "+PONG\r\n"
            return serialize_simple_string("PONG");
        } else if (args.size() == 2) {
            // PING with message returns the message as a bulk string
            return serialize_bulk_string(args[1]);
        } else {
            return serialize_error("wrong number of arguments for 'ping' command");
        }
    } else if (cmd == "ECHO") {
        if (args.size() == 2) {
            // ECHO <message> returns bulk string
            return serialize_bulk_string(args[1]);
        } else {
            return serialize_error("wrong number of arguments for 'echo' command");
        }
    } else if (cmd == "SET") {
        if (args.size() == 3) {
            // SET <key> <value> stores the value and returns "+OK\r\n"
            store.set(args[1], args[2]);
            return serialize_simple_string("OK");
        } else {
            return serialize_error("wrong number of arguments for 'set' command");
        }
    } else if (cmd == "GET") {
        if (args.size() == 2) {
            // GET <key> returns bulk string if found, or "$-1\r\n" if missing
            auto val = store.get(args[1]);
            if (val.has_value()) {
                return serialize_bulk_string(val.value());
            } else {
                return serialize_null_bulk_string();
            }
        } else {
            return serialize_error("wrong number of arguments for 'get' command");
        }
    } else {
        return serialize_error("unknown command '" + args[0] + "'");
    }
}

// Helper to write all bytes to a socket.
// In an interview: Explain that non-blocking send() may send fewer bytes than requested,
// so we loop until all bytes are written or an error occurs.
static bool send_all(int fd, const std::string& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = send(fd, data.data() + total_sent, data.size() - total_sent, 0);
        if (sent > 0) {
            total_sent += static_cast<size_t>(sent);
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // In production, an output buffer with EPOLLOUT would be used.
                // For this interview-grade single-threaded server, short responses fit in TCP buffer.
                continue;
            }
            // Socket write error (e.g. client disconnected abruptly)
            return false;
        } else {
            return false;
        }
    }
    return true;
}

// Cleans up a client connection from epoll and internal map.
static void remove_client(int epoll_fd, int client_fd, std::unordered_map<int, ClientState>& clients) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    clients.erase(client_fd);
}

int main() {
    // 1. Ignore SIGPIPE.
    // In an interview: Explain that if a client closes connection while we write to it,
    // the OS raises SIGPIPE which terminates the process by default.
    // Ignoring SIGPIPE causes send() to return -1 with errno = EPIPE instead, which we handle safely.
    signal(SIGPIPE, SIG_IGN);

    // 2. Register signal handlers for clean shutdown (Ctrl+C).
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // 3. Create listening TCP socket.
    // AF_INET = IPv4, SOCK_STREAM = TCP.
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << "\n";
        return 1;
    }

    // 4. Set SO_REUSEADDR option.
    // In an interview: Explain why SO_REUSEADDR is needed:
    // When the server restarts, the previous socket may linger in TIME_WAIT state for up to 2 minutes.
    // Without SO_REUSEADDR, bind() would fail with EADDRINUSE ("Address already in use").
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    // 5. Set listening socket to non-blocking.
    if (!set_nonblocking(listen_fd)) {
        std::cerr << "Failed to set non-blocking on listening socket\n";
        close(listen_fd);
        return 1;
    }

    // 6. Bind to 0.0.0.0:6379.
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    server_addr.sin_port = htons(PORT);       // Convert host byte order to network byte order (Big Endian)

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "bind() failed on port " << PORT << ": " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    // 7. Start listening for incoming connections.
    if (listen(listen_fd, SOMAXCONN) < 0) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    std::cout << "Redis server listening on port " << PORT << "...\n";

    // 8. Create epoll instance.
    // In an interview: Explain why epoll is chosen over select() / poll():
    // select() and poll() are O(N) because the kernel must scan all monitored fds on every call.
    // epoll is O(1) per ready event because it uses a kernel event poll list and ready list,
    // scaling efficiently to thousands of concurrent client connections.
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "epoll_create1() failed: " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    // Add listen socket to epoll monitoring for incoming connections (EPOLLIN).
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        std::cerr << "epoll_ctl() failed to add listen_fd: " << strerror(errno) << "\n";
        close(listen_fd);
        close(epoll_fd);
        return 1;
    }

    // In-memory key-value store and active client map
    KVStore store;
    std::unordered_map<int, ClientState> clients;
    epoll_event events[MAX_EVENTS];

    // 9. Main epoll event loop.
    while (g_running) {
        // Wait for I/O events. Timeout of 500ms allows checking g_running periodically.
        int num_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);
        if (num_ready < 0) {
            if (errno == EINTR) {
                // Interrupted by signal (e.g. SIGINT); loop condition will handle exit
                continue;
            }
            std::cerr << "epoll_wait() error: " << strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < num_ready; ++i) {
            int current_fd = events[i].data.fd;

            if (current_fd == listen_fd) {
                // --- New incoming client connection ---
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // All pending incoming connections have been accepted
                            break;
                        }
                        std::cerr << "accept() error: " << strerror(errno) << "\n";
                        break;
                    }

                    // Set accepted client socket to non-blocking
                    if (!set_nonblocking(client_fd)) {
                        close(client_fd);
                        continue;
                    }

                    // Register client socket with epoll for read events
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN;
                    client_ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                        close(client_fd);
                        continue;
                    }

                    // Store client state in map
                    clients[client_fd] = ClientState{client_fd, ""};
                }
            } else {
                // --- Data available from existing client ---
                char read_buf[READ_BUFFER_SIZE];
                bool should_disconnect = false;

                while (true) {
                    ssize_t bytes_read = read(current_fd, read_buf, sizeof(read_buf));
                    if (bytes_read > 0) {
                        // Append newly arrived bytes to client's incoming buffer
                        clients[current_fd].in_buffer.append(read_buf, static_cast<size_t>(bytes_read));

                        // Stream parser loop: process all complete commands in buffer
                        while (true) {
                            ParseResult res = parse_resp_command(clients[current_fd].in_buffer);

                            if (res.status == ParseStatus::Ok) {
                                // Erase parsed bytes from buffer
                                clients[current_fd].in_buffer.erase(0, res.bytes_consumed);

                                // Execute command and serialize response
                                std::string reply = dispatch_command(res.args, store);

                                // Send response back to client
                                if (!send_all(current_fd, reply)) {
                                    should_disconnect = true;
                                    break;
                                }
                            } else if (res.status == ParseStatus::Incomplete) {
                                // Incomplete command; wait for more data from socket
                                break;
                            } else {
                                // Protocol syntax error
                                std::string err = serialize_error("protocol error");
                                send_all(current_fd, err);
                                should_disconnect = true;
                                break;
                            }
                        }

                        if (should_disconnect) {
                            break;
                        }
                    } else if (bytes_read == 0) {
                        // EOF: client closed connection cleanly
                        should_disconnect = true;
                        break;
                    } else {
                        // bytes_read < 0
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // Socket buffer drained; no more data ready right now
                            break;
                        }
                        // Connection reset or socket read error
                        should_disconnect = true;
                        break;
                    }
                }

                if (should_disconnect) {
                    remove_client(epoll_fd, current_fd, clients);
                }
            }
        }
    }

    // 10. Clean shutdown.
    std::cout << "\nShutting down server cleanly...\n";
    for (auto& pair : clients) {
        close(pair.first);
    }
    clients.clear();
    close(epoll_fd);
    close(listen_fd);
    std::cout << "Server stopped.\n";

    return 0;
}
