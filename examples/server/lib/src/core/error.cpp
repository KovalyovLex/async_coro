#include "server/core/error.h"

#include <format>

namespace server::core {

namespace {

#if WIN_SOCKET

static std::string wide_to_utf8(const wchar_t* wide) {
  if (!wide) {
    return {};
  }
  int required = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (required == 0) {
    return {};
  }
  std::string result(required - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], required, nullptr, nullptr);
  result.resize(required - 1);
  return result;
}

static std::string format_windows_error(DWORD error_code) {
  wchar_t* message_buffer = nullptr;
  auto flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
  const auto user_locale = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
  const auto system_locale = MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT);
  const auto& preferred_locale = user_locale > system_locale ? user_locale : system_locale;
  auto message_length = FormatMessageW(
      static_cast<DWORD>(flags),
      nullptr,
      error_code,
      static_cast<DWORD>(preferred_locale),
      reinterpret_cast<wchar_t*>(&message_buffer),
      0,
      nullptr);

  std::string result;
  if (message_length > 0) {
    result = wide_to_utf8(message_buffer);
    LocalFree(message_buffer);
    // Trim trailing whitespace/newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
      result.pop_back();
    }
  }
  return result;
}
#endif

constexpr std::string_view get_static_message(error_type type) noexcept {
  switch (type) {
    case error_type::no_error:
      return "No error";
    case error_type::file_closed:
      return "File is closed";
    case error_type::socket_closed:
      return "Socket is closed";
    case error_type::listener_closed:
      return "Listener is closed";
    case error_type::connection_closed:
      return "Connection was closed";
    case error_type::read_failed:
      return "Read operation failed";
    case error_type::write_failed:
      return "Write operation failed";
    case error_type::flush_failed:
      return "Flush operation failed";
    case error_type::close_failed:
      return "Close operation failed";
    case error_type::bind_failed:
      return "Bind failed";
    case error_type::listen_failed:
      return "Listen failed";
    case error_type::accept_failed:
      return "Accept failed";
    case error_type::connect_failed:
      return "Connect failed";
    case error_type::setsockopt_failed:
      return "setsockopt failed";
    case error_type::create_socket_failed:
      return "Socket creation failed";
    case error_type::tcp_nodelay_failed:
      return "setsockopt(TCP_NODELAY) failed";
    case error_type::open_failed:
      return "File open failed";
    case error_type::get_size_failed:
      return "GetFileSizeEx failed";
    case error_type::set_file_pointer_failed:
      return "SetFilePointerEx failed";
    case error_type::path_conversion_failed:
      return "Path conversion to wide string failed";
    case error_type::reactor_create_failed:
      return "Reactor creation failed";
    case error_type::iocp_create_failed:
      return "CreateIoCompletionPort failed";
    case error_type::io_uring_init_failed:
      return "io_uring_queue_init_params failed";
    case error_type::winsock_startup_failed:
      return "WSAStartup failed";
    case error_type::accept_ex_query_failed:
      return "WSAIoctl AcceptEx query failed";
    case error_type::connect_ex_query_failed:
      return "WSAIoctl ConnectEx query failed";
    case error_type::temp_socket_creation_failed:
      return "Temporary socket creation failed";
    case error_type::inet_pton_failed:
      return "inet_pton failed";
    case error_type::decompression_failed:
      return "Decompression failed";
    case error_type::partial_write:
      return "Partial write";
    case error_type::write_zero_bytes:
      return "Write zero bytes";
    case error_type::write_status_code_failed:
      return "Can't write status code";
    case error_type::ws_too_small_buffer:
      return "WebSocket frame too small for buffer";
    case error_type::ws_invalid_payload_length:
      return "Invalid WebSocket payload length";
    case error_type::ws_min_bits_not_used:
      return "WebSocket RSV bits not used";
    case error_type::ws_max_payload_exceeded:
      return "WebSocket max payload exceeded";
    case error_type::ws_missing_mask:
      return "WebSocket frame missing mask";
    case error_type::ws_control_frame_too_large:
      return "WebSocket control frame too large";
    case error_type::http_empty_status_line:
      return "HTTP empty status line";
    case error_type::http_malformed_status_line:
      return "HTTP malformed status line";
    case error_type::http_missing_status_code:
      return "HTTP missing status code";
    case error_type::http_version_not_supported:
      return "HTTP version not supported";
    case error_type::http_invalid_status_code:
      return "HTTP invalid status code";
    case error_type::http_unsupported_method:
      return "HTTP unsupported method";
    case error_type::http_no_uri:
      return "HTTP no URI";
    case error_type::http_no_version:
      return "HTTP no version";
    case error_type::http_nonempty_body_no_content_length:
      return "HTTP nonempty body without Content-Length";
    case error_type::http_wrong_content_length:
      return "HTTP wrong Content-Length";
    case error_type::http_bad_chunked_format:
      return "HTTP bad chunked format";
    case error_type::http_wrong_chunk_size:
      return "HTTP wrong chunk size";
    case error_type::http_parse_error:
      return "HTTP parse error";
    case error_type::system_error:
      return "System error";
  }
  return "Unknown error";
}

}  // namespace

std::string error::to_string() const {
  switch (type) {
    case error_type::system_error: {
#if WIN_SOCKET
      auto msg = format_windows_error(static_cast<DWORD>(code));
      if (!msg.empty()) {
        return msg;
      }
#else
      const char* msg = strerror(code);
      if (msg) {
        return std::string(msg);
      }
#endif
      return std::format("System error (code: {})", code);
    }
    default:
      return std::string(get_static_message(type));
  }
}

}  // namespace server::core
