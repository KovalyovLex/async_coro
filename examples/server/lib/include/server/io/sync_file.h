#pragma once

#include <server/io/file_open_mode.h>
#include <server/utils/expected.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace server::io {

/**
 * @brief Synchronous file I/O operations.
 *
 * Provides blocking read/write operations for regular files. Unlike the async
 * reactor-based file class, this implementation uses standard POSIX file I/O
 * without epoll/kqueue integration since regular files are not truly pollable
 * across platforms (Linux: always ready; macOS: EVFILT_VNODE semantics differ).
 *
 * This is the appropriate choice for:
 * - Log file writing/reading
 * - Configuration file parsing
 * - Batch data persistence operations
 * - Any scenario where blocking I/O does not impact critical paths
 *
 * @note All operations are synchronous and may block.
 * @note No reactor dependency — this class is self-contained.
 */
class sync_file {
 public:
  /**
   * @brief Enumerates the possible seek origins for sync_file::seek().
   */
  enum class seek_whence : uint8_t {
    set,
    current,
    end,
  };

  /**
   * @brief Open a file.
   *
   * @param path The file path to open.
   * @param mode The open mode flags (e.g., file_open_mode::read | file_open_mode::create).
   * @return An expected<sync_file, std::string>.
   *         On success, contains the opened file. On failure, contains an error message.
   */
  [[nodiscard]] static expected<sync_file, std::string> open(const std::string& path, file_open_mode mode) noexcept;

  /**
   * @brief Read data from the file into a buffer.
   *
   * @param buffer The buffer to read into.
   * @return An expected<size_t, std::string>.
   *         On success, contains the number of bytes read. On failure, contains an error message.
   */
  [[nodiscard]] expected<size_t, std::string> read(std::span<std::byte> buffer) const;

  /**
   * @brief Write data to the file.
   *
   * @param data The data to write.
   * @return An expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> write(std::span<const std::byte> data) const;

  /**
   * @brief Flush the file to ensure all data is written to disk.
   *
   * @return An expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> flush() const;

  /**
   * @brief Close the file.
   *
   * After closing, the file object is no longer valid for I/O operations.
   */
  void close() noexcept;

  /**
   * @brief Check if the file is closed.
   *
   * @return true if the file is closed, false otherwise.
   */
  [[nodiscard]] bool is_closed() const noexcept { return _fd == invalid_file_handle; }

  /**
   * @brief Get the file descriptor.
   *
   * @return The file descriptor, or -1 if the file is closed.
   */
  [[nodiscard]] file_handle_t get_fd() const noexcept { return _fd; }

  // Non-copyable to prevent multiple objects from closing the same file descriptor.
  sync_file(const sync_file&) = delete;
  sync_file& operator=(const sync_file&) = delete;

  // Movable - ownership transfers to the new object.
  sync_file(sync_file&& other) noexcept;
  sync_file& operator=(sync_file&& other) noexcept;

  ~sync_file() noexcept;

  /**
   * @brief Get the file size in bytes.
   *
   * Uses fstat() to retrieve the file size. This operation does not block.
   *
   * @return An expected<size_t, std::string>. On success, contains the file size
   *         in bytes. On failure, contains an error message.
   */
  [[nodiscard]] expected<size_t, std::string> get_size() const;

  /**
   * @brief Seek to a position in the file.
   *
   * Uses lseek() to change the file offset. This operation does not block.
   *
   * @param offset The offset to seek to, interpreted according to whence.
   * @param whence The seek origin (set, current, or end).
   * @return An expected<off_t, std::string>. On success, contains the new file offset.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<off_t, std::string> seek(off_t offset, seek_whence whence) const;

  /**
   * @brief Read the entire file contents into a vector.
   *
   * Pre-allocates a buffer of the exact file size (via fstat) and reads
   * all bytes in a single operation.
   *
   * @return An expected<std::vector<std::byte>, std::string>.
   *         On success, contains all bytes read from the file. On failure, contains an error message.
   */
  [[nodiscard]] expected<std::vector<std::byte>, std::string> read_all();

 private:
  explicit sync_file(file_handle_t file_descriptor) noexcept;

  file_handle_t _fd = invalid_file_handle;
};

}  // namespace server::io
