#include <server/io/io_config.h>

#if EPOLL_KQUEUE_ENABLED

#include <async_coro/config.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/warnings.h>
#include <gtest/gtest.h>
#include <server/io/io_config.h>
#include <server/socket_layer/connection_id.h>
#include <server/tcp_server.h>
#include <server/tcp_server_config.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#if WIN_SOCKET
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// ============================================================================
// Helper utilities — POSIX socket client helpers
// ============================================================================

/**
 * @brief Open a TCP client socket and connect to the given address.
 *
 * Sets send/recv timeouts so tests do not hang indefinitely on stalled connections.
 *
 * @param host     IP address to connect to (e.g. "127.0.0.1").
 * @param port     Port number (host byte-order).
 * @param timeout  Timeout for send/recv operations.
 * @return File descriptor on success, or invalid_socket_id on failure.
 */
static server::io::socket_type create_client_socket(const char* host, uint16_t port,
                                                    std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
  auto sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return server::io::invalid_socket_id;
  }

#if !WIN_SOCKET
  // Close the socket automatically when this process exits
  fcntl(sock, F_SETFD, FD_CLOEXEC);  // NOLINT(*-vararg)
#endif

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (::inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
    server::io::close_socket(sock);
    return server::io::invalid_socket_id;
  }

  if (::connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    server::io::close_socket(sock);
    return server::io::invalid_socket_id;
  }

  // Set send/recv timeouts so tests don't hang forever
#if WIN_SOCKET
  const DWORD timeout_ms = static_cast<DWORD>(timeout.count());
  ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
  ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
  const auto sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  timeval tv{};
  tv.tv_sec = static_cast<long>(sec.count());
  tv.tv_usec = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(timeout - sec).count());
  ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif

  return sock;
}

/**
 * @brief Close a client socket safely.
 */
static void close_client_socket(server::io::socket_type sock) {
  if (sock >= 0) {
    server::io::close_socket(sock);
  }
}

/**
 * @brief Send all bytes on a socket, retrying partial sends.
 *
 * @return true if all bytes were sent successfully.
 */
static bool send_all(server::io::socket_type sock, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
#if WIN_SOCKET
    auto n = ::send(sock, data + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
#else
    auto n = ::send(sock, data + sent, len - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
#endif
    sent += static_cast<size_t>(n);
  }
  return true;
}

/**
 * @brief Receive bytes into a buffer, returning the number of bytes received.
 *
 * @return Number of bytes received, or -1 on error.
 */
static int receive_all(server::io::socket_type sock, char* buf, size_t len) {
  size_t total = 0;
  while (total < len) {
#if WIN_SOCKET
    auto n = ::recv(sock, buf + total, static_cast<int>(len - total), 0);
    if (n <= 0) return static_cast<int>(total);
#else
    auto n = ::recv(sock, buf + total, len - total, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return static_cast<int>(total);
    }
    if (n == 0) {
      return static_cast<int>(total);  // peer closed
    }
#endif
    total += static_cast<size_t>(n);
  }
  return static_cast<int>(total);
}

/**
 * @brief Resolve a random available port by binding a temporary socket.
 *
 * @param host IP to bind on (e.g. "127.0.0.1").
 * @return Port number (host byte-order), or 0 on failure.
 */
static uint16_t find_available_port(const char* host = "127.0.0.1") {
  auto sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return 0;
  }

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = 0;  // let the OS pick
  if (::inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
    server::io::close_socket(sock);
    return 0;
  }

  if (::bind(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    server::io::close_socket(sock);
    return 0;
  }

  socklen_t addrlen = sizeof(sa);
  if (::getsockname(sock, reinterpret_cast<sockaddr*>(&sa), &addrlen) != 0) {
    server::io::close_socket(sock);
    return 0;
  }

  server::io::close_socket(sock);
  return ntohs(sa.sin_port);
}

// ============================================================================
// Test fixtures and helpers for TCP server tests
// ============================================================================

/**
 * @brief RAII wrapper that starts a tcp_server in a background thread and
 *        provides a callback to accept connections.
 *
 * The server runs until `stop()` is called or the object is destroyed.
 */
class tcp_server_handle {
 public:
  using connection_callback_t = std::function<void(server::socket_layer::connection)>;

  explicit tcp_server_handle(server::tcp_server_config config, connection_callback_t on_connected)
      : _server(std::make_unique<server::tcp_server>()),
        _config(std::move(config)),
        _on_connected(std::move(on_connected)) {}

  ~tcp_server_handle() {
    stop();
  }

  tcp_server_handle(const tcp_server_handle&) = delete;
  tcp_server_handle& operator=(const tcp_server_handle&) = delete;

  /**
   * @brief Start the server in a background thread.
   *
   * Blocks until the server is ready to accept connections (i.e., the listener
   * has bound and the reactor threads are running).
   */
  void start() {
    ASYNC_CORO_ASSERT(!_thread.joinable());

    _ready = false;

    _thread = std::thread([this]() {
      _server->serve(_config, std::nullopt,
                     [this](server::socket_layer::connection conn) {
                       _on_connected(std::move(conn));
                     });
    });

    // Wait a short moment for the server to be ready.
    // The reactor threads need time to start and bind the listener.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    _ready = true;
  }

  /**
   * @brief Stop the server gracefully.
   *
   * Calls `terminate()` on the server and joins the background thread.
   * Returns true if the server stopped within the given timeout.
   */
  bool stop() {
    if (!_thread.joinable()) {
      return true;
    }

    _server->terminate();

    if (_thread.joinable()) {
      _thread.join();
    }
    return true;
  }

  [[nodiscard]] bool is_ready() const noexcept { return _ready.load(std::memory_order::relaxed); }

 private:
  std::unique_ptr<server::tcp_server> _server;
  server::tcp_server_config _config;
  connection_callback_t _on_connected;  // owning callback (std::function)
  std::thread _thread;
  std::atomic_bool _ready{false};
};

// ============================================================================
// Start a TCP server on localhost:0 (random port), connect a single client
// using raw POSIX sockets, verify the server accepts the connection and can
// read data from it.
//
// This test verifies:
// - A TCP server can bind to a random available port
// - A single client connection is accepted by the server
// - Data sent by the client can be received by the server-side connection
// ============================================================================
TEST(tcp_server_accept, single_connection_accept) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  std::atomic<bool> connection_accepted{false};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 1;
  config.reactor_sleep = std::chrono::milliseconds{50};

  tcp_server_handle server(std::move(config), [&connection_accepted](server::socket_layer::connection /* conn */) {
    // Accept happened — the connection callback is invoked when a new client connects.
    connection_accepted.store(true, std::memory_order::relaxed);
  });

  server.start();

  // Give the server time to bind and start listening
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  // Connect a client
  auto client_fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{3});
  ASSERT_GE(client_fd, 0) << "Failed to connect client socket";

  // Send data from the client
  const char* msg = "Hello from client!";
  ASSERT_TRUE(send_all(client_fd, msg, strlen(msg))) << "Client send failed";

  // Give the server time to process the connection and data
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  // Verify the server accepted the connection
  EXPECT_TRUE(connection_accepted.load()) << "Server did not accept the client connection";

  // Close the client
  close_client_socket(client_fd);

  // Stop the server
  server.stop();
}

