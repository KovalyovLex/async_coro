#include "ws_test_client.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "fixtures/http_test_client.h"

namespace {

void on_error(const std::string& error_str) {
  GTEST_FAIL() << error_str;
#if ASYNC_CORO_WITH_EXCEPTIONS
  throw std::runtime_error(error_str);
#endif
}

void add_length_and_mask(std::vector<std::byte>& frame, size_t payload_len, std::array<std::byte, 4>& mask) {
  // Second byte: mask bit + payload length
  if (payload_len <= 125) {
    frame.push_back(std::byte(static_cast<unsigned char>(0x80U | (payload_len & 0x7FU))));  // Mask bit + length
  } else if (payload_len <= 0xFFFF) {
    frame.push_back(std::byte(0xFE));  // Mask bit + 126 (2-byte length follows)
    frame.push_back(std::byte(static_cast<unsigned char>((payload_len >> 8U) & 0xFFU)));
    frame.push_back(std::byte(static_cast<unsigned char>(payload_len & 0xFFU)));
  } else {
    frame.push_back(std::byte(0xFF));  // Mask bit + 127 (8-byte length follows)
    for (int i = 7; i >= 0; --i) {
      frame.push_back(std::byte(static_cast<unsigned char>((payload_len >> (i * 8U)) & 0xFFU)));
    }
  }

  // Generate 4-byte random mask
  for (auto& b : mask) {
    b = std::byte(static_cast<unsigned char>(std::rand() & 0xFFU));  // NOLINT(*array-index*, *signed*)
  }

  // Add 4-byte random mask
  for (auto b : mask) {
    frame.push_back(b);
  }
}

std::vector<std::byte> generate_frame_impl(std::vector<std::byte>& frame, uint8_t opcode, std::span<const std::byte> payload,
                                           bool final) {
  // First byte: FIN + RSV + opcode
  auto byte1 = static_cast<unsigned char>(opcode & 0x0FU);
  if (final) {
    byte1 = static_cast<unsigned char>(byte1 | 0x80U);  // Set FIN bit
  }
  frame.push_back(std::byte(byte1));

  // Add length and mask
  std::array<std::byte, 4> mask{};
  add_length_and_mask(frame, payload.size(), mask);

  for (size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(payload[i] ^ mask[i % 4]);  // NOLINT(*array-index*)
  }

  return frame;
}

}  // namespace

std::string ws_test_client::generate_handshake_request(const http_test_client& client, const std::string& path, const std::string& key, const std::string& protocol) {  // NOLINT(*swappable*)
  std::string sec_key = key;
  if (sec_key.empty()) {
    sec_key = "dGhlIHNhbXBsZSBub25jZQ==";  // default sample
  }

  std::string req = client.generate_req_head(server::http1::http_method::GET, path);
  req += "Upgrade: websocket\r\n";
  req += "Connection: Upgrade\r\n";
  req += "Sec-WebSocket-Key: ";
  req += sec_key;
  req += "\r\n";
  req += "Sec-WebSocket-Version: 13\r\n";
  if (!protocol.empty()) {
    req += "Sec-WebSocket-Protocol: ";
    req += protocol;
    req += "\r\n";
  }
  req += "\r\n";
  return req;
}

