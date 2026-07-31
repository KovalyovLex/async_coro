#pragma once

#if WIN_IOCP_ENABLED

#include <async_coro/atomic_queue.h>
#include <async_coro/internal/await_callback.h>
#include <async_coro/utils/unique_function.h>
#include <server/io/file_open_mode.h>
#include <server/io/io_config.h>
#include <server/utils/expected.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

// Windows headers — already included via io_config.h when WIN_SOCKET is defined,
// but we need additional IOCP-specific symbols.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace server::io {

/**
 * @brief Async I/O reactor using Windows I/O Completion Ports (IOCP).
 *
 * This reactor provides a high-performance backend using the Windows IOCP
 * subsystem for overlapped file and socket I/O. It supports async read, write,
 * fsync, close, and open operations on file handles.
 *
 * @note The reactor uses external pooling via process_loop (no internal worker threads).
 * @note The reactor must outlive any file or socket registered with it.
 * @note This is an optimization over synchronous I/O, not a replacement.
 */
class iocp_reactor {
 public:
  /**
   * @brief Callback type for IOCP completion events. Returns number of bytes written/read or error.
   */
  using continue_size_callback_t = async_coro::unique_function<void(expected<size_t, std::string>)>;

  /**
   * @brief Callback type for IOCP completion events. Returns success or error.
   */
  using continue_void_callback_t = async_coro::unique_function<void(expected<void, std::string>)>;

  /**
   * @brief Callback type for IOCP completion events. Returns file handle or error.
   */
  using continue_file_callback_t = async_coro::unique_function<void(expected<file_handle_t, std::string>)>;

  /**
   * @brief Factory method to create a new IOCP reactor.
   *
   * Creates an IOCP completion port with the specified number of worker threads.
   * @param ring_size Ring buffer size.
   * @return An expected<iocp_reactor, std::string>. On success, contains the reactor.
   *         On failure, contains an error message describing the initialization failure.
   */
  [[nodiscard]] static expected<iocp_reactor, std::string> create(size_t ring_size = 256) noexcept;

  iocp_reactor(iocp_reactor&& other) noexcept;
  iocp_reactor& operator=(iocp_reactor&& other) noexcept;

  ~iocp_reactor() noexcept;

  iocp_reactor(const iocp_reactor&) = delete;
  iocp_reactor& operator=(const iocp_reactor&) = delete;

  /**
   * @brief Process pending I/O events and resume waiting coroutines.
   *
   * Drains the submission queue, posts overlapped I/O operations, waits for
   * completion via GetQueuedCompletionStatus, then dispatches callbacks.
   * @param max_wait Maximum time to wait for completion events.
   * @note Must be called from the owning thread.
   */
  void process_loop(std::chrono::milliseconds max_wait);

