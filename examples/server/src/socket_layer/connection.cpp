
#include <server/socket_layer/connection.h>
#include <server/socket_layer/reactor.h>
#include <unistd.h>

#include "async_coro/await/await_callback.h"

#if !WIN_SOCKET
#include <sys/socket.h>

#include <cerrno>
#endif

namespace server::socket_layer {

connection::~connection() noexcept {
  if (_reactor != nullptr) {
    _reactor->close_connection(_sock);
  }
}

async_coro::task<expected<void, std::string>> connection::write_buffer(std::span<const std::byte> bytes) {
  if (_reactor == nullptr) {
    co_return expected<void, std::string>{};
  }

  const auto fd_id = _sock.get_platform_id();

  size_t sent = 0;
  while (true) {
    const auto sent_local = ::send(fd_id, bytes.data(), bytes.size(), 0);
    if (sent_local > 0) {
      sent += sent_local;
      if (sent == bytes.size()) {
        co_return expected<void, std::string>{};
      }
    } else if (sent_local == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      co_await async_coro::await_callback([this](auto cont) {
        _reactor->continue_after_sent_data(_sock, std::move(cont));
      });
    } else {
      // error
      break;
    }
  }

  co_return expected<void, std::string>{};
}

async_coro::task<expected<size_t, std::string>> connection::read_buffer(std::span<std::byte> bytes) {
  if (_reactor == nullptr) {
    co_return 0;
  }

  const auto fd_id = _sock.get_platform_id();

  while (true) {
    const auto received = ::recv(fd_id, bytes.data(), bytes.size(), 0);
    if (received > 0) {
      co_return received;
    } else if (received == 0) {
      _reactor->close_connection(_sock);
      _reactor = nullptr;
      co_return 0;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      co_await async_coro::await_callback([this](auto cont) {
        _reactor->continue_after_receive_data(_sock, std::move(cont));
      });
    } else {
      // error
      break;
    }
  }

  co_return 0;
}

}  // namespace server::socket_layer
