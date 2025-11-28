#pragma once

#include <async_coro/task.h>
#include <server/http1/http_status_code.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/compression_pool.h>
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

using response_encoder = pooled_compressor<compressor_variant>;
class response {
 public:
  // Constructs default response with 200 Ok status
  explicit response(http_version ver) noexcept;

  // Sets status code and reason. Reason will be default explanation: as_string (status)
  void set_status(status_code status) noexcept;
  // Sets status code reason and body of message.
  void set_status(http_error&& error) noexcept;
  // Sets status code and reason as dynamic string
  void set_status(http_status_code status, std::string_view reason);
  // Sets status code and reason
  void set_status(http_status_code status, static_string reason) noexcept;

  [[nodiscard]] http_status_code get_status_code() const noexcept { return _status_code; }

  [[nodiscard]] std::string_view get_reason() const noexcept { return _reason; }

  // Adds string to storage associated with this response
  std::string_view add_string(std::string&& str);
  std::string_view add_string(std::string_view str);

  void add_header(static_string name, static_string value);
  void add_header(std::string name, std::string value) {
    add_header(static_string{add_string(std::move(name))}, static_string{add_string(std::move(value))});
  }
  void add_header(static_string name, std::string value) {
    add_header(name, static_string{add_string(std::move(value))});
  }

  void set_body(static_string body, static_string content_type);
  void set_body(std::string body, std::string content_type) {
    set_body(static_string{add_string(std::move(body))}, static_string{add_string(std::move(content_type))});
  }
  void set_body(std::string body, static_string content_type) {
    set_body(static_string{add_string(std::move(body))}, content_type);
  }

  [[nodiscard]] bool was_sent() const noexcept { return _was_sent; }

  [[nodiscard]] async_coro::task<expected<void, std::string>> send(server::socket_layer::connection& conn);

  // Set response encoder for compression support
  void set_encoder(response_encoder&& encoder) noexcept;

  // Check if response has compression encoder
  [[nodiscard]] bool has_encoder() const noexcept;

  // clears status (sets 200 ok), string storage and headers
  void clear();

 private:
  using header_list_t = std::vector<std::pair<std::string_view, std::string_view>>;

  http_version _ver;
  http_status_code _status_code;
  bool _was_sent = false;
  std::string_view _reason;
  header_list_t _headers;
  std::string_view _body;
  string_storage::ptr _string_storage;
  response_encoder _encoder;
};

}  // namespace server::http1
