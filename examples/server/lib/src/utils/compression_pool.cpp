#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/utils/passkey.h>
#include <server/utils/brotli_compression_constants.h>
#include <server/utils/compression_pool.h>
#include <server/utils/zlib_compression_constants.h>
#include <server/utils/zstd_compression_constants.h>

#include <cstdint>
#include <memory>

namespace server {

compression_pool::compression_pool(compression_pool_config config, async_coro::passkey_successors<compression_pool> /*key*/) noexcept
    : _config(std::move(config)) {}

compressor_variant compression_pool::create_compressor(compression_encoding encoding) noexcept {
#if SERVER_HAS_ZLIB
  if (encoding == compression_encoding::deflate) {
    return compressor_variant{std::in_place_type<zlib_compress>, zlib::compression_config{.method = zlib::compression_method::deflate}};
  }
  if (encoding == compression_encoding::gzip) {
    return compressor_variant{std::in_place_type<zlib_compress>, zlib::compression_config{.method = zlib::compression_method::gzip}};
  }
#endif

#if SERVER_HAS_ZSTD
  if (encoding == compression_encoding::zstd) {
    return compressor_variant{std::in_place_type<zstd_compress>, zstd::compression_config{}};
  }
#endif

#if SERVER_HAS_BROTLI
  if (encoding == compression_encoding::br) {
    return compressor_variant{std::in_place_type<brotli_compress>, brotli::compression_config{}};
  }
#endif

  // Return monostate for unsupported encoding
  return compressor_variant{};
}

decompressor_variant compression_pool::create_decompressor(compression_encoding encoding) noexcept {
#if SERVER_HAS_ZLIB
  if (encoding == compression_encoding::deflate) {
    return decompressor_variant{std::in_place_type<zlib_decompress>, zlib::decompression_config{.method = zlib::compression_method::deflate}};
  }
  if (encoding == compression_encoding::gzip) {
    return decompressor_variant{std::in_place_type<zlib_decompress>, zlib::decompression_config{.method = zlib::compression_method::gzip}};
  }
#endif

#if SERVER_HAS_ZSTD
  if (encoding == compression_encoding::zstd) {
    return decompressor_variant{std::in_place_type<zstd_decompress>, zstd::decompression_config{}};
  }
#endif

#if SERVER_HAS_BROTLI
  if (encoding == compression_encoding::br) {
    return decompressor_variant{std::in_place_type<brotli_decompress>, brotli::decompression_config{}};
  }
#endif

  // Return monostate for unsupported encoding
  return decompressor_variant{};
}

pooled_compressor<compressor_variant> compression_pool::acquire_compressor(compression_encoding encoding) noexcept {
  if (encoding == compression_encoding::any || encoding == compression_encoding::none) {
    return {};
  }

  const auto idx = uint32_t(encoding);
  if (idx >= _compressors.size()) {
    return {};
  }

  auto& pool_entry = _compressors[idx];  // NOLINT(*array-index)

  {
    async_coro::unique_lock lock{pool_entry.mutex};

    if (!pool_entry.stack.empty()) {
      auto compressor = std::move(pool_entry.stack.back());
      pool_entry.stack.pop_back();
      return {std::move(compressor),
              encoding,
              this->weak_from_this()};
    }
  }

  // Create new compressor if pool is empty
  auto compressor = create_compressor(encoding);
  return {std::move(compressor),
          encoding,
          this->weak_from_this()};
}

pooled_compressor<decompressor_variant> compression_pool::acquire_decompressor(compression_encoding encoding) noexcept {
  if (encoding == compression_encoding::any || encoding == compression_encoding::none) {
    return {};
  }

  const auto idx = uint32_t(encoding);
  if (idx >= _decompressors.size()) {
    return {};
  }

  auto& pool_entry = _decompressors[idx];  // NOLINT(*array-index)

  {
    async_coro::unique_lock lock{pool_entry.mutex};

    if (!pool_entry.stack.empty()) {
      auto compressor = std::move(pool_entry.stack.back());
      pool_entry.stack.pop_back();
      return {std::move(compressor),
              encoding,
              this->weak_from_this()};
    }
  }

  // Create new decompressor if pool is empty
  auto decompressor = create_decompressor(encoding);
  return {std::move(decompressor),
          encoding,
          this->weak_from_this()};
}

void compression_pool::return_to_pool(pooled_compressor<compressor_variant>&& comp) noexcept {
  const auto idx = static_cast<uint32_t>(comp.get_compression_encoding());

  if (idx >= _compressors.size()) {
    return;
  }

  auto& pool_entry = _compressors[idx];  // NOLINT(*array-index)

  {
    async_coro::unique_lock lock{pool_entry.mutex};

    if (pool_entry.stack.size() < _config.encodings[idx].max_pool_size) {
      pool_entry.stack.emplace_back(*std::move(comp));
    }
  }
}

void compression_pool::return_to_pool(pooled_compressor<decompressor_variant>&& comp) noexcept {
  const auto idx = static_cast<uint32_t>(comp.get_compression_encoding());

  if (idx >= _decompressors.size()) {
    return;
  }

  auto& pool_entry = _decompressors[idx];  // NOLINT(*array-index)

  {
    async_coro::unique_lock lock{pool_entry.mutex};

    if (pool_entry.stack.size() < _config.encodings[idx].max_pool_size) {
      pool_entry.stack.emplace_back(*std::move(comp));
    }
  }
}

std::shared_ptr<compression_pool> compression_pool::create(compression_pool_config config) {
  return std::make_shared<compression_pool>(std::move(config), async_coro::passkey<compression_pool>{});
}

}  // namespace server
