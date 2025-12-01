#pragma once

#include <server/utils/compression_pool.h>

#include <optional>
#include <span>
#include <string_view>

namespace server {

// Represents a single Accept-Encoding entry with quality factor
struct encoding_preference {
  compression_encoding encoding = compression_encoding::none;
  float quality = 1.0F;  // Quality factor (0.0 - 1.0)
};

// Handles Content-Encoding negotiation between client and server
class compression_negotiator {
 public:
  compression_negotiator() noexcept = default;
  explicit compression_negotiator(compression_pool::ptr pool) noexcept;

  // Parse Accept-Encoding header and determine the best encoding
  // Returns the selected encoding or compression_encoding::none if no match or header empty
  [[nodiscard]] compression_encoding negotiate(std::span<const encoding_preference> preferences) const noexcept;

  // Get the priority of an encoding (higher = more preferred by server). Zero means no support
  [[nodiscard]] size_t get_encoding_priority(compression_encoding enc) const noexcept;

  // Get client preferences from Accept-Encoding header
  // Parses and add encoding preference to vector
  static void parse_accept_encoding(std::string_view header, std::pmr::vector<encoding_preference>& preferences) noexcept;

 private:
  // Parse individual encoding preference token (e.g., "gzip;q=0.8")
  [[nodiscard]] static std::optional<encoding_preference> parse_token(std::string_view token) noexcept;

  // Convert encoding string to compression_encoding enum
  [[nodiscard]] static std::optional<compression_encoding> string_to_encoding(std::string_view str) noexcept;

 private:
  compression_pool::ptr _pool;
};

}  // namespace server
