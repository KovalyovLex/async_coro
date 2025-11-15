#pragma once

#include <cstdint>

namespace server::zlib {

enum class compression_method : uint8_t {
  deflate,
  gzip
};

struct compression_level {
  int8_t value = -1;
};

struct window_bits {
  uint8_t value = 15;  // NOLINT(*magic*)
};

struct memory_level {
  uint8_t value = 8;  // NOLINT(*magic*)
};

}  // namespace server::zlib
