// This file was split out from web_socket_tests.cpp
#include <gtest/gtest.h>

#include <span>

#include "fixtures/web_socket_integration_fixture.h"
#include "fixtures/ws_test_client.h"

using namespace server;

TEST_F(web_socket_integration_tests, handshake_success) {
  open_connection();

  auto req = ws_test_client::generate_handshake_request(test_client, "/chat");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_NE(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, text_echo) {
  open_connection();

  auto req = ws_test_client::generate_handshake_request(test_client, "/chat");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  ASSERT_NE(resp.find("101"), std::string::npos);

  auto frame = ws_test_client::generate_text_frame("hello", true);
  test_client.send_all(frame);

  auto parsed = ws_test_client::recv_frame(test_client);
  EXPECT_EQ(parsed.opcode, 0x1);
  std::string payload(reinterpret_cast<const char*>(parsed.payload.data()), parsed.payload.size());
  EXPECT_EQ(payload, "Hello from server!\ngot: hello");
}

static std::string make_bad_handshake_missing(const std::string& missing_header) {
  // build a minimal handshake but omit the value for the specified header
  std::string req = "GET /chat HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (missing_header != "Upgrade") {
    req += "Upgrade: websocket\r\n";
  }
  if (missing_header != "Connection") {
    req += "Connection: Upgrade\r\n";
  }
  if (missing_header != "Sec-WebSocket-Key") {
    req += "Sec-WebSocket-Key: abcdefg==\r\n";
  }
  if (missing_header != "Sec-WebSocket-Version") {
    req += "Sec-WebSocket-Version: 13\r\n";
  }
  req += "\r\n";
  return req;
}

TEST_F(web_socket_integration_tests, missing_upgrade_header) {
  open_connection();

  auto req = make_bad_handshake_missing("Upgrade");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, missing_connection_header) {
  open_connection();

  auto req = make_bad_handshake_missing("Connection");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, missing_ws_key_header) {
  open_connection();

  auto req = make_bad_handshake_missing("Sec-WebSocket-Key");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, missing_ws_version_header) {
  open_connection();

  auto req = make_bad_handshake_missing("Sec-WebSocket-Version");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, unsupported_ws_version) {
  open_connection();

  std::string req = "GET /chat HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: abc==\r\nSec-WebSocket-Version: 999\r\n\r\n";
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

TEST_F(web_socket_integration_tests, unsupported_protocol) {
  open_connection();

  std::string req = ws_test_client::generate_handshake_request(test_client, "/chat", "", "badproto");
  ASSERT_TRUE(test_client.send_all(std::as_bytes(std::span{req})));

  auto resp = test_client.read_response();
  EXPECT_EQ(resp.find("101"), std::string::npos);
}
