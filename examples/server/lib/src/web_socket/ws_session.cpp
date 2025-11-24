#include <server/http1/response.h>
#include <server/utils/base64.h>
#include <server/utils/has_zlib.h>
#include <server/utils/sha1.h>
#include <server/utils/static_string.h>
#include <server/web_socket/request_frame.h>
#include <server/web_socket/response_frame.h>
#include <server/web_socket/ws_error.h>
#include <server/web_socket/ws_extension_parser.h>
#include <server/web_socket/ws_op_code.h>
#include <server/web_socket/ws_session.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

#include "async_coro/config.h"

#if SERVER_HAS_ZLIB
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_compression_constants.h>
#include <server/utils/zlib_decompress.h>
#endif

namespace server::web_socket {

#if SERVER_HAS_ZLIB

// Decompress incoming frame payload when permessage-deflate is enabled
static expected<void, std::string> decompress_frame_payload(std::vector<std::byte>& output_buffer,
                                                            zlib_decompress& decompressor,
                                                            std::span<const std::byte> compressed_data,
                                                            bool no_context_takeover) {
  output_buffer.reserve(compressed_data.size() * 2);  // Initial estimate

  std::array<std::byte, 1024U> tmp_buffer;  // NOLINT(*init*, *magic*)
  std::span<std::byte> data_out = tmp_buffer;

  while (!compressed_data.empty()) {
    if (!decompressor.update_stream(compressed_data, data_out)) {
      return expected<void, std::string>{unexpect, std::string{"Decompression failed"}};
    }

    std::span data_to_copy{tmp_buffer.data(), data_out.data()};
    output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());

    data_out = tmp_buffer;
  }

  if (no_context_takeover) {
    bool has_data = true;
    while (has_data) {
      has_data = decompressor.end_stream(compressed_data, data_out);
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());
    }
  } else {
    std::array<std::byte, 4> flush_trailer{std::byte(0x00U), std::byte(0x00U), std::byte(0xFFU), std::byte(0xFFU)};  // NOLINT(*magic*)
    std::span<const std::byte> data{flush_trailer};

    bool has_data = true;
    while (has_data) {
      has_data = decompressor.flush(data, data_out);
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());
    }
  }

  return {};
}

// Compress outgoing frame payload when permessage-deflate is enabled
static expected<void, std::string> compress_frame_payload(std::vector<std::byte>& output_buffer,
                                                          zlib_compress& compressor,
                                                          std::span<const std::byte> raw_data,
                                                          bool last_frame,
                                                          bool no_context_takeover) {
  std::array<std::byte, 1024U> tmp_buffer;  // NOLINT(*init*, *magic*)

  while (!raw_data.empty()) {
    std::span<std::byte> data_out = tmp_buffer;
    if (!compressor.update_stream(raw_data, data_out)) {
      return expected<void, std::string>{unexpect, std::string{"Decompression failed"}};
    }

    std::span data_to_copy{tmp_buffer.data(), data_out.data()};
    output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());
  }

  if (no_context_takeover) {
    // If this is the last frame, end the stream and append the suffix
    if (last_frame) {
      std::span<std::byte> data_out = tmp_buffer;
      bool has_data = true;
      while (has_data) {
        has_data = compressor.end_stream(raw_data, data_out);
        std::span data_to_copy{tmp_buffer.data(), data_out.data()};
        output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());
      }
    }
  } else {
    std::span<std::byte> data_out = tmp_buffer;
    bool has_data = true;
    while (has_data) {
      has_data = compressor.flush(raw_data, data_out);
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      output_buffer.insert(output_buffer.end(), data_to_copy.begin(), data_to_copy.end());
    }

    if (last_frame) {
      data_out = data_out.subspan(0, data_out.size() - 4);
    }
  }

  return {};
}

#endif

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

#if SERVER_HAS_ZLIB
    if (is_permessage_deflate_enabled()) {
      // Parse and handle Sec-WebSocket-Extensions header for permessage-deflate
      handshake_request.foreach_header_with_name("Sec-WebSocket-Extensions", [this](const auto& ext_pair) {
        if (_used_config) {
          return;
        }

        if (auto deflate_config = parse_permessage_deflate_extension(ext_pair.second)) {
          // Extension is supported and negotiated, enable it
          _used_config = permessage_deflate_config::get_negotiated_config(*_allowed_config, *deflate_config);
        }
      });

      if (_used_config) {
        _compressor = zlib_compress{zlib::compression_method::deflate, zlib::window_bits{_used_config->server_max_window_bits}};
        _decompressor = zlib_decompress{zlib::compression_method::deflate, zlib::window_bits{_used_config->client_max_window_bits}};
      }
    }
#endif

    response res{http_version::http_1_1};
    res.set_status(status_code::switching_protocols);
    res.add_header(static_string{"Connection"}, static_string{"Upgrade"});
    res.add_header(static_string{"Upgrade"}, static_string{"websocket"});
    res.add_header(static_string{"Sec-WebSocket-Accept"}, static_string{key_str});

#if SERVER_HAS_ZLIB
    // Add Sec-WebSocket-Extensions header to response if permessage-deflate is enabled
    std::string deflate_ext;
    if (is_permessage_deflate_used()) {
      deflate_ext = _used_config->get_extension_string();
      res.add_header(static_string{"Sec-WebSocket-Extensions"}, static_string{deflate_ext});
    }
