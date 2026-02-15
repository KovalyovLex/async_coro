#include <gtest/gtest.h>
#include <server/http1/compression_negotiation.h>
#include <server/utils/compression_pool.h>

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "fixtures/compression_helper.h"

// Test fixture for compression pool and negotiation tests
class compression_pool_tests : public ::testing::Test, public compression_helper {
 protected:
  void SetUp() override {
    // Create compression pool with all available encodings
    server::compression_pool_config pool_config{.encodings = server::compression_pool_config::k_all_encodings};
    compression_pool = server::compression_pool::create(pool_config);
  }

  void TearDown() override {
    compression_pool = nullptr;
  }
};

// Tests for encoding_to_string conversion
TEST_F(compression_pool_tests, encoding_to_string_gzip) {
  auto str = as_string(server::compression_encoding::gzip);
  EXPECT_EQ(str, "gzip");
}

TEST_F(compression_pool_tests, encoding_to_string_deflate) {
  auto str = as_string(server::compression_encoding::deflate);
  EXPECT_EQ(str, "deflate");
}

TEST_F(compression_pool_tests, encoding_to_string_none) {
  auto str = as_string(server::compression_encoding::none);
  EXPECT_EQ(str, "identity");
}

TEST_F(compression_pool_tests, encoding_to_string_any) {
  auto str = as_string(server::compression_encoding::any);
  EXPECT_EQ(str, "identity");
}

#if SERVER_HAS_BROTLI
TEST_F(compression_pool_tests, encoding_to_string_brotli) {
  auto str = as_string(server::compression_encoding::br);
  EXPECT_EQ(str, "br");
}
#endif

#if SERVER_HAS_ZSTD
TEST_F(compression_pool_tests, encoding_to_string_zstd) {
  auto str = as_string(server::compression_encoding::zstd);
  EXPECT_EQ(str, "zstd");
}
#endif

// Tests for compression pool and compressor acquisition
TEST_F(compression_pool_tests, acquire_gzip_compressor) {
  auto compressor = compression_pool->acquire_compressor(server::compression_encoding::gzip);
  EXPECT_TRUE(compressor);
  EXPECT_EQ(compressor.get_compression_encoding(), server::compression_encoding::gzip);
}

TEST_F(compression_pool_tests, acquire_deflate_compressor) {
  auto compressor = compression_pool->acquire_compressor(server::compression_encoding::deflate);
  EXPECT_TRUE(compressor);
  EXPECT_EQ(compressor.get_compression_encoding(), server::compression_encoding::deflate);
}

#if SERVER_HAS_BROTLI
TEST_F(compression_pool_tests, acquire_brotli_compressor) {
  auto compressor = compression_pool->acquire_compressor(server::compression_encoding::br);
  EXPECT_TRUE(compressor);
  EXPECT_EQ(compressor.get_compression_encoding(), server::compression_encoding::br);
}
#endif

#if SERVER_HAS_ZSTD
TEST_F(compression_pool_tests, acquire_zstd_compressor) {
  auto compressor = compression_pool->acquire_compressor(server::compression_encoding::zstd);
  EXPECT_TRUE(compressor);
  EXPECT_EQ(compressor.get_compression_encoding(), server::compression_encoding::zstd);
}
#endif

// Tests for compression/decompression round-trips
TEST_F(compression_pool_tests, gzip_compression_roundtrip) {
  std::string_view test_data = "Hello, this is a test message for gzip compression!";

  auto compressed = compress_payload(test_data, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, test_data);
}

TEST_F(compression_pool_tests, deflate_compression_roundtrip) {
  std::string_view test_data = "Hello, this is a test message for deflate compression!";

  auto compressed = compress_payload(test_data, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, test_data);
}

#if SERVER_HAS_BROTLI
TEST_F(compression_pool_tests, brotli_compression_roundtrip) {
  std::string_view test_data = "Hello, this is a test message for Brotli compression!";

  auto compressed = compress_payload(test_data, server::compression_encoding::br);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::br);
  EXPECT_EQ(decompressed, test_data);
}
#endif

#if SERVER_HAS_ZSTD
TEST_F(compression_pool_tests, zstd_compression_roundtrip) {
  std::string_view test_data = "Hello, this is a test message for ZSTD compression!";

  auto compressed = compress_payload(test_data, server::compression_encoding::zstd);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::zstd);
  EXPECT_EQ(decompressed, test_data);
}
#endif

// Tests for compression efficiency
TEST_F(compression_pool_tests, gzip_compresses_repetitive_data) {
  // Create data with high repetition (compresses well)
  std::string repetitive_data;
  repetitive_data.reserve(1000);
  for (int i = 0; i < 100; ++i) {
    repetitive_data += "AAABBBCCCDDDEEEFFF";
  }

  auto compressed = compress_payload(repetitive_data, server::compression_encoding::gzip);
  EXPECT_LT(compressed.size(), repetitive_data.size());
}

