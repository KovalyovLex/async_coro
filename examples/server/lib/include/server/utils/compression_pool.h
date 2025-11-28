#pragma once

#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/utils/passkey.h>
#include <server/utils/has_brotli.h>
#include <server/utils/has_zlib.h>
#include <server/utils/has_zstd.h>

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#if SERVER_HAS_ZLIB
#include <server/utils/zlib_compress.h>
#include <server/utils/zlib_decompress.h>
#endif

#if SERVER_HAS_ZSTD
#include <server/utils/zstd_compress.h>
#include <server/utils/zstd_decompress.h>
#endif

#if SERVER_HAS_BROTLI
#include <server/utils/brotli_compress.h>
#include <server/utils/brotli_decompress.h>
#endif

namespace server {

// Compression encoding types
enum class compression_encoding : std::uint8_t {
#if SERVER_HAS_ZLIB
  deflate,  // zlib with raw deflate
  gzip,     // zlib with gzip headers
#endif
#if SERVER_HAS_ZSTD
  zstd,
#endif
#if SERVER_HAS_BROTLI
  br,  // brotli
#endif
  any,
  none
};

// Configuration for a single compression encoding
struct compression_config {
  static constexpr uint32_t default_max_pool_size = 16;

  compression_encoding encoding = compression_encoding::none;
  uint32_t max_pool_size = default_max_pool_size;  // Max compressors/decompressors to keep per encoding
};

// Compression pool configuration
struct compression_pool_config {
  std::vector<compression_config> encodings;

  static constexpr auto k_all_encodings = {
#if SERVER_HAS_ZSTD
      compression_config{.encoding = compression_encoding::zstd},
#endif
#if SERVER_HAS_BROTLI
      compression_config{.encoding = compression_encoding::br},
#endif
#if SERVER_HAS_ZLIB
      compression_config{.encoding = compression_encoding::gzip},
      compression_config{.encoding = compression_encoding::deflate},
#endif
  };
};

using compressor_variant = std::variant<std::monostate
#if SERVER_HAS_ZLIB
                                        ,
                                        zlib_compress
#endif
#if SERVER_HAS_ZSTD
                                        ,
                                        zstd_compress
#endif
#if SERVER_HAS_BROTLI
                                        ,
                                        brotli_compress
#endif
                                        >;

using decompressor_variant = std::variant<std::monostate
#if SERVER_HAS_ZLIB
                                          ,
                                          zlib_decompress
#endif
#if SERVER_HAS_ZSTD
                                          ,
                                          zstd_decompress
#endif
#if SERVER_HAS_BROTLI
                                          ,
                                          brotli_decompress
#endif
                                          >;

class compression_pool;

// RAII wrapper for compressor/decompressor that returns to pool on destruction
template <typename VariantT>
class pooled_compressor {
 public:
  pooled_compressor() noexcept = default;
  pooled_compressor(VariantT value, compression_encoding enc, std::weak_ptr<compression_pool> pool_to_return) noexcept
      : _value(std::move(value)),
        _encoding(enc),
        _pool_to_return(std::move(pool_to_return)) {}

  pooled_compressor(const pooled_compressor&) = delete;
  pooled_compressor& operator=(const pooled_compressor&) = delete;

  pooled_compressor(pooled_compressor&& other) noexcept
      : _value(std::move(other._value)),
        _encoding(other._encoding),
        _pool_to_return(std::move(other._pool_to_return)) {
    other._pool_to_return.reset();
  }

  pooled_compressor& operator=(pooled_compressor&& other) noexcept {
    if (this != &other) {
      reset();
      _value = std::move(other._value);
      _encoding = other._encoding;
      _pool_to_return = std::move(other._pool_to_return);
      other._pool_to_return.reset();
    }
    return *this;
  }

  ~pooled_compressor() noexcept {
    reset();
  }

  [[nodiscard]] VariantT& operator*() & noexcept { return _value; }
  [[nodiscard]] VariantT&& operator*() && noexcept { return std::move(_value); }
  [[nodiscard]] const VariantT& operator*() const& noexcept { return _value; }

