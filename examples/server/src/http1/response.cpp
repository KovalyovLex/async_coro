#include <async_coro/config.h>
#include <server/http1/response.h>
#include <server/utils/expected.h>

#include <array>
#include <charconv>
#include <memory>
#include <string_view>
#include <system_error>

namespace server::http1 {

response::response(http_version ver, http_status_code status, std::string_view reason)
    : _ver(ver),
      _status_code(status),
      _reason(add_string(reason)) {
}

response::response(http_version ver, http_status_code status, std::string_view reason, static_string_t /*tag*/) noexcept
    : _ver(ver),
      _status_code(status),
      _reason(reason) {
}

response::response(http_version ver, status_code status) noexcept
    : _ver(ver),
      _status_code(status),
      _reason(as_string(status)) {
}

std::string_view response::add_string(std::string_view str) {
  if (str.empty()) {
    return {};
  }

  if (!_string_storage) {
    _string_storage = std::make_unique<string_storage>();
  }

  return _string_storage->put_string(str);
}

void response::add_header(std::string_view name, std::string_view value, static_string_t /*tag*/) {
  _headers.emplace_back(name, value);
}

void response::set_body(std::string_view body, std::string_view content_type, static_string_t /*tag*/) {  // NOLINT(*swap*)
  ASYNC_CORO_ASSERT(_body.empty());

  _body = body;

  std::array<char, 16> buf;  // NOLINT(*)
  auto res = std::to_chars(buf.begin(), buf.end(), body.size());
  if (res.ec == std::errc{}) {
    std::string_view str{buf.begin(), res.ptr};
    _headers.emplace_back("Content-Length", add_string(str));
  }
  if (!content_type.empty()) {
    _headers.emplace_back("Content-Type", content_type);
  }
}

// NOLINTBEGIN(*pointer*,*array-index*,*macro*)
async_coro::task<expected<void, std::string>> response::send(server::socket_layer::connection &conn) {  // NOLINT(*reference*,*complexity*)
  using res_t = expected<void, std::string>;
  using namespace std::string_view_literals;

  std::array<std::byte, 4 * 1024> buffer;  // NOLINT(*)
  size_t buff_i = 0;

#define PUSH_TO_BUF(STR)                                                                              \
  {                                                                                                   \
    const std::string_view str = STR;                                                                 \
    if (buff_i + str.size() >= buffer.size()) {                                                       \
      const auto *ptr = str.data();                                                                   \
      const auto *ptr_end = str.data() + str.size();                                                  \
      while (ptr_end > ptr) {                                                                         \
        const auto to_copy = std::min(size_t(ptr_end - ptr), buffer.size() - buff_i - 1);             \
        std::memcpy(std::addressof(buffer[buff_i]), ptr, to_copy);                                    \
        ptr += to_copy;                                                                               \
        buff_i += to_copy;                                                                            \
        /*write postion to connection layer*/                                                         \
        auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buffer.data() + buff_i}); \
        buff_i = 0; /*reset buffer index*/                                                            \
        if (!send_res) {                                                                              \
          co_return res_t{unexpect, std::move(send_res).error()};                                     \
        }                                                                                             \
      }                                                                                               \
    } else if (!str.empty()) {                                                                        \
      std::memcpy(std::addressof(buffer[buff_i]), str.data(), str.size());                            \
      buff_i += str.size();                                                                           \
    }                                                                                                 \
  }                                                                                                   \
  (void)0

  // http version
  PUSH_TO_BUF(as_string(_ver));
  PUSH_TO_BUF(" ");

  // status code
  std::array<char, 6> buf;  // NOLINT(*)
  auto res = std::to_chars(buf.begin(), buf.end(), _status_code.value);
  if (res.ec == std::errc{}) {
    std::string_view str_num{buf.begin(), res.ptr};
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
  }

#undef PUSH_TO_BUF

  // send
  if (buff_i != 0) {
    auto send_res = co_await conn.write_buffer(std::span{buffer.data(), buffer.data() + buff_i});
    if (!send_res) {
      co_return res_t{unexpect, std::move(send_res).error()};
    }
  }

  co_return res_t{};
}
// NOLINTEND(*pointer*,*array-index*,*macro*)

}  // namespace server::http1