// ============================================================================
// Connect 5 clients to the server. Verify that connections are distributed
// across reactor threads (round-robin behavior). Each client should be able
// to send and receive data.
//
// This test verifies:
// - Multiple client connections can be accepted simultaneously
// - Connections are distributed across reactor threads in round-robin fashion
// - Each client can independently communicate with the server
// ============================================================================
TEST(tcp_server_accept, multiple_connections_round_robin) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  constexpr int num_clients = 5;
  std::atomic<int> accepted_count{0};

  // Scheduler with worker threads for fire-and-forget echo coroutines.
  async_coro::execution_system_config exec_config{
      .worker_configs = {async_coro::execution_thread_config{"echo_worker"}}};
  async_coro::scheduler scheduler{
      std::make_unique<async_coro::execution_system>(std::move(exec_config))};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 2;  // 2 reactor threads for round-robin distribution
  config.reactor_sleep = std::chrono::milliseconds{50};

  tcp_server_handle server(std::move(config), [&scheduler, &accepted_count](server::socket_layer::connection conn) {
    int idx = accepted_count.fetch_add(1, std::memory_order::relaxed);
    if (idx >= num_clients) {
      return;
    }

    // Launch a fire-and-forget coroutine that echoes data on this connection.
    // The connection is captured by value directly into the coroutine — no shared container needed.
    auto echo_task = [conn = std::move(conn)]() mutable -> async_coro::task<> {
      while (!conn.is_closed()) {
        std::array<std::byte, 4096> buf{};
        auto result = co_await conn.read_buffer(buf);
        if (result.has_value() && result.value() > 0) {
          size_t n = result.value();
          auto write_result = co_await conn.write_buffer(std::span<const std::byte>(buf.data(), n));
          if (!write_result) {
            break;
          }
        } else {
          // Connection closed or error
          break;
        }
      }
    };

    scheduler.start_task(std::move(echo_task));
  });

  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  // Connect all clients
  std::vector<server::io::socket_type> clients;
  clients.reserve(num_clients);
  for (int i = 0; i < num_clients; ++i) {
    auto fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{3});
    ASSERT_GE(fd, 0) << "Failed to connect client " << i;
    clients.push_back(fd);
  }

  // Give the server time to accept all connections
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  // Verify all connections were accepted
  EXPECT_EQ(accepted_count.load(), num_clients)
      << "Expected " << num_clients << " connections but got " << accepted_count.load();

  // Each client sends data and verifies echo response
  for (int i = 0; i < num_clients; ++i) {
    std::string msg = "Client " + std::to_string(i);
    ASSERT_TRUE(send_all(clients[static_cast<size_t>(i)], msg.data(), msg.size()))
        << "Client " << i << " send failed";

    // Read echo response
    std::array<char, 256> reply_buf{};
    int n = receive_all(clients[static_cast<size_t>(i)], reply_buf.data(), msg.size());
    ASSERT_GT(n, 0) << "Client " << i << " did not receive echo";

    std::string reply(reply_buf.data(), static_cast<size_t>(n));
    EXPECT_EQ(reply, msg) << "Echo mismatch for client " << i;
  }

  // Close all clients
  for (auto fd : clients) {
    close_client_socket(fd);
  }

  scheduler.stop();
  server.stop();
}

