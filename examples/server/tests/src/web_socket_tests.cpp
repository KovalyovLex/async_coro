
#include <gtest/gtest.h>
#include <server/http1/http_server.h>
#include <server/http1/session.h>
#include <server/tcp_server_config.h>
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_decompress.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_extension_parser.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "async_coro/thread_safety/analysis.h"
#include "async_coro/thread_safety/mutex.h"
#include "async_coro/thread_safety/unique_lock.h"
#include "server/socket_layer/connection_id.h"
#include "server/utils/zlib_compression_constants.h"
#include "ws_test_client.h"

using namespace server::web_socket;

// Test: valid websocket key conversion
TEST(web_socket, test_ws_key) {
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

      std::string protocol;
      decltype(chat_session_handler) session_handler;

      {
        async_coro::unique_lock lock{mutex};
        protocol = accepted_protocols;
        session_handler = chat_session_handler;
      }

      co_await session.run(request, protocol, [session_handler = std::move(session_handler)](const request_frame& req_frame, ws_session& this_session) -> async_coro::task<> {  // NOLINT(*reference*)
        if (session_handler) {
          co_await session_handler(req_frame, this_session);
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
    server::socket_layer::close_socket(client_connection.get_platform_id());
    client_connection = server::socket_layer::invalid_connection;

    server.terminate();
    if (server_thread.joinable()) {
      server_thread.join();
    }

    async_coro::unique_lock lock{mutex};
    accepted_protocols.clear();
    chat_session_handler = nullptr;
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
  async_coro::mutex mutex;
  std::function<async_coro::task<>(const request_frame&, ws_session&)> chat_session_handler CORO_THREAD_GUARDED_BY(mutex);
  std::string accepted_protocols CORO_THREAD_GUARDED_BY(mutex);
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

// Test: invalid handshake - missing Upgrade header
TEST_F(web_socket_integration_tests, missing_upgrade_header) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake without Upgrade header
  std::string req =
      "GET /chat HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test: invalid handshake - missing Connection header
TEST_F(web_socket_integration_tests, missing_connection_header) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake without Connection header
  std::string req =
      "GET /chat HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test: invalid handshake - missing Sec-WebSocket-Key header
TEST_F(web_socket_integration_tests, missing_ws_key_header) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake without Sec-WebSocket-Key header
  std::string req =
      "GET /chat HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test: invalid handshake - missing Sec-WebSocket-Version header
TEST_F(web_socket_integration_tests, missing_ws_version_header) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake without Sec-WebSocket-Version header
  std::string req =
      "GET /chat HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test: invalid handshake - unsupported protocol version
TEST_F(web_socket_integration_tests, unsupported_ws_version) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake with unsupported version (e.g., 12 instead of 13)
  std::string req =
      "GET /chat HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 12\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test: invalid handshake - unsupported protocol
TEST_F(web_socket_integration_tests, unsupported_protocol) {
  {
    async_coro::unique_lock lock{mutex};
    accepted_protocols = "chat";
  }

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Generate handshake with unsupported subprotocol
  std::string req = ws_test_client::generate_handshake_request("/chat", "127.0.0.1", "dGhlIHNhbXBsZSBub25jZQ==", "unsupported-protocol");

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should reject with non-101 status code
  EXPECT_EQ(resp.find("101"), std::string::npos);
}

// Test fixture for permessage-deflate compression tests
class web_socket_deflate_tests : public web_socket_integration_tests {
 protected:
  void SetUp() override {
    using namespace server::web_socket;
    server.get_router().add_advanced_route(server::http1::http_method::GET, "/chat-deflate",
                                           [this](const server::http1::request& request, server::http1::session& http_session) -> async_coro::task<> {  // NOLINT(*reference*)
                                             ws_session session{std::move(http_session.get_connection())};
                                             {
                                               async_coro::unique_lock lock{mutex};
                                               session.set_permessage_deflate(deflate_config);
                                             }
                                             co_await session.run(request, "", [](const request_frame& req_frame, ws_session& this_session) -> async_coro::task<> {  // NOLINT(*reference*)
                                               if (req_frame.get_op_code() == ws_op_code::text_frame) {
                                                 response_frame resp{ws_op_code::text_frame};
                                                 std::string answer = "Deflate: ";
                                                 answer += req_frame.get_payload_as_string();
                                                 co_await this_session.send_data(resp, std::as_bytes(std::span{answer}));
                                               } else {
                                                 ws_error error(ws_status_code::invalid_frame_payload_data, "Expected text");
                                                 co_await response_frame::send_error_and_close_connection(this_session.get_connection(), error);
                                               }
                                             });
                                           });
    web_socket_integration_tests::SetUp();
  }

  void TearDown() override {
    web_socket_integration_tests::TearDown();

    decompressor = server::zlib_decompress{};
    compressor = server::zlib_compress{};
  }

  // Helper: perform handshake with extension string
  static std::string do_handshake(server::socket_layer::connection_id conn, const std::string& ext) {
    std::string handshake =
        "GET /chat-deflate HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n";
    if (!ext.empty()) {
      handshake += "Sec-WebSocket-Extensions: " + ext + "\r\n";
    }
    handshake += "\r\n";
    ws_test_client::send_all(conn, handshake.data(), handshake.size());
    return ws_test_client::recv_http_response(conn);
  }

  // Helper: compress payload using zlib (permessage-deflate)
  std::vector<std::byte> compress_payload(std::string_view payload, bool with_takeover) {
    if (!compressor) {
      compressor = server::zlib_compress{server::zlib::compression_method::deflate};
    }
    std::vector<std::byte> compressed_payload;
    compressed_payload.reserve(payload.size() * 2);
    std::span<const std::byte> data_in{reinterpret_cast<const std::byte*>(payload.data()), payload.size()};
    std::array<std::byte, 256> tmp_buffer{};

    while (!data_in.empty()) {
      std::span<std::byte> data_out = tmp_buffer;
      EXPECT_TRUE(compressor.update_stream(data_in, data_out));
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      compressed_payload.insert(compressed_payload.end(), data_to_copy.begin(), data_to_copy.end());
    }

    if (with_takeover) {
      while (true) {
        std::span<std::byte> data_out = tmp_buffer;
        bool has_more = compressor.flush(data_in, data_out);
        std::span data_to_copy{tmp_buffer.data(), data_out.data()};
        compressed_payload.insert(compressed_payload.end(), data_to_copy.begin(), data_to_copy.end());
        if (!has_more) {
          break;
        }
      }

      EXPECT_GT(compressed_payload.size(), 4);
      for (int i = 0; i < 4; i++) {
        compressed_payload.pop_back();
      }
    } else {
      while (true) {
        std::span<std::byte> data_out = tmp_buffer;
        bool has_more = compressor.end_stream(data_in, data_out);
        std::span data_to_copy{tmp_buffer.data(), data_out.data()};
        compressed_payload.insert(compressed_payload.end(), data_to_copy.begin(), data_to_copy.end());
        if (!has_more) {
          break;
        }
      }

      // optional recreate
      compressor = server::zlib_compress{};
    }

    return compressed_payload;
  }

  // Helper: create a WebSocket frame with compressed payload and RSV1 set
  static std::vector<std::byte> make_compressed_frame(const std::vector<std::byte>& compressed_payload) {
    std::vector<std::byte> frame;
    frame.push_back(std::byte(0xC1));  // FIN=1, RSV1=1, opcode=1 (text)
    auto payload_len = static_cast<size_t>(compressed_payload.size());
    if (payload_len < 126U) {
      frame.push_back(std::byte(0x80U | static_cast<uint8_t>(payload_len)));
    } else if (payload_len < 65536U) {
      frame.push_back(std::byte(0xFEU));
      frame.push_back(std::byte((payload_len >> 8U) & 0xFFU));
      frame.push_back(std::byte(payload_len & 0xFFU));
    }
    std::array<std::byte, 4> mask_key{std::byte(0x01), std::byte(0x02), std::byte(0x03), std::byte(0x04)};
    frame.insert(frame.end(), mask_key.begin(), mask_key.end());
    for (size_t i = 0; i < payload_len; ++i) {
      auto mask_idx = static_cast<size_t>(i % 4U);
      frame.push_back(compressed_payload.at(i) ^ mask_key.at(mask_idx));
    }
    return frame;
  }

  // Helper: decompress payload using zlib (permessage-deflate)
  std::string decompress_payload(const std::vector<std::byte>& payload, bool with_takeover) {
    if (!decompressor) {
      decompressor = server::zlib_decompress{server::zlib::compression_method::deflate};
    }

    std::vector<std::byte> decompressed_response;
    decompressed_response.reserve(payload.size() * 2);
    std::span<const std::byte> compressed_in{payload.data(), payload.size()};
    std::array<std::byte, 256> tmp_buffer{};

    std::vector<std::byte> compressed_with_trailer;
    if (with_takeover) {
      std::array<std::byte, 4> flush_trailer{std::byte(0x00), std::byte(0x00), std::byte(0xFF), std::byte(0xFF)};
      compressed_with_trailer.insert(compressed_with_trailer.end(), compressed_in.begin(), compressed_in.end());
      compressed_with_trailer.insert(compressed_with_trailer.end(), flush_trailer.begin(), flush_trailer.end());
      compressed_in = std::span<const std::byte>{compressed_with_trailer.data(), compressed_with_trailer.size()};
    }

    while (!compressed_in.empty()) {
      std::span<std::byte> data_out = tmp_buffer;
      if (!decompressor.update_stream(compressed_in, data_out)) {
        EXPECT_TRUE(false) << "Cant update stream";
        break;
      }
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      decompressed_response.insert(decompressed_response.end(), data_to_copy.begin(), data_to_copy.end());
    }

    if (!with_takeover) {
      std::span<const std::byte> empty_data;
      while (true) {
        std::span<std::byte> data_out = tmp_buffer;
        bool has_more = decompressor.end_stream(empty_data, data_out);
        std::span data_to_copy{tmp_buffer.data(), data_out.data()};
        decompressed_response.insert(decompressed_response.end(), data_to_copy.begin(), data_to_copy.end());
        if (!has_more) {
          break;
        }
      }

      // drop
      decompressor = server::zlib_decompress{};
    }

    return {reinterpret_cast<const char*>(decompressed_response.data()), decompressed_response.size()};
  }

  // Test: permessage-deflate text message echo with default settings
  void run_deflate_echo_test(server::socket_layer::connection_id conn, const std::string& ext, const std::vector<std::byte>& compressed_payload, const std::string& expected_response, bool with_server_takeover) {  // NOLINT(*swappable*)
    auto resp = web_socket_deflate_tests::do_handshake(conn, ext);
    ASSERT_TRUE(resp.find("101") != std::string::npos);

    auto frame = web_socket_deflate_tests::make_compressed_frame(compressed_payload);
    ASSERT_EQ(ws_test_client::send_all(conn, frame.data(), frame.size()), frame.size());
    auto pframe = ws_test_client::recv_frame(conn);
    auto response = decompress_payload(pframe.payload, with_server_takeover);
    EXPECT_EQ(response, expected_response);
  }

 protected:
  server::web_socket::permessage_deflate_config deflate_config CORO_THREAD_GUARDED_BY(mutex);
  server::zlib_decompress decompressor;
  server::zlib_compress compressor;
};

// Test: permessage-deflate with default settings
TEST_F(web_socket_deflate_tests, deflate_default_settings) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with permessage-deflate extension
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with server_no_context_takeover
TEST_F(web_socket_deflate_tests, deflate_server_no_context_takeover) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with server_no_context_takeover parameter
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header includes server_no_context_takeover
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with client_no_context_takeover
TEST_F(web_socket_deflate_tests, deflate_client_no_context_takeover) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with client_no_context_takeover parameter
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with both no_context_takeover options
TEST_F(web_socket_deflate_tests, deflate_both_no_context_takeover) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with both no_context_takeover parameters
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover; client_no_context_takeover\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with server_max_window_bits=8
TEST_F(web_socket_deflate_tests, deflate_server_window_bits_8) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with server_max_window_bits=8 (minimum window size)
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_max_window_bits=8\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with server_max_window_bits=15
TEST_F(web_socket_deflate_tests, deflate_server_window_bits_15) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with server_max_window_bits=15 (maximum window size)
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_max_window_bits=15\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with client_max_window_bits=8
TEST_F(web_socket_deflate_tests, deflate_client_window_bits_8) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with client_max_window_bits=8 (minimum window size)
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits=8\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with client_max_window_bits=15
TEST_F(web_socket_deflate_tests, deflate_client_window_bits_15) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with client_max_window_bits=15 (maximum window size)
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits=15\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

// Test: permessage-deflate with all parameters combined
TEST_F(web_socket_deflate_tests, deflate_all_parameters_combined) {
  using namespace std::string_view_literals;

  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);

  // Send handshake with all permessage-deflate parameters
  std::string req =
      "GET /chat-deflate HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: Upgrade\r\n"
      "Upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Extensions: permessage-deflate; server_max_window_bits=10; client_max_window_bits=12; server_no_context_takeover; client_no_context_takeover\r\n"
      "\r\n";

  ASSERT_GT(ws_test_client::send_all(client_connection, req.data(), req.size()), 0);
  auto resp = ws_test_client::recv_http_response(client_connection);

  // Should accept with 101 status code
  ASSERT_TRUE(resp.find("101") != std::string::npos);

  // Verify Sec-WebSocket-Extensions header is present
  EXPECT_TRUE(resp.find("Sec-WebSocket-Extensions:") != std::string::npos);
}

TEST_F(web_socket_deflate_tests, deflate_text_echo_with_compression) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  run_deflate_echo_test(client_connection,
                        "permessage-deflate",
                        compress_payload("Hello compressed world - this is a test message that should compress well", true),
                        "Deflate: Hello compressed world - this is a test message that should compress well",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_server_no_context_takeover_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_no_context_takeover",
                        compress_payload("context takeover test", true),
                        "Deflate: context takeover test",
                        false);
}

TEST_F(web_socket_deflate_tests, deflate_client_no_context_takeover_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_no_context_takeover",
                        compress_payload("client context test", false),
                        "Deflate: client context test",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_both_no_context_takeover_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_no_context_takeover; client_no_context_takeover",
                        compress_payload("both context test", false),
                        "Deflate: both context test",
                        false);
}

TEST_F(web_socket_deflate_tests, deflate_server_window_bits_8_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  decompressor = server::zlib_decompress{server::zlib::compression_method::deflate, server::zlib::window_bits{8}};
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=8",
                        compress_payload("window bits 8 test", true),
                        "Deflate: window bits 8 test",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_server_window_bits_15_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  decompressor = server::zlib_decompress{server::zlib::compression_method::deflate, server::zlib::window_bits{15}};
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=15",
                        compress_payload("window bits 15 test", true),
                        "Deflate: window bits 15 test",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_client_window_bits_8_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  compressor = server::zlib_compress{server::zlib::compression_method::deflate, server::zlib::window_bits{8}};
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_max_window_bits=8",
                        compress_payload("client window bits 8", true),
                        "Deflate: client window bits 8",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_client_window_bits_15_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  compressor = server::zlib_compress{server::zlib::compression_method::deflate, server::zlib::window_bits{15}};
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_max_window_bits=15",
                        compress_payload("client window bits 15", true),
                        "Deflate: client window bits 15",
                        true);
}

TEST_F(web_socket_deflate_tests, deflate_all_parameters_combined_echo) {
  open_connection();
  ASSERT_NE(client_connection, server::socket_layer::invalid_connection);
  decompressor = server::zlib_decompress{server::zlib::compression_method::deflate, server::zlib::window_bits{10}};
  compressor = server::zlib_compress{server::zlib::compression_method::deflate, server::zlib::window_bits{12}};
  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=10; client_max_window_bits=12; server_no_context_takeover; client_no_context_takeover",
                        compress_payload("all params test", false),
                        "Deflate: all params test",
                        false);
}
