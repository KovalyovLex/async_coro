#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace server {

struct tcp_server_config {
  std::string ip_address;
  uint16_t port;

  // number of threads than will serve epoll\kqueue events
  std::uint32_t num_reactors = 2;
  std::chrono::nanoseconds reactor_sleep = std::chrono::milliseconds{200};  // NOLINT(*-magic*)
};

}  // namespace server
