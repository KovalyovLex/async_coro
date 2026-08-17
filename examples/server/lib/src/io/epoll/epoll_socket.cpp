#include <server/io/io_config.h>

#if EPOLL_SOCKET

#include <async_coro/await/await_callback.h>
#include <server/core/error.h>
#include <server/io/epoll/epoll_reactor.h>
#include <server/io/epoll/epoll_socket.h>
#include <server/utils/expected.h>

// Linux socket headers
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <utility>

namespace server::io {

// ============================================================================
// Constructor / Destructor / Move semantics
// ============================================================================

epoll_socket::epoll_socket(epoll_reactor& reactor, socket_type socket_handle, size_t index) noexcept
    : _reactor(reactor),
      _sock(socket_handle),
      _index(index) {
}

epoll_socket::epoll_socket(epoll_reactor& reactor, socket_type socket_handle) noexcept
    : epoll_socket(reactor, socket_handle, k_invalid_index) {
}

epoll_socket::~epoll_socket() noexcept {
  (void)close_sync();
}

epoll_socket::epoll_socket(epoll_socket&& other) noexcept
    : _reactor(other._reactor),
      _sock(std::exchange(other._sock, invalid_socket_id)),
      _index(std::exchange(other._index, 0)) {
}

epoll_socket& epoll_socket::operator=(epoll_socket&& other) noexcept {
  if (this != &other) {
    // Explicitly destroy current object (closes socket via destructor).
    this->~epoll_socket();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) epoll_socket(std::move(other));
  }
  return *this;
}

expected<void, core::error> epoll_socket::close_sync() noexcept {
  expected<void, core::error> res;

  if (is_closed()) {
    return res;
  }

  // Close via reactor to cancel all pending I/O operations.
  auto sock = std::exchange(_sock, invalid_socket_id);
  if (_index != k_invalid_index) {
    res = _reactor.remove_sock(sock, _index);
    _index = k_invalid_index;
  }

  if (!close_socket(sock) && res) {
    res = expected<void, core::error>{unexpect, core::error_type::close_failed, errno};
  }

  return res;
}

// ============================================================================
// connect_coro
// ============================================================================

async_coro::task<expected<epoll_socket, core::error>> epoll_socket::connect_coro(  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters): reactor lifetime guaranteed by caller
    epoll_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(socket_type_id::tcp);
  if (!sock_result) {
    co_return expected<epoll_socket, core::error>{unexpect, std::move(sock_result).error()};
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
    close_socket(sock);
    co_return expected<epoll_socket, core::error>{unexpect, std::move(bind_result).error()};
  }

  // Initiate non-blocking connect.
  const int connect_result = ::connect(
      sock,
      reinterpret_cast<const struct sockaddr*>(remote_address),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX connect() API
      static_cast<socklen_t>(address_length));

  if (connect_result == 0) {
    // Synchronous connect succeeded.
    co_return epoll_socket{reactor, sock};
  }

  if (errno != EINPROGRESS) {
    close_socket(sock);
    co_return expected<epoll_socket, core::error>{unexpect, core::error{core::error_type::connect_failed, errno}};
  }

  // Add socket to reactor and wait for connect completion.
  const auto index = reactor.add_sock(sock);
  if (!index) {
    co_return expected<epoll_socket, core::error>{unexpect, index.error()};
  }

  auto socket = epoll_socket{reactor, sock, index.value()};

  auto result = co_await async_coro::await_callback_with_result<expected<void, core::error>>(
      [&](auto cont) {
        reactor.submit_connect_socket(socket, std::move(cont));
      });

  if (!result) {
    reactor.remove_sock(sock, index.value());
    close_socket(sock);
    co_return expected<epoll_socket, core::error>{unexpect, std::move(result).error()};
  }

  co_return std::move(socket);
}

// ============================================================================
// send
// ============================================================================

async_coro::task<expected<size_t, core::error>> epoll_socket::send(std::span<const std::byte> data) {
  if (is_closed()) {
    co_return expected<size_t, core::error>{unexpect, core::error_type::socket_closed};
  }

  size_t total_bytes_sent = 0;
  auto current_data = data;

  while (total_bytes_sent < data.size()) {
    // Attempt non-blocking send immediately
    const ssize_t sent = ::send(_sock, reinterpret_cast<const char*>(current_data.data()), current_data.size(), MSG_NOSIGNAL);  // NOLINT(*reinterpret-cast)
    if (sent > 0) {
      total_bytes_sent += static_cast<size_t>(sent);
      current_data = current_data.subspan(static_cast<size_t>(sent));
      continue;
    }

    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Would block — wait for write readiness notification from reactor
      auto result = co_await async_coro::await_callback_with_result<expected<size_t, core::error>>(
          [this](auto cont) {
            _reactor.submit_send_socket(*this, std::move(cont));
          });

      if (!result) {
        co_return expected<size_t, core::error>{unexpect, std::move(result).error()};
      }

      // Retry send after notification
      continue;
    }

    // Actual error
    co_return expected<size_t, core::error>{unexpect, core::error{core::error_type::write_failed, errno}};
  }

  co_return total_bytes_sent;
}

// ============================================================================
// receive
// ============================================================================

async_coro::task<expected<size_t, core::error>> epoll_socket::receive(std::span<std::byte> buffer) {
  if (is_closed()) {
    co_return expected<size_t, core::error>{unexpect, core::error_type::socket_closed};
  }

  // Attempt non-blocking receive immediately
  const ssize_t received = ::recv(_sock, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0);  // NOLINT(*reinterpret-cast)
  if (received > 0) {
    co_return static_cast<size_t>(received);
  }

  if (received == 0) {
    // Connection closed by peer
    co_return expected<size_t, core::error>{};
  }

  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // Would block — wait for read readiness notification from reactor
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, core::error>>(
        [this](auto cont) {
          _reactor.submit_receive_socket(*this, std::move(cont));
        });

    if (!result) {
      co_return expected<size_t, core::error>{unexpect, std::move(result).error()};
    }

    // Retry receive after notification
    const ssize_t retry_received = ::recv(_sock, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0);  // NOLINT(*reinterpret-cast)
    if (retry_received > 0) {
      co_return static_cast<size_t>(retry_received);
    }

    if (retry_received == 0) {
      co_return expected<size_t, core::error>{};
    }

    // Actual error
    co_return expected<size_t, core::error>{unexpect, core::error{core::error_type::read_failed, errno}};
  }

  // Actual error
  co_return expected<size_t, core::error>{unexpect, core::error{core::error_type::read_failed, errno}};
}

// ============================================================================
// close
// ============================================================================
expected<void, core::error> epoll_socket::close() noexcept {
  return close_sync();
}

// ============================================================================
// set_no_delay
// ============================================================================

expected<void, core::error> epoll_socket::set_no_delay(bool enable) noexcept {
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

void epoll_socket::check_subscribed() {
  if (_index == k_invalid_index) {
    auto res = _reactor.add_sock(_sock);
    if (res) {
      _index = res.value();
    }
  }
}

}  // namespace server::io

#endif  // EPOLL_SOCKET
