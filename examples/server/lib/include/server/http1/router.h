#pragma once

#include <async_coro/utils/unique_function.h>

#include <string>
#include <vector>

#include "request.h"
#include "response.h"

namespace server::http1 {

// lightweight router: match on method + path prefix
class router {
 public:
  using handler_t = async_coro::unique_function<async_coro::task<void>(const request&, response&)>;

  router() noexcept = default;
  router(const router&) = delete;
  router(router&&) noexcept = default;

  ~router() noexcept = default;

  router& operator=(const router&) = delete;
  router& operator=(router&&) noexcept = default;

  void add_route(http_method method, std::string path_prefix, handler_t handler);

  // Finds a handler for the given request; returns nullptr if none found.
  [[nodiscard]] handler_t* find_handler(const request& req) const;

 private:
  struct entry {
    std::string prefix;
    http_method method;
    mutable handler_t handler;
  };
  std::vector<entry> _entries;
};

}  // namespace server::http1
