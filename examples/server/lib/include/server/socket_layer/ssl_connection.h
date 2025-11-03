#pragma once

#include <async_coro/task.h>
#include <server/socket_layer/connection_id.h>
#include <server/utils/expected.h>

#include <cstdint>
#include <span>
#include <utility>

namespace server::socket_layer {

class ssl_context;
class connection;

enum class ssl_error : uint8_t {
  no_error,

  wants_read,
  wants_write,
  connection_was_closed,
  other_error,
};

class ssl_connection {
 public:
  ssl_connection() noexcept = default;

  ssl_connection(ssl_context& context, socket_layer::connection_id conn);

  ssl_connection(const ssl_connection&) = delete;
  ssl_connection(ssl_connection&& other) noexcept
      : _ssl(std::exchange(other._ssl, nullptr)) {}

  ~ssl_connection() noexcept;

  ssl_connection& operator=(const ssl_connection&) = delete;
  ssl_connection& operator=(ssl_connection&& other) noexcept {
    _ssl = std::exchange(other._ssl, nullptr);
    return *this;
  }

  explicit operator bool() noexcept {
    return _ssl != nullptr;
  }

  [[nodiscard]] async_coro::task<expected<bool, std::string>> handshake(connection& connection);

  int read(std::span<std::byte> bytes);
  int write(std::span<const std::byte> bytes);

  [[nodiscard]] ssl_error get_error(int result) const noexcept;

 private:
  void* _ssl = nullptr;
};

}  // namespace server::socket_layer
