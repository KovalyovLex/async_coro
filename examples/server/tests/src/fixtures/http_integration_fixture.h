#pragma once

#include <async_coro/thread_safety/mutex.h>
#include <gtest/gtest.h>
#include <server/http1/http_server.h>

class http_integration_fixture : public ::testing::Test {
 protected:
  void SetUp() override;

  void TearDown() override;

  void open_connection();

  void setup_routes();

  void start_server();

 protected:
  async_coro::mutex mutex;
  std::function<async_coro::task<>(const server::http1::request&, server::http1::response&)> get_test_handler CORO_THREAD_GUARDED_BY(mutex);
  std::function<async_coro::task<>(const server::http1::request&, server::http1::response&)> post_test_handler CORO_THREAD_GUARDED_BY(mutex);
  server::http1::http_server server;
  std::thread server_thread;
  uint16_t port = 0;
  server::socket_layer::connection_id client_connection = server::socket_layer::invalid_connection;
};
