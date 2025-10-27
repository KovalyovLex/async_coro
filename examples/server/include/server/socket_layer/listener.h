#pragma once

#include <async_coro/utils/unique_function.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "connection_id.h"

namespace server::socket_layer {

class reactor;

// Creates wrapper on TCP socket for listening connections
class listener {
 public:
  using handler = async_coro::unique_function<void(connection_id, std::string_view)>;

  listener();
  listener(const listener&) = delete;
  listener(listener&&) = delete;
  ~listener();

  listener& operator=(const listener&) = delete;
  listener& operator=(listener&&) = delete;

  // blocks until a fatal error happened or shutdown requested
  bool serve(const std::string& ip_address, uint16_t port, handler new_connection_handler, reactor& reactor, std::string* error_message);

  void terminate();

 private:
  bool _fatal_error{false};
  std::atomic_bool _has_data{false};
  std::atomic_bool _terminating{false};
};

}  // namespace server::socket_layer
