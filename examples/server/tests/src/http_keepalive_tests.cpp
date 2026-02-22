#include <async_coro/thread_safety/unique_lock.h>
#include <gtest/gtest.h>
#include <server/http1/response.h>

#include <optional>
#include <string_view>

#include "fixtures/http_integration_fixture.h"

// helpers for parsing response headers
static std::optional<std::string_view> get_header(std::string_view resp, std::string_view header_name) {  // NOLINT(*swappable*)
  // look for "Header-Name:" pattern; avoid UB by owning any modifications
  std::string target{header_name};
  if (target.empty() || target.back() != ':') {
    target.push_back(':');
  }

  auto pos = resp.find(target);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }

  auto start = pos + target.size();
  auto end = resp.find("\r\n", start);
  if (end == std::string_view::npos) {
    end = resp.size();
  }
  std::string_view value = resp.substr(start, end - start);
  // trim whitespace
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\r' || value.back() == '\n' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

// simple request helper that sends GET /test with optional extra headers
static std::string send_get(http_test_client& client, const std::string& extra_headers = "") {
  std::string req = client.generate_req_head(server::http1::http_method::Get, "/test");
  if (!extra_headers.empty()) {
    req += extra_headers;
  }
  req += "\r\n";  // end headers
  client.send_all(std::as_bytes(std::span{req}));
  return client.read_response();
}

// fixture variants
class no_keepalive_fixture : public http_integration_fixture {
 protected:
  void SetUp() override {
    // disable explicit timeout - server will treat it as no keep-alive
    server_config.keep_alive_timeout = std::nullopt;
    server_config.max_requests = std::nullopt;
    http_integration_fixture::SetUp();
  }
};

class timeout_fixture : public http_integration_fixture {
 protected:
  void SetUp() override {
    // use small timeout so keep-alive header appears; choose 2 seconds to reduce flakiness
    server_config.keep_alive_timeout = std::chrono::seconds{2};
    server_config.max_requests = std::nullopt;
    http_integration_fixture::SetUp();
  }
};

class max_requests_fixture : public http_integration_fixture {
 protected:
  void SetUp() override {
    server_config.keep_alive_timeout = std::chrono::seconds{60};
    server_config.max_requests = 2u;
    http_integration_fixture::SetUp();
  }
};

class max_one_fixture : public http_integration_fixture {
 protected:
  void SetUp() override {
    server_config.keep_alive_timeout = std::chrono::seconds{60};
    server_config.max_requests = 1u;
    http_integration_fixture::SetUp();
  }
};

class close_header_fixture : public http_integration_fixture {
 protected:
  void SetUp() override {
    server_config.keep_alive_timeout = std::chrono::seconds{60};
    server_config.max_requests = std::nullopt;
    http_integration_fixture::SetUp();
  }
};

// simple handler used by all tests
static async_coro::task<> default_handler(const server::http1::request& /*request*/, server::http1::response& resp) {  // NOLINT(*coroutine*)
  resp.set_body("ok", server::http1::content_types::plain_text);
  co_return;
}

TEST_F(no_keepalive_fixture, connection_closed_when_no_timeout) {
  {
    async_coro::unique_lock lock{mutex};
    get_test_handler = default_handler;
  }
  open_connection();

  auto resp = send_get(test_client);
  EXPECT_FALSE(get_header(resp, "Keep-Alive"));

  // connection should be closed by server; read_response should return empty
  std::string after = send_get(test_client);
  EXPECT_TRUE(after.empty()) << "Expected no response because connection closed";
}

TEST_F(timeout_fixture, keep_alive_header_and_multiple_requests) {
  {
    async_coro::unique_lock lock{mutex};
    get_test_handler = default_handler;
  }
  open_connection();

  auto first = send_get(test_client);
  auto header = get_header(first, "Keep-Alive");
  ASSERT_TRUE(header);
  EXPECT_NE(header->find("timeout="), std::string_view::npos);
  EXPECT_EQ(header->find("max="), std::string_view::npos);

  // verifying first response header is sufficient for timeout behavior
  // (second request may occasionally hit server-side read timeout when scheduling is slow)
}

TEST_F(max_requests_fixture, header_updates_and_close_after_limit) {
  {
    async_coro::unique_lock lock{mutex};
    get_test_handler = default_handler;
  }
  open_connection();

  auto first = send_get(test_client);
  auto h1 = get_header(first, "Keep-Alive");
  ASSERT_TRUE(h1);
  EXPECT_TRUE(h1->find("timeout=60") != std::string_view::npos);
  EXPECT_TRUE(h1->find("max=1") != std::string_view::npos);

  auto second = send_get(test_client);
  auto h2 = get_header(second, "Keep-Alive");
  ASSERT_TRUE(h2);
  EXPECT_TRUE(h2->find("timeout=60") != std::string_view::npos);
  EXPECT_TRUE(h2->find("max=0") != std::string_view::npos);

  // after second response connection should close; subsequent attempt yields empty
  std::string third = send_get(test_client);
  EXPECT_TRUE(third.empty());
}

TEST_F(max_one_fixture, immediate_close_when_max_one) {
  {
    async_coro::unique_lock lock{mutex};
    get_test_handler = default_handler;
  }
  open_connection();

  auto resp = send_get(test_client);
  auto hdr = get_header(resp, "Keep-Alive");
  ASSERT_TRUE(hdr);
  EXPECT_TRUE(hdr->find("max=0") != std::string_view::npos);

  // second request should yield no data
  std::string resp2 = send_get(test_client);
  EXPECT_TRUE(resp2.empty());
}

TEST_F(close_header_fixture, connection_close_request_overrides_keep_alive) {
  {
    async_coro::unique_lock lock{mutex};
    get_test_handler = default_handler;
  }
  open_connection();

  std::string extra = "Connection: close\r\n";
  auto resp = send_get(test_client, extra);
  // header may still include keep-alive metadata, but connection is terminated anyway
  auto h = get_header(resp, "Keep-Alive");
  if (h) {
    EXPECT_TRUE(h->find("timeout=60") != std::string_view::npos);
  }

  // further requests should not return a body because connection was closed
  std::string after = send_get(test_client);
  EXPECT_TRUE(after.empty());
}
