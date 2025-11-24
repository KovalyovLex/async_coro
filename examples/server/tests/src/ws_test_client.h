#pragma once

#include <server/socket_layer/connection_id.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

struct ws_parsed_frame {
  uint8_t opcode;
  bool fin;
  std::vector<std::byte> payload;
};

// Helper class to generate WebSocket frames for testing
class ws_test_client {
 public:
  // Generate text frame
  static std::vector<std::byte> generate_text_frame(std::string_view text, bool final = true) {
    return generate_frame(0x1, text, final);
  }

  // Generate binary frame
  static std::vector<std::byte> generate_binary_frame(std::span<const std::byte> data, bool final = true) {
    return generate_frame(0x2, data, final);
  }

  // Generate continuation frame
  static std::vector<std::byte> generate_continuation_frame(std::string_view data, bool final = true) {
    return generate_frame(0x0, data, final);
  }

  // Generate ping frame
  static std::vector<std::byte> generate_ping_frame(std::string_view data = "") {
    return generate_frame(0x9, data, true);
  }

  // Generate pong frame
  static std::vector<std::byte> generate_pong_frame(std::string_view data = "") {
    return generate_frame(0xA, data, true);
  }

  // Generate close frame
  static std::vector<std::byte> generate_close_frame(uint16_t code = 1000, std::string_view reason = "");

  // Generate frame with no masking (invalid client frame, for testing protocol violations)
  static std::vector<std::byte> generate_unmasked_frame(std::string_view text);

  // Generate frame with wrong payload size (testing protocol violations)
  static std::vector<std::byte> generate_wrong_size_frame(std::string_view text, uint16_t declared_size);

  static server::socket_layer::connection_id connect_blocking(const std::string& host, uint16_t port, int timeout_ms);

  static ssize_t send_all(server::socket_layer::connection_id sock, const void* data, size_t len);

  static std::string recv_http_response(server::socket_layer::connection_id sock);

  static std::string generate_handshake_request(const std::string& path = "/",
                                                const std::string& host = "127.0.0.1",
                                                const std::string& key = "",
                                                const std::string& protocol = "");

  static ws_parsed_frame recv_frame(server::socket_layer::connection_id sock);

  static uint16_t pick_free_port();

 private:
  static std::vector<std::byte> generate_frame(uint8_t opcode, std::string_view text, bool final);

  static std::vector<std::byte> generate_frame(uint8_t opcode, std::span<const std::byte> data, bool final);
};
