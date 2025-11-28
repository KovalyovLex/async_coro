#include <async_coro/thread_safety/unique_lock.h>
#include <gtest/gtest.h>
#include <server/http1/session.h>
#include <server/web_socket/response_frame.h>

#include <string_view>

#include "fixtures/web_socket_integration_fixture.h"
#include "fixtures/ws_test_client.h"
#include "server/utils/zlib_compress.h"
#include "server/utils/zlib_compression_constants.h"

// Test fixture for permessage-deflate compression tests
class web_socket_deflate_tests : public web_socket_integration_tests {
 protected:
  struct compressor_config {
    server::zlib::window_bits server_window{};
    server::zlib::window_bits client_window{};

    bool server_no_takeover = false;
    bool client_no_takeover = false;
  };

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
  static std::string do_handshake(server::socket_layer::connection_id conn, std::string_view ext) {
    std::string handshake =
        "GET /chat-deflate HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n";
    if (!ext.empty()) {
      handshake += "Sec-WebSocket-Extensions: ";
      handshake += ext;
      handshake += "\r\n";
    }
    handshake += "\r\n";
    ws_test_client::send_all(conn, handshake.data(), handshake.size());
    return ws_test_client::recv_http_response(conn);
  }

  // Helper: compress payload using zlib (permessage-deflate)
  std::vector<std::byte> compress_payload(std::string_view payload, const compressor_config& conf) {
    if (!compressor) {
      compressor = server::zlib_compress{server::zlib::compression_config{.method = server::zlib::compression_method::deflate, .window_bits = conf.client_window}};
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

    if (!conf.client_no_takeover) {
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
      if (compressed_payload.size() < 4) {
        return compressed_payload;
      }

      // check flush tail
      EXPECT_EQ(compressed_payload[compressed_payload.size() - 4], std::byte(0x00U));
      EXPECT_EQ(compressed_payload[compressed_payload.size() - 3], std::byte(0x00U));
      EXPECT_EQ(compressed_payload[compressed_payload.size() - 2], std::byte(0xFFU));
      EXPECT_EQ(compressed_payload[compressed_payload.size() - 1], std::byte(0xFFU));

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
  std::string decompress_payload(const std::vector<std::byte>& payload, const compressor_config& conf) {
    if (!decompressor) {
      decompressor = server::zlib_decompress{server::zlib::decompression_config{.method = server::zlib::compression_method::deflate, .window_bits = conf.server_window}};
    }

    std::vector<std::byte> decompressed_response;
    decompressed_response.reserve(payload.size() * 2);
    std::span<const std::byte> compressed_in{payload.data(), payload.size()};
    std::array<std::byte, 256> tmp_buffer{};

    std::vector<std::byte> compressed_with_trailer;
    if (!conf.server_no_takeover) {
      std::array<std::byte, 4> flush_trailer{std::byte(0x00), std::byte(0x00), std::byte(0xFF), std::byte(0xFF)};
      compressed_with_trailer.insert(compressed_with_trailer.end(), compressed_in.begin(), compressed_in.end());
      compressed_with_trailer.insert(compressed_with_trailer.end(), flush_trailer.begin(), flush_trailer.end());
      compressed_in = std::span<const std::byte>{compressed_with_trailer.data(), compressed_with_trailer.size()};
    }

    while (!compressed_in.empty()) {
      std::span<std::byte> data_out = tmp_buffer;
      if (!decompressor.update_stream(compressed_in, data_out)) {
        EXPECT_TRUE(false) << "Can't update stream";
        break;
      }
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      decompressed_response.insert(decompressed_response.end(), data_to_copy.begin(), data_to_copy.end());
    }

    if (conf.server_no_takeover) {
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
  void run_deflate_echo_test(server::socket_layer::connection_id conn, std::string_view ext, std::string_view payload_msg, std::string_view expected_response, const compressor_config& conf) {  // NOLINT(*swappable*)
    auto resp = web_socket_deflate_tests::do_handshake(conn, ext);
    ASSERT_TRUE(resp.find("101") != std::string::npos);

    auto compressed_payload = compress_payload(payload_msg, conf);
    auto frame = web_socket_deflate_tests::make_compressed_frame(compressed_payload);
    ASSERT_EQ(ws_test_client::send_all(conn, frame.data(), frame.size()), frame.size());

    auto pframe = ws_test_client::recv_frame(conn);
    auto response = decompress_payload(pframe.payload, conf);
    EXPECT_EQ(response, expected_response);

    // 2nd turn to check takeover
    compressed_payload = compress_payload(payload_msg, conf);
    frame = web_socket_deflate_tests::make_compressed_frame(compressed_payload);
    ASSERT_EQ(ws_test_client::send_all(conn, frame.data(), frame.size()), frame.size());

    pframe = ws_test_client::recv_frame(conn);
    response = decompress_payload(pframe.payload, conf);
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

  compressor_config conf{};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate",
                        "Hello compressed world - this is a test message that should compress well",
                        "Deflate: Hello compressed world - this is a test message that should compress well",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_server_no_context_takeover_echo) {
  open_connection();

  compressor_config conf{.server_no_takeover = true};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_no_context_takeover",
                        "context takeover test",
                        "Deflate: context takeover test",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_client_no_context_takeover_echo) {
  open_connection();

  compressor_config conf{.client_no_takeover = true};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_no_context_takeover",
                        "client context test",
                        "Deflate: client context test",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_both_no_context_takeover_echo) {
  open_connection();

  compressor_config conf{.server_no_takeover = true, .client_no_takeover = true};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_no_context_takeover; client_no_context_takeover",
                        "both context test",
                        "Deflate: both context test",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_server_window_bits_8_echo) {
  open_connection();

  compressor_config conf{.server_window = server::zlib::window_bits{8}};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=8",
                        "window bits 8 test",
                        "Deflate: window bits 8 test",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_server_window_bits_15_echo) {
  open_connection();

  compressor_config conf{.server_window = server::zlib::window_bits{15}};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=15",
                        "window bits 15 test",
                        "Deflate: window bits 15 test",
                        conf);
}

// Current version of zlib has no support for 8 window bits size
TEST_F(web_socket_deflate_tests, deflate_client_window_bits_9_echo) {
  open_connection();

  compressor_config conf{.client_window = server::zlib::window_bits{9}};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_max_window_bits=9",
                        "client window bits 9",
                        "Deflate: client window bits 9",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_client_window_bits_15_echo) {
  open_connection();

  compressor_config conf{.client_window = server::zlib::window_bits{15}};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; client_max_window_bits=15",
                        "client window bits 15",
                        "Deflate: client window bits 15",
                        conf);
}

TEST_F(web_socket_deflate_tests, deflate_all_parameters_combined_echo) {
  open_connection();

  compressor_config conf{.server_window = server::zlib::window_bits{10}, .client_window = server::zlib::window_bits{12}, .server_no_takeover = true, .client_no_takeover = true};

  run_deflate_echo_test(client_connection,
                        "permessage-deflate; server_max_window_bits=10; client_max_window_bits=12; server_no_context_takeover; client_no_context_takeover",
                        "all params test",
                        "Deflate: all params test",
                        conf);
}
