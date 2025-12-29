#pragma once

#include <async_coro/task.h>
#include <async_coro/utils/unique_function.h>
#include <server/http1/http_method.h>

#include <string>
#include <variant>
#include <vector>

namespace server::http1 {

class session;
class request;
class response;

// lightweight router: match on method + path prefix
class router {
 public:
  using simple_handler_t = async_coro::unique_function<async_coro::task<void>(const request&, response&) const>;
  using advanced_handler_t = async_coro::unique_function<async_coro::task<void>(const request&, http1::session&) const>;
  using handler_t = std::variant<simple_handler_t, advanced_handler_t>;

  router() noexcept = default;
  router(const router&) = delete;
  router(router&&) noexcept = default;

  ~router() noexcept = default;

  router& operator=(const router&) = delete;
  router& operator=(router&&) noexcept = default;

  void add_route(http_method method, std::string path_prefix, simple_handler_t handler);

  void add_advanced_route(http_method method, std::string path_prefix, advanced_handler_t handler);

  // Finds a handler for the given request; returns nullptr if none found.
  [[nodiscard]] const handler_t* find_handler(const request& req) const;

 private:
  struct entry {
    std::string prefix;
    http_method method;
    handler_t handler;
  };
  std::vector<entry> _entries;
};

}  // namespace server::http1