  [[nodiscard]] explicit operator bool() const noexcept {
    return std::visit(
        [](auto& compressor) noexcept {
          using T = std::decay_t<decltype(compressor)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return compressor.is_valid();
          }
        },
        _value);
  }

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false in case of errors in compression. Compression can't be used then.
  // NB: data_in can be consumed partially. All the remaining data should be sent to end_stream then
  bool update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
    return std::visit(
        [&data_in, &data_out](auto& compressor) noexcept {
          using T = std::decay_t<decltype(compressor)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return compressor.update_stream(data_in, data_out);
          }
        },
        _value);
  }

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when compression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
    return std::visit(
        [&data_in, &data_out](auto& compressor) noexcept {
          using T = std::decay_t<decltype(compressor)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return compressor.end_stream(data_in, data_out);
          }
        },
        _value);
  }

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when compression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
    return std::visit(
        [&data_in, &data_out](auto& compressor) noexcept {
          using T = std::decay_t<decltype(compressor)>;
          if constexpr (std::is_same_v<T, std::monostate>) {
            return false;
          } else {
            return compressor.flush(data_in, data_out);
          }
        },
        _value);
  }

  [[nodiscard]] compression_encoding get_compression_encoding() const noexcept {
    return _encoding;
  }

  void reset() noexcept;

 private:
  VariantT _value{};
  compression_encoding _encoding = compression_encoding::none;
  std::weak_ptr<compression_pool> _pool_to_return;
};

// Pool for managing compressor/decompressor instances
class compression_pool : std::enable_shared_from_this<compression_pool> {
 public:
  using ptr = std::shared_ptr<compression_pool>;

  compression_pool(compression_pool_config config, async_coro::passkey_successors<compression_pool>) noexcept;

  ~compression_pool() noexcept = default;

  // Delete copy\move operations
  compression_pool(const compression_pool&) = delete;
  compression_pool(compression_pool&&) noexcept = delete;
  compression_pool& operator=(const compression_pool&) = delete;
  compression_pool& operator=(compression_pool&&) noexcept = delete;

  // Acquire a compressor for the given encoding
  [[nodiscard]] pooled_compressor<compressor_variant> acquire_compressor(compression_encoding encoding) noexcept;

  // Acquire a decompressor for the given encoding
  [[nodiscard]] pooled_compressor<decompressor_variant> acquire_decompressor(compression_encoding encoding) noexcept;

  // Configuration getters
  [[nodiscard]] const compression_pool_config& get_config() const noexcept { return _config; }

  void return_to_pool(pooled_compressor<compressor_variant>&& comp) noexcept;
  void return_to_pool(pooled_compressor<decompressor_variant>&& comp) noexcept;

  static std::shared_ptr<compression_pool> create(compression_pool_config config);

 private:
  static compressor_variant create_compressor(compression_encoding encoding) noexcept;
  static decompressor_variant create_decompressor(compression_encoding encoding) noexcept;

 private:
  struct compressor_entry {
    std::vector<compressor_variant> stack CORO_THREAD_GUARDED_BY(mutex);
    async_coro::mutex mutex;
  };
  struct decompressor_entry {
    std::vector<decompressor_variant> stack CORO_THREAD_GUARDED_BY(mutex);
    async_coro::mutex mutex;
  };

  static constexpr size_t k_num_compressors = size_t(compression_encoding::any) == 0 ? 1 : size_t(compression_encoding::any);

  compression_pool_config _config;
  std::array<compressor_entry, k_num_compressors> _compressors;      // array by type and config
  std::array<decompressor_entry, k_num_compressors> _decompressors;  // array by type and config
};

template <typename VariantT>
inline void pooled_compressor<VariantT>::reset() noexcept {
  if (auto pool = _pool_to_return.lock()) {
    _pool_to_return.reset();
    pool->return_to_pool(std::move(*this));
  }
}

}  // namespace server
