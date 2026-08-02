#pragma once

#if IO_URING_ENABLED

#include <async_coro/atomic_queue.h>
#include <async_coro/internal/await_callback.h>
#include <async_coro/utils/unique_function.h>
#include <server/utils/expected.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>

// io_uring requires Linux 5.1+
#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <liburing.h>
#include <linux/io_uring.h>
#else
#error "io_uring is only available on Linux 5.1+"
#endif

namespace server::io {

/**
 * @brief Async I/O reactor using io_uring for low-latency file and socket I/O.
 *
 * This reactor provides a high-performance backend using the Linux io_uring
 * subsystem. It supports async read, write, fsync, close, and open operations
 * on file descriptors.
 *
 * @note io_uring provides lower latency (<1μs) compared to epoll/kqueue.
 * @note The reactor must outlive any file or socket registered with it.
 * @note This is an optimization over the existing epoll reactor, not a replacement.
 * @note The io_uring reactor uses external pooling via process_loop (no internal thread).
 */
class io_uring_reactor {
 public:
  /**
   * @brief Callback type for io_uring completion events. Returns number of bytes written\read or error.
   */
  using continue_size_callback_t = async_coro::unique_function<void(expected<size_t, std::string>)>;

  /**
   * @brief Callback type for io_uring completion events. Returns success or error.
   */
  using continue_void_callback_t = async_coro::unique_function<void(expected<void, std::string>)>;

  /**
   * @brief Callback type for io_uring completion events. Returns filedescriptor or error.
   */
  using continue_file_callback_t = async_coro::unique_function<void(expected<int, std::string>)>;

  enum class operation_type : uint8_t {
    send_data,
    receive_data,
    fsync,
    open_file,
    close_file,
  };

  /**
   * @brief Factory method to create a new io_uring reactor.
   *
   * Creates an io_uring ring with the specified size.
   * @param ring_size Number of entries in the submission and completion rings.
   * @return An expected<io_uring_reactor, std::string>. On success, contains the reactor.
   *         On failure, contains an error message describing the initialization failure.
   */
  [[nodiscard]] static expected<io_uring_reactor, std::string> create(size_t ring_size = 256) noexcept;

  io_uring_reactor(io_uring_reactor&& other) noexcept;
  io_uring_reactor& operator=(io_uring_reactor&& other) noexcept;

  ~io_uring_reactor() noexcept;

  io_uring_reactor(const io_uring_reactor&) = delete;
  io_uring_reactor& operator=(const io_uring_reactor&) = delete;

  /**
   * @brief Process pending I/O events and resume waiting coroutines.
   *
   * Polls the completion queue and dispatches events to continuation callbacks.
   * @param max_wait Maximum time to wait for events.
   * @note Must be called from the owning thread.
   */
  void process_loop(std::chrono::nanoseconds max_wait);

  /**
   * @brief Submit an async read operation.
   *
   * @param file_descriptor File descriptor to read from.
   * @param offset Offset in the file to start reading from.
   * @param buffer Buffer to read into. MUST remain valid until the callback is invoked.
   * @param callback Continuation callback will be called after read complete.
   * @note The buffer must outlive the callback invocation. The reactor stores a non-owning span.
   */
  void submit_read(int file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async write operation.
   *
   * @param file_descriptor File descriptor to write to.
   * @param offset File offset to write to.
   * @param buffer Buffer containing data to write. MUST remain valid until the callback is invoked.
   * @param callback Continuation callback will be called after write complete.
   * @note The buffer must outlive the callback invocation. The reactor stores a non-owning span.
   */
  void submit_write(int file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async fsync operation.
   *
   * @param file_descriptor File descriptor to fsync.
   * @param callback Continuation callback will be called after write complete.
   */
  void submit_fsync(int file_descriptor, continue_void_callback_t&& callback);

  /**
   * @brief Submit an async close operation.
   *
   * @param file_descriptor File descriptor to close.
   * @param callback Continuation callback will be called after write complete.
   */
  void submit_close(int file_descriptor, continue_void_callback_t&& callback);

  /**
   * @brief Submit an async open operation.
   *
   * @param path Path to the file to open.
   * @param open_mode Open mode.
   * @param permissions File permissions.
   * @return The index of the registered operation, or invalid_index on failure.
   */
  void submit_open(const char* path, file_open_mode open_mode, int permissions, continue_file_callback_t&& callback);

 private:
  io_uring_reactor() noexcept;

 private:
  /**
   * @brief A request entry stored in the atomic_queue.
   *
   * Holds all request data by value — no heap allocation per request.
   * Callbacks live here in the atomic_queue.
   */
  struct request_entry {
    // offset in file to read from\to
    uint64_t offset = 0;

    // Buffer data for read/write operations
    std::span<std::byte> buffer_data;

    // callback to run after complete
    std::variant<continue_file_callback_t, continue_size_callback_t, continue_void_callback_t> callback;

    // Path data for open operations
    const char* file_path = nullptr;
    int open_flags = 0;
    int open_mode = 0;

    // file-socket descriptor
    int fd = -1;

    // operation type
    operation_type operation = operation_type::fsync;
  };

  struct io_uring _ring{};
  bool _ring_initialized = false;
  size_t _ring_size = 0;

  /**
   * @brief The only growing container — absorbs processing peaks.
   *
   * Submit threads push entries here. The update thread drains them.
   */
  async_coro::atomic_queue<request_entry> _requests;

  /**
   * @brief Fixed-capacity local ring buffer (capacity = ring_size).
   *
   * The update thread drains _requests into this buffer, assigns sequential indices,
   * pushes entries to SQEs, submits, then processes CQEs — all without any mutex.
   */
  std::unique_ptr<request_entry[]> _local_ring;

  /**
   * @brief Stack of free indices (capacity = ring_size).
   *
   * Used to recycle indices when entries complete. Works like a stack:
   * push index on completion, pop on next submit. Fixed capacity, no dynamic reallocation.
   */
  std::vector<size_t> _free_indices;

  /**
   * @brief Stack of non submitted indices (capacity = ring_size).
   *
   * Used to push events to SQE in case of exhausted system buffer. Fixed capacity, no dynamic reallocation.
   */
  std::vector<size_t> _events_to_push;
};

}  // namespace server::io

#endif
