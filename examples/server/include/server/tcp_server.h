#pragma once

#include <async_coro/utils/unique_function.h>
#include <server/socket_layer/connection.h>
#include <server/socket_layer/ssl_context.h>
#include <server/ssl_config.h>

#include <atomic>
#include <memory>
#include <optional>

namespace server {

struct tcp_server_config;

class tcp_server {
 public:
  using connection_callback_t = async_coro::unique_function<void(socket_layer::connection)>;

  tcp_server();

  tcp_server(const tcp_server&) = delete;
  tcp_server(tcp_server&&) = delete;
  ~tcp_server();

  tcp_server& operator=(const tcp_server&) = delete;
  tcp_server& operator=(tcp_server&&) = delete;

  // starts listening of socket and blocks current thread
  void serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf, connection_callback_t on_connected);

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
