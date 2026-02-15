#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace server {

struct tcp_server_config {
  std::string ip_address;
  uint16_t port;

  // Number of threads that will serve epoll\kqueue events
  std::uint32_t num_reactors = 2;

  // Max number of seconds that reactor thread will sleep while awaiting io events
  // Long sleeps may lead to long server termination time
  std::chrono::nanoseconds reactor_sleep = std::chrono::milliseconds{300};  // NOLINT(*magic*)

  // Disable Nagle's algorithm for all connections
  bool disable_delay = true;
};

}  // namespace server
