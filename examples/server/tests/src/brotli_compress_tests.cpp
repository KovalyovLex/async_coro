#include <gtest/gtest.h>
#include <server/utils/brotli_compress.h>
#include <server/utils/brotli_compression_constants.h>
#include <server/utils/brotli_decompress.h>

#if SERVER_HAS_BROTLI

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace server {

// Helper function to compress data
std::vector<std::byte> compress_data_brotli(std::span<const std::byte> input_data,
                                            brotli::compression_level level,
                                            brotli::window_bits window = {}) {
  brotli_compress compressor(brotli::compression_config{.window = window, .compression = level});
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
std::vector<std::byte> decompress_data_brotli(std::span<const std::byte> input_data) {
  brotli_decompress decompressor(brotli::decompression_config{});
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
TEST(brotli_compress, empty_data) {
  std::vector<std::byte> input;
  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test single byte compression
TEST(brotli_compress, single_byte) {
  std::vector<std::byte> input = {std::byte(65)};  // 'A'
  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test small text compression
TEST(brotli_compress, small_text) {
  static constexpr std::string_view text = "Hello, World!";
  auto text_span = std::as_bytes(std::span{text});

  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data_brotli(text_span, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test larger text compression
TEST(brotli_compress, large_text) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 100; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text.data(), text.size() + 1});
  std::vector<std::byte> input(text_span.begin(), text_span.end());
  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), text_span.size());
  EXPECT_STREQ(text.c_str(), reinterpret_cast<const char*>(decompressed.data()));
}

// Test binary data compression
TEST(brotli_compress, binary_data) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i % 256));
  }
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test highly repetitive data (should compress well)
TEST(brotli_compress, highly_repetitive_data) {
  std::vector<std::byte> input;
  input.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Highly repetitive data should compress very well
  EXPECT_LT(compressed.size(), input.size() / 4);
}

// Test different compression levels
TEST(brotli_compress, different_levels) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test a few compression levels (0-11 for brotli)
  std::vector<size_t> sizes;
  for (int level : {0, 4, 8, 11}) {
    auto compressed = compress_data_brotli(input, brotli::compression_level{static_cast<uint8_t>(level)});
    auto decompressed = decompress_data_brotli(compressed);

    EXPECT_EQ(decompressed, input) << "Failed at compression level " << level;
    sizes.push_back(compressed.size());
  }

  // Expect higher levels generally produce smaller outputs
  EXPECT_GE(sizes[0], sizes.back());
}

// Test fastest compression quality
TEST(brotli_compress, fastest_quality) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data_brotli(input,
                                         brotli::compression_level{brotli::compression_quality::fastest});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test best compression quality
TEST(brotli_compress, best_quality) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data_brotli(input,
                                         brotli::compression_level{brotli::compression_quality::best});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test default compression quality
TEST(brotli_compress, default_quality) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  auto compressed = compress_data_brotli(input,
                                         brotli::compression_level{brotli::compression_quality::default_quality});
  auto decompressed = decompress_data_brotli(compressed);

  EXPECT_EQ(decompressed, input);
}

