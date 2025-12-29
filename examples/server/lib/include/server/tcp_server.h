#pragma once

#include <async_coro/utils/function_view.h>
#include <server/socket_layer/connection.h>
#include <server/socket_layer/ssl_context.h>
#include <server/ssl_config.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace server {

struct tcp_server_config;

class tcp_server {
 public:
  using connection_callback_t = async_coro::function_view<void(socket_layer::connection)>;
  using listener_connection_opened_t = async_coro::function_view<void(std::string, uint16_t)>;

  tcp_server();

  tcp_server(const tcp_server&) = delete;
  tcp_server(tcp_server&&) = delete;
  ~tcp_server();

  tcp_server& operator=(const tcp_server&) = delete;
  tcp_server& operator=(tcp_server&&) = delete;

  // starts listening of socket and blocks current thread. On SIGTERM or SIGINT will be stopped (terminated)
  void serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf, connection_callback_t on_connected, listener_connection_opened_t on_listener_open = {});

  void terminate();

 private:
  struct reactor_storage;

  socket_layer::ssl_context _ssl_context;
  std::unique_ptr<reactor_storage[]> _reactors;  // NOLINT(*c-arrays*)
  size_t _num_reactors = 0;
  std::atomic_bool _is_terminating = false;
  std::atomic_bool _is_serving = false;
  std::atomic_bool _has_connections = false;
};

}  // namespace server
