#pragma once

#include <async_coro/task.h>
#include <async_coro/utils/function_view.h>
#include <server/core/headers_type.h>
#include <server/http1/headers_holder.h>
#include <server/http1/http_error.h>
#include <server/http1/http_method.h>
#include <server/http1/http_version.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace server::core {
class i_read_connection;
}

namespace server::http1 {

class client_request;

// Simple HTTP/1.x request to the server class.
// Keeps string of original request and parsed string_views for headers\body\etc. to keep memory
// overhead low and throughput high.
class request : public headers_holder {  // NOLINT(*virtual-*destructor*)
  struct parser;

  struct parse_deleter {
    void operator()(parser* parser) const noexcept;
  };

 public:
  using parser_ptr = std::unique_ptr<parser, parse_deleter>;

  request() noexcept;
  request(const request&) = delete;
  request(request&&) noexcept;

  ~request() noexcept = default;

  request& operator=(const request&) = delete;
  request& operator=(request&&) noexcept;

  // Returns a string_view of the entire request (start-line, headers, body) as it was received. Useful for forwarding the request without parsing it.
  [[nodiscard]] std::string_view to_string_view() const { return _request_str; }

  [[nodiscard]] http_method get_method() const noexcept { return _method; }

  [[nodiscard]] http_version get_version() const noexcept { return _version; }

  [[nodiscard]] std::string_view get_body() const noexcept { return _body; }

  // Returns URL part of request e.g. "/index.html"
  [[nodiscard]] std::string_view get_target() const noexcept { return _target; }

  // Returns true if the request was fully parsed and is ready to be processed.
  [[nodiscard]] bool is_parsed() const noexcept { return _parsed; }

  async_coro::task<expected<void, http_error>> read(core::i_read_connection& conn);

  // Begin parsing request. The caller can then feed data to the parser using `parse_data_part` until the request is fully parsed.
  void begin_parse(parser_ptr& parser_p);

  // Parses next portion of the data. Returns http_error if parsing failed, or empty value if parsing succeeded (even if the request is not fully parsed yet).
  // The user can check is_parsed() to see if the request is fully parsed.
  expected<void, http_error> parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes);

  // Converts request to client_request for proxying
  explicit operator client_request() && noexcept;

 private:
  void reset();

  expected<void, http_error> parse_header_line(std::string_view line, const char* init_data_ptr, const char* current_str_start);

  // Used to fix string_views pointers after move of small strings
  void fix_string_pointers(const char* old_str_ptr, std::span<const char> new_str);

 private:
  std::string_view _target;
  std::string_view _body;
  http_method _method;
  http_version _version;
  bool _parsed = false;
  std::string _request_str;
};

// Same as request but with sorted headers for better searching performance over requests with big amount of headers
class request_with_sorted_headers final : private request {
 public:
  explicit request_with_sorted_headers(request&& other) noexcept;

  // Returns a string_view of the entire request (start-line, headers, body) as it was received. Useful for forwarding the request without parsing it.
  [[nodiscard]] std::string_view to_string_view() const { return request::to_string_view(); }

  [[nodiscard]] http_method get_method() const noexcept { return request::get_method(); }

  [[nodiscard]] http_version get_version() const noexcept { return request::get_version(); }

  [[nodiscard]] std::string_view get_body() const noexcept { return request::get_body(); }

  // Returns URL part of request e.g. "/index.html"
  [[nodiscard]] std::string_view get_target() const noexcept { return request::get_target(); }

  // Optimized search for header with name
  [[nodiscard]] const std::pair<ci_string_view, std::string_view>* find_header(std::string_view name) const noexcept;

  void foreach_header_with_name(std::string_view name, async_coro::function_view<void(std::string_view)> func) const;

  void foreach_header_with_name_noexcept(std::string_view name, async_coro::function_view<void(std::string_view) noexcept> func) const noexcept;

  // Checks if there is a header with the given name that contains the given value (e.g. "gzip" in "Accept-Encoding: gzip, deflate").
  [[nodiscard]] bool has_value_in_header(std::string_view name, std::string_view value) const noexcept;

  [[nodiscard]] size_t get_number_of_headers() const noexcept { return request::get_number_of_headers(); }
};

}  // namespace server::http1
