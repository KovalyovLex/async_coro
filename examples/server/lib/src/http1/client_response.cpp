#include <async_coro/config.h>
#include <async_coro/utils/function_view.h>
#include <server/http1/client_response.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "server/core/i_read_connection.h"

namespace server::http1 {

static void remove_lws(std::string_view& str) noexcept {
  while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
    str.remove_prefix(1);
  }
};

static void remove_spaces(std::string_view& str) noexcept {
  while (!str.empty() && str.front() == ' ') {
    str.remove_prefix(1);
  }
};

struct headers_comparator {
  using is_transparent = int;

  bool operator()(const std::pair<ci_string_view, std::string_view>& pair1,
                  const std::pair<ci_string_view, std::string_view>& pair2) const noexcept {
    if (pair1.first.size() != pair2.first.size()) {
      return pair1.first.size() < pair2.first.size();
    }
    return pair1.first < pair2.first;
  }

  bool operator()(const std::pair<ci_string_view, std::string_view>& pair1,
                  ci_string_view ci_name) const noexcept {
    if (pair1.first.size() != ci_name.size()) {
      return pair1.first.size() < ci_name.size();
    }
    return pair1.first < ci_name;
  }
};

client_response::client_response() noexcept
    : _status_code(status_code::ok),
      _version(http_version::http_1_1) {}

const std::pair<ci_string_view, std::string_view>*
client_response::find_header(std::string_view name) const noexcept {
  const auto ci_name = traits_cast<ascii_ci_traits>(name);
  const auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  if (iter != _headers.end() && iter->first == ci_name) {
    return std::addressof(*iter);
  }
  return nullptr;
}

void client_response::foreach_header_with_name(
    std::string_view name,
    async_coro::function_view<void(const std::pair<ci_string_view, std::string_view>&)> func) const {
  if (!func) [[unlikely]] {
    return;
  }
  const auto ci_name = traits_cast<ascii_ci_traits>(name);
  auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  while (iter != _headers.end() && iter->first == ci_name) {
    func(*iter);
    iter++;
  }
}

bool client_response::has_value_in_header(std::string_view name, std::string_view value) const noexcept {  // NOLINT(*swap*)
  bool has_value = false;
  foreach_header_with_name(name, [&](auto& pair) {
    if (has_value) {
      return;
    }
    const auto idx = pair.second.find(value);
    if (idx != std::string_view::npos) {
      if (idx > 0) {
        const auto symbol = pair.second[idx - 1];
        if (symbol != ' ' && symbol != ',') {
          return;
        }
      }
      if (idx + value.size() < pair.second.size()) {
        const auto symbol = pair.second[idx + value.size()];
        if (symbol != ' ' && symbol != ',') {
          return;
        }
      }
      has_value = true;
    }
  });
  return has_value;
}

void client_response::reset() {
  _body = {};
  _status_code = status_code::ok;
  _reason = {};
  _version = http_version::http_1_1;
  _bytes.clear();
  _headers.clear();
  _parsed = false;
}

expected<void, http_error> client_response::parse_status_line(std::string_view start_line) {
  using res_t = expected<void, http_error>;

  std::string_view version;
  std::string_view code;
  std::string_view reason;

  if (start_line.empty()) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Empty status line"}}};
  }

  auto space = start_line.find(' ');
  if (space == std::string_view::npos) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Malformed status line"}}};
  }
  version = start_line.substr(0, space);
  start_line.remove_prefix(space + 1);
  remove_spaces(start_line);

  space = start_line.find(' ');
  if (space == std::string_view::npos) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Missing status code"}}};
  }
  code = start_line.substr(0, space);
  start_line.remove_prefix(space + 1);
  remove_spaces(start_line);

  reason = start_line;

  if (auto ver_val = as_http_version(version)) {
    _version = *ver_val;
  } else {
    return res_t{unexpect, http_error{.status_code = status_code::http_version_not_supported, .reason = static_string{as_string(status_code::http_version_not_supported)}}};
  }

  // parse numeric code
  size_t val = 0;
  auto from_chars_result = std::from_chars(code.data(), code.data() + code.size(), val);
  if (from_chars_result.ec != std::errc{}) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Invalid status code"}}};
  }
  _status_code = http_status_code{static_cast<uint16_t>(val)};
  _reason = reason;
  return res_t{};
}

