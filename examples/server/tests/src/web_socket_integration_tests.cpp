// This file was split out from web_socket_tests.cpp
#include <gtest/gtest.h>

#include "fixtures/web_socket_integration_fixture.h"

using namespace server;

TEST_F(web_socket_integration_tests, handshake_success) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);

  auto req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1");
  ws_test_client::send_all(sock, req.data(), req.size());

  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_NE(resp.find("101"), std::string::npos);

  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, text_echo) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);

  auto req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  ASSERT_NE(resp.find("101"), std::string::npos);

  auto frame = ws_test_client::generate_text_frame("hello", true);
  ws_test_client::send_all(sock, frame.data(), frame.size());

  auto parsed = ws_test_client::recv_frame(sock);
  EXPECT_EQ(parsed.opcode, 0x1);
  std::string payload(reinterpret_cast<const char*>(parsed.payload.data()), parsed.payload.size());
  EXPECT_EQ(payload, "Hello from server!\ngot: hello");

  socket_layer::close_socket(sock.get_platform_id());
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
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  auto req = make_bad_handshake_missing("Upgrade");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, missing_connection_header) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  auto req = make_bad_handshake_missing("Connection");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, missing_ws_key_header) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  auto req = make_bad_handshake_missing("Sec-WebSocket-Key");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, missing_ws_version_header) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  auto req = make_bad_handshake_missing("Sec-WebSocket-Version");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, unsupported_ws_version) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  std::string req = "GET /chat HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: abc==\r\nSec-WebSocket-Version: 999\r\n\r\n";
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}

TEST_F(web_socket_integration_tests, unsupported_protocol) {
  auto sock = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
  ASSERT_NE(sock, socket_layer::invalid_connection);
  std::string req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1", "", "badproto");
  ws_test_client::send_all(sock, req.data(), req.size());
  auto resp = ws_test_client::recv_http_response(sock);
  EXPECT_EQ(resp.find("101"), std::string::npos);
  socket_layer::close_socket(sock.get_platform_id());
}
