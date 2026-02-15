#include <async_coro/config.h>
#include <server/http1/http_error.h>
#include <server/http1/http_status_code.h>
#include <server/http1/response.h>
#include <server/utils/compression_pool.h>
#include <server/utils/expected.h>

#include <array>
#include <charconv>
#include <memory>
#include <span>
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

void response::set_status(http_error &&error) {
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

void response::set_body_impl(std::string_view body, static_string content_type, bool is_body_static, std::string *body_str) {  // NOLINT(*complexity*)
  ASYNC_CORO_ASSERT(_body.empty());

  const auto set_raw_body = [&]() {
    if (is_body_static) {
      _body = body;
    } else if (body_str != nullptr) {
      _body = add_string(std::move(*body_str));
    } else {
      _body = add_string(std::string{body});
    }
  };

  if (this->has_encoding()) {
    // Compress body
    auto body_in = std::as_bytes(std::span{body});

    auto encoder = _compression_pool->acquire_compressor(_encoding);

    std::string compressed_data;
    std::array<std::byte, 4 * 1024> compress_buf;  // NOLINT(*)
    std::span<std::byte> compress_out{compress_buf.data(), compress_buf.size()};
    bool compress{encoder};

    // Update compression with body data
    while (compress && !body_in.empty()) {
      if (!encoder.update_stream(body_in, compress_out)) {
        // send as is
        compress = false;
        break;
      }

      auto data_to_copy = std::span{compress_buf.data(), compress_out.data()};
      if (!data_to_copy.empty()) {
        compressed_data.append(reinterpret_cast<const char *>(data_to_copy.data()), data_to_copy.size());  // NOLINT(*reinterpret-cast)
      }

      compress_out = {compress_buf.data(), compress_buf.size()};
    }

    // End stream
    while (compress) {
      auto has_more_data = encoder.end_stream(body_in, compress_out);

      auto data_to_copy = std::span{compress_buf.data(), compress_out.data()};
      if (!data_to_copy.empty()) {
        compressed_data.append(reinterpret_cast<const char *>(data_to_copy.data()), data_to_copy.size());  // NOLINT(*reinterpret-cast)
      }

      compress_out = {compress_buf.data(), compress_buf.size()};

      if (!has_more_data) {
        break;
      }
    }

    if (compress) {
      _body = add_string(std::move(compressed_data));
      _headers.emplace_back("Content-Encoding", as_string(_encoding));
    } else {
      set_raw_body();
    }
  } else {
    set_raw_body();
  }

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

void response::set_body(static_string body, static_string content_type) {  // NOLINT(*swap*)
  set_body_impl(body.str, content_type, true, nullptr);
}

void response::set_body(std::string body, static_string content_type) {
  set_body_impl(body, content_type, false, std::addressof(body));
}

void response::clear() {
  _headers.clear();
  _body = {};
  _was_sent = false;
  set_status(status_code::ok);
  if (_string_storage) {
    _string_storage->clear(_string_storage);
  }
  _encoding = compression_encoding::none;
}

// NOLINTBEGIN(*pointer*,*array-index*,*macro*)
async_coro::task<expected<void, std::string>> response::send(server::socket_layer::connection &conn) {  // NOLINT(*complexity*)
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
  PUSH_TO_BUF(" "sv);

  // status code
  std::array<char, 6> buf;  // NOLINT(*)
  auto res = std::to_chars(buf.data(), buf.data() + buf.size(), _status_code.value);
  if (res.ec == std::errc{}) {
    std::string_view str_num{buf.data(), res.ptr};
    PUSH_TO_BUF(str_num);
  } else {
    co_return res_t{unexpect, "Can't write status code"};
  }
  PUSH_TO_BUF(" "sv);

  // reason
  PUSH_TO_BUF(_reason);

  // end first line
  PUSH_TO_BUF("\r\n"sv);

  // write headers
  for (auto &header : _headers) {
    PUSH_TO_BUF(header.first);
    PUSH_TO_BUF(": "sv);
    PUSH_TO_BUF(header.second);
    PUSH_TO_BUF("\r\n"sv);
  }

  PUSH_TO_BUF("\r\n"sv);
  if (!_body.empty()) {
    PUSH_TO_BUF(_body);
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
