#include <async_coro/config.h>
#include <server/ssl_config.h>
#include <server/tcp_server.h>
#include <server/tcp_server_config.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>

namespace server {

tcp_server::tcp_server() = default;

void tcp_server::serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf) {
  ASYNC_CORO_ASSERT(_is_serving.load(std::memory_order::relaxed) == false);
  ASYNC_CORO_ASSERT(_reactors == nullptr);

  _is_serving.store(true, std::memory_order::relaxed);

  ASYNC_CORO_ASSERT(conf.num_reactors > 0);

  _num_reactors = conf.num_reactors;
  _reactors = std::make_unique<reactor_storage[]>(conf.num_reactors);  // NOLINT(*c-arrays*)
  auto sleep = conf.reactor_sleep;

  for (size_t i = 0; i < _num_reactors; i++) {
    auto& react = _reactors[i];

    react.thread = std::thread([this, &react, sleep]() {
      while (!_is_terminating.load(std::memory_order::relaxed)) {
        react.reactor_instance.process_loop(sleep);
      }
    });
  }

  if (ssl_conf) {
    _ssl_context = socket_layer::ssl_context{ssl_conf->key_path, ssl_conf->cert_path, ssl_conf->ciphers};
  }

  std::string error_msg;
  const auto success = _listener.serve(conf.ip_address, conf.port, [this](socket_layer::connection_id conn, std::string_view address) { this->new_connection(conn, address); }, _reactors[0].reactor_instance, &error_msg);
  if (!success) {
    std::cerr << error_msg << "\n";
  }

  _is_serving.store(false, std::memory_order::release);
  _is_serving.notify_one();
}

void tcp_server::new_connection(socket_layer::connection_id conn, std::string_view address) {
}

void tcp_server::terminate() {
  _is_terminating.store(true, std::memory_order::release);
  _listener.terminate();

  for (size_t i = 0; i < _num_reactors; i++) {
    auto& react = _reactors[i];

    if (react.thread.joinable()) {
      react.thread.join();
    }
  }

  _reactors = nullptr;

  _is_serving.wait(false, std::memory_order::acquire);
}

tcp_server::~tcp_server() {
  terminate();
}

void new_connection(socket_layer::connection_id conn, std::string_view address) {
}

}  // namespace server
