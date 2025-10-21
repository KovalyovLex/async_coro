#pragma once

#include <async_coro/task.h>
#include <server/utils/expected.h>

#include <span>
#include <utility>

#include "connection_id.h"

namespace server::socket_layer {

class reactor;

class connection {
 public:
  connection(connection_id fd_id, reactor* react) noexcept
      : _sock(fd_id),
        _reactor(react) {}

  connection(const connection&) = delete;
  connection(connection&& other) noexcept
      : _sock(std::exchange(other._sock, invalid_connection)),
        _reactor(std::exchange(other._reactor, nullptr)) {}

  connection& operator=(const connection&) = delete;
  connection& operator=(connection&& other) noexcept {
    _reactor = std::exchange(other._reactor, nullptr);
    _sock = std::exchange(other._sock, invalid_connection);

    return *this;
  }

  ~connection() noexcept;

  [[nodiscard]] auto get_connection_id() const noexcept { return _sock; }

  constexpr auto operator<=>(const connection& other) const noexcept { return _sock <=> other._sock; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> write_buffer(std::span<const std::byte> bytes);

  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read_buffer(std::span<std::byte> bytes);

 private:
  connection_id _sock;
  reactor* _reactor;
};
}  // namespace server::socket_layer
