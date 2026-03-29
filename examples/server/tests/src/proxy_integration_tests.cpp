
#include <gtest/gtest.h>
#include <server/core/i_read_connection.h>
#include <server/core/i_write_connection.h>
#include <server/http1/http_client.h>
#include <server/http1/http_method.h>
#include <server/http1/http_server.h>
#include <server/http1/http_server_config.h>
#include <server/http1/http_status_code.h>
#include <server/http1/request.h>
#include <server/http1/response.h>

#include "fixtures/test_write_connection.h"

TEST(proxy_example, forward_inserts_attributes) {
  using namespace server::http1;

  // dummy request we might want to forward
  request orig;  // empty is enough for compilation

  // For a little more sanity, make a simple request string via parsing and
  // ensure the serialized output contains the X‑Forwarded header.
  const std::string_view wire =
      "GET /foo HTTP/1.1\r\n"
      "Host: myhost\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  auto req = http_client::forward_request(std::move(orig));

  auto serialized = test_write_connection::serialize(req);

  EXPECT_NE(serialized.find("X-Forwarded-Proto"), std::string::npos);
  EXPECT_NE(serialized.find("X-Forwarded-Host: myhost"), std::string::npos);
  EXPECT_NE(serialized.find("X-Forwarded-For"), std::string::npos);
}
