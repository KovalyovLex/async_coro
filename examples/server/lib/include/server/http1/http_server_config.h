#pragma once

#include <server/tcp_server_config.h>

#include <chrono>
#include <optional>

namespace server::http1 {

struct http_server_config {
  // TCP part of server configuration
  server::tcp_server_config tcp_config;

  // Optional keep-alive timeout for sessions created by HTTP server
  // If empty, no explicit keep-alive timeout is configured (5 min default used)
  std::optional<std::chrono::seconds> keep_alive_timeout;

  // Optional max requests per keep-alive connection. If empty, unlimited.
  std::optional<std::uint32_t> max_requests;
};

}  // namespace server::http1
