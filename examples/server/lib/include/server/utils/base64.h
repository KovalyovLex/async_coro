#pragma once

#include <server/utils/base64_decode_table.h>
#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace server {

namespace details {

template <std::size_t N>
struct t_string : public std::array<char, N> {
  static constexpr size_t k_str_size = N;

  constexpr t_string() noexcept = default;

  constexpr t_string(char const (&str)[N]) noexcept  // NOLINT(*explicit*, *array*)
  {
    auto& data = static_cast<std::array<char, N>&>(*this);

    for (size_t i = 0; i != N; i++) {
      data[i] = str[i];  // NOLINT(*array*)
    }
  }

  [[nodiscard]] constexpr std::string_view get_string_view() const& noexcept {
    auto size = this->size();
    while (size > 0 && this->operator[](size - 1) == '\0') {
      size--;
    }
    return {this->data(), size};
  }
};

}  // namespace details

class base64_encoder {
  static constexpr std::string_view k_encode_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  static constexpr std::string_view k_encode_table_url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  // NOLINTBEGIN(*pointer*, *magic*)
  template <class T>
    requires(sizeof(T) == 1)
  constexpr char* encode_to_buffer_impl(std::span<char> out_buffer, std::span<const T> data) noexcept {
    const size_t encoded_size = get_buffer_size(data.size());

    auto* out = out_buffer.data();

    if (out_buffer.size() < encoded_size) {
      return out;
    }

    const auto* bytes_or_chars = data.data();

    uint32_t quad = 0;
    auto len = data.size();
    for (; len >= 3; len -= 3, bytes_or_chars += 3) {
      quad = (uint32_t(uint8_t(bytes_or_chars[0])) << 16U) | (uint32_t(uint8_t(bytes_or_chars[1])) << 8U) | uint8_t(bytes_or_chars[2]);
      *out++ = encode_table[quad >> 18U];
      *out++ = encode_table[(quad >> 12U) & 63U];
      *out++ = encode_table[(quad >> 6U) & 63U];
      *out++ = encode_table[quad & 63U];
    }

    if (len != 0) {
      quad = uint32_t(uint8_t(bytes_or_chars[0])) << 16U;
      *out++ = encode_table[quad >> 18U];
      if (len == 2) {
        quad |= uint32_t(uint8_t(bytes_or_chars[1])) << 8U;
        *out++ = encode_table[(quad >> 12U) & 63U];
        *out++ = encode_table[(quad >> 6U) & 63U];
        if (padding != '\0') {
          *out++ = padding;
        }
      } else {
        *out++ = encode_table[(quad >> 12U) & 63U];
        if (padding != '\0') {
          *out++ = padding;
          *out++ = padding;
        }
      }
    }

    return out;
  }
  // NOLINTEND(*pointer*, *magic*)

 public:
  explicit constexpr base64_encoder(bool url_encode) noexcept
      : encode_table(url_encode ? k_encode_table_url : k_encode_table),
        padding(url_encode ? '\0' : '=') {};

  // Encodes data to buffer and returns end of buffer. Is size not enough end will be same as begin
  constexpr char* encode_to_buffer(std::span<char> out_buffer, std::span<const std::byte> data) noexcept {
    return this->encode_to_buffer_impl<std::byte>(out_buffer, data);
  }

  // Encodes data to buffer and returns end of buffer. Is size not enough end will be same as begin
  constexpr char* encode_to_buffer(std::span<char> out_buffer, std::span<const char> data) noexcept {
    return this->encode_to_buffer_impl<char>(out_buffer, data);
  }

  std::string encode(std::span<std::byte> data) {
    std::string res(get_buffer_size(data.size()), padding);

    const auto* end = encode_to_buffer(res, data);
    // remove extra paddings
    while ((res.data() + res.size()) > end) {  // NOLINT(*pointer*)
      res.pop_back();
    }
    return res;
  }

  std::string encode(std::string_view data) {
    std::string res(get_buffer_size(data.size()), padding);

    const auto* end = encode_to_buffer(res, data);
    // remove extra paddings
    while ((res.data() + res.size()) > end) {  // NOLINT(*pointer*)
      res.pop_back();
    }
    return res;
  }

