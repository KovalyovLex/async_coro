#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <gtest/gtest.h>
#include <server/http1/client_request.h>
#include <server/http1/client_response.h>
#include <server/http1/http_client.h>
#include <server/http1/request.h>
#include <server/http1/response.h>  // for content_types

#include <span>
#include <string_view>

#include "utils/test_write_connection.h"

TEST(client_integration, request_to_string) {
  using namespace server::http1;
  using server::static_string;

  client_request req{http_method::Post, static_string{"/test"}};

  req.add_header(static_string{"Host"}, static_string{"example"});
  req.set_body(std::string{"body"}, content_types::plain_text);

  auto out = test_write_connection::serialize(req);

  EXPECT_NE(out.find("POST /test HTTP/1.1"), std::string::npos);
  EXPECT_NE(out.find("Content-Length: 4"), std::string::npos);
  EXPECT_NE(out.find("body"), std::string::npos);
}

TEST(client_integration, response_parse) {
  using namespace server::http1;

  client_response resp;
  client_response::parser_ptr parser{};

  const std::string_view str =
      "HTTP/1.1 201 Created\r\n"
      "Content-Length: 7\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "payload";

  auto bytes = std::as_bytes(std::span{str});
  resp.begin_parse(parser);
  auto res = resp.parse_data_part(parser, bytes);
  ASSERT_TRUE(res);
  EXPECT_TRUE(resp.is_parsed());
  EXPECT_EQ(resp.get_status_code(), status_code::created);
  EXPECT_EQ(resp.get_body(), "payload");
}
