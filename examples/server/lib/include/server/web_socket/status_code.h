#pragma once

#include <cstdint>

namespace server::web_socket {

enum class status_code : uint16_t {
  normal_closure = 1000,
  going_away = 1001,
  protocol_error = 1002,
  unsupported_data = 1003,
  invalid_frame_payload_data = 1007,
  policy_violation = 1008,
  message_too_big = 1009,
  mandatory_exit = 1010, /* must be used by clients only */
  internal_error = 1011,
  service_restart = 1012,
  try_again_later = 1013,
  bad_gateway = 1014,
};

}
