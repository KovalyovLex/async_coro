#pragma once

#if WIN_IOCP_ENABLED

#include <async_coro/task.h>
#include <server/io/file_open_mode.h>
#include <server/io/iocp_reactor.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace server::io {

/**
 * @brief Async file I/O operations using Windows I/O Completion Ports (IOCP).
 *
 * Provides non-blocking file read/write operations that integrate with the
 * IOCP reactor. This is the high-performance backend for Windows systems,
 * offering overlapped I/O with worker thread processing.
 *
 * @note Requires Windows with IOCP support.
 * @note The iocp_reactor must outlive any iocp_file registered with it.
 * @note File handles are opened with FILE_FLAG_OVERLAPPED for async I/O.
 * @note For non-Windows platforms, use io_uring_file (Linux) or sync_file.
 */
class iocp_file {
 public:
  /**
   * @brief Open a file using IOCP (coroutine version).
   *
   * Converts the UTF-8 path to wide string and opens the file with
   * FILE_FLAG_OVERLAPPED for async I/O.
   *
   * @param reactor The IOCP reactor to use. Must outlive this file.
   * @param path The UTF-8 encoded file path to open.
   * @param mode The open mode flags (e.g., file_open_mode::read,
   *             file_open_mode::write | file_open_mode::create).
   * @param permissions File permissions (only used when creating new files, default 0644).
   * @return An awaitable that resolves to an expected<iocp_file, std::string>.
   *         On success, contains the opened file. On failure, contains an error message.
   * @note The open operation is submitted to IOCP and completes asynchronously.
   *       The coroutine will be suspended until the open completes.
   */
  [[nodiscard]] static async_coro::task<expected<iocp_file, std::string>> open_coro(iocp_reactor& reactor, std::string path, file_open_mode mode) noexcept;

  // Non-copyable to prevent multiple objects from closing the same file handle.
  iocp_file(const iocp_file&) = delete;
  iocp_file& operator=(const iocp_file&) = delete;

  // Movable - ownership of the file handle transfers; reactor reference is preserved.
  ~iocp_file() noexcept;
  iocp_file(iocp_file&& other) noexcept;
  iocp_file& operator=(iocp_file&& other) noexcept;

  /**
   * @brief Read data from the file into a buffer.
   *
   * Reads in a loop until the entire buffer is filled (or EOF/error).
   * Updates the internal seek offset after each read.
   *
   * @param buffer The buffer to read into.
   * @return An awaitable that resolves to an expected<size_t, std::string>.
   *         On success, contains the total number of bytes read.
   *         On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<size_t, std::string>> read(std::span<std::byte> buffer);

  /**
   * @brief Write data to the file.
   *
   * Writes in a loop until all data is written (or error).
   * Updates the internal seek offset after each write.
   *
   * @param data The data to write.
   * @return An awaitable that resolves to an expected<void, std::string>.
   *         On success, contains void. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<void, std::string>> write(std::span<const std::byte> data);

  /**
   * @brief Flush the file to ensure all data is written to disk.
   *
   * Calls FlushFileBuffers synchronously.
   *
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> flush();

  /**
   * @brief Close the file synchronously.
   *
   * Calls CloseHandle directly to close the file handle.
   * After this returns successfully, the file is no longer valid for I/O.
   *
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> close();

  /**
   * @brief Check if the file is closed.
   *
   * @return true if the file is closed, false otherwise.
   */
  [[nodiscard]] bool is_closed() const noexcept { return _fd == invalid_file_handle; }

  /**
   * @brief Get the file handle (cast to int).
   *
   * @return The file handle as an int, or -1 if the file is closed.
   */
  [[nodiscard]] file_handle_t get_native_handle() const noexcept { return _fd; }

  /**
   * @brief Get the file size in bytes.
   *
   * Uses GetFileSizeEx to retrieve the file size. This operation is synchronous
   * and does not block on regular files.
   *
   * @return An expected<size_t, std::string>. On success, contains the file size
   *         in bytes. On failure, contains an error message.
   */
  [[nodiscard]] expected<size_t, std::string> get_size() const;

  /**
   * @brief Seek to an absolute position in the file.
   *
   * Updates the internal file offset tracker. This does not call SetFilePointerEx —
   * the offset is tracked locally and used by subsequent read/write operations.
   *
   * @param offset The absolute offset to seek to.
   * @return An expected<uint64_t, std::string>. On success, contains the new file offset.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<uint64_t, std::string> seek(uint64_t offset);

  /**
   * @brief Read the entire file contents into a vector.
   *
   * Pre-allocates a buffer of the exact file size (via GetFileSizeEx) and reads
   * all bytes using the IOCP async pattern.
   *
   * @return An awaitable that resolves to an expected<std::vector<std::byte>, std::string>.
   *         On success, contains all bytes read from the file. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<std::vector<std::byte>, std::string>> read_all();

 private:
  /**
   * @brief Synchronously close the file by submitting a close operation with empty callback.
   *
   * Submits the close to IOCP and processes the completion queue to wait for completion.
   * This is used by the destructor and move assignment operator.
   */
  void close_sync() noexcept;

 private:
  /**
   * @brief Construct an iocp_file with an already-opened file handle.
   *
   * @param reactor The IOCP reactor this file belongs to. Must outlive this object.
   * @param file_descriptor The file handle, already opened via submit_open.
   */
  explicit iocp_file(iocp_reactor& reactor, file_handle_t file_descriptor) noexcept;

  iocp_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): reactor lifetime guaranteed by owner
  file_handle_t _fd = invalid_file_handle;
  uint64_t _seek_cur = 0;
};

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
