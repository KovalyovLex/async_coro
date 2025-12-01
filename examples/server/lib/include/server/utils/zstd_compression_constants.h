#pragma once

#include <compare>
#include <cstdint>

namespace server::zstd {

struct compression_level {
  int8_t value = 3;  // NOLINT(*magic*) - default compression level

  auto operator<=>(const compression_level& other) const noexcept = default;
};

struct window_log {
  uint8_t value = 0;  // NOLINT(*magic*) - 0 means automatic

  auto operator<=>(const window_log& other) const noexcept = default;
};

class compression_config {
 public:
  compression_level compression_level = {};
  window_log window_log = {};
};

class decompression_config {
 public:
  window_log window_log = {};
};

}  // namespace server::zstd