TEST_F(compression_pool_tests, deflate_compresses_repetitive_data) {
  // Create data with high repetition (compresses well)
  std::string repetitive_data;
  repetitive_data.reserve(1000);
  for (int i = 0; i < 100; ++i) {
    repetitive_data += "AAABBBCCCDDDEEEFFF";
  }

  auto compressed = compress_payload(repetitive_data, server::compression_encoding::deflate);
  EXPECT_LT(compressed.size(), repetitive_data.size());
}

#if SERVER_HAS_BROTLI
TEST_F(compression_pool_tests, brotli_compresses_repetitive_data) {
  // Create data with high repetition (compresses well)
  std::string repetitive_data;
  repetitive_data.reserve(1000);
  for (int i = 0; i < 100; ++i) {
    repetitive_data += "AAABBBCCCDDDEEEFFF";
  }

  auto compressed = compress_payload(repetitive_data, server::compression_encoding::br);
  EXPECT_LT(compressed.size(), repetitive_data.size());
}
#endif

#if SERVER_HAS_ZSTD
TEST_F(compression_pool_tests, zstd_compresses_repetitive_data) {
  // Create data with high repetition (compresses well)
  std::string repetitive_data;
  repetitive_data.reserve(1000);
  for (int i = 0; i < 100; ++i) {
    repetitive_data += "AAABBBCCCDDDEEEFFF";
  }

  auto compressed = compress_payload(repetitive_data, server::compression_encoding::zstd);
  EXPECT_LT(compressed.size(), repetitive_data.size());
}
#endif

// Tests for compression negotiation
TEST_F(compression_pool_tests, negotiate_single_preference) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 1> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::gzip);
}

TEST_F(compression_pool_tests, negotiate_multiple_preferences) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 2> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
      {.encoding = server::compression_encoding::deflate, .quality = 0.5F},
  }};

  auto selected = negotiator.negotiate(prefs);
  // Should select gzip as it has highest quality
  EXPECT_EQ(selected, server::compression_encoding::gzip);
}

TEST_F(compression_pool_tests, negotiate_empty_preferences) {
  server::compression_negotiator negotiator{compression_pool};

  auto selected = negotiator.negotiate({});
  EXPECT_EQ(selected, server::compression_encoding::none);
}

// Tests for large data compression
TEST_F(compression_pool_tests, gzip_compress_large_data) {
  std::string large_data;
  large_data.reserve(100000);

  // Generate reasonably compressible data
  for (int i = 0; i < 10000; ++i) {
    large_data += "The quick brown fox jumps over the lazy dog. ";
  }

  auto compressed = compress_payload(large_data, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);
  EXPECT_LT(compressed.size(), large_data.size());

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, large_data);
}

TEST_F(compression_pool_tests, deflate_compress_large_data) {
  std::string large_data;
  large_data.reserve(100000);

  // Generate reasonably compressible data
  for (int i = 0; i < 10000; ++i) {
    large_data += "The quick brown fox jumps over the lazy dog. ";
  }

  auto compressed = compress_payload(large_data, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);
  EXPECT_LT(compressed.size(), large_data.size());

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, large_data);
}

#if SERVER_HAS_BROTLI
TEST_F(compression_pool_tests, brotli_compress_large_data) {
  std::string large_data;
  large_data.reserve(100000);

  // Generate reasonably compressible data
  for (int i = 0; i < 10000; ++i) {
    large_data += "The quick brown fox jumps over the lazy dog. ";
  }

  auto compressed = compress_payload(large_data, server::compression_encoding::br);
  EXPECT_GT(compressed.size(), 0);
  EXPECT_LT(compressed.size(), large_data.size());

  auto decompressed = decompress_payload(compressed, server::compression_encoding::br);
  EXPECT_EQ(decompressed, large_data);
}
#endif

#if SERVER_HAS_ZSTD
TEST_F(compression_pool_tests, zstd_compress_large_data) {
  std::string large_data;
  large_data.reserve(100000);

  // Generate reasonably compressible data
  for (int i = 0; i < 10000; ++i) {
    large_data += "The quick brown fox jumps over the lazy dog. ";
  }

  auto compressed = compress_payload(large_data, server::compression_encoding::zstd);
  EXPECT_GT(compressed.size(), 0);
  EXPECT_LT(compressed.size(), large_data.size());

  auto decompressed = decompress_payload(compressed, server::compression_encoding::zstd);
  EXPECT_EQ(decompressed, large_data);
}
#endif