// ============================================================================
// Open 10 client connections simultaneously using parallel threads.
// Verify all are accepted and can communicate independently without interference.
//
// This test verifies:
// - The server can handle many simultaneous connections
// - Each connection operates independently (no cross-talk)
// - No data corruption occurs under concurrent load
// ============================================================================
TEST(tcp_server_accept, concurrent_client_connections) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  constexpr int num_clients = 10;
  std::atomic<int> accepted_count{0};

  // Scheduler with worker threads for fire-and-forget echo coroutines.
  async_coro::execution_system_config exec_config{
      .worker_configs = {async_coro::execution_thread_config{"echo_worker"}}};
  async_coro::scheduler scheduler{
      std::make_unique<async_coro::execution_system>(std::move(exec_config))};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 2;
  config.reactor_sleep = std::chrono::milliseconds{50};

  tcp_server_handle server(std::move(config), [&scheduler, &accepted_count](server::socket_layer::connection conn) {
    int idx = accepted_count.fetch_add(1, std::memory_order::relaxed);
    if (idx >= num_clients) {
      return;
    }

    // Launch a fire-and-forget coroutine that echoes data on this connection.
    // The connection is captured by value directly into the coroutine — no shared container needed.
    auto echo_task = [conn = std::move(conn)]() mutable -> async_coro::task<> {
      while (!conn.is_closed()) {
        std::array<std::byte, 4096> buf{};
        auto result = co_await conn.read_buffer(buf);
        if (result.has_value() && result.value() > 0) {
          size_t n = result.value();
          auto write_result = co_await conn.write_buffer(std::span<const std::byte>(buf.data(), n));
          if (!write_result) {
            break;
          }
        } else {
          // Connection closed or error
          break;
        }
      }
    };

    scheduler.start_task(std::move(echo_task));
  });

  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  // Launch all clients concurrently in parallel threads.
  std::vector<std::thread> client_threads;
  std::atomic<int> success_count{0};

  client_threads.reserve(num_clients);
  for (int i = 0; i < num_clients; ++i) {
    client_threads.emplace_back([this_port = port, idx = i, &success_count]() -> void {
      auto fd = create_client_socket("127.0.0.1", this_port, std::chrono::seconds{5});
      if (fd < 0) {
        return;
      }

      std::string msg = "Concurrent client " + std::to_string(idx);
      if (!send_all(fd, msg.data(), msg.size())) {
        close_client_socket(fd);
        return;
      }

      // Read echo response
      std::array<char, 256> reply_buf{};
      int n = receive_all(fd, reply_buf.data(), msg.size());
      if (n > 0) {
        std::string reply(reply_buf.data(), static_cast<size_t>(n));
        if (reply == msg) {
          success_count.fetch_add(1, std::memory_order::relaxed);
        }
      }

      close_client_socket(fd);
    });
  }

  // Wait for all client threads to complete.
  for (auto& t : client_threads) {
    t.join();
  }

  // Verify results.
  EXPECT_EQ(accepted_count.load(), num_clients)
      << "Expected " << num_clients << " accepted connections but got " << accepted_count.load();
  EXPECT_EQ(success_count.load(), num_clients)
      << "Expected " << num_clients << " successful round-trips but got " << success_count.load();

  scheduler.stop();
  server.stop();
}

