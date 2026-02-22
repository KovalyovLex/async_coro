#include <async_coro/config.h>
#include <server/http1/client_request.h>
#include <server/utils/expected.h>

#include <array>
#include <charconv>
#include <span>
#include <string>
#include <utility>

namespace server::http1 {

client_request::client_request(http_method method, std::string_view target, http_version ver) noexcept
    : _method(method),
      _target(target),
      _version(ver) {}

std::string_view client_request::add_string(std::string &&str) {  // NOLINT(*not-moved)
  if (str.empty()) {
    return {};
  }
  if (!_string_storage) {
    _string_storage = std::make_unique<string_storage>();
  }
  return _string_storage->put_string(str, &str);
}

std::string_view client_request::add_string(std::string_view str) {
  if (str.empty()) {
    return {};
  }
  if (!_string_storage) {
    _string_storage = std::make_unique<string_storage>();
  }
  return _string_storage->put_string(str, nullptr);
}

void client_request::add_header(static_string name, static_string value) {
  _headers.emplace_back(name.str, value.str);
}

void client_request::set_body(std::string body, static_string content_type) {
  set_body(static_string{add_string(std::move(body))}, content_type);
}

void client_request::set_body(static_string body, static_string content_type) {
  _body = body.str;
  if (!content_type.str.empty()) {
    _headers.emplace_back("Content-Type", content_type.str);
  }
  std::array<char, 20> buf{};
  auto res = std::to_chars(buf.data(), buf.data() + buf.size(), _body.size());
  if (res.ec == std::errc{}) {
    std::string_view lenstr{buf.data(), res.ptr};
    _headers.emplace_back("Content-Length", add_string(lenstr));
  }
}

void client_request::clear() {
  _headers.clear();
  _body = {};
  _was_sent = false;
  if (_string_storage) {
    _string_storage->clear(_string_storage);
  }
}

// very similar to response::send with adjusted first line
// NOLINTBEGIN(*pointer*,*array-index*,*macro*)
async_coro::task<expected<void, std::string>> client_request::send(server::socket_layer::connection &conn) {  // NOLINT(*complexity*)
  using res_t = expected<void, std::string>;
  using namespace std::string_view_literals;

  std::array<std::byte, 4 * 1024> buffer;  // NOLINT(*)
  size_t buff_i = 0;

#define PUSH_TO_BUF(ARR)                                                                                \
  {                                                                                                     \
    const std::span str{ARR};                                                                           \
    if (buff_i + str.size() >= buffer.size()) {                                                         \
      const auto *ptr = str.data();                                                                     \
      const auto *ptr_end = str.data() + str.size();                                                    \
      while (ptr_end > ptr) {                                                                           \
        const auto to_copy = std::min<size_t>(size_t(ptr_end - ptr), buffer.size() - buff_i - 1);       \
        std::memcpy(std::addressof(buffer[buff_i]), ptr, to_copy);                                      \
        ptr += to_copy;                                                                                 \
        buff_i += to_copy;                                                                              \
        if (buff_i == buffer.size()) {                                                                  \
          auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buffer.data() + buff_i}); \
          buff_i = 0;                                                                                   \
          if (!send_res) {                                                                              \
            co_return res_t{unexpect, std::move(send_res).error()};                                     \
          }                                                                                             \
        } else {                                                                                        \
          break;                                                                                        \
        }                                                                                               \
      }                                                                                                 \
    } else if (!str.empty()) {                                                                          \
      std::memcpy(std::addressof(buffer[buff_i]), str.data(), str.size());                              \
      buff_i += str.size();                                                                             \
    }                                                                                                   \
  }                                                                                                     \
  (void)0

  // start line
  PUSH_TO_BUF(as_string(_method));
  PUSH_TO_BUF(" "sv);
  PUSH_TO_BUF(_target);
  PUSH_TO_BUF(" "sv);
  PUSH_TO_BUF(as_string(_version));
  PUSH_TO_BUF("\r\n"sv);

  for (auto &h : _headers) {
    PUSH_TO_BUF(h.first);
    PUSH_TO_BUF(": "sv);
    PUSH_TO_BUF(h.second);
    PUSH_TO_BUF("\r\n"sv);
  }
  PUSH_TO_BUF("\r\n"sv);
  if (!_body.empty()) {
    PUSH_TO_BUF(_body);
  }

#undef PUSH_TO_BUF

  if (buff_i != 0) {
    auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buff_i});
    if (!send_res) {
      co_return res_t{unexpect, std::move(send_res).error()};
    }
  }

  _was_sent = true;
  co_return res_t{};
}  // NOLINTEND(*pointer*,*array-index*,*macro*)

std::string client_request::to_string() const {
  using namespace std::string_view_literals;
  std::string out;
  out.reserve(256);

  // helper lambda similar to PUSH_TO_BUF but pushing into string
  auto push = [&](std::string_view str) {
    out.append(str);
  };

  push(as_string(_method));
  push(" "sv);
  push(_target);
  push(" "sv);
  push(as_string(_version));
  push("\r\n"sv);

  for (const auto &header : _headers) {
    push(header.first);
    push(": "sv);
    push(header.second);
    push("\r\n"sv);
  }
  push("\r\n"sv);
  if (!_body.empty()) {
    push(_body);
  }

  return out;
}

}  // namespace server::http1
