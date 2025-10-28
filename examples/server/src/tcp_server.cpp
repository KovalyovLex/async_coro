#include <async_coro/config.h>
#include <server/socket_layer/connection.h>
#include <server/socket_layer/listener.h>
#include <server/socket_layer/reactor.h>
#include <server/socket_layer/ssl_connection.h>
#include <server/ssl_config.h>
#include <server/tcp_server.h>
#include <server/tcp_server_config.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace server {

struct tcp_server::reactor_storage {
  socket_layer::reactor reactor_instance;
  std::thread thread;
};

tcp_server::tcp_server() = default;

void tcp_server::serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf, connection_callback_t on_connected) {
  ASYNC_CORO_ASSERT(_is_serving.load(std::memory_order::relaxed) == false);
  ASYNC_CORO_ASSERT(_reactors == nullptr);

  _is_serving.store(true, std::memory_order::relaxed);

  ASYNC_CORO_ASSERT(conf.num_reactors > 0);

  _num_reactors = conf.num_reactors;
  _reactors = std::make_unique<reactor_storage[]>(conf.num_reactors);  // NOLINT(*c-arrays*)

  for (size_t i = 0; i < _num_reactors; i++) {
    auto& react = _reactors[i];

    react.thread = std::thread([this, &react, sleep = conf.reactor_sleep]() {
      while (!_is_terminating.load(std::memory_order::relaxed)) {
        react.reactor_instance.process_loop(sleep);
      }
    });
  }

  if (ssl_conf) {
    _ssl_context = socket_layer::ssl_context{ssl_conf->key_path, ssl_conf->cert_path, ssl_conf->ciphers};
  }

  {
    std::string error_msg;
    socket_layer::listener listener{};
    if (!listener.open(conf.ip_address, conf.port, &error_msg)) {
      std::cerr << error_msg << "\n";
      _is_serving.store(false, std::memory_order::release);
      _is_serving.notify_one();
      return;
    }

    _has_connections.store(false, std::memory_order::release);

    auto& listener_reactor = _reactors[0].reactor_instance;
    size_t reactor_index = 0;

    auto listener_connection = socket_layer::connection{listener.get_connection(), listener_reactor, {}};
    listener_connection.check_subscribed();

    while (!_is_terminating.load(std::memory_order::relaxed)) {
      const auto result = listener.process_loop(nullptr, nullptr);

      if (result.type == socket_layer::listener::listen_result_type::connected) {
        socket_layer::ssl_connection ssl_conn;
        if (_ssl_context) {
          ssl_conn = {_ssl_context, result.connection};
        }
        socket_layer::connection conn{result.connection, _reactors[reactor_index++].reactor_instance, std::move(ssl_conn)};
        if (reactor_index >= _num_reactors) {
          reactor_index = 0;
        }

        on_connected(std::move(conn));
      } else if (result.type == socket_layer::listener::listen_result_type::wait_for_connections && !_has_connections.load(std::memory_order::relaxed)) {
        listener_reactor.continue_after_receive_data(listener_connection.get_connection_id(), listener_connection.get_subscription_index(), [this](auto) {
          _has_connections.store(true, std::memory_order::relaxed);
          _has_connections.notify_one();
        });

        _has_connections.wait(false, std::memory_order::relaxed);
        _has_connections.store(false, std::memory_order::relaxed);
      }
    }

    // reset connection as it will be closed in listener_connection
    listener.reset_opened_connection();
  }

  _is_serving.store(false, std::memory_order::release);
  _is_serving.notify_one();
}

void tcp_server::terminate() {
  _is_terminating.store(true, std::memory_order::release);
  _has_connections.store(true, std::memory_order::release);
  _has_connections.notify_one();

  for (size_t i = 0; i < _num_reactors; i++) {
    auto& react = _reactors[i];

    if (react.thread.joinable()) {
      react.thread.join();
    }
  }
}

tcp_server::~tcp_server() {
  terminate();
}

void new_connection(socket_layer::connection_id conn, std::string_view address) {
}

}  // namespace server
