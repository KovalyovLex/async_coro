#pragma once

#include <async_coro/task.h>
#include <server/http1/compression_negotiation.h>
#include <server/http1/router.h>
#include <server/socket_layer/connection.h>
#include <server/utils/compression_pool.h>

#include <chrono>
#include <cstdint>

namespace server::http1 {

class session_config {
 public:
  const router& router_ref;  // NOLINT(*ref-data*)
  compression_pool::ptr compression;
  std::optional<std::chrono::seconds> keep_alive_timeout;
  std::optional<std::uint32_t> max_requests;
};

async_coro::task<void> start_session(server::socket_layer::connection conn, const session_config& config);

}  // namespace server::http1
