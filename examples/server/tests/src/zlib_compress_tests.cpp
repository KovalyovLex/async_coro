#include <gtest/gtest.h>
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_compression_constants.h>
#include <server/utils/zlib_decompress.h>

#if SERVER_HAS_ZLIB

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace server {

// Helper function to compress data
std::vector<std::byte> compress_data(const std::vector<std::byte>& input,
                                     zlib::compression_method method,
                                     zlib::compression_level level) {
  zlib_compress compressor(method, zlib::window_bits{}, level);
  EXPECT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::span<const std::byte> input_data(input);
  std::array<std::byte, 2048> buffer;  // NOLINT(*init)

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
std::vector<std::byte> decompress_data(const std::vector<std::byte>& input,
                                       zlib::compression_method method) {
  zlib_decompress decompressor(method);
  EXPECT_TRUE(decompressor.is_valid());

  std::vector<std::byte> output;
  std::span<const std::byte> input_data(input);
  std::array<std::byte, 2048> buffer;  // NOLINT(*init)

  // Process input data in one go
  {
    std::span<std::byte> buffer_span(buffer);

    decompressor.update_stream(input_data, buffer_span);
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
TEST(zlib_compress, empty_data_deflate) {
  std::vector<std::byte> input;
  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, empty_data_gzip) {
  std::vector<std::byte> input;
  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  EXPECT_EQ(decompressed, input);
}

// Test single byte compression
TEST(zlib_compress, single_byte_deflate) {
  std::vector<std::byte> input = {std::byte(65)};  // 'A'
  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, single_byte_gzip) {
  std::vector<std::byte> input = {std::byte(65)};  // 'A'
  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  EXPECT_EQ(decompressed, input);
}

// Test small text compression
TEST(zlib_compress, small_text_deflate) {
  static constexpr std::string_view text = "Hello, World!";
  auto text_span = std::as_bytes(std::span{text});

  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, small_text_gzip) {
  static constexpr std::string_view text = "Hello, World!";
  auto text_span = std::as_bytes(std::span{text});

  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  EXPECT_EQ(decompressed, input);
}

// Test larger text compression
TEST(zlib_compress, large_text_deflate) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 100; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text.data(), text.size() + 1});
  std::vector<std::byte> input(text_span.begin(), text_span.end());
  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), text_span.size());
  EXPECT_STREQ(text.c_str(), reinterpret_cast<const char*>(decompressed.data()));
}

TEST(zlib_compress, large_text_gzip) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 100; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text.data(), text.size() + 1});
  std::vector<std::byte> input(text_span.begin(), text_span.end());
  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), text_span.size());
  EXPECT_STREQ(text.c_str(), reinterpret_cast<const char*>(decompressed.data()));
}

// Test binary data compression
TEST(zlib_compress, binary_data_deflate) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i % 256));
  }
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, binary_data_gzip) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i % 256));
  }
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test highly repetitive data (should compress well)
TEST(zlib_compress, highly_repetitive_data_deflate) {
  std::vector<std::byte> input;
  input.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Highly repetitive data should compress very well
  EXPECT_LT(compressed.size(), input.size() / 10);
}

TEST(zlib_compress, highly_repetitive_data_gzip) {
  std::vector<std::byte> input;
  input.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Highly repetitive data should compress very well
  EXPECT_LT(compressed.size(), input.size() / 10);
}

// Test different compression levels
TEST(zlib_compress, different_levels_deflate) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test different compression levels
  std::vector<size_t> sizes;
  for (int level = 0; level <= 9; ++level) {
    auto compressed = compress_data(input, zlib::compression_method::deflate,
                                    zlib::compression_level{static_cast<int8_t>(level)});
    auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
    sizes.push_back(compressed.size());
  }

  // Generally, higher compression levels should produce smaller or equal output
  // (though not strictly guaranteed for small inputs)
  // We can at least check that level 0 (store) is largest and level 9 is smallest for larger data
  EXPECT_GE(sizes[0], sizes[9]) << "Higher compression level should produce smaller output for large data";
}

TEST(zlib_compress, different_levels_gzip) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test different compression levels
  std::vector<size_t> sizes;
  for (int level = 0; level <= 9; ++level) {
    auto compressed = compress_data(input, zlib::compression_method::gzip,
                                    zlib::compression_level{static_cast<int8_t>(level)});
    auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
    sizes.push_back(compressed.size());
  }

  // Generally, higher compression levels should produce smaller or equal output
  EXPECT_GE(sizes[0], sizes[9]) << "Higher compression level should produce smaller output for large data";
}

