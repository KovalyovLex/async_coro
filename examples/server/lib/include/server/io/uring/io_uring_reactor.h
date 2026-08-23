#pragma once

#if IO_URING_ENABLED

#include <async_coro/atomic_queue.h>
#include <async_coro/internal/await_callback.h>
#include <async_coro/utils/unique_function.h>
#include <server/core/error.h>
#include <server/io/file_open_mode.h>
#include <server/io/io_config.h>
#include <server/io/socket_type_id.h>
#include <server/utils/expected.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

// io_uring requires Linux 5.1+
#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <sys/socket.h>
#include <unistd.h>
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
  using continue_size_callback_t = async_coro::unique_function<void(expected<size_t, core::error>)>;

  /**
   * @brief Callback type for io_uring completion events. Returns success or error.
   */
  using continue_void_callback_t = async_coro::unique_function<void(expected<void, core::error>)>;

  /**
   * @brief Callback type for io_uring completion events. Returns filedescriptor or error.
   */
  using continue_file_callback_t = async_coro::unique_function<void(expected<int, core::error>)>;

  /**
   * @brief Callback type for io_uring socket completion events. Returns socket descriptor or error.
   */
  using continue_socket_callback_t = async_coro::unique_function<void(expected<socket_type, core::error>)>;

  static constexpr size_t k_default_ring_size = 256;  // NOLINT(readability-magic-numbers)

  /**
   * @brief Factory method to create a new io_uring reactor.
   *
   * Creates an io_uring ring with the specified size.
   * @param ring_size Number of entries in the submission and completion rings.
   * @return An expected<io_uring_reactor, core::error>. On success, contains the reactor.
   *         On failure, contains an error describing the initialization failure.
   */
  [[nodiscard]] static expected<io_uring_reactor, core::error> create(size_t ring_size = k_default_ring_size) noexcept;

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
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the io_uring error.
   * @note Must be called from the owning thread.
   */
  [[nodiscard]] expected<void, core::error> process_loop(std::chrono::nanoseconds max_wait);

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

  /**
   * @brief Create a new socket and associate it with io_uring.
   *
   * Creates a socket using the POSIX socket() syscall.
   * @param kind The socket kind (stream/TCP or datagram/UDP).
   * @return An expected<socket_type, core::error>. On success, contains the new socket descriptor.
   *         On failure, contains an error describing the creation failure.
   */
  [[nodiscard]] expected<socket_type, core::error> create_socket(socket_type_id kind) noexcept;

  /**
   * @brief Bind a socket to a local address.
   *
   * Binds the socket to the specified sockaddr. The socket must already be created.
   * @param socket_handle The socket descriptor to bind.
   * @param address Buffer containing the local sockaddr structure.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the bind failure.
   */
  [[nodiscard]] expected<void, core::error> bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept;

  /**
   * @brief Set a socket to listening mode.
   *
   * Calls listen() on the socket with the specified backlog.
   * @param socket_handle The listening socket descriptor.
   * @param backlog Maximum length of the pending connections queue.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the listen failure.
   */
  [[nodiscard]] expected<void, core::error> listen_socket(socket_type socket_handle, int backlog = SOMAXCONN) noexcept;

  /**
   * @brief Submit an async send operation on a socket.
   *
   * Uses io_uring's send operation. The socket must already be created.
   * @param socket_handle Socket descriptor to send to.
   * @param buffer Buffer containing data to send. MUST remain valid until callback is invoked.
   * @param callback Continuation called after send completes with bytes sent or error.
   */
  void submit_send_socket(socket_type socket_handle, std::span<const std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async receive operation on a socket.
   *
   * Uses io_uring's recv operation. The socket must already be created.
   * @param socket_handle Socket descriptor to receive from.
   * @param buffer Buffer to receive into. MUST remain valid until callback is invoked.
   * @param callback Continuation called after receive completes with bytes received or error.
   */
  void submit_receive_socket(socket_type socket_handle, std::span<std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async accept operation on a listening socket.
   *
   * Uses io_uring's accept operation. The listen socket must already be created and in listening mode.
   * Creates an accept socket internally.
   * @param listen_socket The listening socket descriptor.
   * @param callback Continuation called after accept completes with the accepted socket or error.
   */
  void submit_accept_socket(socket_type listen_socket, continue_socket_callback_t&& callback);

  /**
   * @brief Submit an async connect operation on a socket.
   *
   * Uses io_uring's connect operation. The socket must already be created and bound to a local address.
   * @param socket_handle The client socket descriptor.
   * @param remote_address Buffer containing destination sockaddr.
   * @param callback Continuation called after connect completes with void or error.
   */
  void submit_connect_socket(socket_type socket_handle, std::span<const std::byte> remote_address, continue_void_callback_t&& callback);

 private:
  io_uring_reactor() noexcept;

  /**
   * @brief Async file read operation.
   *
   * Reads data from a file descriptor at the specified offset using io_uring.
   */
  struct op_read {
    int fd = -1;
    uint64_t offset = 0;
    std::span<std::byte> buffer_data;
    continue_size_callback_t callback;
  };

  /**
   * @brief Async file write operation.
   *
   * Writes data to a file descriptor at the specified offset using io_uring.
   */
  struct op_write {
    int fd = -1;
    uint64_t offset = 0;
    std::span<std::byte> buffer_data;  // cast from const for io_uring API
    continue_size_callback_t callback;
  };

  /**
   * @brief Async fsync operation.
   *
   * Flushes a file descriptor to ensure all data is written to disk.
   */
  struct op_fsync {
    int fd = -1;
    continue_void_callback_t callback;
  };

  /**
   * @brief Async close operation.
   *
   * Closes a file descriptor.
   */
  struct op_close {
    int fd = -1;
    continue_void_callback_t callback;
  };

  /**
   * @brief Async file open operation.
   *
   * Opens a file and returns the new file descriptor.
   */
  struct op_open {
    int fd = -1;
    const char* file_path = nullptr;
    int open_flags = 0;
    int open_mode = 0;
    continue_file_callback_t callback;
  };

  /**
   * @brief Async socket send operation.
   *
   * Sends data using io_uring's send operation.
   */
  struct op_send_socket {
    socket_type socket_fd = invalid_socket_id;
    std::span<std::byte> buffer_data;  // cast from const for io_uring API
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket receive operation.
   *
   * Receives data using io_uring's recv operation.
   */
  struct op_receive_socket {
    socket_type socket_fd = invalid_socket_id;
    std::span<std::byte> buffer_data;
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket accept operation.
   *
   * Accepts a connection using io_uring's accept operation.
   * Creates an accept socket internally.
   */
  struct op_accept_socket {
    socket_type listen_socket_fd = invalid_socket_id;
    continue_socket_callback_t callback;
  };

  /**
   * @brief Async socket connect operation.
   *
   * Connects a client socket using io_uring's connect operation.
   */
  struct op_connect_socket {
    socket_type socket_fd = invalid_socket_id;
    std::span<const std::byte> remote_address;
    continue_void_callback_t callback;
  };

  /**
   * @brief Variant holding all possible io_uring operation types.
   *
   * Each operation type is a struct containing only the fields it needs,
   * eliminating the need for an explicit operation_type enum and reducing
   * wasted space in request_entry.
   */
  using request_variant = std::variant<op_read, op_write, op_fsync, op_close, op_open,
                                       op_send_socket, op_receive_socket,
                                       op_accept_socket, op_connect_socket>;

  struct io_uring _ring{};
  bool _ring_initialized = false;
  size_t _ring_size = 0;

  /**
   * @brief The only growing container — absorbs processing peaks.
   *
   * Submit threads push entries here. The update thread drains them.
   */
  async_coro::atomic_queue<request_variant> _requests;

  /**
   * @brief Fixed-capacity local ring buffer (capacity = ring_size).
   *
   * The update thread drains _requests into this buffer, assigns sequential indices,
   * pushes entries to SQEs, submits, then processes CQEs — all without any mutex.
   */
  std::unique_ptr<request_variant[]> _local_ring;  // NOLINT(*-c-arrays): io_uring requires contiguous heap allocation managed by unique_ptr

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
