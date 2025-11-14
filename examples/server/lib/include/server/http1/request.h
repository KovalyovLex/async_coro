#pragma once

#include <async_coro/task.h>
#include <async_coro/utils/function_view.h>
#include <server/http1/http_error.h>
#include <server/http1/http_method.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace server::http1 {

class request {
  struct parser;

  struct parse_deleter {
    void operator()(parser* parser) const noexcept;
  };

 public:
  using parser_ptr = std::unique_ptr<parser, parse_deleter>;

  request() noexcept;
  request(const request&) = delete;
  request(request&&) noexcept = default;

  ~request() noexcept = default;

  request& operator=(const request&) = delete;
  request& operator=(request&&) = default;

  [[nodiscard]] const auto& get_headers() const noexcept { return _headers; }

  // optimized search for header with name
  [[nodiscard]] const std::pair<ci_string_view, std::string_view>* find_header(std::string_view name) const noexcept;

  void foreach_header_with_name(std::string_view name, async_coro::function_view<void(const std::pair<ci_string_view, std::string_view>&)>) const;

  [[nodiscard]] bool has_value_in_header(std::string_view name, std::string_view value) const noexcept;

  [[nodiscard]] http_method get_method() const noexcept { return _method; }

  [[nodiscard]] http_version get_version() const noexcept { return _version; }

  [[nodiscard]] std::string_view get_body() const noexcept { return _body; }

  // Returns URL part of request e.g. "/index.html"
  [[nodiscard]] std::string_view get_target() const noexcept { return _target; }

  [[nodiscard]] bool is_parsed() const noexcept { return _parsed; }

  async_coro::task<expected<void, http_error>> read(server::socket_layer::connection& conn);

  void begin_parse(parser_ptr& parser_p, std::span<const std::byte> bytes);
  expected<void, http_error> parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes);

 private:
  void reset();

  expected<void, http_error> parse_header_line(std::string_view line);

 private:
  std::string_view _target;
  std::string_view _body;
  http_method _method;
  http_version _version;
  bool _parsed = false;
  std::vector<std::pair<ci_string_view, std::string_view>> _headers;  // sorted
  std::vector<std::byte> _bytes;
};

}  // namespace server::http1
