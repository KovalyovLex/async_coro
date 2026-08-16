#pragma once

#if IO_URING_ENABLED

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/io/file_open_mode.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/utils/expected.h>
#include <sys/types.h>

#include <cstddef>
#include <span>
#include <vector>

namespace server::io {

/// Default file permissions for new files: owner read/write, group/other read (rw-r--r--).
inline constexpr int default_file_permissions = 0644;  // NOLINT(readability-magic-numbers)

/**
 * @brief Async file I/O operations using io_uring for low-latency I/O.
 *
 * Provides non-blocking file read/write operations that integrate with the
 * io_uring reactor. This is the high-performance backend for Linux 5.1+
 * systems, offering lower latency (<1μs) compared to epoll/kqueue.
 *
 * @note Requires Linux kernel 5.1+ with io_uring support.
 * @note The io_uring_reactor must outlive any io_uring_file registered with it.
 * @note This is an optimization over the existing epoll-based file class.
 * @note For non-Linux platforms, use server::io::file instead.
 */
class io_uring_file {
 public:
  /**
   * @brief Open a file using io_uring (coroutine version).
   *
   * @param reactor The io_uring reactor to use. Must outlive this file.
   * @param path The file path to open.
   * @param mode The open mode flags (e.g., file_open_mode::read, file_open_mode::write | file_open_mode::create).
   * @param permissions File permissions (only used when creating new files, default 0644).
   * @return An awaitable that resolves to an expected<io_uring_file, core::error>.
   *         On success, contains the opened file. On failure, contains an error.
   * @note The open operation is submitted to io_uring and completes asynchronously.
   *       The coroutine will be suspended until the open completes.
   */
  [[nodiscard]] static async_coro::task<expected<io_uring_file, core::error>> open_coro(io_uring_reactor& reactor, std::string path, file_open_mode mode, int permissions = default_file_permissions) noexcept;

  // Non-copyable to prevent multiple objects from closing the same file descriptor.
  io_uring_file(const io_uring_file&) = delete;
  io_uring_file& operator=(const io_uring_file&) = delete;

  // Movable - ownership of the file descriptor transfers; reactor reference is preserved.
  ~io_uring_file() noexcept;
  io_uring_file(io_uring_file&& other) noexcept;
  io_uring_file& operator=(io_uring_file&& other) noexcept;

  /**
   * @brief Read data from the file into a buffer.
   *
   * @param buffer The buffer to read into.
   * @param offset The file offset to read from.
   * @return An awaitable that resolves to an expected<size_t, core::error>.
   *         On success, contains the number of bytes read. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<size_t, core::error>> read(std::span<std::byte> buffer);

  /**
   * @brief Write data to the file.
   *
   * @param data The data to write.
   * @param offset The file offset to write to.
   * @return An awaitable that resolves to an expected<void, core::error>.
   *         On success, contains void. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<void, core::error>> write(std::span<const std::byte> data);

  /**
   * @brief Flush the file to ensure all data is written to disk.
   *
   * @return An awaitable that resolves to an expected<void, core::error>.
   *         On success, contains void. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<void, core::error>> flush();

  /**
   * @brief Close the file.
   *
   * @return An awaitable that resolves to an expected<void, core::error>.
   *         On success, contains void. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<void, core::error>> close();

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
  [[nodiscard]] int get_native_handle() const noexcept { return _fd; }

  /**
   * @brief Get the file size in bytes.
   *
   * Uses fstat() to retrieve the file size. This operation is synchronous
   * and does not block on regular files.
   *
   * @return An expected<size_t, core::error>. On success, contains the file size
   *         in bytes. On failure, contains an error.
   */
  [[nodiscard]] expected<size_t, core::error> get_size() const;

  /**
   * @brief Seek to an absolute position in the file.
   *
   * Updates the internal file offset tracker. This does not call lseek() —
   * the offset is tracked locally and used by subsequent read/write operations.
   *
   * @param offset The absolute offset to seek to.
   * @return An expected<off_t, std::string>. On success, contains the new file offset.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<off_t, core::error> seek(off_t offset);

  /**
   * @brief Read the entire file contents into a vector.
   *
   * Pre-allocates a buffer of the exact file size (via fstat) and reads
   * all bytes using the io_uring async pattern.
   *
   * @return An awaitable that resolves to an expected<std::vector<std::byte>, std::string>.
   *         On success, contains all bytes read from the file. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<std::vector<std::byte>, core::error>> read_all();

 private:
  /**
   * @brief Check if an error code indicates a "would block" condition.
   *
   * @param err The error code to check.
   * @return true if the error indicates the operation should be retried, false otherwise.
   */
  [[nodiscard]] static bool would_block(int err) noexcept;

  /**
   * @brief Synchronously close the file by submitting a close operation with empty callback.
   *
   * Submits the close to io_uring and processes the completion queue to wait for completion.
   * This is used by the destructor and move assignment operator.
   */
  void close_sync() noexcept;

 private:
  explicit io_uring_file(io_uring_reactor& reactor, int file_descriptor) noexcept;

  io_uring_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int _fd = -1;
  size_t _seek_cur = 0;
};

}  // namespace server::io

#endif
