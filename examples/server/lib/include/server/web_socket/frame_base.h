#pragma once

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace server::web_socket {

// bit mapped structure of beginning of the frame. See https://datatracker.ietf.org/doc/html/rfc6455#section-5.2
// NOLINTBEGIN(*magic*)
struct frame_base {
  static constexpr uint8_t k_max_size_1_byte = 125U;
  static constexpr uint16_t k_max_size_2_bytes = std::numeric_limits<uint16_t>::max() - 1;
  static constexpr uint64_t k_max_size_8_bytes = std::numeric_limits<uint64_t>::max() - 1;
  static constexpr uint8_t k_payload_len_2_bytes = 126U;
  static constexpr uint8_t k_payload_len_8_bytes = 127U;

  constexpr frame_base() noexcept = default;
  constexpr frame_base(bool final, uint8_t op_code, bool is_rsv1 = false, bool is_rsv2 = false, bool is_rsv3 = false) noexcept
      : frame_base() {
    set_final(final);
    set_rsv1(is_rsv1);
    set_rsv2(is_rsv2);
    set_rsv3(is_rsv3);
    set_opcode(op_code);
  }

  uint8_t byte1 = 0;
  uint8_t byte2 = 0;

  [[nodiscard]] constexpr bool is_final() const noexcept {
    return (byte1 & 0b10000000U) != 0;
  }

  constexpr void set_final(bool value) noexcept {
    byte1 = (byte1 & 0b01111111U) | uint8_t(uint8_t(value) << 7U);
  }

  [[nodiscard]] constexpr bool is_rsv1() const noexcept {
    return (byte1 & 0b01000000U) != 0;
  }

  constexpr void set_rsv1(bool value) noexcept {
    byte1 = (byte1 & 0b10111111U) | uint8_t(uint8_t(value) << 6U);
  }

  [[nodiscard]] constexpr bool is_rsv2() const noexcept {
    return (byte1 & 0b00100000U) != 0;
  }

  constexpr void set_rsv2(bool value) noexcept {
    byte1 = (byte1 & 0b11011111U) | uint8_t(uint8_t(value) << 5U);
  }

  [[nodiscard]] constexpr bool is_rsv3() const noexcept {
    return (byte1 & 0b00010000U) != 0;
  }

  constexpr void set_rsv3(bool value) noexcept {
    byte1 = (byte1 & 0b11101111U) | uint8_t(uint8_t(value) << 4U);
  }

  [[nodiscard]] constexpr uint8_t get_opcode() const noexcept {
    return (byte1 & 0b00001111U);
  }

  constexpr void set_opcode(uint8_t value) noexcept {
    byte1 = (byte1 & 0b11110000U) | (value & 0b00001111U);
  }

  [[nodiscard]] constexpr bool is_masked() const noexcept {
    return (byte2 & 0b10000000U) != 0;
  }

  constexpr void set_masked(bool value) noexcept {
    byte2 = (byte2 & 0b01111111U) | uint8_t(uint8_t(value) << 7U);
  }

  [[nodiscard]] constexpr uint8_t get_payload_len() const noexcept {
    return (byte2 & 0b01111111U);
  }

  constexpr void set_payload_len(uint8_t value) noexcept {
    byte2 = (byte2 & 0b10000000U) | (value & 0b01111111U);
  }
};
// NOLINTEND(*magic*)

// Big enough structure to read the beginning of frame
union frame_begin {
  std::array<std::byte, 14> buffer{};  // NOLINT(*magic*) +8 for len +4 for mask
  frame_base frame;
};

}  // namespace server::web_socket
