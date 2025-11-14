#pragma once

#include <async_coro/config.h>
#include <async_coro/task.h>
#include <server/utils/expected.h>
#include <server/web_socket/frame_base.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_op_code.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace server::socket_layer {
class connection;
}

namespace server::web_socket {

// User friendly class for data frame. Stores payload - may be partitioned on frames
class request_frame {
  constexpr explicit request_frame(const frame_begin& frame) noexcept
      : is_final(frame.frame.is_final()),
        rsv1(frame.frame.is_rsv1()),
        rsv2(frame.frame.is_rsv2()),
        rsv3(frame.frame.is_rsv3()),
        opcode_dec(frame.frame.get_opcode()),
        payload_length(frame.frame.get_payload_len()) {
  }

 public:
  using mask_t = std::array<std::byte, 4>;

  explicit constexpr request_frame(ws_op_code code) noexcept
      : opcode_dec(static_cast<uint8_t>(code)) {}

  async_coro::task<expected<void, std::string>> read_payload(socket_layer::connection& conn, std::span<const std::byte> rest_data_in_buffer);

  constexpr void set_op_code(ws_op_code code) noexcept { opcode_dec = static_cast<uint8_t>(code); }
  [[nodiscard]] constexpr ws_op_code get_op_code() const noexcept { return static_cast<ws_op_code>(opcode_dec); }

  [[nodiscard]] std::string_view get_payload_as_string() const noexcept {
    return {reinterpret_cast<const char*>(payload.get()), payload_length};  // NOLINT(*reinterpret-cast)
  }

  static inline auto make_frame(const frame_begin& frame_beg, size_t buffer_len) noexcept;

 public:
  std::unique_ptr<std::byte[]> payload;  // NOLINT(*array*)
  uint64_t payload_length = 0;
  std::optional<mask_t> mask;
  uint8_t opcode_dec;
  bool is_final = true;
  bool rsv1 = false;
  bool rsv2 = false;
  bool rsv3 = false;
};

auto request_frame::make_frame(const frame_begin& frame_beg, size_t buffer_len) noexcept {
  struct frame_result {
    request_frame frame;
    std::span<const std::byte> rest_data_in_buffer;
  };

  using result_t = expected<frame_result, web_socket::ws_error>;

  frame_result res{.frame{frame_beg}};

  if (buffer_len < 2) {
    return result_t{unexpect, ws_error{ws_status_code::policy_violation, "Too small buffer were read"}};
  }

  uint32_t next_data_byte = 2;
  // NOLINTBEGIN(*magic*, *constant-array-index*)
  if (res.frame.payload_length == frame_base::k_payload_len_2_bytes) {
    // uint16 big endian
    if (buffer_len < 4) {
      return result_t{unexpect, ws_error{ws_status_code::policy_violation, "Too small amount of bytes to read uint16 payload_length"}};
    }

    res.frame.payload_length = uint32_t(frame_beg.buffer[2]);
    res.frame.payload_length |= uint32_t(frame_beg.buffer[3]) << 8U;
    if (res.frame.payload_length <= frame_base::k_max_size_1_byte) {
      return result_t{unexpect, ws_error{ws_status_code::protocol_error, "The minimum number of bits must be used instead of uint16"}};
    }
    next_data_byte = 4;
  } else if (res.frame.payload_length == frame_base::k_payload_len_8_bytes) {
    // uint64 big endian
    if (buffer_len < 10) {
      return result_t{unexpect, ws_error{ws_status_code::policy_violation, "Too small amount of bytes to read uint64 payload_length"}};
    }

    res.frame.payload_length = uint64_t(frame_beg.buffer[2]);
    res.frame.payload_length |= uint64_t(frame_beg.buffer[3]) << 8U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[4]) << 16U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[5]) << 24U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[6]) << 32U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[7]) << 40U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[8]) << 48U;
    res.frame.payload_length |= uint64_t(frame_beg.buffer[9]) << 56U;

    if (res.frame.payload_length <= frame_base::k_max_size_2_bytes) {
      return result_t{unexpect, ws_error{ws_status_code::protocol_error, "The minimum number of bits must be used instead of uint64"}};
    }
    if (res.frame.payload_length == std::numeric_limits<uint64_t>::max()) {
      return result_t{unexpect, ws_error{ws_status_code::protocol_error, "The most significant bit must be zero"}};
    }

    next_data_byte = 10;
  }

  if (frame_beg.frame.is_masked()) {
    // uint32 big endian
    if (buffer_len < next_data_byte + 4) {
      return result_t{unexpect, ws_error{ws_status_code::protocol_error, "Too small amount of bytes for read mask"}};
    }

    mask_t mask_val;
    mask_val[0] = frame_beg.buffer[next_data_byte++];
    mask_val[1] = frame_beg.buffer[next_data_byte++];
    mask_val[2] = frame_beg.buffer[next_data_byte++];
    mask_val[3] = frame_beg.buffer[next_data_byte++];

    res.frame.mask = mask_val;
  }

  if (res.frame.opcode_dec >= k_control_codes_begin && res.frame.payload_length > frame_base::k_max_size_1_byte) {
    return result_t{unexpect, ws_error{ws_status_code::protocol_error, "Control frames must have up to 125 bytes"}};
  }

  res.rest_data_in_buffer = std::span{frame_beg.buffer.data() + next_data_byte, frame_beg.buffer.size() - next_data_byte};  // NOLINT(*pointer*)

  return result_t{std::move(res)};
  // NOLINTEND(*magic*, *constant-array-index*)
}

}  // namespace server::web_socket