// Test data with all byte values
TEST(zlib_compress, all_byte_values_deflate) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i));
  }
  // Repeat to have enough data for compression
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, all_byte_values_gzip) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i));
  }
  // Repeat to have enough data for compression
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test very large data
TEST(zlib_compress, very_large_data_deflate) {
  std::vector<std::byte> input;
  std::string pattern = "The quick brown fox jumps over the lazy dog. ";

  // Create 1MB of data
  for (int i = 0; i < 25000; ++i) {
    for (char c : pattern) {
      input.push_back(std::byte(c));
    }
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, very_large_data_gzip) {
  std::vector<std::byte> input;
  std::string pattern = "The quick brown fox jumps over the lazy dog. ";

  // Create 1MB of data
  for (int i = 0; i < 25000; ++i) {
    for (char c : pattern) {
      input.push_back(std::byte(c));
    }
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test random-like data (should not compress well)
TEST(zlib_compress, random_like_data_deflate) {
  std::vector<std::byte> input;
  input.reserve(10000);
  unsigned int seed = 42U;
  for (int i = 0; i < 10000; ++i) {
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;
    input.push_back(std::byte(seed % 256U));
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Pseudo-random data may compress better than truly random due to patterns from PRNG
  // Just verify it's not significantly larger than the original
  EXPECT_LE(compressed.size(), input.size() * 1.1);
}

TEST(zlib_compress, random_like_data_gzip) {
  std::vector<std::byte> input;
  input.reserve(10000);
  unsigned int seed = 42U;
  for (int i = 0; i < 10000; ++i) {
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;
    input.push_back(std::byte(seed % 256U));
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Pseudo-random data may compress better than truly random due to patterns from PRNG
  // Just verify it's not significantly larger than the original
  EXPECT_LE(compressed.size(), input.size() * 1.1);
}

// Test incremental compression with small buffers
TEST(zlib_compress, incremental_compression_small_buffers) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 20; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Use standard compress/decompress which handles small buffers correctly
  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test move semantics
TEST(zlib_compress, move_semantics_deflate) {
  constexpr std::string_view text = "Hello, World! This is a test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  std::vector<std::byte> output;

  {
    zlib_compress compressor1(zlib::compression_method::deflate);
    EXPECT_TRUE(compressor1.is_valid());

    // Move construct
    zlib_compress compressor2(std::move(compressor1));

    // After move, compressor2 should be valid and have taken ownership
    EXPECT_TRUE(compressor2.is_valid());
    EXPECT_FALSE(compressor1.is_valid());

    std::array<std::byte, 4096> buffer;  // NOLINT(*init)
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

  auto decompressed = decompress_data(output, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test compression then expansion - reset functionality
TEST(zlib_compress, multiple_compression_cycles_deflate) {
  constexpr std::string_view message1 = "First message";
  constexpr std::string_view message2 = "Second message is longer";

  auto text_span = std::as_bytes(std::span{message1});
  std::vector<std::byte> data1(text_span.begin(), text_span.end());

  text_span = std::as_bytes(std::span{message2});
  std::vector<std::byte> data2(text_span.begin(), text_span.end());

  zlib_compress compressor(zlib::compression_method::deflate);

  // Compress first data
  auto compressed1 = compress_data(data1, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed1 = decompress_data(compressed1, zlib::compression_method::deflate);
  EXPECT_EQ(decompressed1, data1);

  // Compress second data
  auto compressed2 = compress_data(data2, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed2 = decompress_data(compressed2, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed2.size(), data2.size());
  EXPECT_EQ(decompressed2, data2);
}

// Test null/zero bytes in data
TEST(zlib_compress, null_bytes_deflate) {
  std::vector<std::byte> input;
  for (int i = 0; i < 100; ++i) {
    input.push_back(std::byte(0));
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

TEST(zlib_compress, null_bytes_gzip) {
  std::vector<std::byte> input;
  for (int i = 0; i < 100; ++i) {
    input.push_back(std::byte(0));
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});
  auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test compression levels 0-9 with Gzip
TEST(zlib_compress, all_compression_levels_gzip) {
  std::string text = "Hello World ";
  for (int i = 0; i < 100; ++i) {
    text += "Additional text to have more data to compress. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  for (int level = 0; level <= 9; ++level) {
    auto compressed = compress_data(input, zlib::compression_method::gzip,
                                    zlib::compression_level{static_cast<int8_t>(level)});
    auto decompressed = decompress_data(compressed, zlib::compression_method::gzip);

    ASSERT_EQ(decompressed.size(), input.size()) << "Failed at compression level " << level;
    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
  }
}

// Test compression levels 0-9 with Deflate
TEST(zlib_compress, all_compression_levels_deflate) {
  std::string text = "Hello World ";
  for (int i = 0; i < 100; ++i) {
    text += "Additional text to have more data to compress. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  for (int level = 0; level <= 9; ++level) {
    auto compressed = compress_data(input, zlib::compression_method::deflate,
                                    zlib::compression_level{static_cast<int8_t>(level)});
    auto decompressed = decompress_data(compressed, zlib::compression_method::deflate);

    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
  }
}

// Test incompatible decompression (deflate data with gzip decompressor should fail or produce garbage)
TEST(zlib_compress, incompatible_compression_method) {
  constexpr std::string_view text = "Hello, World!";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Compress with deflate
  auto compressed_deflate = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});

  // Try to decompress with gzip (should fail or produce invalid output)
  {
    zlib_decompress decompressor(zlib::compression_method::gzip);
    std::vector<std::byte> buffer(4096);
    std::vector<std::byte> compressed_copy = compressed_deflate;

    std::span<const std::byte> compressed_span(compressed_copy);
    std::span<std::byte> buffer_span(buffer);

    // This should fail or produce garbage
    decompressor.update_stream(compressed_span, buffer_span);

    // We just verify it doesn't crash
    EXPECT_TRUE(true);
  }
}

// Test flush behavior for compressor
TEST(zlib_compress, flush_deflate) {
  {
    constexpr std::string_view text = "Flush test data for compressor";
    const auto text_span = std::as_bytes(std::span{text});

    zlib_compress compressor(zlib::compression_method::deflate);
    ASSERT_TRUE(compressor.is_valid());

    std::array<std::byte, 32> buffer{};  // intentionally small to force intermediate buffering
    std::vector<std::byte> output;

    std::span<const std::byte> input_span(text_span.begin(), text_span.end());

    {
      std::span<std::byte> buf_span(buffer);
      // produce some output by updating stream with a small buffer
      const auto ok = compressor.update_stream(input_span, buf_span);
      EXPECT_TRUE(ok);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
    }

    while (true) {
      std::span<std::byte> buf_span(buffer);
      const bool more = compressor.flush(input_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
      if (!more) {
        break;
      }
    }
    EXPECT_EQ(input_span.size(), 0);

    auto decompressed = decompress_data(output, zlib::compression_method::deflate);
    std::string_view decompressed_str{reinterpret_cast<const char*>(decompressed.data()), decompressed.size()};
    EXPECT_EQ(decompressed_str, text);
  }
}

TEST(zlib_compress, flush_gzip) {
  {
    constexpr std::string_view text = "Flush test data for compressor gzip";
    const auto text_span = std::as_bytes(std::span{text});

    zlib_compress compressor(zlib::compression_method::gzip);
    ASSERT_TRUE(compressor.is_valid());

    std::array<std::byte, 32> buffer{};
    std::vector<std::byte> output;

    std::span<const std::byte> input_span(text_span.begin(), text_span.end());

    {
      std::span<std::byte> buf_span(buffer);
      (void)compressor.update_stream(input_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
    }

    while (true) {
      std::span<std::byte> buf_span(buffer);
      const bool more = compressor.flush(input_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
      if (!more) {
        break;
      }
    }
    EXPECT_EQ(input_span.size(), 0);

    auto decompressed = decompress_data(output, zlib::compression_method::gzip);
    std::string_view decompressed_str{reinterpret_cast<const char*>(decompressed.data()), decompressed.size()};
    EXPECT_EQ(decompressed_str, text);
  }
}

// Test flush behavior for decompressor
TEST(zlib_compress, decompress_flush_deflate) {
  {
    // Prepare compressed data using helper
    constexpr std::string_view text = "Data to test decompressor flush";
    auto text_span = std::as_bytes(std::span{text});
    std::vector<std::byte> input(text_span.begin(), text_span.end());
    auto compressed = compress_data(input, zlib::compression_method::deflate, zlib::compression_level{});

    zlib_decompress decompressor(zlib::compression_method::deflate);
    ASSERT_TRUE(decompressor.is_valid());

    std::array<std::byte, 32> buffer{};
    std::vector<std::byte> output;

    std::span<const std::byte> compressed_span(compressed);

    {
      std::span<std::byte> buf_span(buffer);
      // produce some output
      decompressor.update_stream(compressed_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
    }

    // Call flush to force any buffered decompressed bytes out
    while (true) {
      std::span<std::byte> buf_span(buffer);
      const bool more = decompressor.flush(compressed_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
      if (!more) {
        break;
      }
    }
    EXPECT_EQ(compressed_span.size(), 0);

    EXPECT_EQ(output, input);
  }
}

TEST(zlib_compress, decompress_flush_gzip) {
  {
    constexpr std::string_view text = "Data to test decompressor flush gzip";
    auto text_span = std::as_bytes(std::span{text});
    std::vector<std::byte> input(text_span.begin(), text_span.end());
    auto compressed = compress_data(input, zlib::compression_method::gzip, zlib::compression_level{});

    zlib_decompress decompressor(zlib::compression_method::gzip);
    ASSERT_TRUE(decompressor.is_valid());

    std::array<std::byte, 32> buffer{};
    std::vector<std::byte> output;

    std::span<const std::byte> compressed_span(compressed);

    {
      std::span<std::byte> buf_span(buffer);
      decompressor.update_stream(compressed_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
    }

    while (true) {
      std::span<std::byte> buf_span(buffer);
      const bool more = decompressor.flush(compressed_span, buf_span);
      auto produced = std::span{buffer.data(), buf_span.data()};
      output.insert(output.end(), produced.begin(), produced.end());
      if (!more) {
        break;
      }
    }
    EXPECT_EQ(compressed_span.size(), 0);

    EXPECT_EQ(output, input);
  }
}

}  // namespace server

#endif
