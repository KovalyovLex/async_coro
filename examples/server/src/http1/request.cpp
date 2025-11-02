#include <server/http1/request.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/expected.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace server::http1 {

static std::string_view as_string_view(const std::vector<std::byte>& bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};  // NOLINT(*-reinterpret-cast)
}

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

request::request(std::vector<std::byte> bytes) noexcept
    : _bytes(std::move(bytes)),
      method(http_method::TRACE),
      version(http_version::http_0_9) {}

const request::header_list_t& request::get_headers() {
  try_parse_headers();

  return _headers;
}

std::pair<ci_string_view, std::string_view>* request::find_header(std::string_view name) {
  try_parse_headers();

  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  const auto iter = std::lower_bound(_headers.begin(), _headers.end(), ci_name, headers_comparator{});  // NOLINT(*ranges*)
  if (iter != _headers.end() && iter->first == ci_name) {
    return std::addressof(*iter);
  }

  return nullptr;
}

std::optional<size_t> request::get_content_length() {
  try_parse_headers();

  if (_content_length == kEmptyLength) {
    return std::nullopt;
  }
  return _content_length;
}

expected<request, std::string> request::parse(std::vector<std::byte> bytes) {
  using res_t = expected<request, std::string>;

  const auto remove_lws = [](std::string_view& str) noexcept {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
      str.remove_prefix(1);
    }
  };

  const auto remove_spaces = [](std::string_view& str) noexcept {
    while (!str.empty() && str.front() == ' ') {
      str.remove_prefix(1);
    }
  };

  request req{std::move(bytes)};

  const auto request_string = as_string_view(req._bytes);

  size_t next_line_pos = 0;
  auto pos = request_string.find('\n');
  if (pos == std::string::npos) {
    next_line_pos = pos = request_string.size();
  } else {
    next_line_pos = pos + 1;
  }

  if (pos > 0 && request_string[pos - 1] == '\r') {
    pos--;
  }

  auto start_line = request_string.substr(0, pos);
  auto rest_header = request_string.substr(next_line_pos);

  remove_lws(start_line);
  remove_lws(rest_header);

  auto header_split = rest_header.find("\r\n\r\n");
  if (header_split == std::string_view::npos) {
    header_split = rest_header.find("\n\n", next_line_pos);
    next_line_pos = header_split + 2;
  } else {
    next_line_pos = header_split + 4;
  }

  if (header_split != std::string_view::npos) {  // has header
    req._headers_str = rest_header.substr(0, header_split);
    req.body = rest_header.substr(next_line_pos);
  }

  std::string_view method;
  std::string_view path;
  std::string_view version;

  if (start_line.empty()) {
    return res_t(server::unexpect, "Unexpected empty request");
  }

  auto split_index = start_line.find(' ');
  method = start_line.substr(0, split_index);

  if (auto method_val = as_method(method)) {
    req.method = *method_val;
  } else {
    return res_t(server::unexpect, std::string{"Unknown http method type: "}.append(method));
  }

  if (split_index == std::string_view::npos) {
    return res_t(server::unexpect, "Wrong request format. No URI.");
  }

  start_line = start_line.substr(split_index + 1);
  remove_spaces(start_line);

  split_index = start_line.find(' ');
  path = start_line.substr(0, split_index);
  remove_spaces(path);

  req.target = path;

  if (split_index == std::string_view::npos) {
    return res_t(server::unexpect, "Wrong request format. HTTP version.");
  }

  version = start_line.substr(split_index + 1);
  remove_spaces(version);

  if (auto ver_val = as_http_version(version)) {
    req.version = *ver_val;
  } else {
    return res_t(server::unexpect, std::string{"Unsupported http version: "}.append(version));
  }

  return std::move(req);
}

void request::try_parse_headers() {
  if (_headers_parsed) {
    return;
  }

  if (_headers_str.empty()) {
    _headers_parsed = true;
    return;
  }

  size_t cursor = 0;
  while (cursor < _headers_str.size()) {
    const auto next_lf = _headers_str.find('\n', cursor);
    if (next_lf == std::string_view::npos) {
      break;
    }
    auto line = _headers_str.substr(cursor, next_lf - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      const auto name = line.substr(0, colon);
      auto value = line.substr(colon + 1);

      // trim leading spaces
      if (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }

      _headers.emplace_back(traits_cast<ascii_ci_traits>(name), value);
    }
    cursor = next_lf + 1;
  }

  std::ranges::stable_sort(_headers, headers_comparator{});

  _headers_parsed = true;
}

async_coro::task<expected<request, std::string>> request::read(server::socket_layer::connection& conn) {  // NOLINT(*-reference*)
  using res_t = expected<request, std::string>;

  std::array<std::byte, 4 * 1024> buffer;  // NOLINT(*)
  std::vector<std::byte> bytes;

  while (!conn.is_closed()) {
    auto read = co_await conn.read_buffer(std::span{buffer});
    if (!read.has_value()) {
      co_return res_t{unexpect, std::move(read).error()};
    }

    const auto bytes_read = read.value();

    std::copy(buffer.begin(), buffer.begin() + bytes_read, std::back_inserter(bytes));  // NOLINT(*narrowing*, *pointer*)

    if (bytes_read < buffer.size()) {
      // finished read
      break;
    }
  }

  co_return parse(std::move(bytes));
}

}  // namespace server::http1
