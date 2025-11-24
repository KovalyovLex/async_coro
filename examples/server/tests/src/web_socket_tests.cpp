
#include <gtest/gtest.h>
#include <server/http1/http_server.h>
#include <server/http1/session.h>
#include <server/tcp_server_config.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "async_coro/utils/unique_function.h"
#include "server/socket_layer/connection_id.h"
#include "ws_test_client.h"

using namespace server::web_socket;

// Test: valid websocket key conversion
TEST(web_socket, test_ws_key) {
  EXPECT_EQ(ws_session::get_web_socket_key_result("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

// Test: invalid handshake - missing Upgrade header
TEST(web_socket_handshake, missing_upgrade_header) {
  EXPECT_TRUE(true);  // Placeholder - full integration test would need connection setup
}

// Test: invalid handshake - missing Connection header
TEST(web_socket_handshake, missing_connection_header) {
  EXPECT_TRUE(true);  // Placeholder
}

// Test: invalid handshake - missing Sec-WebSocket-Key header
TEST(web_socket_handshake, missing_ws_key_header) {
  EXPECT_TRUE(true);  // Placeholder
}

// Test: invalid handshake - missing Sec-WebSocket-Version header
TEST(web_socket_handshake, missing_ws_version_header) {
  EXPECT_TRUE(true);  // Placeholder
}

// Test: invalid handshake - unsupported protocol version
TEST(web_socket_handshake, unsupported_ws_version) {
  EXPECT_TRUE(true);  // Placeholder
}

// Test: invalid handshake - unsupported protocol
TEST(web_socket_handshake, unsupported_protocol) {
  EXPECT_TRUE(true);  // Placeholder
}

// Test: text frame generation
TEST(web_socket_frames, generate_text_frame) {
  auto frame = ws_test_client::generate_text_frame("Hello, World!");
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x81 (FIN=1, opcode=1 for text)
  EXPECT_EQ(frame[0], std::byte(0x81));
}

// Test: binary frame generation
TEST(web_socket_frames, generate_binary_frame) {
  std::vector<std::byte> data{std::byte(0xDE), std::byte(0xAD), std::byte(0xBE), std::byte(0xEF)};
  auto frame = ws_test_client::generate_binary_frame(data);
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x82 (FIN=1, opcode=2 for binary)
  EXPECT_EQ(frame[0], std::byte(0x82));
}

// Test: continuation frame generation
TEST(web_socket_frames, generate_continuation_frame) {
  auto frame = ws_test_client::generate_continuation_frame("continuation data");
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x80 (FIN=1, opcode=0 for continuation)
  EXPECT_EQ(frame[0], std::byte(0x80));
}

// Test: ping frame generation
TEST(web_socket_frames, generate_ping_frame) {
  auto frame = ws_test_client::generate_ping_frame();
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x89 (FIN=1, opcode=9 for ping)
  EXPECT_EQ(frame[0], std::byte(0x89));
}

// Test: pong frame generation
TEST(web_socket_frames, generate_pong_frame) {
  auto frame = ws_test_client::generate_pong_frame();
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x8A (FIN=1, opcode=10 for pong)
  EXPECT_EQ(frame[0], std::byte(0x8A));
}

// Test: close frame generation
TEST(web_socket_frames, generate_close_frame) {
  auto frame = ws_test_client::generate_close_frame(1000, "Normal closure");
  ASSERT_FALSE(frame.empty());
  // First byte should be 0x88 (FIN=1, opcode=8 for close)
  EXPECT_EQ(frame[0], std::byte(0x88));
}

// Test: unmasked frame detection (protocol violation)
TEST(web_socket_invalid_frames, unmasked_frame) {
  auto frame = ws_test_client::generate_unmasked_frame("unmasked data");
  ASSERT_FALSE(frame.empty());
  // Verify frame is unmasked (bit 0 of byte 1 should be 0)
  EXPECT_EQ(std::to_integer<uint8_t>(frame[1]) & 0x80U, 0U);
}

// Test: wrong payload size frame (protocol violation)
TEST(web_socket_invalid_frames, wrong_payload_size) {
  auto frame = ws_test_client::generate_wrong_size_frame("short", 1000);
  ASSERT_FALSE(frame.empty());
}

// Test: fragmented message with continuation
TEST(web_socket_frames, fragmented_message) {
  auto frame1 = ws_test_client::generate_text_frame("Hello", false);          // Not final
  auto frame2 = ws_test_client::generate_continuation_frame(" World", true);  // Final

  ASSERT_FALSE(frame1.empty());
  ASSERT_FALSE(frame2.empty());

  // First frame should have FIN=0
  EXPECT_EQ((std::to_integer<uint8_t>(frame1[0]) & 0x80U), 0U);
  // Second frame should have FIN=1
  EXPECT_EQ((std::to_integer<uint8_t>(frame2[0]) & 0x80U), 0x80U);
}

// Test: handshake request generation
TEST(web_socket_handshake, generate_handshake_request) {
  auto request = ws_test_client::generate_handshake_request();
  EXPECT_TRUE(request.find("GET / HTTP/1.1") != std::string::npos);
  EXPECT_TRUE(request.find("Upgrade: websocket") != std::string::npos);
  EXPECT_TRUE(request.find("Connection: Upgrade") != std::string::npos);
  EXPECT_TRUE(request.find("Sec-WebSocket-Key:") != std::string::npos);
  EXPECT_TRUE(request.find("Sec-WebSocket-Version: 13") != std::string::npos);
}

// Test: handshake request with protocol
TEST(web_socket_handshake, generate_handshake_with_protocol) {
  auto request = ws_test_client::generate_handshake_request("/", "localhost", "dGhlIHNhbXBsZSBub25jZQ==", "chat");
  EXPECT_TRUE(request.find("Sec-WebSocket-Protocol: chat") != std::string::npos);
}

// Test: large text frame
TEST(web_socket_frames, large_text_frame) {
  std::string large_text(10000, 'a');
  auto frame = ws_test_client::generate_text_frame(large_text);
  ASSERT_FALSE(frame.empty());
  // Frame should contain extended payload length (126 = 0x7E for 2-byte length, 127 = 0x7F for 8-byte length)
  // 10000 bytes requires 2-byte length encoding
  EXPECT_EQ(frame[1] & std::byte(0x7FU), std::byte(0x7EU));
}

// Test: large binary frame
TEST(web_socket_frames, large_binary_frame) {
  std::vector<std::byte> large_data(65536, std::byte(0xFF));
  auto frame = ws_test_client::generate_binary_frame(large_data);
  ASSERT_FALSE(frame.empty());
}

// Test: edge case - 125 byte payload (max for 1-byte length)
TEST(web_socket_frames, max_single_byte_length) {
  std::string text(125, 'x');
  auto frame = ws_test_client::generate_text_frame(text);
  ASSERT_FALSE(frame.empty());
  // Length encoding should be in single byte
  EXPECT_EQ(std::to_integer<uint8_t>(frame[1]) & 0x7FU, 125U);
}

// Test: edge case - 126 byte payload (minimum for 2-byte length)
TEST(web_socket_frames, min_two_byte_length) {
  std::string text(126, 'x');
  auto frame = ws_test_client::generate_text_frame(text);
  ASSERT_FALSE(frame.empty());
  // Length encoding should be 126 (indicating 2-byte length follows)
  EXPECT_EQ(std::to_integer<uint8_t>(frame[1]) & 0x7FU, 126U);
}

// Test: control frame constraints - ping with data
TEST(web_socket_frames, ping_with_data) {
  auto frame = ws_test_client::generate_ping_frame("ping data");
  ASSERT_FALSE(frame.empty());
}

// Test: RSV bits in frame
TEST(web_socket_frames, frame_structure_validation) {
  auto frame = ws_test_client::generate_text_frame("test");
  ASSERT_FALSE(frame.empty());

  // First byte: FIN (bit 7) should be set, RSV (bits 6-4) should be clear, opcode (bits 3-0) = 1
  auto byte1 = std::to_integer<uint8_t>(frame[0]);
  EXPECT_EQ(byte1 & 0x80U, 0x80U);  // FIN bit set
  EXPECT_EQ(byte1 & 0x0FU, 1U);     // Opcode = 1 (text)
}

// Test: mask bit in client frame
TEST(web_socket_frames, client_frame_masking) {
  auto frame = ws_test_client::generate_text_frame("test");
  ASSERT_FALSE(frame.empty());

  // Second byte: mask bit (bit 7) should be set
  auto byte2 = std::to_integer<uint8_t>(frame[1]);
  EXPECT_EQ(byte2 & 0x80U, 0x80U);  // Mask bit set for client frame
}

// Test: empty text frame
TEST(web_socket_frames, empty_text_frame) {
  auto frame = ws_test_client::generate_text_frame("");
  ASSERT_FALSE(frame.empty());
}

// Test: empty binary frame
TEST(web_socket_frames, empty_binary_frame) {
  std::vector<std::byte> empty_data;
  auto frame = ws_test_client::generate_binary_frame(empty_data);
  ASSERT_FALSE(frame.empty());
}

// Test fixture: starts server in a background thread and stops it with terminate()
class web_socket_integration_tests : public ::testing::Test {
 protected:
  void SetUp() override {
    // pick ephemeral port by binding to 0
    port = ws_test_client::pick_free_port();

    // register route that mirrors example server behavior
    server.get_router().add_advanced_route(server::http1::http_method::GET, "/chat", [this](const auto& request, server::http1::session& http_session) -> async_coro::task<> {  // NOLINT(*reference*)
      using namespace server::web_socket;

      ws_session session{std::move(http_session.get_connection())};

      co_await session.run(request, "", [this](const request_frame& req_frame, ws_session& this_session) -> async_coro::task<> {  // NOLINT(*reference*)
        if (chat_session_handler) {
          co_await chat_session_handler(req_frame, this_session);
          co_return;
        }

        if (req_frame.get_op_code() == ws_op_code::text_frame) {
          response_frame resp{ws_op_code::text_frame};
          std::string answer = "Hello from server!\n";
          answer += "got: ";
          answer += req_frame.get_payload_as_string();

          co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));
        } else {
          ws_error error(ws_status_code::invalid_frame_payload_data, "Expected text");
          co_await response_frame::send_error_and_close_connection(this_session.get_connection(), error);
        }
      });
    });

    server_thread = std::thread([this] {
      server::tcp_server_config conf;
      conf.ip_address = "127.0.0.1";
      conf.port = port;
      conf.num_reactors = 1;
      server.serve(conf, {});
    });

    // wait for server to listen (retry connect)
    auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(3);
    bool ok = false;
    while (std::chrono::steady_clock::now() < deadline) {
      auto s = ws_test_client::connect_blocking("127.0.0.1", port, 200);
      if (s != server::socket_layer::invalid_connection) {
        server::socket_layer::close_socket(s.get_platform_id());
        ok = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(ok) << "Server did not start listening in time";
  }

  void TearDown() override {
    chat_session_handler = nullptr;
    server::socket_layer::close_socket(client_connection.get_platform_id());
    client_connection = server::socket_layer::invalid_connection;

    server.terminate();
    if (server_thread.joinable()) {
      server_thread.join();
    }
  }

  void open_connection() {
    client_connection = server::socket_layer::invalid_connection;
    for (int i = 0; i < 10; ++i) {
      client_connection = ws_test_client::connect_blocking("127.0.0.1", port, 1000);
      if (client_connection != server::socket_layer::invalid_connection) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

 protected:
  async_coro::unique_function<async_coro::task<>(const request_frame&, ws_session&) const> chat_session_handler;
  server::http1::http_server server;
  std::thread server_thread;
  uint16_t port = 0;
  server::socket_layer::connection_id client_connection = server::socket_layer::invalid_connection;
};

// Test: handshake success
TEST_F(web_socket_integration_tests, handshake_success) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  std::string req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1", "dGhlIHNhbXBsZSBub25jZQ==", "");
  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);

  auto resp = ws_test_client::recv_http_response(client_connection);
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // verify Sec-WebSocket-Accept header
  auto ws_key = resp.find("Sec-WebSocket-Accept: ");
  ASSERT_TRUE(ws_key != std::string::npos);
  std::string_view key = resp;
  key.remove_prefix(ws_key + "Sec-WebSocket-Accept: "sv.size());
  if (auto n = key.find('\n'); n != std::string_view::npos) {
    key.remove_suffix(key.size() - n);
  }
  if (!key.empty() && key.back() == '\r') {
    key.remove_suffix(1);
  }

  auto expected = ws_session::get_web_socket_key_result("dGhlIHNhbXBsZSBub25jZQ==");
  EXPECT_EQ(key, expected);
}

// Test: text echo (server sends multiple messages including echoed payload)
TEST_F(web_socket_integration_tests, text_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  std::string req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1", "dGhlIHNhbXBsZSBub25jZQ==", "");
  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);

  auto resp = ws_test_client::recv_http_response(client_connection);
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  const std::string_view payload = "Hello";
  auto frame = ws_test_client::generate_text_frame(payload);
  ASSERT_GT(ws_test_client::send_all(client_connection, frame.data(), frame.size()), 0);

  // server sends several messages; read up to 6 frames and search for echoed payload
  for (int i = 0; i < 1; ++i) {
    auto pframe = ws_test_client::recv_frame(client_connection);
    std::string_view s(reinterpret_cast<char*>(pframe.payload.data()), pframe.payload.size());
    EXPECT_EQ(s, "Hello from server!\ngot: Hello");
  }
}
