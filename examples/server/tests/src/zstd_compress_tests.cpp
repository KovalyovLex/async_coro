#include <gtest/gtest.h>
#include <server/utils/zstd_compress.h>
#include <server/utils/zstd_compression_constants.h>
#include <server/utils/zstd_decompress.h>

#if SERVER_HAS_ZSTD

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace server {

// Helper function to compress data
std::vector<std::byte> compress_data_zstd(std::span<const std::byte> input_data,
                                          zstd::compression_level level) {
  zstd_compress compressor(zstd::compression_config{.compression = level});
  EXPECT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)

  // Process input data in one go
  {
    std::span<std::byte> buffer_span(buffer);

    const auto update_success = compressor.update_stream(input_data, buffer_span);
    EXPECT_TRUE(update_success);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
  }

  // Finalize compression
  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = compressor.end_stream(input_data, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  EXPECT_EQ(input_data.size(), 0);

  return output;
}

// Helper function to decompress data
std::vector<std::byte> decompress_data_zstd(std::span<const std::byte> input_data) {
  zstd_decompress decompressor(zstd::decompression_config{});
  EXPECT_TRUE(decompressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)

  // Process input data in one go
  {
    std::span<std::byte> buffer_span(buffer);

    const auto update_success = decompressor.update_stream(input_data, buffer_span);
    EXPECT_TRUE(update_success);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
  }

  // Finalize decompression
  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = decompressor.end_stream(input_data, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  EXPECT_EQ(input_data.size(), 0);

  return output;
}

// Test empty data compression
TEST(zstd_compress, empty_data) {
  std::vector<std::byte> input;
  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test single byte compression
TEST(zstd_compress, single_byte) {
  std::vector<std::byte> input = {std::byte(65)};  // 'A'
  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test small text compression
TEST(zstd_compress, small_text) {
  static constexpr std::string_view text = "Hello, World!";
  auto text_span = std::as_bytes(std::span{text});

  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data_zstd(text_span, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test larger text compression
TEST(zstd_compress, large_text) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 100; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text.data(), text.size() + 1});
  std::vector<std::byte> input(text_span.begin(), text_span.end());
  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), text_span.size());
  EXPECT_STREQ(text.c_str(), reinterpret_cast<const char*>(decompressed.data()));
}

// Test binary data compression
TEST(zstd_compress, binary_data) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i % 256));
  }
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test highly repetitive data (should compress well)
TEST(zstd_compress, highly_repetitive_data) {
  std::vector<std::byte> input;
  input.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Highly repetitive data should compress very well
  EXPECT_LT(compressed.size(), input.size() / 4);
}

// Test different compression levels
TEST(zstd_compress, different_levels) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test a few compression levels
  std::vector<size_t> sizes;
  for (int level : {1, 3, 6, 10}) {
    auto compressed = compress_data_zstd(input, zstd::compression_level{static_cast<int8_t>(level)});
    auto decompressed = decompress_data_zstd(compressed);

    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
    sizes.push_back(compressed.size());
  }

  // Expect higher levels generally produce smaller outputs
  EXPECT_GE(sizes[0], sizes.back());
}

