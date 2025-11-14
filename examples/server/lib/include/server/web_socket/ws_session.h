#pragma once

#include <async_coro/task.h>
#include <server/http1/request.h>
#include <server/socket_layer/connection.h>

#include <string_view>

namespace server::web_socket {

class request_frame;

class ws_session {
 public:
  using message_handler_t = async_coro::unique_function<async_coro::task<void>(const request_frame&, ws_session&) const>;

  explicit ws_session(server::socket_layer::connection conn) noexcept
      : _conn(std::move(conn)) {}

  // Runs request->response loop until connection closed or handler signals close.
  [[nodiscard]] async_coro::task<void> run(const server::http1::request& handshake_request, std::string_view protocol, message_handler_t handler);

  [[nodiscard]] auto& get_connection() noexcept { return _conn; }
  [[nodiscard]] const auto& get_connection() const noexcept { return _conn; }

  static std::string get_web_socket_key_result(std::string_view client_key);

 private:
  server::socket_layer::connection _conn;
};

}  // namespace server::web_socket
