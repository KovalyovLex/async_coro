
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

#include "utils/test_write_connection.h"

TEST(proxy_example, forward_inserts_attributes) {
  using namespace server::http1;

  // Create a basic server-side request via parsing
  request orig;
  const std::string_view wire =
      "GET /foo HTTP/1.1\r\n"
      "Host: myhost\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  // Forward with explicit parameters
  forwarding_params params{.by = "192.0.2.43", .proto = "https", .for_addr = "198.51.100.17"};
  auto req = http_client::forward_request(std::move(orig), params);

  auto serialized = test_write_connection::serialize(req);

  // Verify RFC 7239 Forwarded header is present with correct format (semicolon-separated parameters)
  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
  // RFC 7239 format: parameters within a proxy element separated by semicolons
  EXPECT_NE(serialized.find("by=192.0.2.43;"), std::string::npos);
  EXPECT_NE(serialized.find("for=198.51.100.17;"), std::string::npos);
  EXPECT_NE(serialized.find("proto=https"), std::string::npos);

  // Ensure X-Forwarded-* headers are NOT present
  EXPECT_EQ(serialized.find("X-Forwarded"), std::string::npos);
}

TEST(proxy_example, forward_extends_existing_header) {
  using namespace server::http1;

  // Create a request with an existing Forwarded header (simulating a chain)
  // RFC 7239 format: parameters within element separated by `;`, elements separated by `,`
  request orig;
  const std::string_view wire =
      "GET /resource HTTP/1.1\r\n"
      "Host: intermediate\r\n"
      "Forwarded: by=192.0.2.1;for=198.51.100.1;proto=http\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  // Forward with new proxy parameters
  forwarding_params params{.by = "192.0.2.43", .proto = "https", .for_addr = "198.51.100.17"};
  auto req = http_client::forward_request(std::move(orig), params);

  auto serialized = test_write_connection::serialize(req);

  // Verify the chain: existing proxy followed by new proxy, comma-separated
  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
  EXPECT_NE(serialized.find("by=192.0.2.1;"), std::string::npos);   // From original chain
  EXPECT_NE(serialized.find("by=192.0.2.43;"), std::string::npos);  // New proxy
  EXPECT_NE(serialized.find("proto=http"), std::string::npos);      // From original chain
  EXPECT_NE(serialized.find("proto=https"), std::string::npos);     // New proxy
  // Verify comma separation between proxy elements
  auto forwarded_pos = serialized.find("Forwarded:");
  auto forwarded_end = serialized.find("\r\n", forwarded_pos);
  auto forwarded_value = serialized.substr(forwarded_pos, forwarded_end - forwarded_pos);
  EXPECT_NE(forwarded_value.find(", "), std::string::npos);  // Comma separates proxy elements
}

TEST(proxy_example, forward_with_ipv6) {
  using namespace server::http1;

  request orig;
  const std::string_view wire =
      "POST /api HTTP/1.1\r\n"
      "Host: api.server\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  // Use IPv6 addresses with brackets
  forwarding_params params{.by = "[2001:db8:cafe::17]", .proto = "http", .for_addr = "[2001:db8:cafe::1]"};
  auto req = http_client::forward_request(std::move(orig), params);

  auto serialized = test_write_connection::serialize(req);

  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
  EXPECT_NE(serialized.find("by=[2001:db8:cafe::17]"), std::string::npos);
  EXPECT_NE(serialized.find("for=[2001:db8:cafe::1]"), std::string::npos);
  EXPECT_NE(serialized.find("proto=http"), std::string::npos);
}

TEST(proxy_example, forward_with_hostname) {
  using namespace server::http1;

  request orig;
  const std::string_view wire =
      "PUT /data HTTP/1.1\r\n"
      "Host: origin.example.com\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  // Use hostnames instead of IP addresses
  forwarding_params params{.by = "proxy.internal", .proto = "https", .for_addr = "client.remote"};
  auto req = http_client::forward_request(std::move(orig), params);

  auto serialized = test_write_connection::serialize(req);

  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
  EXPECT_NE(serialized.find("by=proxy.internal"), std::string::npos);
  EXPECT_NE(serialized.find("for=client.remote"), std::string::npos);
  EXPECT_NE(serialized.find("proto=https"), std::string::npos);
}

TEST(proxy_example, forward_empty_parameters) {
  using namespace server::http1;

  request orig;
  const std::string_view wire =
      "DELETE /item HTTP/1.1\r\n"
      "Host: server\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  // Use default parameters (empty by/for_addr, default proto="https")
  auto req = http_client::forward_request(std::move(orig));

  auto serialized = test_write_connection::serialize(req);

  // Should still have Forwarded header with just proto
  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
  EXPECT_NE(serialized.find("proto=https"), std::string::npos);
  // Should not have by or for when not provided
  EXPECT_EQ(serialized.find("by="), std::string::npos);
  EXPECT_EQ(serialized.find("for="), std::string::npos);
}

TEST(proxy_example, forward_removes_x_forwarded_headers) {
  using namespace server::http1;

  // Create a request that already has old-style X-Forwarded-* headers
  request orig;
  const std::string_view wire =
      "GET /old HTTP/1.1\r\n"
      "Host: oldproxy\r\n"
      "X-Forwarded-For: 203.0.113.1\r\n"
      "X-Forwarded-Proto: http\r\n"
      "X-Forwarded-Host: original.example\r\n"
      "\r\n";
  request::parser_ptr parser;
  orig.begin_parse(parser);
  auto res = orig.parse_data_part(parser, std::as_bytes(std::span{wire}));
  ASSERT_TRUE(res);
  ASSERT_TRUE(orig.is_parsed());

  forwarding_params params{.by = "192.0.2.100", .proto = "https"};
  auto req = http_client::forward_request(std::move(orig), params);

  auto serialized = test_write_connection::serialize(req);

  // Old headers must be completely removed
  EXPECT_EQ(serialized.find("X-Forwarded-For"), std::string::npos);
  EXPECT_EQ(serialized.find("X-Forwarded-Proto"), std::string::npos);
  EXPECT_EQ(serialized.find("X-Forwarded-Host"), std::string::npos);

  // New Forwarded header should be present
  EXPECT_NE(serialized.find("Forwarded:"), std::string::npos);
}
