#include <server/http1/compression_negotiation.h>

#include <cctype>
#include <charconv>
#include <cstdint>
#include <memory_resource>
#include <string_view>

namespace server {

compression_negotiator::compression_negotiator(compression_pool::ptr pool) noexcept : _pool(std::move(pool)) {}

std::optional<compression_encoding> compression_negotiator::string_to_encoding(std::string_view str) noexcept {
  // Remove leading/trailing whitespace
  while (!str.empty() && str.front() == ' ') {
    str.remove_prefix(1);
  }
  while (!str.empty() && str.back() == ' ') {
    str.remove_suffix(1);
  }

#if SERVER_HAS_ZLIB
  if (str == "deflate") {
    return compression_encoding::deflate;
  }
  if (str == "gzip" || str == "x-gzip") {
    return compression_encoding::gzip;
  }
#endif

#if SERVER_HAS_ZSTD
  if (str == "zstd") {
    return compression_encoding::zstd;
  }
#endif

#if SERVER_HAS_BROTLI
  if (str == "br") {
    return compression_encoding::br;
  }
#endif

  if (str == "identity") {
    return compression_encoding::none;
  }

  if (str == "*") {
    return compression_encoding::any;
  }

  return std::nullopt;
}

std::optional<encoding_preference> compression_negotiator::parse_token(std::string_view token) noexcept {
  encoding_preference pref{.encoding = compression_encoding::none, .quality = 1.0F};

  // Find semicolon separating encoding from quality factor
  const auto semi_pos = token.find(';');
  std::string_view enc_part = semi_pos != std::string_view::npos ? token.substr(0, semi_pos) : token;

  // Try to parse encoding
  if (const auto enc = string_to_encoding(enc_part)) {
    pref.encoding = *enc;
  } else {
    return {};  // Unknown encoding
  }

  // Parse quality factor if present
  if (semi_pos == std::string_view::npos) {
    return pref;
  }

  std::string_view q_part = token.substr(semi_pos + 1);

  // Find 'q=' prefix
  const auto q_pos = q_part.find('q');
  if (q_pos == std::string_view::npos) {
    return pref;
  }

  const auto quality = parse_quality_float(q_part.substr(q_pos));
  pref.quality = quality.value_or(1.0F);

  return pref;
}

std::optional<float> compression_negotiator::parse_quality_float(std::string_view q_value) noexcept {
  if (q_value.empty() || q_value.front() != 'q') {
    return {};
  }

  std::string_view after_q = q_value.substr(1);
  // Skip whitespace after 'q'
  while (!after_q.empty() && after_q.front() == ' ') {
    after_q.remove_prefix(1);
  }

  if (after_q.empty() || after_q.front() != '=') {
    return {};
  }

  // remove first '='
  after_q = after_q.substr(1);

  // Skip whitespaces
  while (!after_q.empty() && after_q.front() == ' ') {
    after_q.remove_prefix(1);
  }

  if (after_q.empty()) {
    return {};
  }

  // not all compilers still have support of float std::from_chars thats why we parse it manually

  std::string_view decimal_part;
  bool is_one = false;

  switch (after_q.front()) {
    case '1': {
      if (after_q.size() == 1) {
        return 1.0F;
      }

      is_one = true;
      if (after_q[1] == '.') {
        // '1.xxx' format
        decimal_part = after_q.substr(2);
      }
      // wrong format
      break;
    }
    case '0': {
      if (after_q.size() == 1) {
        return 0.0F;
      }

      if (after_q[1] == '.') {
        // '0.xxx' format
        decimal_part = after_q.substr(2);
      }
      // wrong format
      break;
    }
    case '.': {
      // '.xxx' format
      decimal_part = after_q.substr(1);
    }
    default:
      // wrong format
      break;
  }

  if (decimal_part.empty()) {
    return {};
  }

  if (decimal_part.size() > 3) {
    // shrink decimal part to 3 numbers
    decimal_part = decimal_part.substr(0, 3);
  }

  uint32_t dec_value = 0;
  const auto [ptr, ec] = std::from_chars(decimal_part.data(), decimal_part.data() + decimal_part.size(), dec_value);  // NOLINT(*pointer-arithmetic)
  if (ec != std::errc{}) {
    return {};
  }

  if (is_one) {
    if (dec_value > 0) {
      // wrong format '1.x' where x > 0
      return {};
    }
    return 1.0F;
  }

  const auto num_decs = static_cast<uint32_t>(ptr - decimal_part.data());
  uint32_t delimiter = 1;
  for (uint32_t i = 0; i < num_decs; i++) {
    delimiter *= 10U;  // NOLINT(*magic-number*)
  }
  return float(dec_value) / float(delimiter);
}

void compression_negotiator::parse_accept_encoding(std::string_view header, std::pmr::vector<encoding_preference>& preferences) noexcept {
  size_t pos = 0;
  while (pos < header.size()) {
    // Find the next comma
    const auto comma_pos = header.find(',', pos);
    const auto end_pos = comma_pos != std::string_view::npos ? comma_pos : header.size();

    std::string_view token = header.substr(pos, end_pos - pos);
    auto pref = parse_token(token);

    if (pref) {
      preferences.push_back(*pref);
    }

    if (comma_pos == std::string_view::npos) {
      break;
    }
    pos = comma_pos + 1;
  }
}

size_t compression_negotiator::get_encoding_priority(compression_encoding enc) const noexcept {
  if (!_pool) {
    return 0;
  }

  const auto& encodings = _pool->get_config().encodings;
  size_t priority = encodings.size();
  for (const auto& config : encodings) {
    if (config.encoding == enc) {
      return priority;
    }
    priority--;
  }
  return 0;
}

compression_encoding compression_negotiator::negotiate(std::span<const encoding_preference> preferences) const noexcept {
  if (preferences.empty() || !_pool) {
    return compression_encoding::none;
  }

  // Find best match: prioritize by client quality, then by server priority
  compression_encoding best_encoding = compression_encoding::none;
  float best_quality = -1.0F;
  size_t best_priority = 0;

  for (const auto& pref : preferences) {
    // Handle wildcard
    if (pref.encoding == compression_encoding::any && pref.quality > 0.0F) {
      // Client accepts any encoding with this quality
      // Find the highest priority server encoding
      const auto& encodings = _pool->get_config().encodings;
      if (!encodings.empty() && pref.quality >= best_quality) {
        best_encoding = encodings.back().encoding;
        best_quality = pref.quality;
        best_priority = encodings.size();
      }
    } else if (pref.quality >= best_quality) {
      // Check if this is better than current best

      const auto enc_priority = get_encoding_priority(pref.encoding);

      if ((pref.quality > best_quality && enc_priority > 0) || enc_priority > best_priority) {
        best_encoding = pref.encoding;
        best_quality = pref.quality;
        best_priority = enc_priority;
      }
    }
  }

  // Only return an encoding if quality > 0
  if (best_quality > 0.0F) {
    return best_encoding;
  }

  return compression_encoding::none;
}

}  // namespace server
