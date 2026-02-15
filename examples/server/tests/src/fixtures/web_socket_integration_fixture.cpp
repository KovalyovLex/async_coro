#include "web_socket_integration_fixture.h"

#include <async_coro/thread_safety/unique_lock.h>
#include <server/http1/session.h>
#include <server/web_socket/response_frame.h>

void web_socket_integration_tests::SetUp() {
  // register route that mirrors example server behavior
  server.get_router().add_advanced_route(server::http1::http_method::Get, "/chat", [this](const auto& request, server::socket_layer::connection& connection) -> async_coro::task<> {  // NOLINT(*reference*)
    using namespace server::web_socket;

    ws_session session{std::move(connection)};

    std::string protocol;
    decltype(chat_session_handler) session_handler;

    {
      async_coro::unique_lock lock{mutex};
      protocol = accepted_protocols;
      session_handler = chat_session_handler;
    }

    co_await session.run(request, protocol, [session_handler = std::move(session_handler)](const request_frame& req_frame, ws_session& this_session) -> async_coro::task<> {  // NOLINT(*reference*)
      if (session_handler) {
        co_await session_handler(req_frame, this_session);
        co_return;
      }

      if (req_frame.get_op_code() == ws_op_code::text_frame) {
        response_frame resp{ws_op_code::text_frame};
        std::string answer = "Hello from server!\n";
        answer += "got: ";
        answer += req_frame.get_payload_as_string();

        co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));
      } else {
        ws_error error(ws_status_code::invalid_frame_payload_data, "Expected text");
        co_await response_frame::send_error_and_close_connection(this_session.get_connection(), error);
      }
    });
  });

  start_server();
}

void web_socket_integration_tests::TearDown() {
  http_integration_fixture::TearDown();

  async_coro::unique_lock lock{mutex};
  accepted_protocols.clear();
  chat_session_handler = nullptr;
}