#endif

    co_await res.send(_conn);
  }
  // connection was open

  bool receiving_fragments = false;
  std::vector<request_frame> fragments;

#if SERVER_HAS_ZLIB
  std::vector<std::byte> decompress_buffer;
#endif

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
      co_await this->send_data(resp, std::span{frame_s.payload.get(), frame_s.payload_length});
      continue;
    }
    if (frame_s.get_op_code() == ws_op_code::pong) {
      continue;
    }

    co_await frame_s.read_payload(_conn, frame_res->rest_data_in_buffer);

#if SERVER_HAS_ZLIB
    // Decompress payload if permessage-deflate is enabled and RSV1 is set
    if (frame_s.rsv1 && frame_s.opcode_dec < k_control_codes_begin && is_permessage_deflate_used()) {
      frame_s.rsv1 = false;  // Clear RSV1 after decompression
      decompress_buffer.clear();
      auto decompress_result = decompress_frame_payload(decompress_buffer, _decompressor, std::span{frame_s.payload.get(), frame_s.payload_length}, _used_config->client_no_context_takeover);
      if (!decompress_result) {
        web_socket::ws_error err{ws_status_code::protocol_error, decompress_result.error()};
        co_await response_frame::send_error_and_close_connection(_conn, err);
        co_return;
      }

      // Replace compressed payload with decompressed one
      frame_s.payload.reset();

      auto new_buffer = std::make_unique<std::byte[]>(decompress_buffer.size());  // NOLINT(*c-array*)
      std::memcpy(new_buffer.get(), decompress_buffer.data(), decompress_buffer.size());

      frame_s.payload = std::move(new_buffer);
      frame_s.payload_length = decompress_buffer.size();
    }
#endif

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

async_coro::task<void> ws_session::send_data(const response_frame& res_frame, std::span<const std::byte> data) {
  return send_data_impl(res_frame, data, true, res_frame.get_op_code_dec());
}

async_coro::task<void> ws_session::begin_fragmented_send(const response_frame& res_frame, std::span<const std::byte> data) {
  return send_data_impl(res_frame, data, false, res_frame.get_op_code_dec());
}

async_coro::task<void> ws_session::continue_fragmented_send(const response_frame& res_frame, std::span<const std::byte> data, bool last_chunk) {
  return send_data_impl(res_frame, data, last_chunk, static_cast<uint8_t>(ws_op_code::continuation));
}

async_coro::task<void> ws_session::send_data_impl(const response_frame& res_frame, std::span<const std::byte> data, bool last_chunk, uint8_t dec_code) {
  using data_buf_on_stack = std::array<std::byte, 1024UL * 4U>;  // NOLINT(*magic*)

  // Compress data if permessage-deflate is enabled and this is a data frame
  std::span<const std::byte> payload_to_send = data;

  union frame_union {  // NOLINT(*init*)
    data_buf_on_stack buffer;
    frame_base frame;
  };

  frame_union frame{.frame = {last_chunk,
                              dec_code,
                              res_frame.rsv1,
                              res_frame.rsv2,
                              res_frame.rsv3}};

#if SERVER_HAS_ZLIB
  std::vector<std::byte> compressed_data_buffer;

  // Compress only for data frames (not control frames) if compression is enabled
  if (is_permessage_deflate_used() && dec_code < k_control_codes_begin) {
    auto compress_result = compress_frame_payload(compressed_data_buffer, _compressor, data, last_chunk, _used_config->server_no_context_takeover);
    if (!compress_result) {
      if (!_conn.is_closed()) {
        ws_error error{ws_status_code::policy_violation, compress_result.error()};
        co_await response_frame::send_error_and_close_connection(_conn, error);
      }
      co_return;
    }

    payload_to_send = compressed_data_buffer;

    if (!compressed_data_buffer.empty()) {
      // Set RSV1 bit if compression was applied
      frame.frame.set_rsv1(true);
    } else if (!last_chunk) {
      // all data was cached and frame are empty. No need to send it at all
      co_return;
    }
  }
#endif

  std::span<std::byte> buffer_after_frame{frame.buffer};
  buffer_after_frame = buffer_after_frame.subspan(sizeof(frame_base));

  response_frame::fill_frame_size(buffer_after_frame, frame.frame, payload_to_send.size());

  const auto count_to_copy = std::min(buffer_after_frame.size(), payload_to_send.size());
  std::memcpy(buffer_after_frame.data(), payload_to_send.data(), count_to_copy);

  buffer_after_frame = buffer_after_frame.subspan(count_to_copy);
  payload_to_send = payload_to_send.subspan(count_to_copy);

  auto res = co_await _conn.write_buffer({frame.buffer.data(), buffer_after_frame.data()});
  if (!res) {
    if (!_conn.is_closed()) {
      ws_error error{ws_status_code::policy_violation, res.error()};
      co_await response_frame::send_error_and_close_connection(_conn, error);
    }
    co_return;
  }

  if (!payload_to_send.empty()) {
    res = co_await _conn.write_buffer(payload_to_send);
    if (!res) {
      if (!_conn.is_closed()) {
        ws_error error{ws_status_code::policy_violation, res.error()};
        co_await response_frame::send_error_and_close_connection(_conn, error);
      }
      co_return;
    }
  }
}

#if SERVER_HAS_ZLIB

void ws_session::set_permessage_deflate(std::optional<permessage_deflate_config> config) noexcept {
  _allowed_config = config;
}

#endif

}  // namespace server::web_socket
