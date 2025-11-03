#pragma once

#include <async_coro/task.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/ssl_connection.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace server::socket_layer {

class reactor;

class connection {
 public:
  connection(connection_id fd_id, reactor& react, ssl_connection ssl_con) noexcept
      : _reactor(std::addressof(react)),
        _ssl(std::move(ssl_con)),
        _sock(fd_id) {}

  connection(const connection&) = delete;
  connection(connection&& other) noexcept
      : _reactor(std::exchange(other._reactor, nullptr)),
        _ssl(std::move(other._ssl)),
        _subscription_index(other._subscription_index),
        _sock(std::exchange(other._sock, invalid_connection)) {}

  ~connection() noexcept;

  connection& operator=(const connection&) = delete;
  connection& operator=(connection&& other) noexcept {
    _reactor = std::exchange(other._reactor, nullptr);
    _sock = std::exchange(other._sock, invalid_connection);

    _ssl = std::move(other._ssl);
    _subscription_index = other._subscription_index;
    return *this;
  }

  [[nodiscard]] bool is_closed() const noexcept { return _reactor == nullptr; }

  [[nodiscard]] auto get_connection_id() const noexcept { return _sock; }

  [[nodiscard]] auto get_subscription_index() const noexcept { return _subscription_index; }

  constexpr auto operator<=>(const connection& other) const noexcept { return _sock <=> other._sock; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> write_buffer(std::span<const std::byte> bytes);

  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read_buffer(std::span<std::byte> bytes);

  [[nodiscard]] reactor* get_reactor() const noexcept { return _reactor; }

  void check_subscribed();

  void close_connection();

 private:
  static constexpr size_t k_invalid_index = -1;

  reactor* _reactor;
  ssl_connection _ssl;
  size_t _subscription_index = k_invalid_index;
  connection_id _sock;
};
}  // namespace server::socket_layer
