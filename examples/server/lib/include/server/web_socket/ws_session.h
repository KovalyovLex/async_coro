#pragma once

#include <async_coro/task.h>
#include <server/http1/request.h>
#include <server/socket_layer/connection.h>
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_decompress.h>
#include <server/web_socket/ws_extension_parser.h>

#include <optional>
#include <string_view>

namespace server::web_socket {

class request_frame;
class response_frame;

class ws_session {
 public:
  using message_handler_t = async_coro::unique_function<async_coro::task<void>(const request_frame&, ws_session&) const>;

  explicit ws_session(server::socket_layer::connection conn) noexcept
      : _conn(std::move(conn)) {}

  // Runs request->response loop until connection closed or handler signals close.
  [[nodiscard]] async_coro::task<void> run(const server::http1::request& handshake_request, std::string_view protocol, message_handler_t handler);

  [[nodiscard]] auto& get_connection() noexcept { return _conn; }
  [[nodiscard]] const auto& get_connection() const noexcept { return _conn; }

  [[nodiscard]] async_coro::task<void> send_data(const response_frame& frame, std::span<const std::byte> data);

  [[nodiscard]] async_coro::task<void> begin_fragmented_send(const response_frame& frame, std::span<const std::byte> data);

  [[nodiscard]] async_coro::task<void> continue_fragmented_send(const response_frame& frame, std::span<const std::byte> data, bool last_chunk);

  static std::string get_web_socket_key_result(std::string_view client_key);

#if SERVER_HAS_ZLIB
  // Enable\disable permessage-deflate compression with specified configuration.
  // Must be called before run().
  void set_permessage_deflate(std::optional<permessage_deflate_config> config) noexcept;

  // Check if permessage-deflate can be used by this connection.
  [[nodiscard]] bool is_permessage_deflate_enabled() const noexcept { return _allowed_config.has_value(); }

  // Check if permessage-deflate used for this connection.
  [[nodiscard]] bool is_permessage_deflate_used() const noexcept { return _used_config.has_value(); }
#endif

 private:
  async_coro::task<void> send_data_impl(const response_frame& frame, std::span<const std::byte> data, bool last_chunk, uint8_t dec_code);

 private:
  server::socket_layer::connection _conn;

#if SERVER_HAS_ZLIB
  zlib_compress _compressor;
  zlib_decompress _decompressor;
  std::optional<permessage_deflate_config> _allowed_config;
  std::optional<permessage_deflate_config> _used_config;
#endif
};

}  // namespace server::web_socket
