#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/web_socket/error.h>
#include <server/web_socket/response_frame.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

namespace server::web_socket {

// NOLINTBEGIN(*pointer*)

async_coro::task<expected<void, std::string>> response_frame::send_data(socket_layer::connection& conn, std::span<const std::byte> data) {
  using result_t = expected<void, std::string>;

  frame_begin frame;
  auto* write_ptr = frame.buffer.data() + sizeof(frame_base);

  frame.frame = {.is_final = 1U,
                 .rsv1 = static_cast<uint8_t>(rsv1),
                 .rsv2 = static_cast<uint8_t>(rsv2),
                 .rsv3 = static_cast<uint8_t>(rsv3),
                 .opcode = opcode_dec};

  if (data.size() > frame_base::k_max_size_1_byte) {
    if (data.size() > frame_base::k_max_size_2_bytes) {
      // uint64
      frame.frame.payload_len = frame_base::k_payload_len_8_bytes;

      const uint64_t len = data.size();
      std::memcpy(write_ptr, &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        for (size_t i = 0; i < sizeof(len) / 2; i++) {
          std::swap(write_ptr[i], write_ptr[sizeof(len) - 1 - i]);
        }
      }

      write_ptr += sizeof(len);
    } else {
      // uint16
      frame.frame.payload_len = frame_base::k_payload_len_2_bytes;

      const uint16_t len = data.size();
      std::memcpy(write_ptr, &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        std::swap(write_ptr[0], write_ptr[1]);
      }

      write_ptr += sizeof(len);
    }
  } else {
    frame.frame.payload_len = static_cast<uint8_t>(data.size());
  }

  const auto prev_delay = conn.is_no_delay();
  conn.set_no_delay(false);
  auto res = co_await conn.write_buffer({frame.buffer.data(), write_ptr});
  if (!res) {
    conn.set_no_delay(prev_delay);
    co_return result_t{unexpect, std::move(res).error()};
  }

  conn.set_no_delay(prev_delay);
  res = co_await conn.write_buffer(data);
  if (!res) {
    co_return result_t{unexpect, std::move(res).error()};
  }

  co_return result_t{};
}

async_coro::task<void> response_frame::send_error_and_close_connection(socket_layer::connection& conn, const error& error) {
  using max_data_buf = std::array<std::byte, sizeof(frame_base) + sizeof(uint16_t) + error::k_max_message_length>;

  union frame_union {  // NOLINT(*init*)
    max_data_buf buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {
                        .is_final = 1,
                        .opcode = static_cast<uint8_t>(op_code::connection_close),
                        .payload_len = static_cast<uint8_t>(error.get_error_message().size() + sizeof(uint16_t))}};

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
      .is_final = 1,
      .opcode = static_cast<uint8_t>(op_code::connection_close),
  }};

  // no need to process errors as we closing connection
  co_await conn.write_buffer({frame.buffer.data(), sizeof(frame_base)});

  conn.close_connection();
}

// NOLINTEND(*pointer*)

}  // namespace server::web_socket
