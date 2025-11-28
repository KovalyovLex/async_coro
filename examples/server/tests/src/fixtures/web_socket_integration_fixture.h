#pragma once

#include <server/web_socket/request_frame.h>
#include <server/web_socket/ws_session.h>

#include <functional>
#include <string>

#include "fixtures/http_integration_fixture.h"
class web_socket_integration_tests : public http_integration_fixture {
 protected:
  void SetUp() override;

  void TearDown() override;

 protected:
  std::function<async_coro::task<>(const server::web_socket::request_frame&, server::web_socket::ws_session&)> chat_session_handler CORO_THREAD_GUARDED_BY(mutex);
  std::string accepted_protocols CORO_THREAD_GUARDED_BY(mutex);
};