  /**
   * @brief Submit an async read operation.
   *
   * @param file_descriptor File handle (cast to int) to read from.
   * @param offset Offset in the file to start reading from.
   * @param buffer Buffer to read into. MUST remain valid until the callback is invoked.
   * @param callback Continuation callback will be called after read completes.
   * @note The buffer must outlive the callback invocation. The reactor stores a non-owning span.
   */
  void submit_read(file_handle_t file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async write operation.
   *
   * @param file_descriptor File handle (cast to int) to write to.
   * @param offset File offset to write to.
   * @param buffer Buffer containing data to write. MUST remain valid until the callback is invoked.
   * @param callback Continuation callback will be called after write completes.
   * @note The buffer must outlive the callback invocation. The reactor stores a non-owning span.
   */
  void submit_write(file_handle_t file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Synchronously flush the file to ensure all data is written to disk.
   *
   * Calls FlushFileBuffers directly (synchronous operation).
   * @param file_descriptor File handle (cast to int) to flush.
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message describing the failure.
   */
  [[nodiscard]] expected<void, std::string> flush(file_handle_t file_descriptor) noexcept;

  /**
   * @brief Synchronously close the file handle.
   *
   * Calls CloseHandle directly (synchronous operation).
   * @param file_descriptor File handle (cast to int) to close.
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message describing the failure.
   */
  [[nodiscard]] expected<void, std::string> close(file_handle_t file_descriptor) noexcept;

  /**
   * @brief Submit an async open operation.
   *
   * Opens a file with FILE_FLAG_OVERLAPPED for async I/O.
   * @param path UTF-8 encoded file path to open.
   * @param open_mode Open flags.
   * @param callback Continuation callback will be called after open completes with the handle.
   */
  void submit_open(const char* path, file_open_mode open_mode, continue_file_callback_t&& callback);

  /**
   * @brief Convert Windows GetLastError() to a UTF-8 error string.
   *
   * Uses FormatMessageW to get the system error message and converts it to UTF-8.
   * @return A human-readable error string, or empty string if no error.
   */
  [[nodiscard]] static std::string format_windows_error() noexcept;

  /**
   * @brief Convert a wide string (UTF-16) to UTF-8.
   * @param wide The wide string to convert.
   * @return The UTF-8 encoded string.
   */
  [[nodiscard]] static std::string wide_to_utf8(const wchar_t* wide, int length) noexcept;

 private:
  iocp_reactor() noexcept;

  /**
   * @brief Dispatch a completed request entry to its callback.
   *
   * Handles the variant callback type and converts the result to expected<T>.
   * Recycles the index back into _free_indices for reuse.
   * @param index The index in _local_ring of the completed entry.
   * @param bytes_transferred Number of bytes transferred (0 on error).
   * @param success Whether the operation succeeded.
   */
  void dispatch_completion(size_t index, DWORD bytes_transferred, bool success) noexcept;

 private:
  enum class operation_type : uint8_t {
    none,
    send_data,
    receive_data,
    open_file,
  };

  /**
   * @brief A request entry stored in the atomic_queue or local ring.
   *
   * Holds all request data by value — no heap allocation per request.
   * Callbacks live here in the atomic_queue until dispatched to _local_ring.
   *
   * @note The OVERLAPPED struct is embedded here to ensure it remains valid
   *       for the lifetime of the overlapped I/O operation. Windows will access
   *       this struct asynchronously via the worker thread completion handler.
   */
  struct request_entry {
    /** Offset in file to read from/to. */
    uint64_t offset = 0;

    /** Buffer data for read/write operations. */
    std::span<std::byte> buffer_data;

    /** Callback to run after completion. */
    std::variant<continue_file_callback_t, continue_size_callback_t, continue_void_callback_t> callback;

    /** Path data for open operations (UTF-8 encoded). */
    const char* file_path = nullptr;
    file_open_mode open_flags = file_open_mode::append;

    /** File/socket handle. */
    file_handle_t fd = invalid_file_handle;

    /** Operation type. */
    operation_type operation = operation_type::none;
  };

  struct ring_entry {
    request_entry request;

    /**
     * @brief Overlapped I/O state for async operations.
     *
     * This OVERLAPPED struct must remain valid from the time ReadFile/WriteFile
     * is called until the worker thread processes the completion event. By storing
     * it here in _local_ring, we guarantee the correct lifetime.
     */
    OVERLAPPED overlapped{};
  };

 private:
  HANDLE _completion_port = INVALID_HANDLE_VALUE;
  size_t _ring_size = 0;

  /**
   * @brief Fixed-capacity local ring buffer (capacity = ring_size).
   *
   * Holds request entries and their associated OVERLAPPED structs.
   * Entries are allocated from _free_indices and recycled on completion.
   */
  std::unique_ptr<ring_entry[]> _local_ring;

  /**
   * @brief Stack of free indices (capacity = ring_size).
   *
   * Used to recycle indices when entries complete. Works like a stack:
   * push index on completion, pop on next submit. Fixed capacity, no dynamic reallocation.
   */
  std::vector<size_t> _free_indices;

  /**
   * @brief Stack of non-submitted indices (capacity = ring_size).
   *
   * Used to buffer events when the IOCP submission buffer is full.
   */
  std::vector<size_t> _events_to_push;

  /**
   * @brief The only growing container — absorbs processing peaks.
   *
   * Submit threads push entries here. Worker threads drain them.
   */
  async_coro::atomic_queue<request_entry> _requests;

  std::vector<wchar_t> _temp_w_path;
};

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
