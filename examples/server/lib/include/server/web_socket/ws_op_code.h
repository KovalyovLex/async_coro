#pragma once

#include <cstdint>

namespace server::web_socket {

enum class ws_op_code : uint8_t {
  continuation = 0,
  text_frame = 1,
  binary_frame = 2,
  // reserved for non control codes
  connection_close = 8,
  ping = 9,
  pong = 10,
  // reserved for control
};

static inline constexpr uint8_t k_control_codes_begin = 8;

}  // namespace server::web_socket
