#pragma once

#if WIN_IOCP_ENABLED

#include <async_coro/atomic_queue.h>
#include <async_coro/internal/await_callback.h>
#include <async_coro/utils/unique_function.h>
#include <server/core/error.h>
#include <server/io/file_open_mode.h>
#include <server/io/io_config.h>
#include <server/io/socket_type_id.h>
#include <server/io/winsock_init.h>
#include <server/utils/expected.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

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
  using continue_size_callback_t = async_coro::unique_function<void(expected<size_t, core::error>)>;

  /**
   * @brief Callback type for IOCP completion events. Returns success or error.
   */
  using continue_void_callback_t = async_coro::unique_function<void(expected<void, core::error>)>;

  /**
   * @brief Callback type for IOCP completion events. Returns file handle or error.
   */
  using continue_file_callback_t = async_coro::unique_function<void(expected<file_handle_t, core::error>)>;

  /**
   * @brief Callback type for IOCP socket completion events. Returns socket handle or error.
   */
  using continue_socket_callback_t = async_coro::unique_function<void(expected<socket_type, core::error>)>;

  /**
   * @brief Factory method to create a new IOCP reactor.
   *
   * Creates an IOCP completion port with the specified number of worker threads.
   * @param ring_size Ring buffer size.
   * @return An expected<iocp_reactor, core::error>. On success, contains the reactor.
   *         On failure, contains an error describing the initialization failure.
   */
  [[nodiscard]] static expected<iocp_reactor, core::error> create(size_t ring_size = 256) noexcept;

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
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the failure.
   */
  [[nodiscard]] expected<void, core::error> flush(file_handle_t file_descriptor) noexcept;

  /**
   * @brief Synchronously close the file handle.
   *
   * Calls CloseHandle directly (synchronous operation).
   * @param file_descriptor File handle (cast to int) to close.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the failure.
   */
  [[nodiscard]] expected<void, core::error> close_file(file_handle_t file_descriptor) noexcept;

  /**
   * @brief Cancel all pending overlapped IO operations on a socket.
   *
   * Calls shutdown(SD_BOTH) to signal both directions are closed. This causes
   * any pending WSASend/WSARecv/AcceptEx/ConnectEx operations to complete with
   * an error via the IOCP completion port.
   *
   * @param socket_handle The socket to cancel operations on.
   * @return true if shutdown succeeded or socket was already invalid/closed.
   * @return false if shutdown failed.
   */
  [[nodiscard]] bool cancel_socket_io(socket_type socket_handle) noexcept;

  /**
   * @brief Synchronously close a socket and cancel all pending IO operations.
   *
   * Calls shutdown() to abort any pending send/receive/accept/connect operations,
   * then calls closesocket() to release the socket handle. Pending overlapped
   * operations will complete with an error (e.g., WSAECONNRESET) and their
   * callbacks will be invoked via the normal IOCP completion path.
   *
   * @param socket_handle Socket handle to close.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the failure.
   */
  [[nodiscard]] expected<void, core::error> close_socket(socket_type socket_handle) noexcept;

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
   * @brief Create a new socket and associate it with the IOCP port.
   *
   * Creates a socket using WSASocket and associates it with this reactor's completion port.
   * @param kind The socket kind (stream/TCP or datagram/UDP).
   * @return An expected<socket_type, core::error>. On success, contains the new socket handle.
   *         On failure, contains an error describing the creation failure.
   */
  [[nodiscard]] expected<socket_type, core::error> create_socket(socket_type_id kind) noexcept;

  /**
   * @brief Bind a socket to a local address.
   *
   * Binds the socket to the specified sockaddr. The socket must already be created.
   * @param socket_handle The socket handle to bind.
   * @param address Buffer containing the local sockaddr structure.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the bind failure.
   */
  [[nodiscard]] expected<void, core::error> bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept;

  /**
   * @brief Set a socket to listening mode.
   *
   * Calls listen() on the socket with the specified backlog.
   * @param socket_handle The listening socket handle.
   * @param backlog Maximum length of the pending connections queue.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the listen failure.
   */
  [[nodiscard]] expected<void, core::error> listen_socket(socket_type socket_handle, int backlog = SOMAXCONN) noexcept;

  /**
   * @brief Submit an async send operation on a socket.
   *
   * Uses WSASend for overlapped I/O. The socket must already be associated with the IOCP port.
   * @param socket_handle Socket handle to send to.
   * @param buffer Buffer containing data to send. MUST remain valid until callback is invoked.
   * @param callback Continuation called after send completes with bytes sent or error.
   */
  void submit_send_socket(socket_type socket_handle, std::span<const std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async receive operation on a socket.
   *
   * Uses WSARecv for overlapped I/O. The socket must already be associated with the IOCP port.
   * @param socket_handle Socket handle to receive from.
   * @param buffer Buffer to receive into. MUST remain valid until callback is invoked.
   * @param callback Continuation called after receive completes with bytes received or error.
   */
  void submit_receive_socket(socket_type socket_handle, std::span<std::byte> buffer, continue_size_callback_t&& callback);

  /**
   * @brief Submit an async accept operation on a listening socket.
   *
   * Uses AcceptEx for overlapped I/O. The listen socket must already be associated with the IOCP port.
   * Creates an accept socket internally and associates it with the IOCP port.
   * @param listen_socket The listening socket handle.
   * @param local_address_buffer Buffer for local address (sockaddr_in, typically 16 bytes).
   * @param remote_address_buffer Buffer for remote address (sockaddr_in, typically 16 bytes).
   * @param buffer_data Extra receive buffer for client data (can be empty).
   * @param callback Continuation called after accept completes with the accepted socket or error.
   */
  void submit_accept_socket(socket_type listen_socket,
                            std::span<std::byte> local_address_buffer,
                            std::span<std::byte> remote_address_buffer,
                            std::span<std::byte> buffer_data,
                            continue_socket_callback_t&& callback);

  /**
   * @brief Submit an async connect operation on a socket.
   *
   * Uses ConnectEx for overlapped I/O. The socket must already be associated with the IOCP port
   * and bound to a local address (via bind_socket).
   * @param socket_handle The client socket handle.
   * @param remote_address Buffer containing destination sockaddr.
   * @param callback Continuation called after connect completes with void or error.
   */
  void submit_connect_socket(socket_type socket_handle, std::span<const std::byte> remote_address, continue_void_callback_t&& callback);

 private:
  iocp_reactor() noexcept;

  /**
   * @brief Dispatch completion for a known operation type (used in process_loop submit phase).
   *
   * Invokes the callback directly without std::visit since the operation type is already known.
   * Template parameter T is the specific operation struct type (e.g., op_read, op_write).
   */
  template <typename OpType>
  void dispatch_completion_for_op(DWORD bytes_transferred, bool success, OpType& op) noexcept;

 private:
  /**
   * @brief Async file read operation.
   *
   * Reads data from a file handle at the specified offset using overlapped I/O.
   */
  struct op_read {
    file_handle_t fd = invalid_file_handle;
    uint64_t offset = 0;
    std::span<std::byte> buffer_data;
    continue_size_callback_t callback;
  };

  /**
   * @brief Async file write operation.
   *
   * Writes data to a file handle at the specified offset using overlapped I/O.
   */
  struct op_write {
    file_handle_t fd = invalid_file_handle;
    uint64_t offset = 0;
    std::span<std::byte> buffer_data;  // cast from const for Windows API
    continue_size_callback_t callback;
  };

  /**
   * @brief Async file open operation.
   *
   * Opens a file with FILE_FLAG_OVERLAPPED and associates it with the IOCP port.
   */
  struct op_open {
    file_handle_t fd = invalid_file_handle;
    const char* file_path = nullptr;
    file_open_mode open_flags = file_open_mode::append;
    continue_file_callback_t callback;
  };

  /**
   * @brief Async socket send operation.
   *
   * Sends data using WSASend with overlapped I/O.
   */
  struct op_send_socket {
    socket_type socket_fd = invalid_socket_id;
    WSABUF wsa_buf{};
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket receive operation.
   *
   * Receives data using WSARecv with overlapped I/O.
   */
  struct op_receive_socket {
    socket_type socket_fd = invalid_socket_id;
    WSABUF wsa_buf{};
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket accept operation.
   *
   * Accepts a connection using AcceptEx with overlapped I/O.
   * Creates an accept socket internally and associates it with the IOCP port.
   */
  struct op_accept_socket {
    socket_type listen_socket_fd = invalid_socket_id;
    socket_type accept_socket_fd = invalid_socket_id;
    std::span<std::byte> local_address_buffer;
    std::span<std::byte> remote_address_buffer;
    std::span<std::byte> buffer_data;  // extra receive buffer for client data
    continue_socket_callback_t callback;
  };

  /**
   * @brief Async socket connect operation.
   *
   * Connects a client socket using ConnectEx with overlapped I/O.
   */
  struct op_connect_socket {
    socket_type socket_fd = invalid_socket_id;
    std::span<const std::byte> remote_address;
    continue_void_callback_t callback;
  };

  /**
   * @brief Variant holding all possible IOCP operation types.
   *
   * Each operation type is a struct containing only the fields it needs,
   * eliminating the need for an explicit operation_type enum and reducing
   * wasted space in request_entry.
   */
  using request_variant = std::variant<op_read, op_write, op_open,
                                       op_send_socket, op_receive_socket,
                                       op_accept_socket, op_connect_socket>;

  /**
   * @brief A ring buffer entry combining a request variant with its OVERLAPPED state.
   *
   * The OVERLAPPED struct must remain valid from the time the I/O call is made
   * until the worker thread processes the completion event. By storing it here
   * in _local_ring, we guarantee the correct lifetime.
   */
  struct ring_entry {
    request_variant request;
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
  async_coro::atomic_queue<request_variant> _requests;

  std::vector<wchar_t> _temp_w_path;

  winsock_extensions _winsock_extensions{};
};

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
