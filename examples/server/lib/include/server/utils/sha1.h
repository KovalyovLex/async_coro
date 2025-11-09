#pragma once

#include <async_coro/config.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace server {

// NOLINTBEGIN(*array*, *magic*, *identifier-length*)

class sha1_hash {
  using sha1_block_t = std::array<uint32_t, 16>;
  static constexpr size_t k_block_size_bytes = sha1_block_t{}.size() * sizeof(sha1_block_t::value_type);
  using digest_t = std::array<uint32_t, 5>;
  using bytes_buffer_t = std::array<unsigned char, k_block_size_bytes>;

  static constexpr auto k_hex_len = sizeof(uint32_t) << 1U;

 public:
  // Buffer for hex representation of sha1 hash
  using sha1_str_buffer_t = std::array<char, k_hex_len * digest_t{}.size()>;

  constexpr sha1_hash() noexcept = default;
  explicit constexpr sha1_hash(std::string_view str) noexcept {
    update(str);
  }

  // Adds data to the hasher. Should not be called after get_value\get_value_str
  constexpr void update(std::string_view str) noexcept {
    add_data(std::span{str.data(), str.size()});
  }

  // Adds data to the hasher. Should not be called after get_value\get_value_str
  void update(std::span<const std::byte> bytes) noexcept {
    add_data(std::span<const char>{reinterpret_cast<const char *>(bytes.data()), bytes.size()});  // NOLINT(*reinterpret-cast)
  }

  // Fills result string buffer with hex string
  constexpr void get_value(sha1_str_buffer_t &buffer_str) noexcept {
    if (!_finalized) [[likely]] {
      finalize();
    }

    uint32_t start_index = 0;
    for (auto val : _digest) {
      const auto bytes = std::span<char, k_hex_len>(&buffer_str[start_index], k_hex_len);
      uint32_to_hex(bytes, val);
      start_index += k_hex_len;
    }
  }

  constexpr sha1_str_buffer_t get_value() noexcept {
    sha1_str_buffer_t buf;
    get_value(buf);
    return buf;
  }

  // Returns hex string
  std::string get_value_str() {
    if (!_finalized) [[likely]] {
      finalize();
    }

    std::string str(sha1_str_buffer_t{}.size(), '\0');

    uint32_t start_index = 0;
    for (auto val : _digest) {
      const auto bytes = std::span<char, k_hex_len>(&str[start_index], k_hex_len);
      uint32_to_hex(bytes, val);
      start_index += k_hex_len;
    }

    return str;
  }

 private:
  constexpr void append_bytes(const char *val, uint32_t size) noexcept {
    if (std::is_constant_evaluated()) {
      while (size != 0) {
        const uint32_t int_byte = (_buffer_length_bytes % sizeof(uint32_t));
        const uint32_t val32(uint32_t(*val++) & 0xFFU);  // NOLINT(*pointer*)

        auto &int_val = _block[_buffer_length_bytes / sizeof(uint32_t)];
        int_val |= val32 << 8 * int_byte;

        _buffer_length_bytes++;
        size--;
      }
    } else {
      auto *ptr = reinterpret_cast<char *>(_block.data()) + _buffer_length_bytes;  // NOLINT(*pointer*, *reinterpret-cast)
      std::memcpy(ptr, val, size);
      _buffer_length_bytes += size;
    }
  }

  constexpr void add_data(std::span<const char> bytes) noexcept {
    ASYNC_CORO_ASSERT(!_finalized);

    size_t n_remaining_bytes = bytes.size();
    const auto *bytes_ptr = bytes.data();

    while (true) {
      const auto n_copy = std::min(n_remaining_bytes, k_block_size_bytes - _buffer_length_bytes);
      append_bytes(bytes_ptr, n_copy);

      bytes_ptr += n_copy;  // NOLINT(*pointer*)
      n_remaining_bytes -= n_copy;

      if (_buffer_length_bytes != k_block_size_bytes) {
        return;
      }

      change_byte_order();
      transform();
      _block = {};
      _buffer_length_bytes = 0;
    }
  }

