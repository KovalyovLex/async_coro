#include "http_test_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <server/socket_layer/connection_id.h>
#include <sys/socket.h>
#include <unistd.h>

#include <charconv>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

bool set_socket_timeouts(server::socket_layer::socket_type sock, std::chrono::microseconds timeout) {
  if (timeout.count() == 0) {
    return true;
  }

  const auto second = std::chrono::duration_cast<std::chrono::seconds>(timeout);
  timeval tv{};
  tv.tv_sec = second.count();
  tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(timeout - second).count();
  if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    return false;
  }
  if (::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    return false;
  }
  return true;
}

}  // namespace

http_test_client::http_test_client(std::string host, uint16_t port, std::chrono::microseconds timeout) noexcept
    : _host(std::move(host)) {
  // we use blocking connect and rely on connect retry from caller
  auto sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return;
  }

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = ::htons(port);
  if (::inet_pton(AF_INET, _host.c_str(), &sa.sin_addr) != 1) {
    ::close(sock);
    return;
  }

  if (::connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    ::close(sock);
    return;
  }

  // set recv/send timeouts
  set_socket_timeouts(sock, timeout);

  _connection = server::socket_layer::connection_id{sock};
}

http_test_client::~http_test_client() noexcept {
  server::socket_layer::close_socket(_connection.get_platform_id());
}

http_test_client::http_test_client(http_test_client&& other) noexcept
    : _host(std::move(other._host)),
      _connection(std::exchange(other._connection, server::socket_layer::invalid_connection)) {}

http_test_client& http_test_client::operator=(http_test_client&& other) noexcept {
  _host = std::move(other._host);
  _connection = std::exchange(other._connection, server::socket_layer::invalid_connection);
  return *this;
}

void http_test_client::try_send_all(std::span<const std::byte>& bytes) noexcept {
  if (_connection == server::socket_layer::invalid_connection) {
    return;
  }

  while (!bytes.empty()) {
    ssize_t s = ::send(_connection.get_platform_id(), bytes.data(), bytes.size(), 0);
    if (s <= 0) {
      return;
    }
    bytes = bytes.subspan(static_cast<size_t>(s));
  }
}

bool http_test_client::recv_bytes(std::span<std::byte>& bytes) noexcept {
  if (_connection == server::socket_layer::invalid_connection) {
    return false;
  }

  if (!bytes.empty()) {
    ssize_t r = ::recv(_connection.get_platform_id(), bytes.data(), bytes.size(), 0);
    if (r <= 0) {
      return false;
    }
    bytes = bytes.subspan(r);
  }

  return true;
}

std::string http_test_client::read_response() {
  // simple read until CRLFCRLF
  std::string out;
  std::array<char, 1024> buf;  // NOLINT(*init)
  constexpr std::string_view k_split_str = "\r\n\r\n";
  constexpr std::string_view k_content_len = "Content-Length:";

  size_t offset = 0;
  std::optional<size_t> cnt_len = {};
  size_t body_start = 0;
  while (true) {
    auto bytes = std::as_writable_bytes(std::span<char>{buf});
    if (!recv_bytes(bytes)) {
      break;
    }
    out.append(buf.data(), buf.data() + buf.size() - bytes.size());

    auto it = body_start == 0 ? out.find(k_split_str, offset) : std::string::npos;
    if (it != std::string::npos) {
      offset = body_start = it + k_split_str.size();
      if (out.size() >= body_start) {
        const auto headers = std::string_view{out.data(), body_start};
        const auto cnt_start_i = headers.find(k_content_len);
        std::string_view cnt_len_str;
        if (cnt_start_i != std::string_view::npos) {
          cnt_len_str = headers.substr(cnt_start_i + k_content_len.size());
          cnt_len_str = cnt_len_str.substr(0, cnt_len_str.find('\n'));
          while (!cnt_len_str.empty() && cnt_len_str.front() == ' ') {
            cnt_len_str.remove_prefix(1);
          }
          while (!cnt_len_str.empty() && (cnt_len_str.back() == '\r' || cnt_len_str.front() == ' ')) {
            cnt_len_str.remove_suffix(1);
          }
        }
        if (!cnt_len_str.empty()) {
          size_t len = 0;
          auto res = std::from_chars(cnt_len_str.data(), cnt_len_str.data() + cnt_len_str.size(), len);
          if (res.ec == std::errc{}) {
            cnt_len = len;
          }
        }
      }
    } else {
      offset = out.size();
      while (offset > 0 && (out[offset - 1] == '\r' || out[offset - 1] == '\n')) {
        offset--;
      }
    }

    if (body_start > 0 && (!cnt_len || out.size() >= body_start + *cnt_len)) {
      // end
      break;
    }
  }
  return out;
}

std::string http_test_client::generate_req_head(server::http1::http_method method, std::string_view path) const {
  std::string req{as_string(method)};
  req += ' ';
  req += path;
  req += " HTTP/1.1\r\n";
  req += "Host: ";
  req += _host;
  req += "\r\n";

  return req;
}