  static constexpr size_t get_buffer_size(size_t data_size) noexcept {
    return (data_size / 3 + size_t(data_size % 3 > 0)) << 2U;
  }

 private:
  std::string_view encode_table;
  char padding;
};

class base64_decoder {
  using decode_table_t = details::base64_decode_table_t;
  static constexpr uint32_t k_invalid_sym = details::k_base64_invalid_sym;

  // NOLINTBEGIN(*pointer*, *magic*, *array-index*)
  template <class T, bool strict_check>
    requires(sizeof(T) == 1)
  constexpr size_t decode_to_buffer_impl(std::span<T> out_buffer, std::span<const char> data, const decode_table_t& decode_table) noexcept {  // NOLINT(*complexity*)
    const size_t encoded_size = get_buffer_size(data.size());
    auto* out = out_buffer.data();

    if (out_buffer.size() < encoded_size) {
      return 0;
    }

    const auto* str_data = data.data();

    uint32_t quad = 0;
    auto len = data.size();
    while (len > 0 && decode_table[uint8_t(str_data[len - 1])] == k_invalid_sym) {
      len--;
    }
    for (; len >= 4; len -= 4) {
      {
        if constexpr (strict_check) {
          if (decode_table[uint8_t(str_data[0])] == k_invalid_sym ||
              decode_table[uint8_t(str_data[1])] == k_invalid_sym ||
              decode_table[uint8_t(str_data[2])] == k_invalid_sym ||
              decode_table[uint8_t(str_data[3])] == k_invalid_sym) {
            return 0;
          }
        }
        quad = decode_table[uint8_t(*str_data++)] << 6U;
        quad |= decode_table[uint8_t(*str_data++)];
        quad = quad << 6U;
        quad |= decode_table[uint8_t(*str_data++)];
        quad = quad << 6U;
        quad |= decode_table[uint8_t(*str_data++)];
      }

      *out++ = T(quad >> 16U);
      *out++ = T(quad >> 8U);
      *out++ = T(quad);
    }

    if (len != 0) {
      if (len == 1) {
        return 0;
      }

      if constexpr (strict_check) {
        if (decode_table[uint8_t(str_data[0])] == k_invalid_sym ||
            decode_table[uint8_t(str_data[1])] == k_invalid_sym) {
          return 0;
        }
      }

      quad = decode_table[uint8_t(*str_data++)] << 6U;
      quad |= decode_table[uint8_t(*str_data++)];
      quad = quad << 6U;

      if (len == 2) {
        quad = quad << 6U;
        *out++ = T(quad >> 16U);
      } else {
        if constexpr (strict_check) {
          if (decode_table[uint8_t(str_data[0])] == k_invalid_sym) {
            return 0;
          }
        }

        quad |= decode_table[uint8_t(*str_data++)];
        quad = quad << 6U;

        *out++ = T(quad >> 16U);
        *out++ = T(quad >> 8U);
      }
    }

    return size_t(out - out_buffer.data());
  }
  // NOLINTEND(*pointer*, *magic*, *array-index*)

 public:
  enum class decode_policy : uint8_t {
    // Checks whole string on valid base64 alphabet. In case of invalid symbol in the base64 string found, length of the result buffer will be zero
    strict_base64,
    // Checks whole string on valid base64 URL Safe alphabet. In case of invalid symbol in the base64 string found, length of the result buffer will be zero
    strict_base64_url,
    // No Checks on alphabet, decodes both base64 and base64_url encoded strings
    universal
  };

  explicit constexpr base64_decoder(decode_policy decode) noexcept
      : _decode_policy(decode) {};

