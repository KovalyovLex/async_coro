#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "connection_id.h"

namespace server::socket_layer {

class reactor;

// Creates wrapper on TCP socket for listening connections
class listener {
 public:
  enum class listen_result_type : uint8_t {
    connected,
    wait_for_connections
  };

  struct connection_result {
    connection_id connection;
    listen_result_type type;
    // optionally resolved host name with getnameinfo if buffer was provided
    std::string_view host_name;
    // optional resolved service name with getnameinfo if buffer was provided
    std::string_view service_name;
  };

  listener();
  listener(const listener&) = delete;
  listener(listener&&) = delete;
  ~listener();

  listener& operator=(const listener&) = delete;
  listener& operator=(listener&&) = delete;

  // Opens connection to listen. Returns true if connections was opened successful
  bool open(const std::string& ip_address, uint16_t port, std::string* error_message);

  // Returns opened connection
  [[nodiscard]] connection_id get_connection() const noexcept { return _opened_connection; }

  // Trying to process queue of connections to accept. Returns new connection and host name of connection if host_hame_buffer was provided
  connection_result process_loop(std::span<char>* host_name_buffer, std::span<char>* service_name_buffer);

  void reset_opened_connection() { _opened_connection = invalid_connection; }

 private:
  connection_id _opened_connection = invalid_connection;
};

}  // namespace server::socket_layer
