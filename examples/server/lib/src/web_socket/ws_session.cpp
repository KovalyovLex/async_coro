#include <server/http1/response.h>
#include <server/utils/base64.h>
#include <server/utils/sha1.h>
#include <server/utils/static_string.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <cstring>
#include <memory>

namespace server::web_socket {

std::string ws_session::get_web_socket_key_result(std::string_view client_key) {
  constexpr std::string_view k_salt = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  constexpr auto sha1_size = decltype(sha1_hash{}.get_bytes()){}.size();

  const auto key_size = client_key.size() + k_salt.size();
  const auto key_buffer_size = std::max(base64_encoder::get_buffer_size(sha1_size), key_size);

  std::string key_str;
  key_str.reserve(key_buffer_size);
  key_str.append(client_key);
  key_str.append(k_salt);

  auto digest = sha1_hash{key_str}.get_bytes();

  key_str.resize(base64_encoder::get_buffer_size(sha1_size));
  base64_encoder enc{false};

  {
    auto* end = enc.encode_to_buffer(key_str, digest);
    // remove extra paddings
    while ((key_str.data() + key_str.size()) > end) {  // NOLINT(*pointer*)
      key_str.pop_back();
    }
  }

  return key_str;
}

async_coro::task<void> ws_session::run(const server::http1::request& handshake_request, std::string_view protocol, message_handler_t handler) {  // NOLINT(*complexity)
  ASYNC_CORO_ASSERT(handler);

  if (_conn.is_closed() || !handler) {
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
      res.set_status(status_code::upgrade_required);
      co_await res.send(_conn);
      co_return;
    }

    if (ws_key == nullptr) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::bad_request, .reason = static_string{"No Sec-WebSocket-Key."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }
    if (ws_version == nullptr) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::bad_request, .reason = static_string{"No Sec-WebSocket-Version."}};
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

      res.set_status(http_error{.status_code = status_code::bad_request, .reason = static_string{"Sec-WebSocket-Version"}});
      res.add_header(static_string{"Sec-WebSocket-Version"}, static_string{k_supported_version});
      co_await res.send(_conn);
      co_return;
    }

    std::string key_str = get_web_socket_key_result(ws_key->second);

    if (key_str.empty()) {
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::internal_server_error, .reason = static_string{"Can't encode base64 Sec-WebSocket-Key."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }

    if (!protocol.empty()) {
      if (!handshake_request.has_value_in_header("Sec-WebSocket-Protocol", protocol)) {
        // unsupported protocol
        response res{http_version::http_1_1};
        http_error error{.status_code = status_code::method_not_allowed, .reason = static_string{"Not allowed Sec-WebSocket-Protocol."}};
        res.set_status(std::move(error));
        co_await res.send(_conn);
        co_return;
      }
    } else if (const auto* prot_pair = handshake_request.find_header("Sec-WebSocket-Protocol"); prot_pair != nullptr && !prot_pair->second.empty()) {
      // unsupported protocol
      response res{http_version::http_1_1};
      http_error error{.status_code = status_code::method_not_allowed, .reason = static_string{"Not allowed Sec-WebSocket-Protocol."}};
      res.set_status(std::move(error));
      co_await res.send(_conn);
      co_return;
    }

    response res{http_version::http_1_1};
    res.set_status(status_code::switching_protocols);
    res.add_header(static_string{"Connection"}, static_string{"Upgrade"});
    res.add_header(static_string{"Upgrade"}, static_string{"websocket"});
    res.add_header(static_string{"Sec-WebSocket-Accept"}, static_string{key_str});

    co_await res.send(_conn);
  }
  // connection was open

  bool receiving_fragments = false;
  std::vector<request_frame> fragments;

  while (true) {
    frame_begin frame_beg;
    auto res = co_await _conn.read_buffer(frame_beg.buffer);
    if (!res) {
      if (_conn.is_closed()) {
        co_return;
      }
      web_socket::ws_error err{
          ws_status_code::policy_violation,
          res.error()};
      co_await response_frame::send_error_and_close_connection(_conn, err);
      co_return;
    }

    auto frame_res = request_frame::make_frame(frame_beg, res.value());
    if (!frame_res) {
      co_await response_frame::send_error_and_close_connection(_conn, frame_res.error());
      co_return;
    }

    auto& frame_s = frame_res->frame;

    if (!frame_s.mask) {
      web_socket::ws_error err{
          ws_status_code::protocol_error,
          "All client requests should be masked"};
      co_await response_frame::send_error_and_close_connection(_conn, err);
      co_return;
    }

    if (frame_s.opcode_dec >= k_control_codes_begin) {
      if (!frame_s.is_final) {
        web_socket::ws_error err{
            ws_status_code::protocol_error,
            "Control frames are unfragmentable"};
        co_await response_frame::send_error_and_close_connection(_conn, err);
        co_return;
      }
    }

    if (frame_s.get_op_code() == ws_op_code::ping) {
      co_await frame_s.read_payload(_conn, frame_res->rest_data_in_buffer);

      response_frame resp{ws_op_code::pong};
      auto res = co_await resp.send_data(_conn, std::span{frame_s.payload.get(), frame_s.payload_length});
      if (!res) {
        if (_conn.is_closed()) {
          co_return;
        }

        web_socket::ws_error err{
            ws_status_code::policy_violation,
            res.error()};
        co_await response_frame::send_error_and_close_connection(_conn, err);
        co_return;
      }

      continue;
    }
    if (frame_s.get_op_code() == ws_op_code::pong) {
      continue;
    }

    co_await frame_s.read_payload(_conn, frame_res->rest_data_in_buffer);

    if (frame_s.get_op_code() == ws_op_code::connection_close) {
      co_await response_frame::close_connection(_conn);
      co_return;
    }

    if (frame_s.get_op_code() == ws_op_code::continuation) {
      if (!receiving_fragments) {
        web_socket::ws_error err{
            ws_status_code::protocol_error,
            "First frame cant be continuation"};
        co_await response_frame::send_error_and_close_connection(_conn, err);
        co_return;
      }

      if (frame_s.is_final) {
        receiving_fragments = false;

        size_t all_data_size = frame_s.payload_length;
        for (const auto& frame : fragments) {
          all_data_size += frame.payload_length;
        }
        auto final_buffer = std::make_unique<std::byte[]>(all_data_size);  // NOLINT(*c-array*)

        for (auto& frame : fragments) {
          std::memcpy(final_buffer.get(), frame.payload.get(), frame.payload_length);
          frame.payload = nullptr;
        }
        // copy last frame data
        std::memcpy(final_buffer.get(), frame_s.payload.get(), frame_s.payload_length);
        frame_s.payload = nullptr;

        if (!fragments.empty()) {
          // override frame
          frame_s = std::move(fragments.front());
          fragments.clear();
        }

        frame_s.payload = std::move(final_buffer);
        frame_s.payload_length = all_data_size;
        frame_s.is_final = true;
      } else {
        fragments.emplace_back(std::move(frame_s));
      }
      continue;
    }

    if (!frame_s.is_final) {
      if (receiving_fragments) {
        web_socket::ws_error err{
            ws_status_code::protocol_error,
            "Non final frame should be continuation"};
        co_await response_frame::send_error_and_close_connection(_conn, err);
        co_return;
      }

      receiving_fragments = true;
      fragments.emplace_back(std::move(frame_s));
      continue;
    }

    // common process of application frame
    co_await handler(frame_s, *this);
  }

  co_return;
}

}  // namespace server::web_socket
