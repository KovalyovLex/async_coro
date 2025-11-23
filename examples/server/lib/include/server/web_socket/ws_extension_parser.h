#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace server::web_socket {

// Default window bits for permessage-deflate (maximum compression window)
constexpr uint8_t k_default_window_bits = 15U;  // NOLINT(*magic-numbers*)

// Represents the configuration for permessage-deflate compression extension
struct permessage_deflate_config {
  // Server's maximum window bits (for decompressing client messages)
  // Valid range: 8-15, default: 15
  uint8_t server_max_window_bits = k_default_window_bits;

  // Client's maximum window bits (for compressing server messages)
  // Valid range: 8-15, default: 15
  uint8_t client_max_window_bits = k_default_window_bits;

  // Whether server context takeover is negotiated (server reuses compression state across messages)
  bool server_no_context_takeover = false;

  // Whether client context takeover is negotiated (client reuses compression state across messages)
  bool client_no_context_takeover = false;

  [[nodiscard]] std::string get_extension_string() const;

  // retuns empty value if cliend config cant be used by server required config
  static std::optional<permessage_deflate_config> get_negotiated_config(permessage_deflate_config server, permessage_deflate_config client);
};

// Parses Sec-WebSocket-Extensions header to extract permessage-deflate configuration
// Returns the configuration if permessage-deflate is present and valid, std::nullopt otherwise
std::optional<permessage_deflate_config> parse_permessage_deflate_extension(std::string_view extensions_header);

}  // namespace server::web_socket
