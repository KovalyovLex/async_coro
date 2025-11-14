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

async_coro::task<expected<void, std::string>> response_frame::send_data(socket_layer::connection& conn, std::span<const std::byte> data) const {
  using result_t = expected<void, std::string>;

  union frame_union {  // NOLINT(*init*)
    data_buf_on_stack buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {true,
                              _opcode_dec,
                              rsv1,
                              rsv2,
                              rsv3}};

  auto* write_ptr = fill_frame_size(frame.buffer.data() + sizeof(frame_base), frame.frame, data.size());

  const auto count_to_copy = std::min(frame.buffer.size() - (write_ptr - frame.buffer.data()), data.size());
  std::memcpy(write_ptr, data.data(), count_to_copy);
  write_ptr += count_to_copy;

  data = data.subspan(count_to_copy);

  auto res = co_await conn.write_buffer({frame.buffer.data(), write_ptr});
  if (!res) {
    co_return result_t{unexpect, std::move(res).error()};
  }

  if (!data.empty()) {
    res = co_await conn.write_buffer(data);
    if (!res) {
      co_return result_t{unexpect, std::move(res).error()};
    }
  }

  co_return result_t{};
}

std::byte* response_frame::fill_frame_size(std::byte* buffer_after_frame, frame_base& frame, size_t cont_length) noexcept {
  if (cont_length > frame_base::k_max_size_1_byte) {
    if (cont_length > frame_base::k_max_size_2_bytes) {
      // uint64
      frame.set_payload_len(frame_base::k_payload_len_8_bytes);

      const uint64_t len = cont_length;
      std::memcpy(buffer_after_frame, &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        for (size_t i = 0; i < sizeof(len) / 2; i++) {
          std::swap(buffer_after_frame[i], buffer_after_frame[sizeof(len) - 1 - i]);
        }
      }

      buffer_after_frame += sizeof(len);
    } else {
      // uint16
      frame.set_payload_len(frame_base::k_payload_len_2_bytes);

      const uint16_t len = cont_length;
      std::memcpy(buffer_after_frame, &len, sizeof(len));

      if constexpr (std::endian::native != std::endian::big) {
        std::swap(buffer_after_frame[0], buffer_after_frame[1]);
      }

      buffer_after_frame += sizeof(len);
    }
  } else {
    frame.set_payload_len(static_cast<uint8_t>(cont_length));
  }

  return buffer_after_frame;
}

async_coro::task<expected<void, std::string>> response_frame::begin_fragmented_send(socket_layer::connection& conn, std::span<const std::byte> data) const {
  using result_t = expected<void, std::string>;

  union frame_union {  // NOLINT(*init*)
    data_buf_on_stack buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {false,
                              _opcode_dec,
                              rsv1,
                              rsv2,
                              rsv3}};
  auto* write_ptr = fill_frame_size(frame.buffer.data() + sizeof(frame_base), frame.frame, data.size());

  const auto count_to_copy = std::min(frame.buffer.size() - (write_ptr - frame.buffer.data()), data.size());
  std::memcpy(write_ptr, data.data(), count_to_copy);
  write_ptr += count_to_copy;

  data = data.subspan(count_to_copy);

  auto res = co_await conn.write_buffer({frame.buffer.data(), write_ptr});
  if (!res) {
    co_return result_t{unexpect, std::move(res).error()};
  }

  if (!data.empty()) {
    res = co_await conn.write_buffer(data);
    if (!res) {
      co_return result_t{unexpect, std::move(res).error()};
    }
  }

  co_return result_t{};
}

async_coro::task<expected<void, std::string>> response_frame::continue_fragmented_send(socket_layer::connection& conn, std::span<const std::byte> data, bool last_chunk) const {
  using result_t = expected<void, std::string>;

  union frame_union {  // NOLINT(*init*)
    data_buf_on_stack buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {last_chunk,
                              static_cast<uint8_t>(ws_op_code::continuation),
                              rsv1,
                              rsv2,
                              rsv3}};

  auto* write_ptr = fill_frame_size(frame.buffer.data() + sizeof(frame_base), frame.frame, data.size());

  const auto count_to_copy = std::min(frame.buffer.size() - (write_ptr - frame.buffer.data()), data.size());
  std::memcpy(write_ptr, data.data(), count_to_copy);
  write_ptr += count_to_copy;

  data = data.subspan(count_to_copy);

  auto res = co_await conn.write_buffer({frame.buffer.data(), write_ptr});
  if (!res) {
    co_return result_t{unexpect, std::move(res).error()};
  }

  if (!data.empty()) {
    res = co_await conn.write_buffer(data);
    if (!res) {
      co_return result_t{unexpect, std::move(res).error()};
    }
  }

  co_return result_t{};
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