// parser implementation largely mirrors request::parser
struct client_response::parser {
  enum class parse_state : uint8_t { init,
                                     reading_headers,
                                     reading_body,
                                     finished };

  static constexpr const char* init_data_ptr = nullptr;

  size_t line_start = 0;
  size_t body_start = 0;
  std::optional<size_t> content_length;
  parse_state state = parse_state::init;
  bool is_chunked = false;

  // NOLINTBEGIN(*pointer*, *reinterpret-cast)
  expected<void, http_error> process_next_portion(client_response& resp) noexcept {  // NOLINT(*complexity*)
    using res_t = expected<void, http_error>;
    const auto* const current_bytes_start = reinterpret_cast<const char*>(resp._bytes.data());
    std::string_view string_to_process{current_bytes_start + line_start, resp._bytes.size() - line_start};

    if (state == parse_state::init) {
      const auto first_line_end = string_to_process.find('\n');
      if (first_line_end != std::string_view::npos) {
        auto start_line = string_to_process.substr(0, first_line_end);
        remove_lws(start_line);
        if (!start_line.empty() && start_line.back() == '\r') {
          start_line.remove_suffix(1);
        }
        auto res = resp.parse_status_line(start_line);
        if (!res) {
          return res_t{unexpect, std::move(res).error()};
        }
        state = parse_state::reading_headers;
        string_to_process.remove_prefix(first_line_end + 1);
        line_start += first_line_end + 1;
      }
    }
    if (state == parse_state::reading_headers) {
      auto next_line_end = string_to_process.find('\n');
      while (next_line_end != std::string_view::npos) {
        auto line = string_to_process.substr(0, next_line_end);
        string_to_process.remove_prefix(next_line_end + 1);
        line_start += next_line_end + 1;
        next_line_end = string_to_process.find('\n');

        if (!line.empty() && line.back() == '\r') {
          line.remove_suffix(1);
        }

        if (line.empty()) {
          state = parse_state::reading_body;
          body_start = line_start;
          if (!content_length.has_value() && !is_chunked) {
            if (resp._bytes.size() > line_start) {
              return res_t{unexpect, http_error{.status_code = status_code::length_required, .reason = static_string{"Non empty body should have Content-Length."}}};
            }
            content_length = 0;
          }
          break;
        }

        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
          auto name = line.substr(0, colon);
          auto value = line.substr(colon + 1);
          if (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
          }
          auto name_ci = traits_cast<ascii_ci_traits>(name);
          if (!content_length.has_value() && name_ci == "Content-Length"_ci_sv) {
            size_t size = 0;
            auto from_chars_result = std::from_chars(value.data(), value.data() + value.size(), size);
            if (from_chars_result.ec != std::errc{}) {
              auto detailed_ec = std::make_error_code(from_chars_result.ec);
              return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::string{"Wrong Content-Length: "}.append(detailed_ec.message())}};
            }
            content_length = size;
          } else if (!is_chunked && name_ci == "Transfer-Encoding"_ci_sv) {
            if (value.find("chunked") != std::string_view::npos) {
              is_chunked = true;
              content_length = std::nullopt;
            }
          }

          // fix pointers
          name = {init_data_ptr + (name.data() - current_bytes_start), name.size()};
          value = {init_data_ptr + (value.data() - current_bytes_start), value.size()};
          resp._headers.emplace_back(traits_cast<ascii_ci_traits>(name), value);
        }
      }
    }
    if (state == parse_state::reading_body) {
      if (!is_chunked) {
        if (resp._bytes.size() - body_start >= content_length.value_or(0)) {
          state = parse_state::finished;
        }
      } else {
        while (true) {
          if (content_length.has_value()) {
            if (resp._bytes.size() - line_start >= *content_length) {
              string_to_process.remove_prefix(*content_length);
              line_start += *content_length;
              size_t bytes_to_remove = 0;
              if (!string_to_process.empty() && string_to_process.front() == '\r') {
                bytes_to_remove += 1;
                if (string_to_process.size() > 1 && string_to_process[1] == '\n') {
                  bytes_to_remove += 1;
                }
              }
              if (bytes_to_remove > 0) {
                auto erase_begin = resp._bytes.begin() + static_cast<std::vector<std::byte>::difference_type>(line_start);
                auto erase_end = erase_begin + static_cast<std::vector<std::byte>::difference_type>(bytes_to_remove);
                resp._bytes.erase(erase_begin, erase_end);
              }
              if (*content_length == 0) {
                state = parse_state::finished;
                resp._bytes.erase(resp._bytes.begin() + static_cast<std::vector<std::byte>::difference_type>(line_start), resp._bytes.end());  // NOLINT(cppcoreguidelines-narrowing-conversions): line_start is size_t bounded by content-length
                break;
              }
              content_length = std::nullopt;
            } else {
              break;
            }
          }
          const auto next_line_end = string_to_process.find('\n');
          if (next_line_end == std::string_view::npos) {
            break;
          }
          auto line = string_to_process.substr(0, next_line_end);
          const auto bytes_to_remove = line.size() + 1;
          const auto iter_to_remove = resp._bytes.begin() + static_cast<std::vector<std::byte>::difference_type>(line_start);
          if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
          }
          if (line.empty()) {
            return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Unexpected chunked contend format. Can't read chunk length"}}};
          }
          size_t size = 0;
          auto from_chars_result = std::from_chars(line.data(), line.data() + line.size(), size);
          if (from_chars_result.ec != std::errc{}) {
            auto detailed_ec = std::make_error_code(from_chars_result.ec);
            return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::string{"Wrong chunk size: "}.append(detailed_ec.message()).append(". Size: ").append(line)}};
          }
          content_length = size;
          resp._bytes.erase(iter_to_remove, iter_to_remove + static_cast<std::vector<std::byte>::difference_type>(bytes_to_remove));
        }
      }
    }
    if (state == parse_state::finished) {
      const auto* const bytes_start = reinterpret_cast<const char*>(resp._bytes.data());
      resp._body = {bytes_start + static_cast<std::ptrdiff_t>(body_start), static_cast<std::size_t>(resp._bytes.size() - body_start)};
      // version was set while parsing status line
      for (auto& pair : resp._headers) {
        pair.first = {bytes_start + static_cast<std::ptrdiff_t>(pair.first.data() - init_data_ptr), pair.first.size()};
        pair.second = {bytes_start + static_cast<std::ptrdiff_t>(pair.second.data() - init_data_ptr), pair.second.size()};
      }
      std::ranges::stable_sort(resp._headers, headers_comparator{});
    }

    return res_t{};
  }
  // NOLINTEND(*pointer*, *reinterpret-cast)
};

