#include <async_coro/config.h>
#include <server/http1/http_error.h>
#include <server/http1/http_status_code.h>
#include <server/http1/response.h>
#include <server/utils/expected.h>

#include <array>
#include <charconv>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace server::http1 {

response::response(http_version ver) noexcept
    : _ver(ver),
      _status_code(status_code::ok),
      _reason(as_string(status_code::ok)) {
}

void response::set_status(http_status_code status, std::string_view reason) {
  _status_code = status;
  _reason = add_string(reason);
}

void response::set_status(http_status_code status, static_string reason) noexcept {
  _status_code = status;
  _reason = reason.str;
}

void response::set_status(status_code status) noexcept {
  _status_code = status;
  _reason = as_string(status);
}

void response::set_status(http_error &&error) noexcept {
  _status_code = error.status_code;

  std::visit(
      [&](auto &&val) {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, static_string>) {
          _reason = val.str;
        } else {
          _reason = add_string(std::forward<decltype(val)>(val));
        }
      },
      std::move(error).reason);
  set_body(static_string{_reason}, content_types::plain_text);
}

std::string_view response::add_string(std::string &&str) {  // NOLINT(*not-moved)
  if (str.empty()) {
    return {};
  }

  if (!_string_storage) {
    _string_storage = std::make_unique<string_storage>();
  }

  return _string_storage->put_string(str, &str);
}

std::string_view response::add_string(std::string_view str) {
  if (str.empty()) {
    return {};
  }

  if (!_string_storage) {
    _string_storage = std::make_unique<string_storage>();
  }

  return _string_storage->put_string(str, nullptr);
}

void response::add_header(static_string name, static_string value) {
  _headers.emplace_back(name.str, value.str);
}

void response::set_body(static_string body, static_string content_type) {  // NOLINT(*swap*)
  ASYNC_CORO_ASSERT(_body.empty());

  _body = body.str;

  std::array<char, 16> buf;  // NOLINT(*init*, *magic*)

  auto res = std::to_chars(buf.data(), buf.data() + buf.size(), _body.size());  // NOLINT(*pointer*)
  if (res.ec == std::errc{}) {
    std::string_view str{buf.data(), res.ptr};
    _headers.emplace_back("Content-Length", add_string(str));
  }
  if (!content_type.str.empty()) {
    _headers.emplace_back("Content-Type", content_type.str);
  }
}

void response::clear() {
  _headers.clear();
  _body = {};
  _was_sent = false;
  set_status(status_code::ok);
  if (_string_storage) {
    _string_storage->clear(_string_storage);
  }
}

// NOLINTBEGIN(*pointer*,*array-index*,*macro*)
async_coro::task<expected<void, std::string>> response::send(server::socket_layer::connection &conn) {  // NOLINT(*complexity*)
  using res_t = expected<void, std::string>;
  using namespace std::string_view_literals;

  std::array<std::byte, 4 * 1024> buffer;  // NOLINT(*)
  size_t buff_i = 0;

#define PUSH_TO_BUF(STR)                                                                                \
  {                                                                                                     \
    const std::string_view str{STR};                                                                    \
    if (buff_i + str.size() >= buffer.size()) {                                                         \
      const auto *ptr = str.data();                                                                     \
      const auto *ptr_end = str.data() + str.size();                                                    \
      while (ptr_end > ptr) {                                                                           \
        const auto to_copy = std::min<size_t>(size_t(ptr_end - ptr), buffer.size() - buff_i - 1);       \
        std::memcpy(std::addressof(buffer[buff_i]), ptr, to_copy);                                      \
        ptr += to_copy;                                                                                 \
        buff_i += to_copy;                                                                              \
        if (buff_i == buffer.size()) {                                                                  \
          /*send this portion to the client*/                                                           \
          auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buffer.data() + buff_i}); \
          buff_i = 0; /*reset buffer index*/                                                            \
          if (!send_res) {                                                                              \
            co_return res_t{unexpect, std::move(send_res).error()};                                     \
          }                                                                                             \
        } else {                                                                                        \
          /*accumulate buffer*/                                                                         \
          break;                                                                                        \
        }                                                                                               \
      }                                                                                                 \
    } else if (!str.empty()) {                                                                          \
      std::memcpy(std::addressof(buffer[buff_i]), str.data(), str.size());                              \
      buff_i += str.size();                                                                             \
    }                                                                                                   \
  }                                                                                                     \
  (void)0

  // http version
  PUSH_TO_BUF(as_string(_ver));
  PUSH_TO_BUF(" ");

  // status code
  std::array<char, 6> buf;  // NOLINT(*)
  auto res = std::to_chars(buf.data(), buf.data() + buf.size(), _status_code.value);
  if (res.ec == std::errc{}) {
    std::string_view str_num{buf.data(), res.ptr};
    PUSH_TO_BUF(str_num);
  } else {
    co_return res_t{unexpect, "Can't write status code"};
  }
  PUSH_TO_BUF(" ");

  // reason
  PUSH_TO_BUF(_reason);

  // end first line
  PUSH_TO_BUF("\r\n");

  // write headers
  for (auto &header : _headers) {
    PUSH_TO_BUF(header.first);
    PUSH_TO_BUF(":");
    PUSH_TO_BUF(header.second);
    PUSH_TO_BUF("\r\n");
  }

  // write body
  if (!_body.empty()) {
    PUSH_TO_BUF("\r\n");
    PUSH_TO_BUF(_body);
  } else {
    PUSH_TO_BUF("\r\n");
  }

#undef PUSH_TO_BUF

  // send final part
  if (buff_i != 0) {
    auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buff_i});
    if (!send_res) {
      co_return res_t{unexpect, std::move(send_res).error()};
    }
  }

  _was_sent = true;
  co_return res_t{};
}
// NOLINTEND(*pointer*,*array-index*,*macro*)

}  // namespace server::http1
