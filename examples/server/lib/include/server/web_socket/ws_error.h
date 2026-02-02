#pragma once

#include <server/web_socket/ws_status_code.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace server::web_socket {

class ws_error {
 public:
  static constexpr size_t k_max_message_length = 125U - sizeof(uint16_t);

  constexpr ws_error(uint16_t code, std::string_view msg) noexcept  // NOLINT(*member-init*)
      : _code(code),
        _buf_len(static_cast<uint16_t>(std::min(k_max_message_length, msg.size()))) {
    for (uint16_t i = 0; i < _buf_len; i++) {
      _message_buf[i] = msg[i];  // NOLINT(*array*)
    }
  }

  constexpr ws_error(ws_status_code code, std::string_view msg) noexcept
      : ws_error(static_cast<uint16_t>(code), msg) {
  }

  [[nodiscard]] constexpr std::string_view get_error_message() const noexcept {
    return {_message_buf.data(), _buf_len};
  }

  [[nodiscard]] constexpr ws_status_code get_status_code() const noexcept {
    return static_cast<ws_status_code>(_code);
  }

  [[nodiscard]] constexpr uint16_t get_status_code_dec() const noexcept {
    return _code;
  }

 private:
  uint16_t _code;
  uint16_t _buf_len = 0;
  std::array<char, k_max_message_length> _message_buf;  // NOLINT(*magic*)
};

}  // namespace server::web_socket
