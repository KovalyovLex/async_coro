#pragma once

#include <async_coro/task.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/ssl_connection.h>
#include <server/utils/expected.h>

#include <span>
#include <utility>

namespace server::socket_layer {

class reactor;

class connection {
 public:
  connection(connection_id fd_id, reactor* react, ssl_connection ssl_con) noexcept
      : _reactor(react),
        _ssl(std::move(ssl_con)),
        _sock(fd_id) {}

  connection(const connection&) = delete;
  connection(connection&& other) noexcept
      : _reactor(std::exchange(other._reactor, nullptr)),
        _sock(std::exchange(other._sock, invalid_connection)),
        _ssl(std::move(other._ssl)),
        _is_listening(other._is_listening) {}

  ~connection() noexcept;

  connection& operator=(const connection&) = delete;
  connection& operator=(connection&& other) noexcept {
    _reactor = std::exchange(other._reactor, nullptr);
    _sock = std::exchange(other._sock, invalid_connection);
    _is_listening = other._is_listening;
    _ssl = std::move(other._ssl);

    return *this;
  }

  [[nodiscard]] auto get_connection_id() const noexcept { return _sock; }

  constexpr auto operator<=>(const connection& other) const noexcept { return _sock <=> other._sock; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> write_buffer(std::span<const std::byte> bytes);

  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read_buffer(std::span<std::byte> bytes);

  [[nodiscard]] reactor* get_reactor() const noexcept { return _reactor; }

  void check_subscribed();

  void close_connection();

 private:
  reactor* _reactor;
  ssl_connection _ssl;
  connection_id _sock;
  bool _is_listening = false;
};
}  // namespace server::socket_layer
