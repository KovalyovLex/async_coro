#include <gtest/gtest.h>

#include <array>
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

  static std::string decompress_body(std::string_view str, server::pooled_compressor<server::decompressor_variant>& decompress) {
    auto bytes_in = std::as_bytes(std::span{str});
    std::string res;

    std::array<char, 1024> tmp_buf{};

    while (!bytes_in.empty()) {
      auto bytes_out = std::as_writable_bytes(std::span<char>{tmp_buf});
      if (!decompress.update_stream(bytes_in, bytes_out)) {
        EXPECT_TRUE(false) << "Decompression failed";
        return {};
      }

      auto data_to_copy = std::string_view{tmp_buf.data(), tmp_buf.size() - bytes_out.size()};
      res += data_to_copy;
    }

    while (true) {
      auto bytes_out = std::as_writable_bytes(std::span<char>{tmp_buf});
      bool has_more_data = decompress.end_stream(bytes_in, bytes_out);

      auto data_to_copy = std::string_view{tmp_buf.data(), tmp_buf.size() - bytes_out.size()};
      res += data_to_copy;

      if (!has_more_data) {
        break;
      }
    }

    return res;
  }

  static std::string get_long_message() {
    std::string long_string;

    long_string = "The quick brown fox jumps over the lazy dog.";
    for (int i = 0; i < 10000; ++i) {
      long_string += (i % 1 == 0) ? ';' : '\n';
      long_string += "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
    }

    return long_string;
  }

  void test_message_compression(std::string_view str_view, server::compression_encoding enc) {
    const auto enc_str = as_string(enc);

    {
      async_coro::unique_lock lock{mutex};

      get_test_handler = [str = std::string{str_view}](const server::http1::request&, server::http1::response& resp) -> async_coro::task<> {  // NOLINT(*reference*)
        resp.set_body(str, server::http1::content_types::plain_text);
        co_return;
      };
    }
    open_connection();

    std::string req = test_client.generate_req_head(server::http1::http_method::GET, "/test");
    req += "Accept-Encoding: ";
    req += enc_str;
    req += "\r\n";
    req += "\r\n";

    test_client.send_all(std::as_bytes(std::span{req}));

    auto resp = test_client.read_response();

    std::string_view req_enc = get_encoding(resp);
    EXPECT_EQ(req_enc, enc_str);

    std::string_view body = get_body(resp);
    auto cnt_len = get_content_length(resp);
    ASSERT_TRUE(cnt_len.has_value());
    EXPECT_EQ(*cnt_len, body.size());  // NOLINT(*unchecked-optional-access) bug, check happened earlier
    EXPECT_NE(body, str_view);

    auto decoder = compression_pool->acquire_decompressor(enc);
    ASSERT_TRUE(decoder);

    auto body_str = decompress_body(body, decoder);
    EXPECT_EQ(body_str, str_view);
  }
};

#if SERVER_HAS_ZLIB
// Tests for gzip encoding message
TEST_F(http_encoding_tests, test_gzip_encoded) {
  constexpr std::string_view k_server_body = "Hello, this is a test message for gzip compression!";

  test_message_compression(k_server_body, server::compression_encoding::gzip);
}

TEST_F(http_encoding_tests, test_gzip_encoded_long) {
  test_message_compression(get_long_message(), server::compression_encoding::gzip);
}

// Tests for deflate encoding message
TEST_F(http_encoding_tests, test_deflate_encoded) {
  constexpr std::string_view k_server_body = "Hello, this is a test message for deflate compression!";

  test_message_compression(k_server_body, server::compression_encoding::deflate);
}

TEST_F(http_encoding_tests, test_deflate_encoded_long) {
  test_message_compression(get_long_message(), server::compression_encoding::deflate);
}
#endif  // SERVER_HAS_ZLIB

#if SERVER_HAS_BROTLI
// Tests for brotli encoding message
TEST_F(http_encoding_tests, test_brotli_encoded) {
  constexpr std::string_view k_server_body = "Hello, this is a test message for brotli compression!";

  test_message_compression(k_server_body, server::compression_encoding::br);
}

TEST_F(http_encoding_tests, test_brotli_encoded_long) {
  test_message_compression(get_long_message(), server::compression_encoding::br);
}
#endif  // SERVER_HAS_BROTLI

#if SERVER_HAS_ZSTD
// Tests for zstd encoding message
TEST_F(http_encoding_tests, test_zstd_encoded) {
  constexpr std::string_view k_server_body = "Hello, this is a test message for zstd compression!";

  test_message_compression(k_server_body, server::compression_encoding::zstd);
}

TEST_F(http_encoding_tests, test_zstd_encoded_long) {
  test_message_compression(get_long_message(), server::compression_encoding::zstd);
}
#endif  // SERVER_HAS_ZSTD
