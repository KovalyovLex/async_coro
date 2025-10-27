#pragma once

#include <async_coro/utils/passkey.h>

#include <string>
#include <utility>

namespace server::socket_layer {

class ssl_connection;

class ssl_context {
 public:
  ssl_context(const std::string& key_path, const std::string& cert_path,
              const std::string& ciphers);

  ssl_context(const ssl_context&) = delete;
  ssl_context(ssl_context&& other) noexcept
      : _ctx(std::exchange(other._ctx, nullptr)) {}

  ~ssl_context() noexcept;

  ssl_context& operator=(const ssl_context&) = delete;
  ssl_context& operator=(ssl_context&& other) noexcept {
    _ctx = std::exchange(other._ctx, nullptr);
    return *this;
  }

  explicit operator bool() noexcept {
    return _ctx != nullptr;
  }

  void* get_context(async_coro::passkey<ssl_connection> /*key*/) noexcept {
    return _ctx;
  }

  static std::string get_ssl_error();

 private:
  void* _ctx = nullptr;
};

}  // namespace server::socket_layer
