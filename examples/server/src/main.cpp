

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <server/http1/http_method.h>
#include <server/http1/http_server.h>
#include <server/http1/response.h>
#include <server/tcp_server_config.h>

#include <csignal>
#include <memory>
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

  server.get_router().add_route(server::http1::http_method::GET, "/", say_hello);
  server.get_router().add_route(server::http1::http_method::HEAD, "/", say_hello);

  server.get_router().add_route(server::http1::http_method::GET, "/hello.html", send_html);
  server.get_router().add_route(server::http1::http_method::HEAD, "/hello.html", send_html);

  server.serve(conf, {});

  return 0;
}
