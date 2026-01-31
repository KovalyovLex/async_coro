#include "http_integration_fixture.h"

#include <async_coro/thread_safety/unique_lock.h>
#include <server/http1/http_error.h>
#include <server/http1/http_status_code.h>
#include <server/http1/response.h>
#include <server/tcp_server_config.h>

#include <semaphore>

void http_integration_fixture::SetUp() {
  setup_routes();

  start_server();
}

void http_integration_fixture::TearDown() {
  test_client = {};

  server.terminate();
  if (server_thread.joinable()) {
    server_thread.join();
  }

  async_coro::unique_lock lock{mutex};
  get_test_handler = nullptr;
  post_test_handler = nullptr;
}

void http_integration_fixture::open_connection() {
  test_client = http_test_client{"127.0.0.1", port};
  ASSERT_TRUE(test_client.is_valid());
}

void http_integration_fixture::setup_routes() {
  // register route that mirrors example server behavior
  server.get_router().add_route(server::http1::http_method::GET, "/test", [this](const server::http1::request& req, server::http1::response& resp) -> async_coro::task<> {  // NOLINT(*reference*)
    using namespace server::http1;

    decltype(get_test_handler) session_handler;

    {
      async_coro::unique_lock lock{mutex};
      session_handler = get_test_handler;
    }

    if (session_handler) {
      co_await session_handler(req, resp);
    } else {
      resp.set_status(status_code::not_found);
    }

    co_return;
  });

  server.get_router().add_route(server::http1::http_method::POST, "/test", [this](const server::http1::request& req, server::http1::response& resp) -> async_coro::task<> {  // NOLINT(*reference*)
    using namespace server::http1;

    decltype(post_test_handler) session_handler;

    {
      async_coro::unique_lock lock{mutex};
      session_handler = post_test_handler;
    }

    if (session_handler) {
      co_await session_handler(req, resp);
    } else {
      resp.set_status(status_code::not_found);
    }

    co_return;
  });

  server.get_router().add_route(server::http1::http_method::HEAD, "/test", [this](const server::http1::request& req, server::http1::response& resp) -> async_coro::task<> {  // NOLINT(*reference*)
    using namespace server::http1;

    decltype(head_test_handler) session_handler;

    {
      async_coro::unique_lock lock{mutex};
      session_handler = head_test_handler;
    }

    if (session_handler) {
      co_await session_handler(req, resp);
    } else {
      resp.set_status(status_code::not_found);
    }

    co_return;
  });
}

void http_integration_fixture::start_server() {
  std::binary_semaphore sem{0};

  server_thread = std::thread([this, &sem] {
    server::tcp_server_config conf;
    conf.ip_address = "127.0.0.1";
    conf.port = 0;
    conf.num_reactors = 1;

    server.serve(conf, {}, [this, &sem](const auto&, auto port) {
      this->port = port;
      sem.release();
    });
  });

  // wait for server to listen (retry connect)
  ASSERT_TRUE(sem.try_acquire_for(std::chrono::seconds(3))) << "Server did not start listening in time";
}
