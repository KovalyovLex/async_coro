#pragma once

#include <async_coro/task.h>
#include <server/http1/http_status_code.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/utils/static_string.h>
#include <server/utils/string_storage.h>

#include <string_view>
#include <vector>

namespace server::http1 {

struct http_error;
namespace content_types {
inline constexpr static_string plain_text{"text/plain; charset=utf-8"};
inline constexpr static_string html{"text/html; charset=utf-8"};
inline constexpr static_string json{"application/json"};
}  // namespace content_types

class response {
 public:
  using header_list_t = std::vector<std::pair<std::string_view, std::string_view>>;

  response(http_version ver, http_status_code status, std::string_view reason);
  response(http_version ver, http_status_code status, static_string reason) noexcept;

  response(http_version ver, status_code status) noexcept;

  response(http_version ver, http_error&& error) noexcept;

  [[nodiscard]] http_status_code get_status_code() const noexcept { return _status_code; }

  [[nodiscard]] std::string_view get_reason() const noexcept { return _reason; }

  // Adds string to storage associated with this response
  std::string_view add_string(std::string&& str);
  std::string_view add_string(std::string_view str);

  void add_header(static_string name, static_string value);
  void add_header(std::string name, std::string value) {
    add_header(static_string{add_string(std::move(name))}, static_string{add_string(std::move(value))});
  }

  void set_body(static_string body, static_string content_type);
  void set_body(std::string body, std::string content_type) {
    set_body(static_string{add_string(std::move(body))}, static_string{add_string(std::move(content_type))});
  }

  async_coro::task<expected<void, std::string>> send(server::socket_layer::connection& conn);

 private:
  http_version _ver;
  http_status_code _status_code;
  std::string_view _reason;
  header_list_t _headers;
  std::string_view _body;
  string_storage::ptr _string_storage;
};

}  // namespace server::http1
