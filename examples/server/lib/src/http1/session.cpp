#include <async_coro/await/await_callback.h>
#include <async_coro/await/cancel_after_time.h>
#include <async_coro/await/execute_after_time.h>
#include <async_coro/await/start_task.h>
#include <server/http1/compression_negotiation.h>
#include <server/http1/request.h>
#include <server/http1/response.h>
#include <server/http1/router.h>
#include <server/http1/session.h>
#include <server/socket_layer/connection.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>

namespace server::http1 {

async_coro::task<void> start_session(server::socket_layer::connection conn, const session_config& config) {  // NOLINT(*complexity)
  request req;
  response res{http_version::http_1_1};
  res.set_compression_pool(config.compression);

  compression_negotiator negotiator{config.compression};

  // Build keep-alive header prefix once if configured
  std::string keep_alive_prefix;
  if (config.keep_alive_timeout) {
    keep_alive_prefix = "timeout=";
    std::array<char, 16> buf{};  // NOLINT(*magic-numbers*)

    auto conv_res = std::to_chars(buf.data(), buf.data() + buf.size(), config.keep_alive_timeout->count());  // NOLINT(*pointer*)
    if (conv_res.ec == std::errc{}) {
      keep_alive_prefix.append(buf.data(), conv_res.ptr);
    } else {
      keep_alive_prefix += '0';
    }

    if (config.max_requests) {
      keep_alive_prefix += ", max=";
    }
  }

  bool keep_alive = true;
  uint32_t request_count = 0;

  struct cancel_token {};

  auto add_keep_alive = [&]() {
    if (config.keep_alive_timeout) {
      if (config.max_requests) {
        // removing all after last = - number of max requests
        keep_alive_prefix.erase(keep_alive_prefix.begin() + (long)(keep_alive_prefix.rfind('=') + 1), keep_alive_prefix.end());

        std::array<char, 16> buf{};  // NOLINT(*magic-numbers*)

        uint32_t remaining = *config.max_requests - request_count;
        auto max_res = std::to_chars(buf.data(), buf.data() + buf.size(), remaining);  // NOLINT(*pointer*)
        if (max_res.ec == std::errc{}) {
          keep_alive_prefix.append(buf.data(), max_res.ptr);
        } else {
          keep_alive_prefix += '0';
        }
      }

      res.add_header(static_string{"Keep-Alive"}, static_string{keep_alive_prefix});
    }
  };

  // Use keep_alive timeout if configured, otherwise use a reasonable default (30 sec)
  auto effective_timeout = config.keep_alive_timeout.has_value() ? *config.keep_alive_timeout : std::chrono::seconds{30};  // NOLINT(*magic*)

  while (!conn.is_closed() && keep_alive) {
    // Check if max requests reached
    if (config.max_requests && request_count >= *config.max_requests) {
      keep_alive = false;
      conn.close_connection();
      break;
    }

    request_count++;

    // Read with timeout
    auto read_res = co_await (co_await async_coro::start_task(req.read(conn)) || async_coro::execute_after_time([] { return cancel_token{}; }, effective_timeout));

    if (auto* read_success = std::get_if<0>(&read_res)) {
      if (!*read_success) {
        if (!conn.is_closed()) {
          res.set_status(std::move(*read_success).error());
          if (req.get_version() >= http_version::http_1_1) {
            add_keep_alive();
          }
          co_await res.send(conn);
        }
        continue;
      }
    } else {
      // Read timeout - send 408 Request Timeout
      if (!conn.is_closed()) {
        res.set_status(status_code::request_timeout);
        res.add_header(static_string{"Connection"}, static_string{"close"});
        co_await res.send(conn);
        conn.close_connection();
      }
      co_return;
    }

    // Check the http version
    if (req.get_version() != http_version::http_1_1) {
      res.set_status(status_code::http_version_not_supported);
      res.add_header(static_string{"Connection"}, static_string{"close"});
      co_await res.send(conn);
      conn.close_connection();
      co_return;
    }

    if (const auto* head = req.find_header("Connection")) {
      if (head->second == "close") {
        keep_alive = false;
      }
    } else if (!config.keep_alive_timeout) {
      keep_alive = false;
    }

    // Negotiate compression based on Accept-Encoding header
    if (config.compression != nullptr) {
      constexpr size_t k_preallocated_size = 16;

      std::array<std::byte, sizeof(encoding_preference) * k_preallocated_size> tmp_buffer;  // NOLINT(*init)
      std::pmr::monotonic_buffer_resource buf_mem_res{tmp_buffer.data(), tmp_buffer.size()};
      std::pmr::vector<encoding_preference> preferences{std::pmr::polymorphic_allocator<encoding_preference>{&buf_mem_res}};
      preferences.reserve(k_preallocated_size);

      req.foreach_header_with_name("Accept-Encoding", [&preferences](const auto& pair) {
        server::compression_negotiator::parse_accept_encoding(pair.second, preferences);
      });

      const auto negotiated_encoding = negotiator.negotiate(std::span{preferences});

      // Set up encoder if compression is negotiated
      if (negotiated_encoding != compression_encoding::none) {
        res.set_encoding(negotiated_encoding);
      }
    }

    if (const auto* handler = config.router_ref.find_handler(req)) {
      if (const auto* advanced_f = std::get_if<router::advanced_handler_t>(handler)) {
        // advanced handler should send all data on their own without any limitations
        co_await (*advanced_f)(req, conn);
        continue;
      }

      if (const auto* simple_f = std::get_if<router::simple_handler_t>(handler)) {
        co_await (*simple_f)(req, res);
      }
    } else {
      res.set_status(status_code::not_found);
    }

    if (!res.was_sent()) {
      add_keep_alive();
      co_await res.send(conn);
    }
    res.clear();
  }
}

}  // namespace server::http1
