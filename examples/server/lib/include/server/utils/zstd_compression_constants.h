#pragma once

#include <cstdint>

namespace server::zstd {

struct compression_level {
  int8_t value = 3;  // NOLINT(*magic*) - default compression level
};

struct window_log {
  uint8_t value = 0;  // NOLINT(*magic*) - 0 means automatic
};

}  // namespace server::zstd
