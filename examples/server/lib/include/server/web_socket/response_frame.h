#pragma once

#include <async_coro/task.h>
#include <server/utils/expected.h>
#include <server/web_socket/frame_base.h>
#include <server/web_socket/op_code.h>
#include <server/web_socket/status_code.h>

#include <span>

namespace server::socket_layer {
class connection;
}

namespace server::web_socket {

class error;

class response_frame {
 public:
  explicit constexpr response_frame(op_code code) noexcept
      : opcode_dec(static_cast<uint8_t>(code)) {}
  explicit constexpr response_frame(uint8_t code) noexcept
      : opcode_dec(code) {}

  async_coro::task<expected<void, std::string>> send_data(socket_layer::connection& conn, std::span<const std::byte> data);

  static async_coro::task<void> send_error_and_close_connection(socket_layer::connection& conn, const error& error);

  static async_coro::task<void> close_connection(socket_layer::connection& conn);

 private:
  static constexpr size_t get_frame_size_by_content_length(size_t cont_length) noexcept {
    size_t size = sizeof(frame_base);
    if (cont_length > 125U) {  // NOLINT(*magic*)
      size += cont_length > (std::numeric_limits<uint16_t>::max() - 1) ? sizeof(uint64_t) : sizeof(uint16_t);
    }
    return size;
  }

 public:
  uint8_t opcode_dec;
  bool rsv1 = false;
  bool rsv2 = false;
  bool rsv3 = false;
};

}  // namespace server::web_socket
