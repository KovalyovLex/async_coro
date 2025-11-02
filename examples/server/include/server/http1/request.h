#pragma once

#include <async_coro/task.h>
#include <server/socket_layer/connection.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "http_method.h"
#include "http_version.h"

namespace server::http1 {

class request {
 public:
  using header_list_t = std::vector<std::pair<ci_string_view, std::string_view>>;

  request(const request&) = delete;
  request(request&&) noexcept = default;

  ~request() noexcept = default;

  request& operator=(const request&) = delete;
  request& operator=(request&&) = default;

  std::string_view target;  // e.g. "/index.html"
  std::string_view body;    // request body string
  http_method method;       // e.g. "GET"
  http_version version;     // e.g. "HTTP/1.1"

  // Parses all headers on first request then keeps it cached
  [[nodiscard]] const header_list_t& get_headers();

  // Parses all headers on first request then keeps it cached
  [[nodiscard]] std::pair<ci_string_view, std::string_view>* find_header(std::string_view name);

  // Parses all headers on first request then keeps it cached
  std::optional<size_t> get_content_length();

  // Request can only be created with parse method
  static expected<request, std::string> parse(std::vector<std::byte> bytes);

  static async_coro::task<expected<request, std::string>> read(server::socket_layer::connection& conn);

 private:
  explicit request(std::vector<std::byte> bytes) noexcept;

  void try_parse_headers();

 private:
  static constexpr size_t kEmptyLength = -1;

  bool _headers_parsed = false;
  std::vector<std::byte> _bytes;
  header_list_t _headers;  // sorted
  std::string_view _headers_str;
  size_t _content_length = kEmptyLength;
};

}  // namespace server::http1
