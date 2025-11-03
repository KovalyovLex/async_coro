

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <server/http1/http_server.h>
#include <server/tcp_server_config.h>

#include <atomic>
#include <csignal>
#include <memory>

#include "async_coro/task.h"
#include "server/http1/http_method.h"
#include "server/http1/response.h"

std::atomic<server::http1::http_server*> global_server;  // NOLINT(*-non-const-*)

void signal_handler(int signal) {
  if (auto* serv = global_server.load(std::memory_order::acquire)) {
    serv->terminate();
  }
}

int main(int argc, char** argv) {
  int port = 8080;                          // NOLINT
  if (argc > 1) port = std::atoi(argv[1]);  // NOLINT

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  server::http1::http_server server{
      std::make_unique<async_coro::execution_system>(
          async_coro::execution_system_config{
              .worker_configs = {{"worker1"},
                                 {"worker2"}},
              .main_thread_allowed_tasks = async_coro::execution_thread_mask{}})};

  global_server.store(std::addressof(server), std::memory_order::relaxed);

  server::tcp_server_config conf{
      .port = static_cast<uint16_t>(port),
  };

  const auto send_html = [](const auto& request) -> async_coro::task<server::http1::response> {  // NOLINT(*reference*)
    using namespace server::http1;

    response res{request.get_version(), status_code::Ok};

    std::string_view html_body = R"(<!doctype html>
<html>
  <body>

    <h1>Hello, world in an Html page</h1>
    <p>A Paragraph</p>

  </body>
</html>
)";

    res.set_body(server::static_string{html_body}, content_types::html);

    co_return res;
  };

  const auto say_hello = [](const auto& request) -> async_coro::task<server::http1::response> {  // NOLINT(*reference*)
    using namespace server::http1;

    response res{request.get_version(), status_code::Ok};
    res.set_body(server::static_string{"Hello world"}, content_types::plain_text);

    co_return res;
  };

  server.get_router().add_route(server::http1::http_method::GET, "/", say_hello);
  server.get_router().add_route(server::http1::http_method::HEAD, "/", say_hello);

  server.get_router().add_route(server::http1::http_method::GET, "/hello.html", send_html);
  server.get_router().add_route(server::http1::http_method::HEAD, "/hello.html", send_html);

  server.serve(conf, {});

  global_server.store(nullptr, std::memory_order::relaxed);

  return 0;
}
