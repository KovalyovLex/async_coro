
#include <gtest/gtest.h>
#include <server/http1/http_server.h>
#include <server/http1/session.h>
#include <server/tcp_server_config.h>
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_decompress.h>
#include <server/web_socket/ws_session.h>

#include <string>
#include <string_view>
#include <vector>

#include "fixtures/ws_test_client.h"

// Test: valid websocket key conversion
TEST(web_socket, test_ws_key) {
  using namespace server::web_socket;
  EXPECT_EQ(ws_session::get_web_socket_key_result("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
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
  using namespace std::string_view_literals;

  auto frame = ws_test_client::generate_ping_frame("ping data");
  ASSERT_FALSE(frame.empty());
  EXPECT_EQ(frame.size(), 2 + 4 + "ping data"sv.size());  // masking key
}

// Test: RSV bits in frame
TEST(web_socket_frames, frame_structure_validation) {
  using namespace std::string_view_literals;

  auto frame = ws_test_client::generate_text_frame("test1");
  ASSERT_FALSE(frame.empty());

  // First byte: FIN (bit 7) should be set, RSV (bits 6-4) should be clear, opcode (bits 3-0) = 1
  auto byte1 = std::to_integer<uint8_t>(frame[0]);
  EXPECT_EQ(byte1 & 0x80U, 0x80U);  // FIN bit set
  EXPECT_EQ(byte1 & 0x0FU, 1U);     // Opcode = 1 (text)

  EXPECT_EQ(frame.size(), 6 + "test1"sv.size());
}

// Test: mask bit in client frame
TEST(web_socket_frames, client_frame_masking) {
  using namespace std::string_view_literals;

  auto frame = ws_test_client::generate_text_frame("test2");
  ASSERT_FALSE(frame.empty());

  // Second byte: mask bit (bit 7) should be set
  auto byte2 = std::to_integer<uint8_t>(frame[1]);
  EXPECT_EQ(byte2 & 0x80U, 0x80U);  // Mask bit set for client frame

  EXPECT_EQ(frame.size(), 6 + "test2"sv.size());
}

// Test: empty text frame
TEST(web_socket_frames, empty_text_frame) {
  auto frame = ws_test_client::generate_text_frame("");
  EXPECT_EQ(frame.size(), 2 + 4);
}

// Test: empty binary frame
TEST(web_socket_frames, empty_binary_frame) {
  std::vector<std::byte> empty_data;
  auto frame = ws_test_client::generate_binary_frame(empty_data);
  EXPECT_EQ(frame.size(), 2 + 4);
}