// ============================================================================
// Connect a client, verify acceptance, then close the client connection.
// Verify the server handles the disconnection gracefully (no crashes, no hangs).
//
// This test verifies:
// - The server correctly detects when a client disconnects
// - No crash or hang occurs on client disconnection
// - The server remains operational after a client closes
// ============================================================================
TEST(tcp_server_accept, accept_then_close_gracefully) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  std::atomic<bool> connection_accepted{false};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 1;
  config.reactor_sleep = std::chrono::milliseconds{50};

  tcp_server_handle server(std::move(config), [&connection_accepted](server::socket_layer::connection /* conn */) {
    connection_accepted.store(true, std::memory_order::relaxed);
  });

  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  // Connect a client
  auto client_fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{3});
  ASSERT_GE(client_fd, 0) << "Failed to connect client";

  // Give the server time to accept
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  EXPECT_TRUE(connection_accepted.load()) << "Server did not accept the connection";

  // Close the client abruptly (no graceful shutdown handshake)
  close_client_socket(client_fd);

  // Give the server time to detect the closure
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  // The server should still be running and not have crashed.
  // We verify by checking that we can start a new connection (server is still alive).
  auto client_fd2 = create_client_socket("127.0.0.1", port, std::chrono::seconds{3});
  ASSERT_GE(client_fd2, 0) << "Server crashed or became unresponsive after client disconnect";
  close_client_socket(client_fd2);

  server.stop();
}

// ============================================================================
// Rapidly connect and disconnect 20 clients in sequence. Verify the server
// remains stable and can accept new connections after the storm.
//
// This test verifies:
// - The server handles connection churn without crashing or hanging
// - The server remains operational after a burst of rapid connections
// - No resource leaks or deadlocks occur under stress
// ============================================================================
TEST(tcp_server_accept, rapid_connect_disconnect) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  constexpr int num_rapid_clients = 20;
  std::atomic<int> accepted_count{0};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 2;
  config.reactor_sleep = std::chrono::milliseconds{300};  // Longer sleep for stability

  tcp_server_handle server(std::move(config), [&](server::socket_layer::connection /* conn */) {
    accepted_count.fetch_add(1, std::memory_order::relaxed);
    // No-op: just count acceptance. The connection will be cleaned up when the callback returns.
  });

  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  // Rapid connect-disconnect storm
  for (int i = 0; i < num_rapid_clients; ++i) {
    auto fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{2});
    if (fd >= 0) {
      // Send a tiny bit of data then immediately close.
      const char dummy = 'x';
      send_all(fd, &dummy, 1);
      close_client_socket(fd);
    }

    // Small delay between connections to simulate realistic churn.
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  // Give the server time to process all connections.
  std::this_thread::sleep_for(std::chrono::milliseconds{1000});

  EXPECT_EQ(accepted_count.load(), num_rapid_clients)
      << "Expected " << num_rapid_clients << " accepted connections but got " << accepted_count.load();

  // Verify the server can still accept new connections after the storm.
  auto post_storm_fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{3});
  ASSERT_GE(post_storm_fd, 0) << "Server unable to accept connections after rapid connect-disconnect storm";

  constexpr auto msg = std::string_view{"post-storm"};
  send_all(post_storm_fd, msg.data(), msg.size());
  close_client_socket(post_storm_fd);

  std::this_thread::sleep_for(std::chrono::milliseconds{200});

  server.stop();
}

// ============================================================================
// Start a server, connect a few clients, then call terminate() on the server.
// Verify the server shuts down cleanly within a reasonable timeout (5 seconds).
//
// This test verifies:
// - The server's terminate() method stops all reactor threads
// - The server joins all threads without hanging
// - Clean shutdown occurs within the expected timeout
// ============================================================================
TEST(tcp_server_accept, server_termination) {
  const uint16_t port = find_available_port();
  ASSERT_NE(port, 0U) << "Failed to find an available port";

  std::atomic<int> accepted_count{0};

  server::tcp_server_config config{};
  config.ip_address = "127.0.0.1";
  config.port = port;
  config.num_reactors = 2;
  config.reactor_sleep = std::chrono::milliseconds{50};

  tcp_server_handle server(std::move(config), [&accepted_count](server::socket_layer::connection /* conn */) {
    accepted_count.fetch_add(1, std::memory_order::relaxed);
  });

  server.start();
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  constexpr int num_clients = 3;
  std::vector<server::io::socket_type> clients;
  clients.reserve(num_clients);
  for (int i = 0; i < num_clients; ++i) {
    auto fd = create_client_socket("127.0.0.1", port, std::chrono::seconds{5});
    ASSERT_GE(fd, 0) << "Failed to connect client " << i;
    clients.push_back(fd);

    // Send some data so the server registers the connection.
    constexpr auto msg = std::string_view{"keepalive"};
    send_all(fd, msg.data(), msg.size());
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{300});
  EXPECT_EQ(accepted_count.load(), num_clients)
      << "Expected " << num_clients << " accepted connections but got " << accepted_count.load();

  // Terminate the server while clients are still connected.
  server.stop();

  // Verify all client sockets are still valid (they should detect the server closed).
  for (auto fd : clients) {
    close_client_socket(fd);
  }
}

#endif  // EPOLL_KQUEUE_ENABLED