  constexpr void finalize() noexcept {
    constexpr auto padding = char(0x80);

    /* Total number of hashed bits */
    uint64_t total_bits = (_transforms * k_block_size_bytes + _buffer_length_bytes) * 8;

    /* Padding */
    append_bytes(&padding, 1);
    /* The rest of the buffer should remain zero */

    change_byte_order();

    if (_buffer_length_bytes > k_block_size_bytes - 8) {
      transform();
      for (size_t i = 0; i < _block.size() - 2; i++) {
        _block[i] = 0;
      }
    }

    /* Append total_bits, split this uint64_t into two uint32_t */
    _block[_block.size() - 1] = (uint32_t)total_bits;
    _block[_block.size() - 2] = (uint32_t)(total_bits >> 32U);
    _buffer_length_bytes = k_block_size_bytes;
    transform();

    _finalized = true;
  }

  constexpr void change_byte_order() noexcept {
    if constexpr (std::endian::native == std::endian::big) {
      // no need to transform
    } else {
      for (auto &val : _block) {
        uint32_t y = (val << 24U) | ((val << 8U) & 0xff0000U) | ((val >> 8U) & 0xff00U) | (val >> 24U);
        val = y;
      }
    }
  }

  constexpr void transform() noexcept {
    /* Copy digest[] to working vars */
    uint32_t a = _digest[0];
    uint32_t b = _digest[1];
    uint32_t c = _digest[2];
    uint32_t d = _digest[3];
    uint32_t e = _digest[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(_block, a, b, c, d, e, 0);
    R0(_block, e, a, b, c, d, 1);
    R0(_block, d, e, a, b, c, 2);
    R0(_block, c, d, e, a, b, 3);
    R0(_block, b, c, d, e, a, 4);
    R0(_block, a, b, c, d, e, 5);
    R0(_block, e, a, b, c, d, 6);
    R0(_block, d, e, a, b, c, 7);
    R0(_block, c, d, e, a, b, 8);
    R0(_block, b, c, d, e, a, 9);
    R0(_block, a, b, c, d, e, 10);
    R0(_block, e, a, b, c, d, 11);
    R0(_block, d, e, a, b, c, 12);
    R0(_block, c, d, e, a, b, 13);
    R0(_block, b, c, d, e, a, 14);
    R0(_block, a, b, c, d, e, 15);
    R1(_block, e, a, b, c, d, 0);
    R1(_block, d, e, a, b, c, 1);
    R1(_block, c, d, e, a, b, 2);
    R1(_block, b, c, d, e, a, 3);
    R2(_block, a, b, c, d, e, 4);
    R2(_block, e, a, b, c, d, 5);
    R2(_block, d, e, a, b, c, 6);
    R2(_block, c, d, e, a, b, 7);
    R2(_block, b, c, d, e, a, 8);
    R2(_block, a, b, c, d, e, 9);
    R2(_block, e, a, b, c, d, 10);
    R2(_block, d, e, a, b, c, 11);
    R2(_block, c, d, e, a, b, 12);
    R2(_block, b, c, d, e, a, 13);
    R2(_block, a, b, c, d, e, 14);
    R2(_block, e, a, b, c, d, 15);
    R2(_block, d, e, a, b, c, 0);
    R2(_block, c, d, e, a, b, 1);
    R2(_block, b, c, d, e, a, 2);
    R2(_block, a, b, c, d, e, 3);
    R2(_block, e, a, b, c, d, 4);
    R2(_block, d, e, a, b, c, 5);
    R2(_block, c, d, e, a, b, 6);
    R2(_block, b, c, d, e, a, 7);
    R3(_block, a, b, c, d, e, 8);
    R3(_block, e, a, b, c, d, 9);
    R3(_block, d, e, a, b, c, 10);
    R3(_block, c, d, e, a, b, 11);
    R3(_block, b, c, d, e, a, 12);
    R3(_block, a, b, c, d, e, 13);
    R3(_block, e, a, b, c, d, 14);
    R3(_block, d, e, a, b, c, 15);
    R3(_block, c, d, e, a, b, 0);
    R3(_block, b, c, d, e, a, 1);
    R3(_block, a, b, c, d, e, 2);
    R3(_block, e, a, b, c, d, 3);
    R3(_block, d, e, a, b, c, 4);
    R3(_block, c, d, e, a, b, 5);
    R3(_block, b, c, d, e, a, 6);
    R3(_block, a, b, c, d, e, 7);
    R3(_block, e, a, b, c, d, 8);
    R3(_block, d, e, a, b, c, 9);
    R3(_block, c, d, e, a, b, 10);
    R3(_block, b, c, d, e, a, 11);
    R4(_block, a, b, c, d, e, 12);
    R4(_block, e, a, b, c, d, 13);
    R4(_block, d, e, a, b, c, 14);
    R4(_block, c, d, e, a, b, 15);
    R4(_block, b, c, d, e, a, 0);
    R4(_block, a, b, c, d, e, 1);
    R4(_block, e, a, b, c, d, 2);
    R4(_block, d, e, a, b, c, 3);
    R4(_block, c, d, e, a, b, 4);
    R4(_block, b, c, d, e, a, 5);
    R4(_block, a, b, c, d, e, 6);
    R4(_block, e, a, b, c, d, 7);
    R4(_block, d, e, a, b, c, 8);
    R4(_block, c, d, e, a, b, 9);
    R4(_block, b, c, d, e, a, 10);
    R4(_block, a, b, c, d, e, 11);
    R4(_block, e, a, b, c, d, 12);
    R4(_block, d, e, a, b, c, 13);
    R4(_block, c, d, e, a, b, 14);
    R4(_block, b, c, d, e, a, 15);

    /* Add the working vars back into digest[] */
    _digest[0] += a;
    _digest[1] += b;
    _digest[2] += c;
    _digest[3] += d;
    _digest[4] += e;

    /* Count the number of transformations */
    _transforms++;
  }

  static constexpr uint32_t rol(const uint32_t value, const size_t bits) noexcept {
    return (value << bits) | (value >> (32 - bits));
  }

  static constexpr uint32_t blk(const sha1_block_t &block, const size_t i) noexcept {
    return rol(block[(i + 13) & 15U] ^ block[(i + 8) & 15U] ^ block[(i + 2) & 15U] ^ block[i], 1);
  }

  /*
   * (R0+R1), R2, R3, R4 are the different operations used in SHA1
   */
  static constexpr void R0(const sha1_block_t &block, const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i) noexcept {
    z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
    w = rol(w, 30);
  }

  static constexpr void R1(sha1_block_t &block, const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i) noexcept {
    block[i] = blk(block, i);
    z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
    w = rol(w, 30);
  }

  static constexpr void R2(sha1_block_t &block, const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i) noexcept {
    block[i] = blk(block, i);
    z += (w ^ x ^ y) + block[i] + 0x6ed9eba1 + rol(v, 5);
    w = rol(w, 30);
  }

  static constexpr void R3(sha1_block_t &block, const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i) noexcept {
    block[i] = blk(block, i);
    z += (((w | x) & y) | (w & x)) + block[i] + 0x8f1bbcdc + rol(v, 5);
    w = rol(w, 30);
  }

  static constexpr void R4(sha1_block_t &block, const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i) noexcept {
    block[i] = blk(block, i);
    z += (w ^ x ^ y) + block[i] + 0xca62c1d6 + rol(v, 5);
    w = rol(w, 30);
  }

  static constexpr void uint32_to_hex(std::span<char, k_hex_len> buffer, uint32_t value) noexcept {
    constexpr std::string_view digits{"0123456789abcdef"};
    static_assert(digits.size() >= 0x0FU);

    for (uint32_t i = 0, j = (buffer.size() - 1) * sizeof(uint32_t); i < buffer.size(); ++i, j -= sizeof(uint32_t)) {
      buffer[i] = digits[(value >> j) & 0x0FU];
    }
  }

 private:
  sha1_block_t _block{};
  uint32_t _buffer_length_bytes = 0;
  digest_t _digest{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
  uint64_t _transforms = 0;
  bool _finalized = false;
};
// NOLINTEND(*array*, *magic*, *identifier-length*)

std::string sha1(std::span<const std::byte> input) {
  sha1_hash hash;
  hash.update(input);
  return hash.get_value_str();
}

std::string sha1(std::string_view input) {
  sha1_hash hash;
  hash.update(input);
  return hash.get_value_str();
}

}  // namespace server
