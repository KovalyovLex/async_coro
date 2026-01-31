#pragma once

#include <compare>
#include <cstdint>

namespace server::zlib {

enum class compression_method : uint8_t {
  deflate,
  gzip
};

struct compression_level {
  int8_t value = -1;

  auto operator<=>(const compression_level& other) const noexcept = default;
};

struct window_bits {
  uint8_t value = 15;  // NOLINT(*magic*)

  auto operator<=>(const window_bits& other) const noexcept = default;
};

struct memory_level {
  uint8_t value = 8;  // NOLINT(*magic*)

  auto operator<=>(const memory_level& other) const noexcept = default;
};

class compression_config {
 public:
  compression_method method = compression_method::deflate;
  window_bits window = {};
  compression_level compression = {};
  memory_level memory = {};
};

static constexpr auto k_default_gzip_compression = compression_config{.method = zlib::compression_method::gzip};
static constexpr auto k_default_deflate_compression = compression_config{.method = zlib::compression_method::deflate};

class decompression_config {
 public:
  compression_method method = compression_method::deflate;
  window_bits window = {};
};

static constexpr auto k_default_gzip_decompression = decompression_config{.method = zlib::compression_method::gzip};
static constexpr auto k_default_deflate_decompression = decompression_config{.method = zlib::compression_method::deflate};

}  // namespace server::zlib
