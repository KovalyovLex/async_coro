#pragma once

#include <server/io/io_config.h>

#include <cstdint>

namespace server::io {

/**
 * @brief Enum flags for file open modes.
 *
 * These flags are platform-independent and are converted to POSIX/O_* or
 * Windows CreateFile access/creation flags at the API boundary.
 */
enum class file_open_mode : uint8_t {
  append = 1U << 0U,
  create = 1U << 1U,
  trunc = 1U << 2U,

  read = 1U << 3U,
  write = 1U << 4U,
};

// NOLINTBEGIN(*identifier-length)

constexpr file_open_mode operator~(file_open_mode a) noexcept {
  return file_open_mode(~static_cast<uint8_t>(a));
}

constexpr file_open_mode operator|(file_open_mode a, file_open_mode b) noexcept {
  return file_open_mode(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr file_open_mode& operator|=(file_open_mode& a, file_open_mode b) noexcept {
  a = a | b;
  return a;
}

constexpr file_open_mode operator&(file_open_mode a, file_open_mode b) noexcept {
  return file_open_mode(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr file_open_mode& operator&=(file_open_mode& a, file_open_mode b) noexcept {
  a = a & b;
  return a;
}

constexpr file_open_mode operator^(file_open_mode a, file_open_mode b) noexcept {
  return file_open_mode(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
}

constexpr file_open_mode& operator^=(file_open_mode& a, file_open_mode b) noexcept {
  a = a ^ b;
  return a;
}

// NOLINTEND(*identifier-length)

#if WIN_SOCKET

/**
 * @brief Convert file_open_mode flags to Windows CreateFile access/creation flags.
 *
 * @param mode The file_open_mode flags.
 * @param access Output parameter for the DWORD access flag (GENERIC_READ, GENERIC_WRITE).
 * @param creation Output parameter for the DWORD creation disposition.
 */
void mode_to_win_flags(file_open_mode mode, DWORD& access, DWORD& creation) noexcept;

#else

/**
 * @brief Convert file_open_mode flags to POSIX O_* flags.
 *
 * @param mode The file_open_mode flags.
 * @return The corresponding POSIX open() mode flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.).
 */
[[nodiscard]] int mode_to_posix_flags(file_open_mode mode) noexcept;

#endif

}  // namespace server::io
