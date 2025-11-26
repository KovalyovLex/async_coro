

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <server/http1/http_method.h>
#include <server/http1/http_server.h>
#include <server/http1/response.h>
#include <server/http1/session.h>
#include <server/tcp_server_config.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <csignal>
#include <iostream>
#include <memory>
#include <span>
#include <tracy/Tracy.hpp>

int main(int argc, char** argv) {
  TracySetProgramName("Simple server");

  int port = 8080;                          // NOLINT
  if (argc > 1) port = std::atoi(argv[1]);  // NOLINT

  server::http1::http_server server{
      std::make_unique<async_coro::execution_system>(
          async_coro::execution_system_config{
              .worker_configs = {{"worker1"},
                                 {"worker2"}},
              .main_thread_allowed_tasks = async_coro::execution_thread_mask{}})};

  server::tcp_server_config conf{
      .port = static_cast<uint16_t>(port),
  };

  const auto send_html = [](const auto& request, auto& resp) -> async_coro::task<> {
    std::string_view html_body = R"(<!doctype html>
<html>
  <body>

    <h1>Hello, world in an Html page</h1>
    <p>A Paragraph</p>

  </body>
</html>
)";

    resp.set_body(server::static_string{html_body}, server::http1::content_types::html);

    co_return;
  };

  const auto say_hello = [](const auto& request, auto& resp) -> async_coro::task<> {
    resp.set_body(server::static_string{"Hello world"}, server::http1::content_types::plain_text);

    co_return;
  };

  const auto web_socket_communication = [](const auto& request, server::http1::session& http_session) -> async_coro::task<> {
    using namespace server::web_socket;

    ws_session session{std::move(http_session.get_connection())};

    co_await session.run(request, "", [](const request_frame& req_frame, auto& this_session) -> async_coro::task<> {
      if (req_frame.get_op_code() == ws_op_code::text_frame) {
        response_frame resp{ws_op_code::text_frame};
        std::string answer = "Hello from server!\n";
        answer += "got: ";
        answer += req_frame.get_payload_as_string();

        co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));

        answer = "Message 2!";
        co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));

        answer = "Message 3!";
        co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));

        answer = "Echo:";
        co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));

        co_await this_session.send_data(resp, std::as_bytes(std::span{req_frame.get_payload_as_string()}));
      } else {
        ws_error error(ws_status_code::invalid_frame_payload_data, "Expected text");
        co_await response_frame::send_error_and_close_connection(this_session.get_connection(), error);
      }
    });
  };

  server.get_router().add_route(server::http1::http_method::GET, "/", say_hello);
  server.get_router().add_route(server::http1::http_method::HEAD, "/", say_hello);

  server.get_router().add_route(server::http1::http_method::GET, "/hello.html", send_html);
  server.get_router().add_route(server::http1::http_method::HEAD, "/hello.html", send_html);

  server.get_router().add_advanced_route(server::http1::http_method::GET, "/chat", web_socket_communication);

  server.serve(conf, {}, [](const auto& addr, auto port) {
    std::cout << "Server started listening on: " << addr << ":" << port;
  });

  return 0;
}
