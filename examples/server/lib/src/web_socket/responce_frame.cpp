#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

namespace server::web_socket {

using data_buf_on_stack = std::array<std::byte, 1024UL * 4U>;  // NOLINT(*magic*)

// NOLINTBEGIN(*pointer*)

void response_frame::fill_frame_size(std::span<std::byte>& buffer_after_frame, frame_base& frame, size_t cont_length) noexcept {
  if (cont_length > frame_base::k_max_size_1_byte) {
    if (cont_length > frame_base::k_max_size_2_bytes) {
      // uint64
      frame.set_payload_len(frame_base::k_payload_len_8_bytes);

      const uint64_t len = cont_length;
      std::memcpy(buffer_after_frame.data(), &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        for (size_t i = 0; i < sizeof(len) / 2; i++) {
          std::swap(buffer_after_frame[i], buffer_after_frame[sizeof(len) - 1 - i]);
        }
      }

      buffer_after_frame = buffer_after_frame.subspan(sizeof(len));
    } else {
      // uint16
      frame.set_payload_len(frame_base::k_payload_len_2_bytes);

      const uint16_t len = cont_length;
      std::memcpy(buffer_after_frame.data(), &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        std::swap(buffer_after_frame[0], buffer_after_frame[1]);
      }

      buffer_after_frame = buffer_after_frame.subspan(sizeof(len));
    }
  } else {
    frame.set_payload_len(static_cast<uint8_t>(cont_length));
  }
}

async_coro::task<void> response_frame::send_error_and_close_connection(socket_layer::connection& conn, const ws_error& error) {
  using max_data_buf = std::array<std::byte, sizeof(frame_base) + sizeof(uint16_t) + ws_error::k_max_message_length>;

  union frame_union {  // NOLINT(*init*)
    max_data_buf buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {true, static_cast<uint8_t>(ws_op_code::connection_close)}};
  frame.frame.set_payload_len(error.get_error_message().size() + sizeof(uint16_t));

  auto* write_ptr = frame.buffer.data() + sizeof(frame_base);

  {
    const uint16_t status = error.get_status_code_dec();

    std::memcpy(write_ptr, &status, sizeof(status));

    if constexpr (std::endian::native != std::endian::big) {
      std::swap(write_ptr[0], write_ptr[1]);
    }

    write_ptr += sizeof(status);
  }

  const auto message = error.get_error_message();
  std::memcpy(write_ptr, message.data(), message.size());
  write_ptr += message.size();

  // no need to process errors as we closing connection
  co_await conn.write_buffer({frame.buffer.data(), write_ptr});

  conn.close_connection();
}

async_coro::task<void> response_frame::close_connection(socket_layer::connection& conn) {
  frame_begin frame{.frame{
      true,
      static_cast<uint8_t>(ws_op_code::connection_close),
  }};

  // no need to process errors as we closing connection
  co_await conn.write_buffer({frame.buffer.data(), sizeof(frame_base)});

  conn.close_connection();
}

// NOLINTEND(*pointer*)

}  // namespace server::web_socket
