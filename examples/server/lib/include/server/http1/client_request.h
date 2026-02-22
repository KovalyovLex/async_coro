#pragma once

#include <async_coro/task.h>
#include <server/http1/http_method.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/utils/static_string.h>
#include <server/utils/string_storage.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server::http1 {

// Simple HTTP/1.x request builder that knows how to send itself over a
// socket_layer::connection using the reactor-driven awaitable API.  The
// implementation mirrors the server-side `response` class to keep memory
// overhead low and throughput high.
class client_request {
 public:
  client_request(http_method method, std::string_view target, http_version ver = http_version::http_1_1) noexcept;

  client_request(const client_request&) = delete;
  client_request(client_request&&) noexcept = default;
  client_request& operator=(const client_request&) = delete;
  client_request& operator=(client_request&&) noexcept = default;

  ~client_request() noexcept = default;

  void set_method(http_method method) noexcept { _method = method; }
  void set_target(static_string target) { _target = target.str; }
  void set_target(std::string_view target) { set_target(static_string{add_string(target)}); }
  void set_version(http_version version) noexcept { _version = version; }

  void add_header(static_string name, static_string value);
  void add_header(std::string name, std::string value) {
    add_header(static_string{add_string(std::move(name))}, static_string{add_string(std::move(value))});
  }
  void add_header(static_string name, std::string value) {
    add_header(name, static_string{add_string(std::move(value))});
  }

  void set_body(std::string body, static_string content_type);
  void set_body(std::string body, std::string content_type) {
    set_body(std::move(body), static_string{add_string(std::move(content_type))});
  }
  void set_body(static_string body, static_string content_type);

  [[nodiscard]] bool was_sent() const noexcept { return _was_sent; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> send(server::socket_layer::connection& conn);

  // produce formatted request as string (same bytes that send would write)
  [[nodiscard]] std::string to_string() const;

  void clear();

 private:
  std::string_view add_string(std::string&& str);
  std::string_view add_string(std::string_view str);

  http_method _method;
  std::string_view _target;
  http_version _version;

  bool _was_sent = false;

  using header_list_t = std::vector<std::pair<std::string_view, std::string_view>>;
  header_list_t _headers;

  std::string_view _body;
  string_storage::ptr _string_storage;
};

}  // namespace server::http1