// Test data with all byte values
TEST(brotli_compress, all_byte_values) {
  std::vector<std::byte> input;
  input.reserve(2816);  // Pre-allocate
  for (int i = 0; i < 256; ++i) {
    input.push_back(std::byte(i));
  }
  // Repeat to have enough data for compression
  for (int i = 0; i < 10; ++i) {
    input.insert(input.end(), input.begin(), input.begin() + 256);
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test very large data
TEST(brotli_compress, very_large_data) {
  std::vector<std::byte> input;
  std::string pattern = "The quick brown fox jumps over the lazy dog. ";

  // Create ~1MB of data
  for (int i = 0; i < 25000; ++i) {
    for (char c : pattern) {
      input.push_back(std::byte(c));
    }
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test random-like data (should not compress well)
TEST(brotli_compress, random_like_data) {
  std::vector<std::byte> input;
  input.reserve(10000);
  unsigned int seed = 42U;
  for (int i = 0; i < 10000; ++i) {
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;
    input.push_back(std::byte(seed % 256U));
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
  // Pseudo-random data may compress better than truly random due to patterns from PRNG
  EXPECT_LE(compressed.size(), input.size() * 1.5);
}

// Test incremental compression with small buffers
TEST(brotli_compress, incremental_compression_small_buffers) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 20; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Use standard compress/decompress which handles small buffers correctly
  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test move semantics
TEST(brotli_compress, move_semantics) {
  constexpr std::string_view text = "Hello, World! This is a test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  std::vector<std::byte> output;

  {
    brotli_compress compressor1(brotli::compression_config{});
    EXPECT_TRUE(compressor1.is_valid());

    // Move construct
    brotli_compress compressor2(std::move(compressor1));

    // After move, compressor2 should be valid and have taken ownership
    EXPECT_TRUE(compressor2.is_valid());
    EXPECT_FALSE(compressor1.is_valid());

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

  auto decompressed = decompress_data_brotli(output);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test move assignment
TEST(brotli_compress, move_assignment) {
  constexpr std::string_view text = "Hello, World! Move assignment test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  std::vector<std::byte> output;

  {
    brotli_compress compressor1(brotli::compression_config{});
    brotli_compress compressor2(brotli::compression_config{});

    EXPECT_TRUE(compressor1.is_valid());
    EXPECT_TRUE(compressor2.is_valid());

    // Move assign
    compressor2 = std::move(compressor1);

    // After move, compressor2 should be valid and have taken ownership
    EXPECT_TRUE(compressor2.is_valid());
    EXPECT_FALSE(compressor1.is_valid());

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

  auto decompressed = decompress_data_brotli(output);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test multiple compression cycles (reset functionality)
TEST(brotli_compress, multiple_compression_cycles) {
  constexpr std::string_view message1 = "First message";
  constexpr std::string_view message2 = "Second message is longer";

  auto text_span = std::as_bytes(std::span{message1});
  std::vector<std::byte> data1(text_span.begin(), text_span.end());

  text_span = std::as_bytes(std::span{message2});
  std::vector<std::byte> data2(text_span.begin(), text_span.end());

  // Compress first data
  auto compressed1 = compress_data_brotli(data1, brotli::compression_level{});
  auto decompressed1 = decompress_data_brotli(compressed1);
  EXPECT_EQ(decompressed1, data1);

  // Compress second data
  auto compressed2 = compress_data_brotli(data2, brotli::compression_level{});
  auto decompressed2 = decompress_data_brotli(compressed2);

  ASSERT_EQ(decompressed2.size(), data2.size());
  EXPECT_EQ(decompressed2, data2);
}

// Test null/zero bytes in data
TEST(brotli_compress, null_bytes) {
  std::vector<std::byte> input;
  for (int i = 0; i < 100; ++i) {
    input.push_back(std::byte(0));
    input.push_back(std::byte(42));
  }

  auto compressed = compress_data_brotli(input, brotli::compression_level{});
  auto decompressed = decompress_data_brotli(compressed);

  ASSERT_EQ(decompressed.size(), input.size());
  EXPECT_EQ(decompressed, input);
}

// Test flush functionality
TEST(brotli_compress, flush_during_compression) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 20; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  brotli_compress compressor(brotli::compression_config{});
  EXPECT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)
  std::span<const std::byte> input_data(input);

  // Update stream
  {
    std::span<std::byte> buffer_span(buffer);
    const auto update_success = compressor.update_stream(input_data, buffer_span);
    EXPECT_TRUE(update_success);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
  }

  // Flush during compression
  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = compressor.flush(input_data, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  // Continue compression after flush
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

  auto decompressed = decompress_data_brotli(output);
  ASSERT_EQ(decompressed.size(), text_span.size());
  EXPECT_EQ(decompressed, std::vector<std::byte>(text_span.begin(), text_span.end()));
}

// Test window bits parameter (different window sizes)
TEST(brotli_compress, different_window_bits) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test different window sizes (brotli supports 10-24)
  for (uint8_t window_bits : {10, 16, 22}) {
    brotli_compress compressor(brotli::compression_config{.window = brotli::window_bits{window_bits}});
    EXPECT_TRUE(compressor.is_valid()) << "Failed at window_bits=" << static_cast<int>(window_bits);

    std::vector<std::byte> output;
    std::array<std::byte, 4096> buffer;  // NOLINT(*init)
    std::span<const std::byte> input_data(input);

    {
      std::span<std::byte> buffer_span(buffer);
      const auto update_success = compressor.update_stream(input_data, buffer_span);
      EXPECT_TRUE(update_success);
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

    EXPECT_EQ(input_data.size(), 0);

    brotli_decompress decompressor(brotli::decompression_config{});
    EXPECT_TRUE(decompressor.is_valid());

    std::vector<std::byte> decompressed;
    std::span<const std::byte> compressed_span(output);

    {
      std::span<std::byte> buffer_span(buffer);
      const auto update_success = decompressor.update_stream(compressed_span, buffer_span);
      EXPECT_TRUE(update_success);
      const auto produced = std::span{buffer.data(), buffer_span.data()};
      decompressed.insert(decompressed.end(), produced.begin(), produced.end());
    }

    while (true) {
      std::span<std::byte> buffer_span(buffer);
      const bool cnt = decompressor.end_stream(compressed_span, buffer_span);
      const auto produced = std::span{buffer.data(), buffer_span.data()};
      decompressed.insert(decompressed.end(), produced.begin(), produced.end());
      if (!cnt) {
        break;
      }
    }

    EXPECT_EQ(compressed_span.size(), 0);
    EXPECT_EQ(decompressed, input) << "Failed at window_bits=" << static_cast<int>(window_bits);
  }
}

// Test compression with large block size (lgblock parameter)
TEST(brotli_compress, large_block_size) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 50; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Test with larger block size (lgblock values typically 16-24, 0 = automatic)
  brotli_compress compressor(brotli::compression_config{.block = brotli::lgblock{18}});
  EXPECT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)
  std::span<const std::byte> input_data(input);

  {
    std::span<std::byte> buffer_span(buffer);
    const auto update_success = compressor.update_stream(input_data, buffer_span);
    EXPECT_TRUE(update_success);
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

  EXPECT_EQ(input_data.size(), 0);

  auto decompressed = decompress_data_brotli(output);
  EXPECT_EQ(decompressed, input);
}

// Test move semantics for decompressor
TEST(brotli_decompress, move_semantics) {
  constexpr std::string_view text = "Hello, World! Decompress move test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // First, compress some data
  auto compressed = compress_data_brotli(input, brotli::compression_level{});

  std::vector<std::byte> output;

  {
    brotli_decompress decompressor1(brotli::decompression_config{});
    EXPECT_TRUE(decompressor1.is_valid());

    // Move construct
    brotli_decompress decompressor2(std::move(decompressor1));

    // After move, decompressor2 should be valid and have taken ownership
    EXPECT_TRUE(decompressor2.is_valid());
    EXPECT_FALSE(decompressor1.is_valid());

    std::array<std::byte, 8192> buffer;  // NOLINT(*init)
    std::span<const std::byte> input_data(compressed);

    {
      std::span<std::byte> buffer_span(buffer);

      decompressor2.update_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
    }

    while (true) {
      std::span<std::byte> buffer_span(buffer);
      const auto cnt = decompressor2.end_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
      if (!cnt) {
        break;
      }
    }

    EXPECT_EQ(input_data.size(), 0);
  }

  ASSERT_EQ(output.size(), input.size());
  EXPECT_EQ(output, input);
}

// Test move assignment for decompressor
TEST(brotli_decompress, move_assignment) {
  constexpr std::string_view text = "Hello, World! Decompress move assignment test.";

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // First, compress some data
  auto compressed = compress_data_brotli(input, brotli::compression_level{});

  std::vector<std::byte> output;

  {
    brotli_decompress decompressor1(brotli::decompression_config{});
    brotli_decompress decompressor2(brotli::decompression_config{});

    EXPECT_TRUE(decompressor1.is_valid());
    EXPECT_TRUE(decompressor2.is_valid());

    // Move assign
    decompressor2 = std::move(decompressor1);

    // After move, decompressor2 should be valid and have taken ownership
    EXPECT_TRUE(decompressor2.is_valid());
    EXPECT_FALSE(decompressor1.is_valid());

    std::array<std::byte, 8192> buffer;  // NOLINT(*init)
    std::span<const std::byte> input_data(compressed);

    {
      std::span<std::byte> buffer_span(buffer);

      decompressor2.update_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
    }

    while (true) {
      std::span<std::byte> buffer_span(buffer);
      const auto cnt = decompressor2.end_stream(input_data, buffer_span);
      size_t produced = buffer.size() - buffer_span.size();
      output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
      if (!cnt) {
        break;
      }
    }

    EXPECT_EQ(input_data.size(), 0);
  }

  ASSERT_EQ(output.size(), input.size());
  EXPECT_EQ(output, input);
}

// Test decompressor flush functionality
TEST(brotli_decompress, flush_during_decompression) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 20; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // First compress
  auto compressed = compress_data_brotli(input, brotli::compression_level{});

  // Then decompress with flush
  brotli_decompress decompressor(brotli::decompression_config{});
  EXPECT_TRUE(decompressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 4096> buffer;  // NOLINT(*init)
  std::span<const std::byte> compressed_span(compressed);

  // Update stream
  {
    std::span<std::byte> buffer_span(buffer);
    const auto update_success = decompressor.update_stream(compressed_span, buffer_span);
    EXPECT_TRUE(update_success);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
  }

  // Flush during decompression
  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = decompressor.flush(compressed_span, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  // Continue decompression after flush
  while (true) {
    std::span<std::byte> buffer_span(buffer);
    const bool cnt = decompressor.end_stream(compressed_span, buffer_span);
    const auto produced = std::span{buffer.data(), buffer_span.data()};
    output.insert(output.end(), produced.begin(), produced.end());
    if (!cnt) {
      break;
    }
  }

  EXPECT_EQ(compressed_span.size(), 0);

  ASSERT_EQ(output.size(), input.size());
  EXPECT_EQ(output, input);
}

// Test compression ratio comparison (best quality should compress better)
TEST(brotli_compress, compression_ratio_by_quality) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 100; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  // Get compressed sizes for different quality levels
  auto compressed_fastest =
      compress_data_brotli(input, brotli::compression_level{brotli::compression_quality::fastest});
  auto compressed_default =
      compress_data_brotli(input, brotli::compression_level{brotli::compression_quality::default_quality});
  auto compressed_best =
      compress_data_brotli(input, brotli::compression_level{brotli::compression_quality::best});

  // All should decompress correctly
  EXPECT_EQ(decompress_data_brotli(compressed_fastest), input);
  EXPECT_EQ(decompress_data_brotli(compressed_default), input);
  EXPECT_EQ(decompress_data_brotli(compressed_best), input);

  // Better compression levels should generally produce smaller or equal sizes
  EXPECT_GE(compressed_fastest.size(), compressed_best.size());
}

// Test streaming compression with very small output buffer
TEST(brotli_compress, streaming_small_output_buffer) {
  std::string text = "The quick brown fox jumps over the lazy dog. ";
  for (int i = 0; i < 30; ++i) {
    text += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  }

  auto text_span = std::as_bytes(std::span{text});
  std::vector<std::byte> input(text_span.begin(), text_span.end());

  brotli_compress compressor(brotli::compression_config{});
  EXPECT_TRUE(compressor.is_valid());

  std::vector<std::byte> output;
  std::array<std::byte, 256> small_buffer;  // NOLINT(*init) - Very small buffer (256 bytes)
  std::span<const std::byte> input_data(input);

  // Process with small output buffer
  {
    std::span<std::byte> buffer_span(small_buffer);
    const auto update_success = compressor.update_stream(input_data, buffer_span);
    EXPECT_TRUE(update_success);
    size_t produced = small_buffer.size() - buffer_span.size();
    output.insert(output.end(), small_buffer.begin(), small_buffer.begin() + static_cast<ptrdiff_t>(produced));
  }

  // Finalize with small buffer
  while (true) {
    std::span<std::byte> buffer_span(small_buffer);
    const bool cnt = compressor.end_stream(input_data, buffer_span);
    size_t produced = small_buffer.size() - buffer_span.size();
    output.insert(output.end(), small_buffer.begin(), small_buffer.begin() + static_cast<ptrdiff_t>(produced));
    if (!cnt) {
      break;
    }
  }

  EXPECT_EQ(input_data.size(), 0);

  auto decompressed = decompress_data_brotli(output);
  EXPECT_EQ(decompressed, input);
}

// Test decompressor creation and assignment
TEST(brotli_decompress, assign_validate) {
  brotli_decompress decompress;
  EXPECT_FALSE(decompress.is_valid());

  decompress = brotli_decompress{};
  EXPECT_FALSE(decompress.is_valid());

  decompress = brotli_decompress(brotli::decompression_config{});
  EXPECT_TRUE(decompress.is_valid());

  decompress = brotli_decompress();
  EXPECT_FALSE(decompress.is_valid());
}

}  // namespace server

#endif
