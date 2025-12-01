#pragma once

#include <server/socket_layer/connection_id.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "server/http1/http_method.h"

// Helper class to generate http requests for testing
class http_test_client {
 public:
  static constexpr auto k_default_timeout = std::chrono::seconds{2};

  http_test_client() noexcept = default;
  http_test_client(std::string host, uint16_t port, std::chrono::microseconds timeout = k_default_timeout) noexcept;
  http_test_client(const http_test_client&) = delete;
  http_test_client(http_test_client&&) noexcept;
  http_test_client& operator=(const http_test_client&) = delete;
  http_test_client& operator=(http_test_client&&) noexcept;

  ~http_test_client() noexcept;

  // modifies span. In case of partial send bytes will not be empty
  void try_send_all(std::span<const std::byte>& bytes) noexcept;

  // returns false in case of error
  bool send_all(std::span<const std::byte> bytes) noexcept {
    try_send_all(bytes);
    return bytes.empty();
  }

  bool recv_bytes(std::span<std::byte>& bytes) noexcept;

  std::string read_response();

  auto get_connection() { return _connection; }

  [[nodiscard]] const std::string& get_host() const { return _host; }

  explicit operator bool() const noexcept { return _connection != server::socket_layer::invalid_connection; }
  [[nodiscard]] bool is_valid() const noexcept { return _connection != server::socket_layer::invalid_connection; }

  [[nodiscard]] std::string generate_req_head(server::http1::http_method method, std::string_view path) const;

 private:
  std::string _host;
  server::socket_layer::connection_id _connection = server::socket_layer::invalid_connection;
};
