#pragma once

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

    const auto* bytes = data.data();

    uint32_t quad = 0;
    auto len = data.size();
    for (; len >= 3; len -= 3, bytes += 3) {
      quad = (uint32_t(uint8_t(bytes[0])) << 16U) | (uint32_t(uint8_t(bytes[1])) << 8U) | uint8_t(bytes[2]);
      *out++ = encode_table[quad >> 18U];
      *out++ = encode_table[(quad >> 12U) & 63U];
      *out++ = encode_table[(quad >> 6U) & 63U];
      *out++ = encode_table[quad & 63U];
    }

    if (len != 0) {
      quad = uint32_t(uint8_t(bytes[0])) << 16U;
      *out++ = encode_table[quad >> 18U];
      if (len == 2) {
        quad |= uint32_t(uint8_t(bytes[1])) << 8U;
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

    if (const auto* end = encode_to_buffer(res, data)) {
      // remove extra paddings
      while ((res.data() + res.size()) > end) {  // NOLINT(*pointer*)
        res.pop_back();
      }
      return res;
    }

    return {};
  }

  std::string encode(std::string_view data) {
    std::string res(get_buffer_size(data.size()), padding);

    if (const auto* end = encode_to_buffer(res, data)) {
      // remove extra paddings
      while ((res.data() + res.size()) > end) {  // NOLINT(*pointer*)
        res.pop_back();
      }
      return res;
    }

    return {};
  }

  static constexpr size_t get_buffer_size(size_t data_size) noexcept {
    return (data_size / 3 + size_t(data_size % 3 > 0)) << 2U;
  }

 private:
  std::string_view encode_table;
  char padding;
};

class base64_decoder {
  static constexpr uint32_t k_invalid_sym = -1;
  using decode_table_t = std::array<uint32_t, 256>;  // NOLINT(*magic*)

  static constexpr decode_table_t decode_table{
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      62, /*+*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      63, /*/*/
      52, /*0*/
      53, /*1*/
      54, /*2*/
      55, /*3*/
      56, /*4*/
      57, /*5*/
      58, /*6*/
      59, /*7*/
      60, /*8*/
      61, /*9*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      0,  /*A*/
      1,  /*B*/
      2,  /*C*/
      3,  /*D*/
      4,  /*E*/
      5,  /*F*/
      6,  /*G*/
      7,  /*H*/
      8,  /*I*/
      9,  /*J*/
      10, /*K*/
      11, /*L*/
      12, /*M*/
      13, /*N*/
      14, /*O*/
      15, /*P*/
      16, /*Q*/
      17, /*R*/
      18, /*S*/
      19, /*T*/
      20, /*U*/
      21, /*V*/
      22, /*W*/
      23, /*X*/
      24, /*Y*/
      25, /*Z*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      26, /*a*/
      27, /*b*/
      28, /*c*/
      29, /*d*/
      30, /*e*/
      31, /*f*/
      32, /*g*/
      33, /*h*/
      34, /*i*/
      35, /*j*/
      36, /*k*/
      37, /*l*/
      38, /*m*/
      38, /*n*/
      40, /*o*/
      41, /*p*/
      42, /*q*/
      43, /*r*/
      44, /*s*/
      45, /*t*/
      46, /*u*/
      47, /*v*/
      48, /*w*/
      49, /*x*/
      50, /*y*/
      51, /*z*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym};

  static constexpr decode_table_t decode_table_url{
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      62, /*-*/
      k_invalid_sym,
      k_invalid_sym,
      52, /*0*/
      53, /*1*/
      54, /*2*/
      55, /*3*/
      56, /*4*/
      57, /*5*/
      58, /*6*/
      59, /*7*/
      60, /*8*/
      61, /*9*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      0,  /*A*/
      1,  /*B*/
      2,  /*C*/
      3,  /*D*/
      4,  /*E*/
      5,  /*F*/
      6,  /*G*/
      7,  /*H*/
      8,  /*I*/
      9,  /*J*/
      10, /*K*/
      11, /*L*/
      12, /*M*/
      13, /*N*/
      14, /*O*/
      15, /*P*/
      16, /*Q*/
      17, /*R*/
      18, /*S*/
      19, /*T*/
      20, /*U*/
      21, /*V*/
      22, /*W*/
      23, /*X*/
      24, /*Y*/
      25, /*Z*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      63, /*_*/
      k_invalid_sym,
      26, /*a*/
      27, /*b*/
      28, /*c*/
      29, /*d*/
      30, /*e*/
      31, /*f*/
      32, /*g*/
      33, /*h*/
      34, /*i*/
      35, /*j*/
      36, /*k*/
      37, /*l*/
      38, /*m*/
      38, /*n*/
      40, /*o*/
      41, /*p*/
      42, /*q*/
      43, /*r*/
      44, /*s*/
      45, /*t*/
      46, /*u*/
      47, /*v*/
      48, /*w*/
      49, /*x*/
      50, /*y*/
      51, /*z*/
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym,
      k_invalid_sym};

