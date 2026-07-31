#include <async_coro/config.h>
#include <server/core/i_read_connection.h>
#include <server/http1/client_request.h>
#include <server/http1/request.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>
#include <server/utils/static_string.h>

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

request::request(request&& other) noexcept
    : _target(other._target),
      _body(other._body),
      _method(other._method),
      _version(other._version),
      _parsed(other._parsed) {
  _headers = std::move(other._headers);
  auto* old_str_ptr = other._request_str.data();
  _request_str = std::move(other._request_str);  // NOLINT(*member-initializer*)

  if (_request_str.data() != old_str_ptr) {
    // fix pointers
    fix_string_pointers(old_str_ptr, _request_str);  // NOLINT(*-cplusplus.InnerPointer) its correct here
  }
}

request& request::operator=(request&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  _headers = std::move(other._headers);
  _target = other._target;
  _body = other._body;
  _method = other._method;
  _version = other._version;
  _parsed = other._parsed;

  auto* old_str_ptr = other._request_str.data();
  _request_str = std::move(other._request_str);

  if (_request_str.data() != old_str_ptr) {
    // fix pointers
    fix_string_pointers(old_str_ptr, _request_str);  // NOLINT(*-cplusplus.InnerPointer) its correct here
  }

  return *this;
}

void request::fix_string_pointers(const char* old_str_ptr, std::span<const char> new_str) {
  const auto get_string_view = [&](auto str) {
    const auto index = str.data() - old_str_ptr;

    ASYNC_CORO_ASSERT(index < new_str.size() || (index == new_str.size() && str.empty()));

    using StrType = decltype(str);
    return StrType{new_str.data() + index, str.size()};
  };

  _body = get_string_view(_body);
  _target = get_string_view(_target);

  for (auto& pair : _headers) {
    pair.first = get_string_view(pair.first);
    pair.second = get_string_view(pair.second);
  }
}

void request::reset() {
  _target = {};
  _body = {};
  _request_str.clear();
  _headers.clear();
  _parsed = false;
}

