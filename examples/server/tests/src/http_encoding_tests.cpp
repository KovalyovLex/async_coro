#include <gtest/gtest.h>

#include <charconv>
#include <optional>
#include <span>
#include <string_view>

#include "async_coro/task.h"
#include "async_coro/thread_safety/unique_lock.h"
#include "fixtures/compression_helper.h"
#include "fixtures/http_integration_fixture.h"
#include "server/http1/response.h"

class http_encoding_tests : public http_integration_fixture, public compression_helper {
 protected:
  void SetUp() override {
    // Create compression pool with all available encodings
    server::compression_pool_config pool_config{.encodings = server::compression_pool_config::k_all_encodings};
    compression_pool = server::compression_pool::create(pool_config);

    server.set_compression_config(compression_pool);

    http_integration_fixture::SetUp();
  }

  void TearDown() override {
    http_integration_fixture::TearDown();

    compression_pool = nullptr;
  }

  static std::pair<std::string_view, std::string_view> get_headers_and_body(std::string_view http_resp) {
    constexpr std::string_view k_body_sep = "\r\n\r\n";

    const auto idx = http_resp.find(k_body_sep);
    std::string_view body = {};
    std::string_view headers = http_resp;
    if (idx != std::string_view::npos) {
      body = http_resp.substr(idx + k_body_sep.size());
      headers = http_resp.substr(0, idx);
    }
    return {headers, body};
  }

  static std::string_view get_body(std::string_view http_resp) {
    return get_headers_and_body(http_resp).second;
  }

  static std::string_view get_encoding(std::string_view http_resp) {
    auto headers = get_headers_and_body(http_resp).first;

    constexpr std::string_view k_encoding_str = "Content-Encoding:";

    const auto idx = headers.find(k_encoding_str);
    std::string_view enc = {};
    if (idx != std::string_view::npos) {
      enc = http_resp.substr(idx + k_encoding_str.size());
      enc = enc.substr(0, enc.find('\n'));
      if (!enc.empty() && enc.back() == '\r') {
        enc.remove_suffix(1);
      }
      if (!enc.empty() && enc.front() == ' ') {
        enc.remove_prefix(1);
      }
    }
    return enc;
  }

  static std::optional<size_t> get_content_length(std::string_view http_resp) {
    auto headers = get_headers_and_body(http_resp).first;

    constexpr std::string_view k_len_str = "Content-Length:";

    const auto idx = headers.find(k_len_str);
    std::string_view len_str = {};
    std::optional<size_t> len;
    if (idx != std::string_view::npos) {
      len_str = http_resp.substr(idx + k_len_str.size());
      len_str = len_str.substr(0, len_str.find('\n'));
      if (!len_str.empty() && len_str.back() == '\r') {
        len_str.remove_suffix(1);
      }
      if (!len_str.empty() && len_str.front() == ' ') {
        len_str.remove_prefix(1);
      }

      if (!len_str.empty()) {
        size_t t_len = 0;
        auto res = std::from_chars(len_str.data(), len_str.data() + len_str.size(), t_len);
        if (res.ec == std::errc{}) {
          len = t_len;
        }
      }
    }
    return len;
  }
};

// Tests for gzip encoding message
TEST_F(http_encoding_tests, test_gzip_encoded) {
  {
    async_coro::unique_lock lock{mutex};

    get_test_handler = [](const server::http1::request&, server::http1::response& resp) -> async_coro::task<> {  // NOLINT(*reference*)
      resp.set_body("Hello, this is a test message for gzip compression!", server::http1::content_types::plain_text);
      co_return;
    };
  }
  open_connection();

  std::string req = test_client.generate_req_head(server::http1::http_method::GET, "/test");
  req += "Accept-Encoding: gzip\r\n";
  req += "\r\n";

  test_client.send_all(std::as_bytes(std::span{req}));

  auto resp = test_client.read_response();

  std::string_view enc = get_encoding(resp);
  EXPECT_EQ(enc, "gzip");

  std::string_view body = get_body(resp);
  auto cnt_len = get_content_length(resp);

  ASSERT_TRUE(cnt_len);
  EXPECT_EQ(*cnt_len, body.size());
}
