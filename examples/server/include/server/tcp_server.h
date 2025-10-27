#pragma once

#include <openssl/ssl.h>
#include <server/socket_layer/listener.h>
#include <server/socket_layer/reactor.h>
#include <server/socket_layer/ssl_context.h>
#include <server/ssl_config.h>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

namespace server {

struct tcp_server_config;

class tcp_server {
 public:
  tcp_server();

  tcp_server(const tcp_server&) = delete;
  tcp_server(tcp_server&&) = delete;
  ~tcp_server();

  tcp_server& operator=(const tcp_server&) = delete;
  tcp_server& operator=(tcp_server&&) = delete;

  // starts listening of socket and blocks current thread
  void serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf);

  void terminate();

 private:
  void new_connection(socket_layer::connection_id conn, std::string_view address);

 private:
  struct reactor_storage {
    socket_layer::reactor reactor_instance;
    std::thread thread;
  };

  socket_layer::ssl_context _ssl_context;
  socket_layer::listener _listener;
  std::unique_ptr<reactor_storage[]> _reactors;  // NOLINT(*c-arrays*)
  size_t _num_reactors = 0;
  std::atomic_bool _is_terminating = false;
  std::atomic_bool _is_serving = false;
};

}  // namespace server
