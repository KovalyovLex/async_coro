#pragma once

#include <cstdint>

namespace server::brotli {

enum class compression_quality : uint8_t {
  fastest = 0,
  default_quality = 4,
  best = 11
};

struct compression_level {
  compression_level() noexcept = default;
  explicit compression_level(uint8_t val) noexcept : value(val) {}
  explicit compression_level(compression_quality val) noexcept : value(uint8_t(val)) {}

  uint8_t value = uint8_t(compression_quality::default_quality);
};

struct window_bits {
  uint8_t value = 22;  // NOLINT(*magic*) - default window size (4MB)
};

struct lgblock {
  uint8_t value = 0;  // NOLINT(*magic*) - 0 means automatic
};

}  // namespace server::brotli
