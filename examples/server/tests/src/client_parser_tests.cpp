#include <gtest/gtest.h>
#include <server/http1/client_response.h>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

using namespace server::http1;  // NOLINT(*-using-namespace)

static auto to_bytes(std::string_view str) {
  std::span span{str};
  return std::as_bytes(span);
}

TEST(client_response_parser, simple_ok_single_portion) {
  client_response resp;
  client_response::parser_ptr parser{};

  resp.begin_parse(parser);

  const auto bytes = to_bytes(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 5\r\n"
      "Host: example.com\r\n"
      "\r\n"
      "hello");
  auto res = resp.parse_data_part(parser, bytes);

  ASSERT_TRUE(res);
  EXPECT_TRUE(resp.is_parsed());
  EXPECT_EQ(resp.get_status_code(), status_code::ok);
  EXPECT_EQ(resp.get_body(), "hello");
}

TEST(client_response_parser, chunked_various_splits) {
  const std::string_view total =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "4\r\nWiki\r\n"
      "5\r\npedia\r\n"
      "0\r\n\r\n";

  std::vector<std::vector<size_t>> patterns = {
      {10, 30, 10, 100},
      {1, 1, 1, 1, 1, 1000},
      {25, 2, 2, 2, 2, 1000},
  };

  for (const auto& cuts : patterns) {
    client_response resp;
    client_response::parser_ptr parser{};
    resp.begin_parse(parser);

    size_t pos = 0;
    for (size_t cut : cuts) {
      size_t len = std::min(cut, total.size() - pos);
      auto part = total.substr(pos, len);
      auto bytes_part = to_bytes(part);
      auto res = resp.parse_data_part(parser, bytes_part);
      ASSERT_TRUE(res);
      pos += len;
      if (pos >= total.size()) {
        break;
      }
    }
    if (pos < total.size()) {
      auto bytes_part = to_bytes(total.substr(pos));
      auto res = resp.parse_data_part(parser, bytes_part);
      ASSERT_TRUE(res);
    }

    EXPECT_TRUE(resp.is_parsed());
    EXPECT_EQ(std::string(resp.get_body()), "Wikipedia");
  }
}

TEST(client_response_parser, invalid_status_line) {
  client_response resp;
  client_response::parser_ptr parser{};
  resp.begin_parse(parser);
  const auto bytes = to_bytes("BADVERSION 200 OK\r\n\r\n");
  auto res = resp.parse_data_part(parser, bytes);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::http_version_not_supported);
  EXPECT_FALSE(resp.is_parsed());
}

TEST(client_response_parser, missing_code) {
  client_response resp;
  client_response::parser_ptr parser{};
  resp.begin_parse(parser);
  const auto bytes = to_bytes("HTTP/1.1 OK\r\n\r\n");
  auto res = resp.parse_data_part(parser, bytes);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::bad_request);
}

TEST(client_response_parser, wrong_content_length) {
  client_response resp;
  client_response::parser_ptr parser{};
  resp.begin_parse(parser);
  const auto bytes = to_bytes(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: notnumber\r\n"
      "\r\n"
      "hello");
  auto res = resp.parse_data_part(parser, bytes);
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::bad_request);
}
