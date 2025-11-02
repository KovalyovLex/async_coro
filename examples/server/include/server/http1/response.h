#pragma once

#include <async_coro/task.h>
#include <server/http1/http_status_code.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/utils/string_storage.h>

#include <string_view>
#include <vector>

namespace server::http1 {

namespace content_types {
inline constexpr std::string_view plain_text = "text/plain; charset=utf-8";
inline constexpr std::string_view html = "text/html; charset=utf-8";
inline constexpr std::string_view json = "application/json";
}  // namespace content_types

class response {
 public:
  struct static_string_t {};

  static constexpr static_string_t static_string = {};

  using header_list_t = std::vector<std::pair<std::string_view, std::string_view>>;

  response(http_version ver, http_status_code status, std::string_view reason);
  response(http_version ver, http_status_code status, std::string_view reason, static_string_t) noexcept;

  response(http_version ver, status_code status) noexcept;

  [[nodiscard]] http_status_code get_status_code() const noexcept { return _status_code; }

  [[nodiscard]] std::string_view get_reason() const noexcept { return _reason; }

  // Adds string to storage associated with this response
  std::string_view add_string(std::string_view str);

  void add_header(std::string_view name, std::string_view value, static_string_t);
  void add_header(std::string_view name, std::string_view value) {
    add_header(add_string(name), add_string(value), static_string);
  }

  void set_body(std::string_view body, std::string_view content_type, static_string_t);
  void set_body(std::string_view body, std::string_view content_type) {
    set_body(add_string(body), add_string(content_type), static_string);
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