// Tests for binary data compression
TEST_F(compression_pool_tests, gzip_compress_binary_data) {
  std::vector<std::byte> binary_data;
  binary_data.reserve(1000);

  // Create some pseudo-binary data
  for (int i = 0; i < 1000; ++i) {
    binary_data.push_back(std::byte(i % 256));
  }

  std::string_view data_view{reinterpret_cast<const char*>(binary_data.data()), binary_data.size()};
  auto compressed = compress_payload(data_view, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, data_view);
}

TEST_F(compression_pool_tests, deflate_compress_binary_data) {
  std::vector<std::byte> binary_data;
  binary_data.reserve(1000);

  // Create some pseudo-binary data
  for (int i = 0; i < 1000; ++i) {
    binary_data.push_back(std::byte(i % 256));
  }

  std::string_view data_view{reinterpret_cast<const char*>(binary_data.data()), binary_data.size()};
  auto compressed = compress_payload(data_view, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, data_view);
}

// Tests for empty data handling
TEST_F(compression_pool_tests, gzip_compress_empty_data) {
  std::string_view empty_data;

  auto compressed = compress_payload(empty_data, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);  // Even empty data produces gzip header

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, empty_data);
}

TEST_F(compression_pool_tests, deflate_compress_empty_data) {
  std::string_view empty_data;

  auto compressed = compress_payload(empty_data, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);  // Even empty data produces deflate header

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, empty_data);
}

// Tests for small data compression
TEST_F(compression_pool_tests, gzip_compress_single_byte) {
  std::string_view single_byte = "A";

  auto compressed = compress_payload(single_byte, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, single_byte);
}

TEST_F(compression_pool_tests, deflate_compress_single_byte) {
  std::string_view single_byte = "A";

  auto compressed = compress_payload(single_byte, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, single_byte);
}

// Tests for special characters
TEST_F(compression_pool_tests, gzip_compress_special_characters) {
  std::string_view special_chars = "!@#$%^&*()_+-={}[]|:;<>?,./~`\n\r\t";

  auto compressed = compress_payload(special_chars, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, special_chars);
}

TEST_F(compression_pool_tests, deflate_compress_special_characters) {
  std::string_view special_chars = "!@#$%^&*()_+-={}[]|:;<>?,./~`\n\r\t";

  auto compressed = compress_payload(special_chars, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, special_chars);
}

// Tests for UTF-8 data
TEST_F(compression_pool_tests, gzip_compress_utf8_data) {
  std::string_view utf8_data = "Hello 世界 🌍 مرحبا Привет";

  auto compressed = compress_payload(utf8_data, server::compression_encoding::gzip);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::gzip);
  EXPECT_EQ(decompressed, utf8_data);
}

TEST_F(compression_pool_tests, deflate_compress_utf8_data) {
  std::string_view utf8_data = "Hello 世界 🌍 مرحبا Привет";

  auto compressed = compress_payload(utf8_data, server::compression_encoding::deflate);
  EXPECT_GT(compressed.size(), 0);

  auto decompressed = decompress_payload(compressed, server::compression_encoding::deflate);
  EXPECT_EQ(decompressed, utf8_data);
}

// Tests for pool reuse
TEST_F(compression_pool_tests, compressor_pool_reuse) {
  std::string_view test_data = "Test data for pool reuse";

  // First compression
  {
    auto compressor = compression_pool->acquire_compressor(server::compression_encoding::gzip);
    EXPECT_TRUE(compressor);
    compress_payload(test_data, server::compression_encoding::gzip);
  }

  // Second compression - should reuse from pool
  {
    auto compressor = compression_pool->acquire_compressor(server::compression_encoding::gzip);
    EXPECT_TRUE(compressor);
    auto compressed = compress_payload(test_data, server::compression_encoding::gzip);
    EXPECT_GT(compressed.size(), 0);
  }
}

TEST_F(compression_pool_tests, decompressor_pool_reuse) {
  std::string_view test_data = "Test data for pool reuse";
  auto first_compressed = compress_payload(test_data, server::compression_encoding::gzip);

  // First decompression
  {
    auto decompressor = compression_pool->acquire_decompressor(server::compression_encoding::gzip);
    EXPECT_TRUE(decompressor);
    decompress_payload(first_compressed, server::compression_encoding::gzip);
  }

  // Second decompression - should reuse from pool
  {
    auto decompressor = compression_pool->acquire_decompressor(server::compression_encoding::gzip);
    EXPECT_TRUE(decompressor);
    auto decompressed = decompress_payload(first_compressed, server::compression_encoding::gzip);
    EXPECT_EQ(decompressed, test_data);
  }
}