void client_response::parse_deleter::operator()(parser* parser) const noexcept {
  delete parser;  // NOLINT(*owning-memory)
}

async_coro::task<expected<void, http_error>> client_response::read(server::core::i_read_connection& conn) {  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
  using res_t = expected<void, http_error>;

  reset();
  parser parse{};
  std::array<std::byte, 4 * 1024> buffer;  // NOLINT(*)

  while (!conn.is_closed() && parse.state != parser::parse_state::finished) {
    auto read = co_await conn.read_buffer(std::span{buffer});
    if (!read.has_value()) {
      co_return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::move(read).error()}};
    }
    const auto bytes_read = read.value();
    std::copy(buffer.data(), buffer.data() + bytes_read, std::back_inserter(_bytes));  // NOLINT(*narrowing*, *pointer*)
    auto res = parse.process_next_portion(*this);
    if (!res) {
      co_return res_t{unexpect, std::move(res).error()};
    }
  }
  if (parse.state != parser::parse_state::finished) {
    co_return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Response parse error."}}};
  }
  _parsed = true;
  co_return res_t{};
}

void client_response::begin_parse(parser_ptr& parser_p) {
  reset();
  if (parser_p == nullptr) {
    parser_p.reset(new client_response::parser());  // NOLINT(*owning-memory*)
  } else {
    *parser_p = client_response::parser{};
  }
}

expected<void, http_error> client_response::parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes) {
  ASYNC_CORO_ASSERT(parser_p != nullptr);
  std::ranges::copy(bytes, std::back_inserter(_bytes));
  auto res = parser_p->process_next_portion(*this);
  if (!res) {
    return expected<void, http_error>{unexpect, std::move(res).error()};
  }
  if (parser_p->state == parser::parse_state::finished) {
    _parsed = true;
  }
  return {};
}

}  // namespace server::http1
