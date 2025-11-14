#include <server/http1/request.h>
#include <server/http1/router.h>

#include <string_view>

namespace server::http1 {

void router::add_route(http_method method, std::string path_prefix, simple_handler_t handler) {
  _entries.emplace_back(std::move(path_prefix), method, std::move(handler));
}

void router::add_advanced_route(http_method method, std::string path_prefix, advanced_handler_t handler) {
  _entries.emplace_back(std::move(path_prefix), method, std::move(handler));
}

const router::handler_t* router::find_handler(const request& req) const {
  std::size_t best_match = 0;
  const handler_t* result = nullptr;

  for (const auto& entry : _entries) {
    if (entry.method == req.get_method()) {
      if (req.get_target().starts_with(entry.prefix) && entry.prefix.size() > best_match) {  // prefix matches
        best_match = entry.prefix.size();
        result = std::addressof(entry.handler);
      }
    }
  }
  return result;
}

}  // namespace server::http1
