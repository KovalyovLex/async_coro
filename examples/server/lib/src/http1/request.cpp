#include <async_coro/config.h>
#include <server/http1/request.h>
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

namespace server::http1 {

static std::string_view as_string_view(const std::vector<std::byte>& bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};  // NOLINT(*-reinterpret-cast)
}

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

  bool operator()(const std::pair<ci_string_view, std::string_view>& pair1, const std::pair<ci_string_view, std::string_view>& pair2) const noexcept {
    if (pair1.first.size() != pair2.first.size()) {
      return pair1.first.size() < pair2.first.size();
    }

    return pair1.first < pair2.first;
  }

  bool operator()(const std::pair<ci_string_view, std::string_view>& pair1, ci_string_view ci_name) const noexcept {
    if (pair1.first.size() != ci_name.size()) {
      return pair1.first.size() < ci_name.size();
    }

    return pair1.first < ci_name;
  }
};

request::request() noexcept
    : _method(http_method::Trace),
      _version(http_version::http_0_9) {}

const std::pair<ci_string_view, std::string_view>* request::find_header(std::string_view name) const noexcept {
  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  const auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  if (iter != _headers.end() && iter->first == ci_name) {
    return std::addressof(*iter);
  }

  return nullptr;
}

void request::foreach_header_with_name(std::string_view name, async_coro::function_view<void(const std::pair<ci_string_view, std::string_view>&)> func) const {
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

bool request::has_value_in_header(std::string_view name, std::string_view value) const noexcept {  // NOLINT(*swap*)
  bool has_value = false;

  foreach_header_with_name(name, [&](auto& pair) {
    if (has_value) {
      return;
    }

    const auto idx = pair.second.find(value);
    if (idx != std::string_view::npos) {
      if (idx > 0) {
        // check begin
        const auto symbol = pair.second[idx - 1];
        if (symbol != ' ' && symbol != ',') {
          // its a substring
          return;
        }
      }
      if (idx + value.size() < pair.second.size()) {
        // check end
        const auto symbol = pair.second[idx + value.size()];
        if (symbol != ' ' && symbol != ',') {
          // its a substring
          return;
        }
      }

      has_value = true;
    }
  });

  return has_value;
}

void request::reset() {
  _target = {};
  _body = {};
  _bytes.clear();
  _headers.clear();
  _parsed = false;
}

expected<void, http_error> request::parse_header_line(std::string_view start_line) {
  using res_t = expected<void, http_error>;

  std::string_view method;
  std::string_view path;
  std::string_view version;

  if (start_line.empty()) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Wrong request format. Empty request header."}}};
  }

  auto split_index = start_line.find(' ');
  method = start_line.substr(0, split_index);

  if (auto method_val = as_method(method)) {
    _method = *method_val;
  } else {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Unsupported HTTP method type."}}};
  }

  if (split_index == std::string_view::npos) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Wrong request format. No URI."}}};
  }

  start_line = start_line.substr(split_index + 1);
  remove_spaces(start_line);

  split_index = start_line.find(' ');
  path = start_line.substr(0, split_index);
  remove_spaces(path);

  _target = path;

  if (split_index == std::string_view::npos) {
    return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Wrong request format. No HTTP version."}}};
  }

  version = start_line.substr(split_index + 1);
  remove_spaces(version);

  if (auto ver_val = as_http_version(version)) {
    _version = *ver_val;
  } else {
    return res_t{unexpect, http_error{.status_code = status_code::http_version_not_supported, .reason = static_string{as_string(status_code::http_version_not_supported)}}};
  }

  return res_t{};
}

struct request::parser {
  enum class parse_state : uint8_t {
    init,
    reading_headers,
    reading_body,
    finished
  };

  static constexpr const char* init_data_ptr = nullptr;

  size_t line_start = 0;
  size_t body_start = 0;
  std::optional<size_t> content_length;
  parse_state state = parse_state::init;
  bool is_chunked = false;