ws_parsed_frame ws_test_client::recv_frame(http_test_client& client) {
  ws_parsed_frame res{};
  std::array<uint8_t, 2> header{};

  auto bytes = std::as_writable_bytes(std::span<uint8_t>{header});
  if (!client.recv_bytes(bytes)) {
    on_error("recv failed or connection closed");
  }

  res.fin = (header[0] & 0x80U) != 0;
  res.opcode = header[0] & 0x0FU;

  bool mask = (header[1] & 0x80U) != 0;
  uint64_t len = static_cast<uint8_t>(header[1] & 0x7FU);

  if (len == 126) {
    std::array<uint8_t, 2> ext{};
    bytes = std::as_writable_bytes(std::span<uint8_t>{ext});
    if (!client.recv_bytes(bytes)) {
      on_error("recv failed");
    }
    len = (static_cast<uint64_t>(ext[0]) << 8U) | static_cast<uint64_t>(ext[1]);
  } else if (len == 127) {
    std::array<uint8_t, 8> ext{};
    bytes = std::as_writable_bytes(std::span<uint8_t>{ext});
    if (!client.recv_bytes(bytes)) {
      on_error("recv failed");
    }
    len = 0;
    for (int i = 0; i < 8; ++i) {
      len = (len << 8U) | ext[i];  // NOLINT(*array-index*)
    }
  }

  std::array<uint8_t, 4> mask_key = {};
  if (mask) {
    bytes = std::as_writable_bytes(std::span<uint8_t>{mask_key});
    if (!client.recv_bytes(bytes)) {
      on_error("recv failed");
    }
  }

  res.payload.resize(len);
  if (len > 0) {
    bytes = std::as_writable_bytes(std::span{res.payload.data(), len});
    if (!client.recv_bytes(bytes)) {
      on_error("recv failed");
    }
  }

  if (mask) {
    for (size_t i = 0; i < len; ++i) {
      reinterpret_cast<uint8_t*>(res.payload.data())[i] ^= mask_key[i % 4];  // NOLINT(*array-index*)
    }
  }

  return res;
}
std::vector<std::byte> ws_test_client::generate_close_frame(uint16_t code, std::string_view reason) {
  std::vector<std::byte> frame;

  // FIN + opcode (close = 0x8)
  frame.push_back(std::byte(0x88));

  // Payload length + mask
  size_t payload_len = 2 + reason.size();  // 2 bytes for code
  std::array<std::byte, 4> mask{};
  add_length_and_mask(frame, payload_len, mask);

  // Add code (big endian)
  frame.push_back(std::byte(static_cast<unsigned char>((code >> 8U) & 0xFFU)));  // NOLINT(*signed-bitwise)
  frame.push_back(std::byte(static_cast<unsigned char>(code & 0xFFU)));          // NOLINT(*signed-bitwise)

  // Add reason
  for (size_t i = 0; i < reason.size(); ++i) {
    frame.push_back(std::byte(reason[i]) ^ mask[i % 4]);  // NOLINT(*array-index*)
  }

  return frame;
}

std::vector<std::byte> ws_test_client::generate_unmasked_frame(std::string_view text) {
  std::vector<std::byte> frame;

  // FIN + opcode (text = 0x1)
  frame.push_back(std::byte(0x81));

  // Add length without mask
  size_t len = text.size();
  if (len <= 125) {
    frame.push_back(std::byte(static_cast<unsigned char>(len & 0x7FU)));  // No mask bit  // NOLINT(*signed-bitwise)
  } else if (len <= 0xFFFF) {
    frame.push_back(std::byte(0x7E));                                             // 2-byte length, no mask
    frame.push_back(std::byte(static_cast<unsigned char>((len >> 8U) & 0xFFU)));  // NOLINT(*signed-bitwise)
    frame.push_back(std::byte(static_cast<unsigned char>(len & 0xFFU)));          // NOLINT(*signed-bitwise)
  }

  // Add text without masking
  for (char c : text) {
    frame.push_back(std::byte(c));
  }

  return frame;
}
std::vector<std::byte> ws_test_client::generate_wrong_size_frame(std::string_view text, uint16_t declared_size) {
  std::vector<std::byte> frame;

  // FIN + opcode (text = 0x1)
  frame.push_back(std::byte(0x81));

  // Add declared length with mask
  frame.push_back(std::byte(0xFE));                                                       // 2-byte length, with mask
  frame.push_back(std::byte(static_cast<unsigned char>((declared_size >> 8U) & 0xFFU)));  // NOLINT(*signed-bitwise)
  frame.push_back(std::byte(static_cast<unsigned char>(declared_size & 0xFFU)));          // NOLINT(*signed-bitwise)

  // Add mask
  std::array<std::byte, 4> mask{std::byte(0), std::byte(0), std::byte(0), std::byte(0)};
  frame.insert(frame.end(), mask.begin(), mask.end());

  // Add actual text (which is shorter/longer than declared)
  for (char c : text) {
    frame.push_back(std::byte(c));
  }

  return frame;
}

std::vector<std::byte> ws_test_client::generate_frame(uint8_t opcode, std::string_view text, bool final) {
  std::vector<std::byte> frame_bytes;
  return generate_frame_impl(frame_bytes, opcode, std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()),  // NOLINT(*reinterpret-cast)
                                                                             text.size()},
                             final);
}

std::vector<std::byte> ws_test_client::generate_frame(uint8_t opcode, std::span<const std::byte> data, bool final) {
  std::vector<std::byte> frame_bytes;
  return generate_frame_impl(frame_bytes, opcode, data, final);
}
