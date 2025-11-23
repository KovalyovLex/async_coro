#include <server/web_socket/ws_extension_parser.h>

#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>

namespace server::web_socket {

namespace {

// Trim whitespace from the beginning and end of a string view
std::string_view trim(std::string_view str) {
  // Trim from start
  while (!str.empty() && str.front() == ' ') {
    str.remove_prefix(1);
  }

  // Trim from end
  while (!str.empty() && str.back() == ' ') {
    str.remove_suffix(1);
  }
  return str;
}

// Parse a single window_bits parameter value
std::optional<uint8_t> parse_window_bits(std::string_view value_str) {
  value_str = trim(value_str);

  if (value_str.empty()) {
    return std::nullopt;
  }

  uint8_t value = 0;
  auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), value);  // NOLINT(*pointer*)

  if (result.ec != std::errc{} || result.ptr != (value_str.data() + value_str.size())) {  // NOLINT(*pointer*)
    return std::nullopt;
  }

  // Validate window bits range (8-15 per RFC 7692)
  constexpr uint8_t k_min_window_bits = 8U;   // NOLINT(*magic-numbers*)
  constexpr uint8_t k_max_window_bits = 15U;  // NOLINT(*magic-numbers*)

  if (value < k_min_window_bits || value > k_max_window_bits) {
    return std::nullopt;
  }

  return value;
}

// Parse extension parameters and update config
bool parse_single_param(std::string_view param, permessage_deflate_config& config) {
  param = trim(param);
  if (param.empty()) {
    return true;
  }

  size_t equals_pos = param.find('=');
  std::string_view param_name = trim(param.substr(0, equals_pos));

  if (equals_pos != std::string_view::npos) {
    std::string_view param_value = param.substr(equals_pos + 1);

    // Parse parameter based on name
    if (param_name == "server_max_window_bits") {
      auto bits = parse_window_bits(param_value);
      return bits.has_value() && (config.server_max_window_bits = bits.value(), true);
    }
    if (param_name == "client_max_window_bits") {
      auto bits = parse_window_bits(param_value);
      return bits.has_value() && (config.client_max_window_bits = bits.value(), true);
    }
  } else {
    // Parameter without value (flag-style)
    if (param_name == "server_no_context_takeover") {
      config.server_no_context_takeover = true;
    } else if (param_name == "client_no_context_takeover") {
      config.client_no_context_takeover = true;
    }
  }

  return true;
}

// Parse extension parameters and update config
bool parse_permessage_deflate_params(std::string_view params_str, permessage_deflate_config& config) {
  // Split parameters by semicolon
  size_t param_start = 0;
  while (param_start < params_str.size()) {
    size_t param_end = params_str.find(';', param_start);
    if (param_end == std::string_view::npos) {
      param_end = params_str.size();
    }

    std::string_view param = params_str.substr(param_start, param_end - param_start);

    if (!parse_single_param(param, config)) {
      return false;
    }

    param_start = param_end + 1;
  }

  return true;
}

}  // namespace

std::optional<permessage_deflate_config> parse_permessage_deflate_extension(std::string_view extensions_header) {
  permessage_deflate_config config;

  // Split by commas to find different extensions
  size_t extension_start = 0;
  while (extension_start < extensions_header.size()) {
    size_t extension_end = extensions_header.find(',', extension_start);
    if (extension_end == std::string_view::npos) {
      extension_end = extensions_header.size();
    }

    std::string_view extension = trim(extensions_header.substr(extension_start, extension_end - extension_start));

    // Split extension name from parameters by semicolon
    size_t semicolon_pos = extension.find(';');
    std::string_view extension_name = (semicolon_pos != std::string_view::npos)
                                          ? trim(extension.substr(0, semicolon_pos))
                                          : trim(extension);

    // Check if this is the permessage-deflate extension
    if (extension_name == "permessage-deflate") {
      // Parse parameters
      if (semicolon_pos != std::string_view::npos) {
        std::string_view params_str = extension.substr(semicolon_pos + 1);
        if (!parse_permessage_deflate_params(params_str, config)) {
          return std::nullopt;
        }
      }

      return config;
    }

    extension_start = extension_end + 1;
  }

  // permessage-deflate not found
  return std::nullopt;
}

std::optional<permessage_deflate_config> permessage_deflate_config::get_negotiated_config(permessage_deflate_config server, permessage_deflate_config client) {  // NOLINT(*swappable*)
  if (server.client_max_window_bits < client.client_max_window_bits) {
    // client will going to use greater sliding windows that we accept. Refuse connection.
    return std::nullopt;
  }
  permessage_deflate_config conf = client;

  conf.server_max_window_bits = std::min(server.server_max_window_bits, client.server_max_window_bits);

  if (server.server_no_context_takeover) {
    conf.server_no_context_takeover = true;
  }

  return conf;
}

std::string permessage_deflate_config::get_extension_string() const {
  std::string deflate_ext = "permessage-deflate";

  if (server_no_context_takeover) {
    deflate_ext += "; server_no_context_takeover";
  }
  if (client_no_context_takeover) {
    deflate_ext += "; client_no_context_takeover";
  }

  std::array<char, 4> str;  // NOLINT(*init*)
  if (server_max_window_bits != k_default_window_bits) {
    deflate_ext += "; server_max_window_bits=";

    auto result = std::to_chars(str.data(), str.data() + str.size(), server_max_window_bits);  // NOLINT(*pointer*)

    if (result.ec != std::errc{}) {
      std::string_view str_v{str.data(), result.ptr};
      deflate_ext += deflate_ext;
    }
  }

  if (client_max_window_bits != k_default_window_bits) {
    deflate_ext += "; client_max_window_bits=";

    auto result = std::to_chars(str.data(), str.data() + str.size(), client_max_window_bits);  // NOLINT(*pointer*)

    if (result.ec != std::errc{}) {
      std::string_view str_v{str.data(), result.ptr};
      deflate_ext += deflate_ext;
    }
  } else {
    deflate_ext += "; client_max_window_bits";
  }

  return deflate_ext;
}

}  // namespace server::web_socket
