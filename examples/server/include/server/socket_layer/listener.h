#pragma once

#include <async_coro/utils/unique_function.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "connection_id.h"

namespace server::socket_layer {

// Creates wrapper on TCP socket for listening connections
class listener {
 public:
  using handler = async_coro::unique_function<void(connection_id, std::string_view)>;

  listener();

  // blocks until a fatal error happened or shutdown requested
  bool serve(const std::string& ip_address, uint16_t port, handler new_connection_handler, std::string* error_message);

  // thread safe
  void shut_down() noexcept;

 private:
  std::atomic_bool _is_terminating{false};
  bool _fatal_error{false};
};

}  // namespace server::socket_layer