  // Returns length of decoded buffer
  constexpr size_t decode_to_buffer(std::span<std::byte> out_buffer, std::span<const char> data) noexcept {
    if (_decode_policy == decode_policy::universal) {
      return this->decode_to_buffer_impl<std::byte, false>(out_buffer, data, details::k_base64_decode_table_universal);
    }
    if (_decode_policy == decode_policy::strict_base64) {
      return this->decode_to_buffer_impl<std::byte, true>(out_buffer, data, details::k_base64_decode_table_strict);
    }
    if (_decode_policy == decode_policy::strict_base64_url) {
      return this->decode_to_buffer_impl<std::byte, true>(out_buffer, data, details::k_base64_decode_table_strict_url);
    }
    return 0;
  }

  // Returns length of decoded buffer
  constexpr size_t decode_to_buffer(std::span<char> out_buffer, std::span<const char> data) noexcept {
    if (_decode_policy == decode_policy::universal) {
      return this->decode_to_buffer_impl<char, false>(out_buffer, data, details::k_base64_decode_table_universal);
    }
    if (_decode_policy == decode_policy::strict_base64) {
      return this->decode_to_buffer_impl<char, true>(out_buffer, data, details::k_base64_decode_table_strict);
    }
    if (_decode_policy == decode_policy::strict_base64_url) {
      return this->decode_to_buffer_impl<char, true>(out_buffer, data, details::k_base64_decode_table_strict_url);
    }
    return 0;
  }

  std::vector<std::byte> decode(std::span<const char> data) {
    std::vector<std::byte> res;
    res.resize(get_buffer_size(data.size()));

    const auto length = decode_to_buffer(res, data);

    // remove extra paddings
    while (res.size() > length) {  // NOLINT(*pointer*)
      res.pop_back();
    }
    return res;
  }

  std::string decode_str(std::span<const char> data) {
    std::string res;
    res.resize(get_buffer_size(data.size()));

    const auto length = decode_to_buffer({reinterpret_cast<std::byte*>(res.data()), res.size()}, data);  // NOLINT(*reinterpret-cast*)

    // remove extra paddings
    res.resize(length);

    return res;
  }

  static constexpr size_t get_buffer_size(size_t data_size) noexcept {
    return data_size * 3 / 4;
  }

 private:
  decode_policy _decode_policy;
};

inline std::string base64_encode(std::span<const std::byte> input) {
  base64_encoder enc{false};
  return enc.encode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

inline std::string base64_encode(std::string_view input) {
  base64_encoder enc{false};
  return enc.encode(input);
}

inline std::string base64_url_encode(std::span<const std::byte> input) {
  base64_encoder enc{true};
  return enc.encode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

inline std::string base64_url_encode(std::string_view input) {
  base64_encoder enc{true};
  return enc.encode(input);
}

// Makes constexpr array of chars that is base64 encoded string without null termination
template <details::t_string str>
consteval auto operator""_base64() noexcept {
  constexpr auto str_size = decltype(str)::k_str_size;

  details::t_string<base64_encoder::get_buffer_size(str_size)> data_out{};

  base64_encoder enc{false};
  // avoid null termination char
  if (!enc.encode_to_buffer(data_out, {str.data(), str_size - 1})) {
    data_out = {};
  }

  return data_out;
}

// Makes constexpr array of chars that is ULR safe base64 encoded string without null termination
template <details::t_string str>
consteval auto operator""_base64_url() noexcept {
  constexpr auto str_size = decltype(str)::k_str_size;

  details::t_string<base64_encoder::get_buffer_size(str_size)> data_out{};

  base64_encoder enc{true};
  // avoid null termination char
  if (!enc.encode_to_buffer(data_out, {str.data(), str_size - 1})) {
    data_out = {};
  }

  return data_out;
}

// Universally decodes base64 or base64_url encoded string.
// If the string is not a valid base64 encoded string - result is undefined.
inline std::vector<std::byte> base64_decode(std::string_view input) {
  base64_decoder dec{base64_decoder::decode_policy::universal};
  return dec.decode(input);
}

// Universally decodes base64 or base64_url encoded string.
// If the string is not a valid base64 encoded string - result is undefined.
inline std::string base64_decode_str(std::string_view input) {
  base64_decoder dec{base64_decoder::decode_policy::universal};
  return dec.decode_str(input);
}

}  // namespace server