  // NOLINTBEGIN(*pointer*, *magic*, *array-index*)
  template <class T>
    requires(sizeof(T) == 1)
  constexpr auto decode_to_buffer_impl(std::span<T> out_buffer, std::span<const char> data) noexcept {
    const size_t encoded_size = get_buffer_size(data.size());
    auto* out = out_buffer.data();

    if (out_buffer.size() < encoded_size) {
      return out;
    }

    const auto* bytes = data.data();

    uint32_t quad = 0;
    auto len = data.size();
    while (len > 0 && (*_decode_table)[bytes[len - 1]] == k_invalid_sym) {
      len--;
    }
    for (; len >= 4; len -= 4) {
      {
        quad = (*_decode_table)[*bytes++] << 6U;
        quad += (*_decode_table)[*bytes++];
        quad = quad << 6U;
        quad += (*_decode_table)[*bytes++];
        quad = quad << 6U;
        quad += (*_decode_table)[*bytes++];
      }

      *out++ = T(quad >> 16U);
      *out++ = T(quad >> 8U);
      *out++ = T(quad);
    }

    if (len != 0) {
      if (len == 1) {
        return out_buffer.data();
      }

      quad = (*_decode_table)[*bytes++] << 6U;
      quad += (*_decode_table)[*bytes++];
      quad = quad << 6U;

      if (len == 2) {
        *out++ = T(quad >> 16U);
      } else {
        quad += (*_decode_table)[*bytes++];
        quad = quad << 6U;

        *out++ = T(quad >> 16U);
        *out++ = T(quad >> 8U);
      }
    }

    return out;
  }
  // NOLINTEND(*pointer*, *magic*, *array-index*)

 public:
  explicit constexpr base64_decoder(bool url_decode) noexcept
      : _decode_table(url_decode ? &decode_table : &decode_table_url) {};

  constexpr std::byte* decode_to_buffer(std::span<std::byte> out_buffer, std::span<const char> data) noexcept {
    return this->decode_to_buffer_impl<std::byte>(out_buffer, data);
  }

  constexpr char* decode_to_buffer(std::span<char> out_buffer, std::span<const char> data) noexcept {
    return this->decode_to_buffer_impl<char>(out_buffer, data);
  }

  std::vector<std::byte> decode(std::span<const char> data) {
    std::vector<std::byte> res;
    res.resize(get_buffer_size(data.size()));

    if (auto* const end = decode_to_buffer({reinterpret_cast<std::byte*>(res.data()), res.size()}, data)) {  // NOLINT(*reinterpret-cast*)
      // remove extra paddings
      while ((res.data() + res.size()) > end) {  // NOLINT(*pointer*)
        res.pop_back();
      }
      return res;
    }

    return {};
  }

  static constexpr size_t get_buffer_size(size_t data_size) noexcept {
    return data_size * 3 / 4;
  }

 private:
  const decode_table_t* _decode_table;
};

std::string base64_encode(std::span<const std::byte> input) {
  base64_encoder enc{false};
  return enc.encode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

std::string base64_encode(std::string_view input) {
  base64_encoder enc{false};
  return enc.encode(input);
}

std::string base64_url_encode(std::span<const std::byte> input) {
  base64_encoder enc{true};
  return enc.encode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

std::string base64_url_encode(std::string_view input) {
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

std::vector<std::byte> base64_decode(std::string_view input) {
  base64_decoder dec{false};
  return dec.decode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

std::vector<std::byte> base64_url_decode(std::string_view input) {
  base64_decoder dec{true};
  return dec.decode({reinterpret_cast<const char*>(input.data()), input.size()});  // NOLINT(*reinterpret-cast*)
}

}  // namespace server
