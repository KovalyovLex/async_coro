#pragma once

#include <async_coro/task.h>
#include <server/utils/expected.h>
#include <server/web_socket/frame_base.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_status_code.h>

#include <cstdint>
#include <span>

namespace server::socket_layer {
class connection;
}

namespace server::web_socket {

class ws_error;

class response_frame {
 public:
  explicit constexpr response_frame(ws_op_code code) noexcept
      : _opcode_dec(static_cast<uint8_t>(code)) {}
  explicit constexpr response_frame(uint8_t code) noexcept
      : _opcode_dec(code) {}

  [[nodiscard]] ws_op_code get_op_code() const noexcept { return static_cast<ws_op_code>(_opcode_dec); }
  [[nodiscard]] uint8_t get_op_code_dec() const noexcept { return _opcode_dec; }

  static async_coro::task<void> send_error_and_close_connection(socket_layer::connection& conn, const ws_error& error);

  static async_coro::task<void> close_connection(socket_layer::connection& conn);

  static void fill_frame_size(std::span<std::byte>& buffer_after_frame, frame_base& frame, size_t cont_length) noexcept;

 private:
  uint8_t _opcode_dec;

 public:
  bool rsv1 = false;
  bool rsv2 = false;
  bool rsv3 = false;
};

}  // namespace server::web_socket
