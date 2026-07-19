#pragma once

#include <async_coro/task.h>
#include <server/io/reactor.h>
#include <server/utils/expected.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace server::io {

/**
 * @brief Async file I/O operations using epoll (Linux) or kqueue (macOS).
 *
 * Provides non-blocking file read/write operations that integrate with the
 * existing reactor pattern. File descriptors are pollable on Linux (epoll
 * supports regular files) and macOS (kqueue supports regular files via
 * EVFILT_VNODE).
 *
 * @note The reactor must outlive any file that is registered with it.
 *       The caller is responsible for ensuring proper lifetime management.
 * @note On Linux, regular files are pollable with EPOLLIN/EPOLLOUT. On macOS,
 *       EVFILT_VNODE is used for file change notifications.
 * @note For production low-latency I/O on Linux 5.1+, consider using io_uring
 *       backend (IORING_OP_READ / IORING_OP_WRITE).
 */
class file {
 public:
  /**
   * @brief Enumerates the possible seek origins for file::seek().
   *
   * Wraps the POSIX lseek whence constants to avoid exposing them in the public API.
   */
  enum class seek_whence : uint8_t {
    set,
    current,
    end,
  };

  /**
   * @brief Open a file (non-blocking).
   *
   * @param reactor The reactor to use for async I/O. Must outlive this file.
   * @param path The file path to open.
   * @param mode The open mode (e.g., std::ios::in, std::ios::out, std::ios::binary).
   * @param permissions File permissions (only used when creating new files).
   * @return An expected<file, std::string>.
   *         On success, contains the opened file. On failure, contains an error message.
   */
  [[nodiscard]] static expected<file, std::string> open(reactor& reactor, const std::string& path, int mode, int permissions = 0644) noexcept;

  /**
   * @brief Read data from the file into a buffer.
   *
   * @param buffer The buffer to read into.
   * @return An awaitable that resolves to an expected<size_t, std::string>.
   *         On success, contains the number of bytes read. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);

  /**
   * @brief Write data to the file.
   *
   * @param data The data to write.
   * @return An awaitable that resolves to an expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> data);

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
  [[nodiscard]] bool is_closed() const noexcept;

  /**
   * @brief Get the file descriptor.
   *
   * @return The file descriptor, or -1 if the file is closed.
   */
  [[nodiscard]] int get_fd() const noexcept;

  /**
   * @brief Get the file size in bytes.
   *
   * Uses fstat() to retrieve the file size. This operation is synchronous
   * and does not block on regular files.
   *
   * @return An expected<size_t, std::string>. On success, contains the file size
   *         in bytes. On failure, contains an error message.
   */
  [[nodiscard]] expected<size_t, std::string> get_size() const;

  /**
   * @brief Seek to a position in the file.
   *
   * Uses lseek() to change the file offset. This operation is synchronous
   * and does not block on regular files.
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
   * all bytes using the async reactor pattern. Yields to the reactor on
   * EAGAIN/EWOULDBLOCK for non-blocking I/O.
   *
   * @return An awaitable that resolves to an expected<std::vector<std::byte>, std::string>.
   *         On success, contains all bytes read from the file. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<std::vector<std::byte>, std::string>> read_all();

 private:
  /**
   * @brief Map a seek_whence enum value to the corresponding POSIX lseek constant.
   *
   * @param whence The seek origin enum value.
   * @return The corresponding POSIX whence constant (SEEK_SET, SEEK_CUR, or SEEK_END).
   */
  [[nodiscard]] static int map_whence(seek_whence whence) noexcept;

  /**
   * @brief Check if an error code indicates a "would block" condition.
   *
   * @param err The error code to check.
   * @return true if the error indicates the operation should be retried, false otherwise.
   */
  [[nodiscard]] static bool would_block(int err) noexcept;

 private:
  explicit file(reactor& reactor, socket_type file_descriptor, size_t index) noexcept;

  reactor& _reactor;
  socket_type _fd = -1;
  size_t _index = -1;
};

}  // namespace server::io