expected<void, http_error> request::parse_header_line(std::string_view start_line, const char* init_data_ptr, const char* current_str_start) {
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

  // target should point to init_data_ptr (probably invalid string)
  _target = {init_data_ptr + (path.data() - current_str_start), path.size()};  // NOLINT(*pointer-arithmetic*)

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

  // NOLINTBEGIN(*pointer*, *narrowing*, *reinterpret-cast)
  expected<void, http_error> process_next_portion(request& req) noexcept {  // NOLINT(*complexity*)
    using res_t = expected<void, http_error>;

    std::string& request_str = req._request_str;

    const auto* const current_str_start = request_str.data();

    auto request_size = request_str.size();

    std::string_view string_to_process{current_str_start + line_start, request_size - line_start};

    if (state == parse_state::init) {
      const auto first_line_end = string_to_process.find('\n');
      if (first_line_end != std::string_view::npos) {
        auto start_line = string_to_process.substr(0, first_line_end);
        remove_lws(start_line);
        if (!start_line.empty() && start_line.back() == '\r') {
          start_line.remove_suffix(1);
        }

        auto res = req.parse_header_line(start_line, init_data_ptr, current_str_start);
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
            if (request_size > line_start) {
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
          name = {init_data_ptr + (name.data() - current_str_start), name.size()};
          value = {init_data_ptr + (value.data() - current_str_start), value.size()};

          req._headers.emplace_back(traits_cast<ascii_ci_traits>(name), value);
        }
      }
    }

    if (state == parse_state::reading_body) {
      if (!is_chunked) {
        if (request_size - body_start >= content_length.value_or(0)) {
          // finished read
          state = parse_state::finished;
        }
      } else {
        // chunked
        while (true) {
          // read chunk
          if (content_length.has_value()) {
            if (request_size - line_start >= *content_length) {
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
                // removing excessive data from request string
                const auto it_to_remove = request_str.begin() + line_start;
                request_str.erase(it_to_remove, it_to_remove + bytes_to_remove);
                request_size -= bytes_to_remove;
              }

              if (*content_length == 0) {
                // termination chunk
                state = parse_state::finished;
                // remove all data at the end (supposed to be \r\n)
                request_str.erase(request_str.begin() + line_start, request_str.end());
                [[maybe_unused]] auto request_size_final = line_start;
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
          const auto iter_to_remove = request_str.begin() + line_start;

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
          request_str.erase(iter_to_remove, iter_to_remove + bytes_to_remove);
          request_size -= bytes_to_remove;
        }
      }
    }

    if (state == parse_state::finished) {
      // making body invalid string
      req._body = {init_data_ptr + body_start, request_str.size() - body_start};

      req.fix_string_pointers(init_data_ptr, request_str);
    }

    return res_t{};
  }
  // NOLINTEND(*pointer*, *narrowing*, *reinterpret-cast)
};

void request::parse_deleter::operator()(parser* parser) const noexcept {
  delete parser;  // NOLINT(*owning-memory)
}

async_coro::task<expected<void, http_error>> request::read(core::i_read_connection& conn) {  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
  using res_t = expected<void, http_error>;

  reset();

  parser parse{};

  std::array<char, 4 * 1024> buffer;  // NOLINT(*)

  while (!conn.is_closed() && parse.state != parser::parse_state::finished) {
    auto read = co_await conn.read_buffer(as_writable_bytes(std::span{buffer}));
    if (!read.has_value()) {
      co_return res_t{unexpect, http_error{.status_code = status_code::bad_request, .reason = std::move(read).error()}};
    }

    auto bytes_str = std::string_view{buffer.data(), read.value()};

    std::ranges::copy(bytes_str, std::back_inserter(_request_str));

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

void request::begin_parse(parser_ptr& parser_p) {
  reset();

  if (parser_p == nullptr) {
    parser_p.reset(new request::parser());  // NOLINT(*owning-memory*)
  } else {
    *parser_p = request::parser{};
  }
}

expected<void, http_error> request::parse_data_part(parser_ptr& parser_p, std::span<const std::byte> bytes) {
  ASYNC_CORO_ASSERT(parser_p != nullptr);

  const auto* bytes_data_ptr = reinterpret_cast<const char*>(bytes.data());  // NOLINT(clang-analyzer-cplusplus.InnerPointer, cppcoreguidelines-pro-type-reinterpret-cast)
  std::string_view bytes_str{bytes_data_ptr, bytes.size()};

  std::ranges::copy(bytes_str, std::back_inserter(_request_str));

  auto res = parser_p->process_next_portion(*this);
  if (!res) {
    return expected<void, http_error>{unexpect, std::move(res).error()};
  }

  if (parser_p->state == parser::parse_state::finished) {
    _parsed = true;
  }

  return {};
}

// Converts request to client_request for proxying
request::operator client_request() && noexcept {
  auto request = client_request{_method, _version};

  const auto* old_data_start = _request_str.data();

  auto new_str = request.add_string(std::move(_request_str));

  fix_string_pointers(old_data_start, new_str);  // NOLINT(*-cplusplus.InnerPointer) its correct here

  request.set_target(static_string{_target});
  request.set_body_without_content_headers(static_string{_body});
  request.set_headers(std::move(_headers));

  return request;
}

request_with_sorted_headers::request_with_sorted_headers(request&& other) noexcept
    : request(std::move(other)) {
  ASYNC_CORO_ASSERT(this->is_parsed());

  std::ranges::stable_sort(_headers, headers_comparator{});
}

const std::pair<ci_string_view, std::string_view>* request_with_sorted_headers::find_header(std::string_view name) const noexcept {
  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  const auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  if (iter != _headers.end() && iter->first == ci_name) {
    return std::addressof(*iter);
  }

  return nullptr;
}

void request_with_sorted_headers::foreach_header_with_name(std::string_view name, async_coro::function_view<void(std::string_view)> func) const {
  if (!func) [[unlikely]] {
    return;
  }

  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  while (iter != _headers.end() && iter->first == ci_name) {
    func(iter->second);
    iter++;
  }
}

void request_with_sorted_headers::foreach_header_with_name_noexcept(std::string_view name, async_coro::function_view<void(std::string_view) noexcept> func) const noexcept {
  if (!func) [[unlikely]] {
    return;
  }

  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  while (iter != _headers.end() && iter->first == ci_name) {
    func(iter->second);
    iter++;
  }
}

bool request_with_sorted_headers::has_value_in_header(std::string_view name, std::string_view value) const noexcept {  // NOLINT(*swap*)
  bool has_value = false;

  foreach_header_with_name_noexcept(name, [&](std::string_view head_value) noexcept {
    if (has_value) {
      return;
    }

    const auto idx = head_value.find(value);
    if (idx != std::string_view::npos) {
      if (idx > 0) {
        // check begin
        const auto symbol = head_value[idx - 1];
        if (symbol != ' ' && symbol != ',') {
          // its a substring
          return;
        }
      }
      if (idx + value.size() < head_value.size()) {
        // check end
        const auto symbol = head_value[idx + value.size()];
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

}  // namespace server::http1
