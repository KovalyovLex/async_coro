#pragma once

#include <async_coro/task.h>
#include <async_coro/utils/function_view.h>
#include <server/core/headers_type.h>
#include <server/http1/headers_holder.h>
#include <server/http1/http_error.h>
#include <server/http1/http_status_code.h>
#include <server/http1/http_version.h>
#include <server/socket_layer/connection.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace server::core {
class i_read_connection;
}

namespace server::http1 {

class client_response final : public headers_holder {
  struct parser;

  struct parse_deleter {
    void operator()(parser* parser) const noexcept;
  };

 public:
  using parser_ptr = std::unique_ptr<parser, parse_deleter>;
  using headers = std::vector<std::pair<ci_string_view, std::string_view>>;  // Sorted by header name (case-insensitive)

  client_response() noexcept;
  client_response(const client_response&) = delete;
  client_response(client_response&&) noexcept = default;
  ~client_response() noexcept = default;

  client_response& operator=(const client_response&) = delete;
  client_response& operator=(client_response&&) noexcept = default;

  [[nodiscard]] const auto& get_headers() const noexcept { return _headers; }

  [[nodiscard]] const std::pair<ci_string_view, std::string_view>* find_header(std::string_view name) const noexcept;

  void foreach_header_with_name(std::string_view name, async_coro::function_view<void(const std::pair<ci_string_view, std::string_view>&)>) const;

  [[nodiscard]] bool has_value_in_header(std::string_view name, std::string_view value) const noexcept;

  [[nodiscard]] http_status_code get_status_code() const noexcept { return _status_code; }

  [[nodiscard]] std::string_view get_reason() const noexcept { return _reason; }

  [[nodiscard]] http_version get_version() const noexcept { return _version; }

  [[nodiscard]] std::string_view get_body() const noexcept { return _body; }

  [[nodiscard]] bool is_parsed() const noexcept { return _parsed; }

  async_coro::task<expected<void, http_error>> read(server::core::i_read_connection& conn);

  void begin_parse(parser_ptr& parser_p);

  expected<void, http_error> parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes);

 private:
  void reset();
  expected<void, http_error> parse_status_line(std::string_view start_line);

 private:
  http_version _version;
  http_status_code _status_code;
  bool _parsed = false;

  std::string_view _body;
  std::string_view _reason;

  std::vector<std::byte> _bytes;
};

}  // namespace server::http1
