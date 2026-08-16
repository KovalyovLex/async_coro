#if IO_URING_ENABLED

#include <arpa/inet.h>
#include <async_coro/await/await_callback.h>
#include <netinet/in.h>
#include <server/core/error.h>
#include <server/io/uring/io_uring_listener.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/utils/expected.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <utility>

namespace server::io {

// ============================================================================
// Constructor / Destructor
// ============================================================================

io_uring_listener::io_uring_listener(io_uring_reactor& reactor, socket_type sock) noexcept
    : _reactor(reactor),
      _sock(sock) {}

io_uring_listener::~io_uring_listener() noexcept {
  (void)close();
}

// ============================================================================
// Move semantics
// ============================================================================

io_uring_listener::io_uring_listener(io_uring_listener&& other) noexcept
    : _reactor(other._reactor),
      _sock(std::exchange(other._sock, invalid_socket_id)) {}

io_uring_listener& io_uring_listener::operator=(io_uring_listener&& other) noexcept {
  if (this != &other) {
    // Explicitly destroy current object (closes socket via destructor).
    this->~io_uring_listener();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) io_uring_listener(std::move(other));
  }
  return *this;
}

// ============================================================================
// open
// ============================================================================

expected<io_uring_listener, core::error> io_uring_listener::open(io_uring_reactor& reactor, std::string_view ip_address, uint16_t port) {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(socket_type_id::tcp);
  if (!sock_result) {
    return expected<io_uring_listener, core::error>{unexpect, std::move(sock_result).error()};
  }

  socket_type sock = sock_result.value();

  // Set SO_REUSEADDR to allow quick rebinding.
  int reuse_addr = 1;
  const int result = setsockopt(
      sock,
      SOL_SOCKET,
      SO_REUSEADDR,
      reinterpret_cast<const char*>(&reuse_addr),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX setsockopt() API
      sizeof(reuse_addr));
  if (result < 0) {
    (void)reactor.submit_close(sock, {});
    return expected<io_uring_listener, core::error>{unexpect, core::error{core::error_type::setsockopt_failed, errno}};  // NOLINT(readability-redundant-casting): suppress unused result warning from submit_close
  }

  // Convert IP address string to sockaddr_in.
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address.data(), &addr.sin_addr) != 1) {  // NOLINT(bugprone-suspicious-stringview-data-usage): inet_pton takes a C string, not a span
    reactor.submit_close(sock, {});
    return expected<io_uring_listener, core::error>{unexpect, core::error_type::inet_pton_failed};
  }

  // Bind to the address.
  auto bind_result = reactor.bind_socket(
      sock,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&addr),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX bind() API
          sizeof(addr)));
  if (!bind_result) {
    reactor.submit_close(sock, {});
    return expected<io_uring_listener, core::error>{unexpect, std::move(bind_result).error()};
  }

  // Listen with default backlog.
  auto listen_result = reactor.listen_socket(sock, SOMAXCONN);
  if (!listen_result) {
    reactor.submit_close(sock, {});
    return expected<io_uring_listener, core::error>{unexpect, std::move(listen_result).error()};
  }

  io_uring_listener listener{reactor, sock};
  return listener;
}

// ============================================================================
// accept
// ============================================================================

async_coro::task<expected<io_uring_socket, core::error>> io_uring_listener::accept() {
  if (!is_open()) {
    co_return expected<io_uring_socket, core::error>{unexpect, core::error_type::listener_closed};
  }

  // Submit async accept operation (reactor creates the accept socket internally).
  auto result = co_await async_coro::await_callback_with_result<expected<socket_type, core::error>>(
      [&](auto cont) {
        _reactor.submit_accept_socket(_sock, std::move(cont));
      });

  if (!result) {
    co_return expected<io_uring_socket, core::error>{unexpect, std::move(result).error()};
  }

  co_return io_uring_socket{_reactor, result.value()};
}

// ============================================================================
// close
// ============================================================================

expected<void, core::error> io_uring_listener::close() noexcept {
  if (!is_open()) {
    return expected<void, core::error>{};
  }

  // Close via reactor to cancel all pending I/O operations (e.g., in-flight accepts).
  _reactor.submit_close(std::exchange(_sock, invalid_socket_id), {});
  return expected<void, core::error>{};
}

}  // namespace server::io

#endif  // IO_URING_ENABLED