  // NOLINTBEGIN(*pointer*, *reinterpret-cast)
  expected<void, http_error> process_next_portion(request& req) noexcept {  // NOLINT(*complexity*)
    using res_t = expected<void, http_error>;

    const auto* const current_bytes_start = reinterpret_cast<const char*>(req._bytes.data());

    std::string_view string_to_process{current_bytes_start + line_start, req._bytes.size() - line_start};

    if (state == parse_state::init) {
      const auto first_line_end = string_to_process.find('\n');
      if (first_line_end != std::string_view::npos) {
        auto start_line = string_to_process.substr(0, first_line_end);
        remove_lws(start_line);
        if (!start_line.empty() && start_line.back() == '\r') {
          start_line.remove_suffix(1);
        }

        auto res = req.parse_header_line(start_line);
        if (!res) {
          return res_t{unexpect, std::move(res).error()};
        }
        // fixing target to original ptr (invalid string)
        req._target = {init_data_ptr + (req._target.data() - current_bytes_start), req._target.size()};

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
            if (req._bytes.size() > line_start) {
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

          // trim leading spaces
          if (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
          }

          auto name_ci = traits_cast<ascii_ci_traits>(name);
          if (!content_length.has_value() && name_ci == "Content-Length"_ci_sv) {
            size_t size = 0;
            auto res = std::from_chars(value.data(), value.data() + value.size(), size);
            if (res.ec != std::errc{}) {
              auto detailed_ec = std::make_error_code(res.ec);
              return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::string{"Wrong Content-Length: "}.append(detailed_ec.message())}};
            }
            content_length = size;

          } else if (!is_chunked && name_ci == "Transfer-Encoding"_ci_sv) {
            if (value.find("chunked") != std::string_view::npos) {
              is_chunked = true;
              content_length = std::nullopt;
            }
          }

          // fixing target to original ptr (invalid string)
          name = {init_data_ptr + (name.data() - current_bytes_start), name.size()};
          value = {init_data_ptr + (value.data() - current_bytes_start), value.size()};

          req._headers.emplace_back(traits_cast<ascii_ci_traits>(name), value);
        }
      }
    }
    if (state == parse_state::reading_body) {
      if (!is_chunked) {
        if (req._bytes.size() - body_start >= content_length.value_or(0)) {
          // finished read
          state = parse_state::finished;
        }
      } else {
        // chunked
        while (true) {
          // read chunk
          if (content_length.has_value()) {
            if (req._bytes.size() - line_start >= *content_length) {
              string_to_process.remove_prefix(*content_length);
              line_start += *content_length;

              // remove trailing \r\n
              size_t bytes_to_remove = 0;
              if (!string_to_process.empty() && string_to_process.front() == '\r') {
                bytes_to_remove += 1;
                if (string_to_process.size() > 1 && string_to_process[1] == '\n') {
                  bytes_to_remove += 1;
                }
              }

              if (bytes_to_remove > 0) {
                // removing excessive data from bytes
                req._bytes.erase(req._bytes.begin() + line_start, req._bytes.begin() + line_start + bytes_to_remove);  // NOLINT(*narrowing*)
              }

              if (*content_length == 0) {
                // termination chunk
                state = parse_state::finished;
                // remove all data at the end (supposed to be \r\n)
                req._bytes.erase(req._bytes.begin() + line_start, req._bytes.end());  // NOLINT(*narrowing*)
                break;
              }

              content_length = std::nullopt;
            } else {
              // not enough data
              break;
            }
          }

          const auto next_line_end = string_to_process.find('\n');
          if (next_line_end == std::string_view::npos) {
            break;
          }

          auto line = string_to_process.substr(0, next_line_end);
          const auto bytes_to_remove = line.size() + 1;
          const auto iter_to_remove = req._bytes.begin() + line_start;  // NOLINT(*narrowing*)

          if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
          }

          if (line.empty()) {
            return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Unexpected chunked contend format. Can't read chunk length"}}};
          }

          size_t size = 0;
          auto res = std::from_chars(line.data(), line.data() + line.size(), size);
          if (res.ec != std::errc{}) {
            auto detailed_ec = std::make_error_code(res.ec);
            return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::string{"Wrong chunk size: "}.append(detailed_ec.message()).append(". Size: ").append(line)}};
          }
          content_length = size;

          // removing line with chunk size from the stream
          req._bytes.erase(iter_to_remove, iter_to_remove + bytes_to_remove);  // NOLINT(*narrowing*)
        }
      }
    }
    if (state == parse_state::finished) {
      const auto* const bytes_start = reinterpret_cast<const char*>(req._bytes.data());

      // Fix pointers of string views
      req._body = {bytes_start + body_start, req._bytes.size() - body_start};
      req._target = {bytes_start + (req._target.data() - init_data_ptr), req._target.size()};
      for (auto& pair : req._headers) {
        pair.first = {bytes_start + (pair.first.data() - init_data_ptr), pair.first.size()};
        pair.second = {bytes_start + (pair.second.data() - init_data_ptr), pair.second.size()};
      }

      // sort headers
      std::ranges::stable_sort(req._headers, headers_comparator{});
    }

    return res_t{};
  }
  // NOLINTEND(*pointer*, *reinterpret-cast)
};

void request::parse_deleter::operator()(parser* parser) const noexcept {
  delete parser;  // NOLINT(*owning-memory)
}

async_coro::task<expected<void, http_error>> request::read(server::socket_layer::connection& conn) {
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
    co_return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = static_string{"Request Parse error."}}};
  }

  _parsed = true;

  co_return res_t{};
}

void request::begin_parse(parser_ptr& parser_p, std::span<const std::byte> bytes) {
  reset();

  if (parser_p == nullptr) {
    parser_p.reset(new request::parser());  // NOLINT(*owning-memory*)
  }
}

expected<void, http_error> request::parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes) {
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
