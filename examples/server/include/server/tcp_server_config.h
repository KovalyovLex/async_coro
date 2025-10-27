#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace server {

struct tcp_server_config {
  std::string ip_address;
  uint16_t port;
  std::uint32_t num_reactors;
  std::chrono::nanoseconds reactor_sleep = std::chrono::seconds{1};
};

}  // namespace server
