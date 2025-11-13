#pragma once

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace server::web_socket {

// bit mapped structure of beginning of the frame. See https://datatracker.ietf.org/doc/html/rfc6455#section-5.2
struct frame_base {
  static constexpr u_int8_t k_max_size_1_byte = 125U;
  static constexpr uint16_t k_max_size_2_bytes = std::numeric_limits<uint16_t>::max() - 1;
  static constexpr uint64_t k_max_size_8_bytes = std::numeric_limits<uint64_t>::max() - 1;
  static constexpr u_int8_t k_payload_len_2_bytes = 126U;
  static constexpr u_int8_t k_payload_len_8_bytes = 127U;

  uint8_t is_final : 1 {};
  uint8_t rsv1 : 1 {};
  uint8_t rsv2 : 1 {};
  uint8_t rsv3 : 1 {};
  uint8_t opcode : 4 {};
  uint8_t mask : 1 {};
  uint8_t payload_len : 7 {};
};

// Big enough structure to read the beginning of frame
union frame_begin {
  std::array<std::byte, 14> buffer{};  // NOLINT(*magic*) +8 for len +4 for mask
  frame_base frame;
};

}  // namespace server::web_socket
