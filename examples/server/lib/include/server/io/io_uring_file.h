#pragma once

#if IO_URING_ENABLED

#include <async_coro/task.h>
#include <server/io/file_open_mode.h>
#include <server/io/io_uring_reactor.h>
#include <server/utils/expected.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace server::io {

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
   * @return An awaitable that resolves to an expected<io_uring_file, std::string>.
   *         On success, contains the opened file. On failure, contains an error message.
   * @note The open operation is submitted to io_uring and completes asynchronously.
   *       The coroutine will be suspended until the open completes.
   */
  [[nodiscard]] static async_coro::task<expected<io_uring_file, std::string>> open_coro(io_uring_reactor& reactor, std::string path, file_open_mode mode, int permissions = 0644) noexcept;

  /**
   * @brief Read data from the file into a buffer.
   *
   * @param buffer The buffer to read into.
   * @param offset The file offset to read from.
   * @return An awaitable that resolves to an expected<size_t, std::string>.
   *         On success, contains the number of bytes read. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);

  /**
   * @brief Write data to the file.
   *
   * @param data The data to write.
   * @param offset The file offset to write to.
   * @return An awaitable that resolves to an expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> data);

  /**
   * @brief Flush the file to ensure all data is written to disk.
   *
   * @return An awaitable that resolves to an expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<void, std::string>> flush();

  /**
   * @brief Close the file.
   *
   * @return An awaitable that resolves to an expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<void, std::string>> close();

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
   * @brief Seek to an absolute position in the file.
   *
   * Updates the internal file offset tracker. This does not call lseek() —
   * the offset is tracked locally and used by subsequent read/write operations.
   *
   * @param offset The absolute offset to seek to.
   * @return An expected<off_t, std::string>. On success, contains the new file offset.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<off_t, std::string> seek(off_t offset);

  /**
   * @brief Read the entire file contents into a vector.
   *
   * Pre-allocates a buffer of the exact file size (via fstat) and reads
   * all bytes using the io_uring async pattern.
   *
   * @return An awaitable that resolves to an expected<std::vector<std::byte>, std::string>.
   *         On success, contains all bytes read from the file. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<std::vector<std::byte>, std::string>> read_all();

 private:
  /**
   * @brief Check if an error code indicates a "would block" condition.
   *
   * @param err The error code to check.
   * @return true if the error indicates the operation should be retried, false otherwise.
   */
  [[nodiscard]] static bool would_block(int err) noexcept;

 private:
  explicit io_uring_file(io_uring_reactor& reactor, int fd) noexcept;

  io_uring_reactor& _reactor;
  int _fd = -1;
  size_t _seek_cur = 0;
};

}  // namespace server::io

#endif
