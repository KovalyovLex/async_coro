#include <gtest/gtest.h>

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
};

// Tests for encoding_to_string conversion
TEST_F(http_encoding_tests, echo_deflate_encoded) {
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

  constexpr std::string_view encoding = "Content-Encoding:";

  ASSERT_TRUE(resp.find(encoding) != std::string::npos);
  std::string_view enc = resp;
  {
    enc = enc.substr(resp.find(encoding));
    enc.remove_prefix(encoding.size());
    enc = enc.substr(0, enc.find('\n'));
    if (!enc.empty() && enc.back() == '\r') {
      enc.remove_suffix(1);
    }
    if (!enc.empty() && enc.front() == ' ') {
      enc.remove_prefix(1);
    }
  }
  EXPECT_EQ(enc, "gzip");
}
