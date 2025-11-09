#include <server/http1/response.h>
#include <server/utils/base64.h>
#include <server/utils/sha1.h>
#include <server/utils/static_string.h>
#include <server/web_socket/frame.h>
#include <server/web_socket/web_socket_session.h>

namespace server::web_socket {

async_coro::task<void> web_socket_session::run(const server::http1::request& handshake_request) {  // NOLINT(*complexity)
  if (_conn.is_closed()) {
    co_return;
  }

  constexpr std::string_view k_supported_version = "13";

  {
    using namespace http1;

    const auto* upgrade_header = handshake_request.find_header("Upgrade");
    const auto* connection_header = handshake_request.find_header("Connection");
    const auto* ws_key = handshake_request.find_header("Sec-WebSocket-Key");
    const auto* ws_version = handshake_request.find_header("Sec-WebSocket-Version");

    if (upgrade_header != nullptr && upgrade_header->second.find("websocket") == std::string_view::npos) {
      upgrade_header = nullptr;
    }
    if (connection_header != nullptr && connection_header->second.find("Upgrade") == std::string_view::npos) {
      connection_header = nullptr;
    }

    if (upgrade_header == nullptr || connection_header == nullptr) {
      response res{http_version::http_1_1};
      res.set_status(status_code::UpgradeRequired);
      co_await res.send(_conn);
      co_return;
    }

    if (ws_key == nullptr) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::BadRequest, .reason = static_string{"No Sec-WebSocket-Key."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }
    if (ws_version == nullptr) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::BadRequest, .reason = static_string{"No Sec-WebSocket-Version."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }

    std::string_view version_str = ws_version->second;
    while (!version_str.empty() && version_str.front() == ' ') {
      version_str.remove_prefix(1);
    }
    while (!version_str.empty() && version_str.back() == ' ') {
      version_str.remove_suffix(1);
    }

    bool version_matched = true;
    if (version_str != k_supported_version) {
      version_matched = false;

      auto ver_pos = version_str.find(k_supported_version);
      if (ver_pos != std::string_view::npos) {
        ver_pos += k_supported_version.size();
        if (ver_pos == version_str.size()) {
          version_matched = true;
        } else {
          version_matched = ver_pos < version_str.size() && (version_str[ver_pos] == ' ' || version_str[ver_pos] == ',');
        }
      }
    }

    if (!version_matched) {
      response res{http_version::http_1_1};

      res.set_status(http_error{.status_code = status_code::BadRequest, .reason = static_string{"Sec-WebSocket-Version"}});
      res.add_header(static_string{"Sec-WebSocket-Version"}, static_string{k_supported_version});
      co_await res.send(_conn);
      co_return;
    }

    constexpr std::string_view k_salt = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    constexpr auto sha1_size = sha1_hash::sha1_str_buffer_t{}.size();

    const auto key_size = ws_key->second.size() + k_salt.size();
    const auto key_buffer_size = base64_encoder::get_buffer_size(key_size);  // always bigger than key_size

    std::string key_str;
    key_str.reserve(key_buffer_size);
    key_str.append(ws_key->second);
    key_str.append(k_salt);

    auto hash_str = sha1_hash{key_str}.get_value();

    key_str.resize(key_buffer_size);
    base64_encoder enc{false};
    if (!enc.encode_to_buffer(key_str, hash_str)) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::InternalServerError, .reason = static_string{"Can't encode base64 Sec-WebSocket-Key."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }

    response res{http_version::http_1_1};
    res.set_status(status_code::SwitchingProtocols);
    res.add_header(static_string{"Connection"}, static_string{"Upgrade"});
    res.add_header(static_string{"Upgrade"}, static_string{"websocket"});
    res.add_header(static_string{"Sec-WebSocket-Accept"}, static_string{key_str});

    co_await res.send(_conn);
  }
  // connection was open

  while (true) {
    frame_struct frame_data{.buffer = {}};
    auto res = co_await _conn.read_buffer(frame_data.buffer);
    if (!res) {
      // error
      co_return;
    }
    frame frame_d{frame_data, res.value()};
  }

  co_return;
}

}  // namespace server::web_socket
