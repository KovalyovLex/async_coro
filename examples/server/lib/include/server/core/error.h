#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace server::core {

enum class error_type : uint16_t {
  no_error = 0,

  // --- System/OS errors (int code = OS-specific error) ---
  system_error = 1,  // code = GetLastError()\WSAGetLastError()\errno

  // --- Closed resources ---
  file_closed = 100,
  socket_closed = 101,
  listener_closed = 102,
  connection_closed = 103,

  // --- I/O operation failures (typed, some carry OS error code) ---
  read_failed = 200,   // + Windows/POSIX error code
  write_failed = 201,  // + Windows/POSIX error code
  flush_failed = 202,  // + Windows/POSIX error code (FlushFileBuffers)
  close_failed = 203,  // + Windows/POSIX error code

  // --- Socket operations (typed, some carry OS error code) ---
  bind_failed = 300,           // + Windows error code
  listen_failed = 301,         // + Windows error code
  accept_failed = 302,         // + Windows error code
  connect_failed = 303,        // + Windows error code
  setsockopt_failed = 304,     // + Windows error code (e.g., TCP_NODELAY)
  create_socket_failed = 305,  // + Windows error code (WSASocket)
  tcp_nodelay_failed = 306,    // + Windows error code

  // --- File operations (typed, some carry OS error code) ---
  open_failed = 400,              // + Windows error code (CreateFile)
  get_size_failed = 401,          // + Windows error code (GetFileSizeEx)
  set_file_pointer_failed = 402,  // + Windows error code (SetFilePointerEx)
  path_conversion_failed = 403,   // MultiByteToWideChar failure

  // --- Reactor / infrastructure ---
  reactor_create_failed = 500,
  iocp_create_failed = 501,       // CreateIoCompletionPort failure
  io_uring_init_failed = 502,     // io_uring_queue_init_params failure
  winsock_startup_failed = 503,   // WSAStartup failure
  accept_ex_query_failed = 504,   // WSAIoctl AcceptEx query failure
  connect_ex_query_failed = 505,  // WSAIoctl ConnectEx query failure
  temp_socket_creation_failed = 506,

  // --- Network / protocol ---
  inet_pton_failed = 600,

  // --- Decompression ---
  decompression_failed = 700,

  // --- Write errors ---
  partial_write = 800,
  write_zero_bytes = 801,
  write_status_code_failed = 802,

  // --- WebSocket protocol errors (replacing ws_error) ---
  ws_too_small_buffer = 900,
  ws_invalid_payload_length = 901,
  ws_min_bits_not_used = 902,
  ws_max_payload_exceeded = 903,
  ws_missing_mask = 904,
  ws_control_frame_too_large = 905,

  // --- HTTP protocol errors (replacing http_error reason strings) ---
  // Note: http_status_code stays in http_error struct for the status field.
  // These are for internal parsing failures that don't map to HTTP status codes.
  http_empty_status_line = 1000,
  http_malformed_status_line = 1001,
  http_missing_status_code = 1002,
  http_version_not_supported = 1003,
  http_invalid_status_code = 1004,
  http_unsupported_method = 1005,
  http_no_uri = 1006,
  http_no_version = 1007,
  http_nonempty_body_no_content_length = 1008,
  http_wrong_content_length = 1009,
  http_bad_chunked_format = 1010,
  http_wrong_chunk_size = 1011,
  http_parse_error = 1012,
};

struct error {
  error_type type = error_type::no_error;
  int code = 0;

  constexpr error() noexcept = default;
  constexpr error(error_type err_t, int err_c = 0) noexcept : type(err_t), code(err_c) {}

  [[nodiscard]] constexpr bool empty() const noexcept {
    return type == error_type::no_error;
  }

  [[nodiscard]] std::string to_string() const;

  [[nodiscard]] constexpr bool operator==(const error& other) const noexcept {
    return type == other.type && code == other.code;
  }
};

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const error& err) {
  os << err.to_string();
  return os;
}

}  // namespace server::core
