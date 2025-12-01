#include <async_coro/await/await_callback.h>
#include <server/http1/compression_negotiation.h>
#include <server/http1/request.h>
#include <server/http1/response.h>
#include <server/http1/router.h>
#include <server/http1/session.h>
#include <server/socket_layer/connection.h>

#include <memory>
#include <memory_resource>
#include <type_traits>
#include <variant>

namespace server::http1 {

session::session(server::socket_layer::connection conn, const router& router, compression_pool::ptr pool) noexcept
    : _conn(std::move(conn)),
      _router(std::addressof(router)),
      _compression_pool(std::move(pool)),
      _negotiator(_compression_pool) {}

[[nodiscard]] async_coro::task<void> session::run() {  // NOLINT(*complexity)
  request req;
  response res{http_version::http_1_1};
  res.set_compression_pool(_compression_pool);

  while (!_conn.is_closed() && _keep_alive) {
    auto read_success = co_await req.read(_conn);
    if (!read_success) {
      if (!_conn.is_closed()) {
        res.set_status(std::move(read_success).error());
        co_await res.send(_conn);
      }
      continue;
    }

    if (req.get_version() < http_version::http_1_1) {
      res.set_status(status_code::http_version_not_supported);
      res.add_header(static_string{"Connection"}, static_string{"close"});
      co_await res.send(_conn);
      _conn.close_connection();
      co_return;
    }

    if (const auto* head = req.find_header("Connection")) {
      if (head->second == "close") {
        set_keep_alive(false);
      }
    }

    // Negotiate compression based on Accept-Encoding header
    if (_compression_pool != nullptr) {
      constexpr size_t k_preallocated_size = 16;

      std::array<std::byte, sizeof(encoding_preference) * k_preallocated_size> tmp_buffer;  // NOLINT(*init)
      std::pmr::monotonic_buffer_resource buf_mem_res{tmp_buffer.data(), tmp_buffer.size()};
      std::pmr::vector<encoding_preference> preferences{std::pmr::polymorphic_allocator<encoding_preference>{&buf_mem_res}};
      preferences.reserve(k_preallocated_size);

      req.foreach_header_with_name("Accept-Encoding", [&preferences](const auto& pair) {
        server::compression_negotiator::parse_accept_encoding(pair.second, preferences);
      });

      const auto negotiated_encoding = _negotiator.negotiate(std::span{preferences});

      // Set up encoder if compression is negotiated
      if (negotiated_encoding != compression_encoding::none) {
        res.set_encoding(negotiated_encoding);
      }
    }

    if (const auto* handler = _router->find_handler(req)) {
      if (const auto* advanced_f = std::get_if<router::advanced_handler_t>(handler)) {
        co_await (*advanced_f)(req, *this);
        continue;
      }

      if (const auto* simple_f = std::get_if<router::simple_handler_t>(handler)) {
        co_await (*simple_f)(req, res);
        if (!res.was_sent()) {
          co_await res.send(_conn);
        }
        res.clear();
        continue;
      }
    }
    res.set_status(status_code::not_found);
    co_await res.send(_conn);
    res.clear();
  }
}

}  // namespace server::http1