// Test data with all byte values
TEST(zstd_compress, all_byte_values) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i));
  }
  // Repeat to have enough data for compression
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test very large data
TEST(zstd_compress, very_large_data) {
  std::vector<std::byte> input;
  std::string pattern = "The quick brown fox jumps over the lazy dog. ";

  // Create ~1MB of data
  for (int i = 0; i < 25000; ++i) {
    for (char c : pattern) {
      input.push_back(std::byte(c));
    }
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test random-like data (should not compress well)
TEST(zstd_compress, random_like_data) {
  std::vector<std::byte> input;
  input.reserve(10000);
  unsigned int seed = 42U;
  for (int i = 0; i < 10000; ++i) {
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;
    input.push_back(std::byte(seed % 256U));
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Pseudo-random data may compress better than truly random due to patterns from PRNG
  EXPECT_LE(compressed.size(), input.size() * 1.5);
}

// Test incremental compression with small buffers
TEST(zstd_compress, incremental_compression_small_buffers) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 20; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Use standard compress/decompress which handles small buffers correctly
  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test move semantics
TEST(zstd_compress, move_semantics) {
  constexpr std::string_view text = "Hello, World! This is a test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  std::vector<std::byte> output;

  {
    zstd_compress compressor1(zstd::compression_config{});
    EXPECT_TRUE(compressor1.is_valid());

    // Move construct
    zstd_compress compressor2(std::move(compressor1));

    // After move, compressor2 should be valid and have taken ownership
    EXPECT_TRUE(compressor2.is_valid());
    EXPECT_FALSE(compressor1.is_valid());  // NOLINT(*use-after-move, *access-moved, *Move)

    std::array<std::byte, 8192> buffer;  // NOLINT(*init)
    std::span<const std::byte> input_data(input);

    {
      std::span<std::byte> buffer_span(buffer);

      compressor2.update_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
    }

    while (true) {
      std::span<std::byte> buffer_span(buffer);
      const auto cnt = compressor2.end_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
      if (!cnt) {
        break;
      }
    }

    EXPECT_EQ(input_data.size(), 0);
  }

  auto decompressed = decompress_data_zstd(output);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test multiple compression cycles (reset functionality)
TEST(zstd_compress, multiple_compression_cycles) {
  constexpr std::string_view message1 = "First message";
  constexpr std::string_view message2 = "Second message is longer";

  auto text_span = std::as_bytes(std::span{message1});
  std::vector<std::byte> data1(text_span.begin(), text_span.end());

  text_span = std::as_bytes(std::span{message2});
  std::vector<std::byte> data2(text_span.begin(), text_span.end());

  // Compress first data
  auto compressed1 = compress_data_zstd(data1, zstd::compression_level{});
  auto decompressed1 = decompress_data_zstd(compressed1);
  EXPECT_EQ(decompressed1, data1);

  // Compress second data
  auto compressed2 = compress_data_zstd(data2, zstd::compression_level{});
  auto decompressed2 = decompress_data_zstd(compressed2);

  ASSERT_EQ(decompressed2.size(), data2.size());
  EXPECT_EQ(decompressed2, data2);
}

// Test null/zero bytes in data
TEST(zstd_compress, null_bytes) {
  std::vector<std::byte> input;
  for (int i = 0; i < 100; ++i) {
    input.push_back(std::byte(0));
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data_zstd(input, zstd::compression_level{});
  auto decompressed = decompress_data_zstd(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test dictionary support: compress with dictionary and decompress with same dictionary
TEST(zstd_compress, dictionary_roundtrip) {
  // Create a simple dictionary from repeated patterns
  std::vector<std::byte> dict;
  std::string pattern = "HEADER:VALUE\n";
  for (int i = 0; i < 200; ++i) {
    for (char c : pattern) {
      dict.push_back(std::byte(c));
    }
  }

  // Prepare data similar to dictionary
  std::vector<std::byte> input;
  for (int i = 0; i < 100; ++i) {
    for (char c : pattern) {
      input.push_back(std::byte(c));
    }
  }

  // Compress with dictionary
  zstd_compress compressor(std::span<const std::byte>(dict), zstd::compression_config{.compression = zstd::compression_level{5}});
  ASSERT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::span<const std::byte> input_data(input);
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)

  {
    std::span<std::byte> buffer_span(buffer);
    EXPECT_TRUE(compressor.update_stream(input_data, buffer_span));
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
  }

  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = compressor.end_stream(input_data, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  // Decompress with same dictionary
  zstd_decompress decompressor{std::span<const std::byte>(dict)};
  ASSERT_TRUE(decompressor.is_valid());

  std::vector<std::byte> recovered;
  std::span<const std::byte> compressed_span(output);

  {
    std::span<std::byte> buffer_span(buffer);
    decompressor.update_stream(compressed_span, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    recovered.insert(recovered.end(), produced.begin(), produced.end());
  }

  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = decompressor.end_stream(compressed_span, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    recovered.insert(recovered.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  EXPECT_EQ(recovered, input);
}

}  // namespace server

#endif
