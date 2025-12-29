#include <server/http1/compression_negotiation.h>

#include <cctype>
#include <charconv>
#include <memory_resource>

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

  std::string_view after_q = q_part.substr(q_pos + 1);
  // Skip whitespace after 'q'
  while (!after_q.empty() && after_q.front() == ' ') {
    after_q.remove_prefix(1);
  }

  if (after_q.empty() || after_q.front() != '=') {
    return pref;
  }

  after_q.remove_prefix(1);  // Skip '='
  // Skip whitespace after '='
  while (!after_q.empty() && after_q.front() == ' ') {
    after_q.remove_prefix(1);
  }

  if (after_q.empty()) {
    return pref;
  }

  std::string_view q_value = after_q;
  // Remove trailing whitespace
  while (!q_value.empty() && q_value.back() == ' ') {
    q_value.remove_suffix(1);
  }

  float quality = 1.0F;
  const auto [ptr, ec] = std::from_chars(q_value.data(), q_value.data() + q_value.size(), quality);  // NOLINT(*-pointer-arithmetic)
  if (ec == std::errc{} && quality >= 0.0F && quality <= 1.0F) {
    pref.quality = quality;
  }

  return pref;
}

void compression_negotiator::parse_accept_encoding(std::string_view header, std::pmr::vector<encoding_preference>& preferences) noexcept {
  size_t pos = 0;
  while (pos < header.size()) {
    // Find the next comma
    const auto comma_pos = header.find(',', pos);
    const auto end_pos = comma_pos != std::string_view::npos ? comma_pos : header.size();

    std::string_view token = header.substr(pos, end_pos - pos);
    auto pref = parse_token(token);

    if (pref && pref->encoding != compression_encoding::none) {
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
      if (!encodings.empty()) {
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
