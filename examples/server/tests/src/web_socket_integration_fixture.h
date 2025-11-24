#pragma once

#include <async_coro/thread_safety/mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <gtest/gtest.h>
#include <server/http1/http_server.h>
#include <server/http1/session.h>
#include <server/socket_layer/connection_id.h>
#include <server/tcp_server_config.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <functional>
#include <string>
#include <thread>

#include "ws_test_client.h"

class web_socket_integration_tests : public ::testing::Test {
 protected:
  void SetUp() override {
    // pick ephemeral port by binding to 0
    port = ws_test_client::pick_free_port();

    // register route that mirrors example server behavior
    server.get_router().add_advanced_route(server::http1::http_method::GET, "/chat", [this](const auto& request, server::http1::session& http_session) -> async_coro::task<> {  // NOLINT(*reference*)
      using namespace server::web_socket;

      ws_session session{std::move(http_session.get_connection())};

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

    server_thread = std::thread([this] {
      server::tcp_server_config conf;
      conf.ip_address = "127.0.0.1";
      conf.port = port;
      conf.num_reactors = 1;
      server.serve(conf, {});
    });

    // wait for server to listen (retry connect)
    auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(3);
    bool ok = false;
    while (std::chrono::steady_clock::now() < deadline) {
      auto s = ws_test_client::connect_blocking("127.0.0.1", port, 200);
      if (s != server::socket_layer::invalid_connection) {
        server::socket_layer::close_socket(s.get_platform_id());
        ok = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(ok) << "Server did not start listening in time";
  }

  void TearDown() override {
    server::socket_layer::close_socket(client_connection.get_platform_id());
    client_connection = server::socket_layer::invalid_connection;

    server.terminate();
    if (server_thread.joinable()) {
      server_thread.join();
    }

    async_coro::unique_lock lock{mutex};
    accepted_protocols.clear();
    chat_session_handler = nullptr;
  }

  void open_connection() {
    client_connection = server::socket_layer::invalid_connection;
    for (int i = 0; i < 10; ++i) {
      client_connection = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
      if (client_connection != server::socket_layer::invalid_connection) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

 protected:
  async_coro::mutex mutex;
  std::function<async_coro::task<>(const server::web_socket::request_frame&, server::web_socket::ws_session&)> chat_session_handler CORO_THREAD_GUARDED_BY(mutex);
  std::string accepted_protocols CORO_THREAD_GUARDED_BY(mutex);
  server::http1::http_server server;
  std::thread server_thread;
  uint16_t port = 0;
  server::socket_layer::connection_id client_connection = server::socket_layer::invalid_connection;
};
