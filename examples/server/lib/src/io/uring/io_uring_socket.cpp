#if IO_URING_ENABLED

#include <async_coro/await/await_callback.h>
#include <server/core/error.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/io/uring/io_uring_socket.h>
#include <server/utils/expected.h>

// Linux socket headers
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <utility>

namespace server::io {

// ============================================================================
// Constructor / Destructor / Move semantics
// ============================================================================

io_uring_socket::io_uring_socket(io_uring_reactor& reactor, socket_type socket_handle) noexcept
    : _reactor(reactor),
      _sock(socket_handle) {
}

io_uring_socket::~io_uring_socket() noexcept {
  close_sync();
}

io_uring_socket::io_uring_socket(io_uring_socket&& other) noexcept
    : _reactor(other._reactor),
      _sock(std::exchange(other._sock, invalid_socket_id)) {
}

io_uring_socket& io_uring_socket::operator=(io_uring_socket&& other) noexcept {
  if (this != &other) {
    // Explicitly destroy current object (closes socket via destructor).
    this->~io_uring_socket();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) io_uring_socket(std::move(other));
  }
  return *this;
}

void io_uring_socket::close_sync() noexcept {
  if (is_closed()) {
    return;
  }

  // Close via reactor to cancel all pending I/O operations.
  _reactor.submit_close(std::exchange(_sock, invalid_socket_id), {});
}

// ============================================================================
// connect_coro
// ============================================================================

async_coro::task<expected<io_uring_socket, core::error>> io_uring_socket::connect_coro(  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters): reactor lifetime guaranteed by caller
    io_uring_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(socket_type_id::tcp);
  if (!sock_result) {
    co_return expected<io_uring_socket, core::error>{unexpect, std::move(sock_result).error()};
  }

  socket_type sock = sock_result.value();

  // Bind to any local address.
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = 0;  // Let the system assign a port
  auto bind_result = reactor.bind_socket(
      sock,
      std::as_bytes(std::span{&bind_addr, 1}));
  if (!bind_result) {
    reactor.submit_close(sock, {});
    co_return expected<io_uring_socket, core::error>{unexpect, std::move(bind_result).error()};
  }

  // Submit async connect operation.
  auto result = co_await async_coro::await_callback_with_result<expected<void, core::error>>(
      [&](auto cont) {
        reactor.submit_connect_socket(
            sock,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(remote_address),  // NOLINT(*reinterpret-cast)
                static_cast<size_t>(address_length)),
            std::move(cont));
      });

  if (!result) {
    reactor.submit_close(sock, {});
    co_return expected<io_uring_socket, core::error>{unexpect, std::move(result).error()};
  }

  co_return io_uring_socket{reactor, sock};
}

// ============================================================================
// send
// ============================================================================

async_coro::task<expected<size_t, core::error>> io_uring_socket::send(std::span<const std::byte> data) {
  if (is_closed()) {
    co_return expected<size_t, core::error>{unexpect, core::error_type::socket_closed};
  }

  size_t total_bytes_sent = 0;
  auto current_data = data;

  while (total_bytes_sent < data.size()) {
    // Submit async send operation.
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, core::error>>(
        [this, &current_data](auto cont) {
          _reactor.submit_send_socket(
              _sock,
              current_data,
              std::move(cont));
        });

    if (!result) {
      co_return expected<size_t, core::error>{unexpect, std::move(result).error()};
    }

    const auto sent = result.value();
    total_bytes_sent += sent;
    current_data = current_data.subspan(sent);
  }

  co_return total_bytes_sent;
}

// ============================================================================
// receive
// ============================================================================

async_coro::task<expected<size_t, core::error>> io_uring_socket::receive(std::span<std::byte> buffer) {
  if (is_closed()) {
    co_return expected<size_t, core::error>{unexpect, core::error_type::socket_closed};
  }

  // Submit async receive operation.
  auto result = co_await async_coro::await_callback_with_result<expected<size_t, core::error>>(
      [this, buffer](auto cont) {
        _reactor.submit_receive_socket(_sock, buffer, std::move(cont));
      });

  if (!result) {
    co_return expected<size_t, core::error>{unexpect, std::move(result).error()};
  }

  co_return result.value();
}

// ============================================================================
// close
// ============================================================================

expected<void, core::error> io_uring_socket::close() noexcept {
  if (is_closed()) {
    return expected<void, core::error>{};
  }

  // Close via reactor to cancel all pending I/O operations.
  _reactor.submit_close(std::exchange(_sock, invalid_socket_id), {});
  return expected<void, core::error>{};
}

// ============================================================================
// set_no_delay
// ============================================================================

expected<void, core::error> io_uring_socket::set_no_delay(bool enable) noexcept {
  if (is_closed()) {
    return expected<void, core::error>{unexpect, core::error_type::socket_closed};
  }

  int val = enable ? 1 : 0;
  const int result = setsockopt(
      _sock,
      IPPROTO_TCP,
      TCP_NODELAY,
      &val,
      static_cast<int>(sizeof(val)));

  if (result < 0) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::setsockopt_failed, errno}};
  }

  return expected<void, core::error>{};
}

}  // namespace server::io

#endif  // IO_URING_ENABLED
