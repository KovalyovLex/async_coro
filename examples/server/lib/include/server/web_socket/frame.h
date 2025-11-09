#pragma once

#include <async_coro/config.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace server::web_socket {

// bit mapped structure of beginning of the frame. See https://datatracker.ietf.org/doc/html/rfc6455#section-5.2
struct frame_base {
  constexpr frame_base() noexcept = default;

  uint8_t is_final : 1 {};
  uint8_t rsv1 : 1 {};
  uint8_t rsv2 : 1 {};
  uint8_t rsv3 : 1 {};
  uint8_t opcode : 4 {};
  uint8_t mask : 1 {};
  uint8_t payload_len : 7 {};
};

union frame_struct {
  std::array<std::byte, 14> buffer{};  // NOLINT(*magic*) +8 for len +4 for mask
  frame_base frame;
};

// User friendly class for data frame
class frame {
 public:
  enum class op_code : uint8_t {
    continuation = 0,
    text_frame = 1,
    binary_frame = 2,
    // reserved for non control codes
    connection_close = 8,
    ping = 9,
    pong = 10,
    // reserved for control
  };

  constexpr frame() noexcept = default;

  frame(const frame_struct& frame, ASYNC_CORO_ASSERT_VARIABLE size_t buffer_len) noexcept
      : is_final(frame.frame.is_final),
        rsv1(frame.frame.rsv1),
        rsv2(frame.frame.rsv2),
        rsv3(frame.frame.rsv3),
        opcode(frame.frame.opcode),
        payload_length(frame.frame.payload_len) {
    ASYNC_CORO_ASSERT(buffer_len >= 2);

    uint32_t next_data_byte = 2;
    // NOLINTBEGIN(*magic*, *constant-array-index*)
    if (payload_length == 126U) {
      // uint16 big endian
      ASYNC_CORO_ASSERT(buffer_len >= 4);

      payload_length = uint32_t(frame.buffer[2]);
      payload_length |= uint32_t(frame.buffer[3]) << 8U;
      next_data_byte = 4;
    } else if (payload_length == 127U) {
      // uint64 big endian
      ASYNC_CORO_ASSERT(buffer_len >= 10);

      payload_length = uint64_t(frame.buffer[2]);
      payload_length |= uint64_t(frame.buffer[3]) << 8U;
      payload_length |= uint64_t(frame.buffer[4]) << 16U;
      payload_length |= uint64_t(frame.buffer[5]) << 24U;
      payload_length |= uint64_t(frame.buffer[6]) << 32U;
      payload_length |= uint64_t(frame.buffer[7]) << 40U;
      payload_length |= uint64_t(frame.buffer[8]) << 48U;
      payload_length |= uint64_t(frame.buffer[9]) << 56U;
      next_data_byte = 10;
    }

    if (frame.frame.mask) {
      // uint32 big endian
      ASYNC_CORO_ASSERT(buffer_len >= next_data_byte + 4);

      auto mask_val = uint32_t(frame.buffer[next_data_byte++]);
      mask_val |= uint32_t(frame.buffer[next_data_byte++]) << 8U;
      mask_val |= uint32_t(frame.buffer[next_data_byte++]) << 16U;
      mask_val |= uint32_t(frame.buffer[next_data_byte++]) << 24U;
      mask = mask_val;
    }
    // NOLINTEND(*magic*, *constant-array-index*)
  }

  std::unique_ptr<std::byte[]> payload;  // NOLINT(*array*)
  uint64_t payload_length = 0;
  std::optional<uint32_t> mask;
  uint8_t opcode = static_cast<uint8_t>(op_code::continuation);
  bool is_final = true;
  bool rsv1 = false;
  bool rsv2 = false;
  bool rsv3 = false;

  void set_op_code(op_code code) noexcept { opcode = static_cast<uint8_t>(code); }
  [[nodiscard]] op_code get_op_code(op_code code) const noexcept { return static_cast<op_code>(opcode); }
};

}  // namespace server::web_socket
