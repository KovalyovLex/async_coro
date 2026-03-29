#pragma once

#include <async_coro/task.h>
#include <server/core/headers_type.h>
#include <server/http1/headers_holder.h>
#include <server/http1/http_method.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/utils/static_string.h>
#include <server/utils/string_storage.h>

#include <string>
#include <string_view>
#include <utility>

namespace server::core {
class i_write_connection;
}

namespace server::http1 {

// Simple HTTP/1.x request builder that knows how to send itself over a i_write_connection.
// The implementation mirrors the server-side `response` class to keep memory
// overhead low and throughput high.
class client_request final : public headers_holder {
 public:
  client_request(http_method method, static_string target, http_version ver = http_version::http_1_1) noexcept;
  explicit client_request(http_method method, http_version ver = http_version::http_1_1) noexcept;

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

  void set_headers(core::headers_type headers) noexcept;

  void reserve_headers(size_t num);

  // Sets the body and Content-Type and Content-Length headers
  void set_body(std::string body, static_string content_type);
  void set_body(std::string body, std::string content_type) {
    set_body(std::move(body), static_string{add_string(std::move(content_type))});
  }
  void set_body(static_string body, static_string content_type);

  // Sets just the body. User should add Content-Type and Content-Length headers on their own
  void set_body_without_content_headers(static_string body) noexcept;

  void clear();

  // Adds a string to internal string storage, it will live as long as this request
  std::string_view add_string(std::string&& str);

  // Adds a string to internal string storage, it will live as long as this request
  std::string_view add_string(std::string_view str);

  [[nodiscard]] bool was_sent() const noexcept { return _was_sent; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> send(server::core::i_write_connection& conn);

 private:
  std::string_view _target;
  std::string_view _body;
  http_method _method;
  http_version _version;
  bool _was_sent = false;

  string_storage::ptr _string_storage;
};

}  // namespace server::http1
