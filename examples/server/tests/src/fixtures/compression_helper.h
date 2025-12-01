#pragma once

#include <gtest/gtest.h>
#include <server/utils/compression_pool.h>

// class - extension for fixtures. It stores compression poll and use it for compress\decompress
class compression_helper {
 protected:
  // Helper: compress payload using the compression pool
  std::vector<std::byte> compress_payload(std::string_view payload, server::compression_encoding encoding) {
    EXPECT_TRUE(compression_pool != nullptr);
    if (!compression_pool) {
      return {};
    }

    auto compressor = compression_pool->acquire_compressor(encoding);
    if (!compressor) {
      return {};
    }

    std::vector<std::byte> compressed_payload;
    compressed_payload.reserve(payload.size() * 2);
    std::span<const std::byte> data_in{reinterpret_cast<const std::byte*>(payload.data()), payload.size()};
    std::array<std::byte, 256> tmp_buffer{};

    while (!data_in.empty()) {
      std::span<std::byte> data_out = tmp_buffer;
      EXPECT_TRUE(compressor.update_stream(data_in, data_out));
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      compressed_payload.insert(compressed_payload.end(), data_to_copy.begin(), data_to_copy.end());
    }

    // End the compression stream
    while (true) {
      std::span<std::byte> data_out = tmp_buffer;
      bool has_more = compressor.end_stream(data_in, data_out);
      std::span data_to_copy{tmp_buffer.data(), data_out.data()};
      compressed_payload.insert(compressed_payload.end(), data_to_copy.begin(), data_to_copy.end());
      if (!has_more) {
        break;
      }
    }

    return compressed_payload;
  }

  // Helper: decompress payload using the compression pool
  std::string decompress_payload(const std::vector<std::byte>& compressed_data, server::compression_encoding encoding) {
    EXPECT_TRUE(compression_pool != nullptr);
    if (!compression_pool) {
      return {};
    }

    auto decompressor = compression_pool->acquire_decompressor(encoding);
    if (!decompressor) {
      return {};
    }

    std::string decompressed_data;
    decompressed_data.reserve(compressed_data.size() * 4);
    std::span<const std::byte> data_in{compressed_data.data(), compressed_data.size()};
    std::array<char, 256> tmp_buffer{};

    while (!data_in.empty()) {
      std::span<std::byte> data_out = std::as_writable_bytes(std::span{tmp_buffer});
      EXPECT_TRUE(decompressor.update_stream(data_in, data_out));
      std::string_view data_to_copy{tmp_buffer.data(), tmp_buffer.size() - data_out.size()};

      decompressed_data += data_to_copy;
    }

    // End the decompression stream
    while (true) {
      std::span<std::byte> data_out = std::as_writable_bytes(std::span{tmp_buffer});
      bool has_more = decompressor.end_stream(data_in, data_out);
      std::string_view data_to_copy{tmp_buffer.data(), tmp_buffer.size() - data_out.size()};

      decompressed_data += data_to_copy;
      if (!has_more) {
        break;
      }
    }

    return decompressed_data;
  }

 protected:
  server::compression_pool::ptr compression_pool;
};
