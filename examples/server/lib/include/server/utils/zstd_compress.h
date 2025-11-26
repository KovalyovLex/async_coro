#pragma once

#include <server/utils/has_zstd.h>

#if SERVER_HAS_ZSTD
#include <server/utils/zstd_compression_constants.h>

#include <memory>
#include <span>

namespace server {

class zstd_compress {
 public:
  zstd_compress() noexcept;
  explicit zstd_compress(zstd::compression_level compression_level = {}, zstd::window_log window_log = {});

  // Constructor with optional dictionary
  explicit zstd_compress(std::span<const std::byte> dictionary, zstd::compression_level compression_level = {}, zstd::window_log window_log = {});

  zstd_compress(const zstd_compress&) = delete;
  zstd_compress(zstd_compress&&) noexcept;

  ~zstd_compress() noexcept;

  zstd_compress& operator=(const zstd_compress&) = delete;
  zstd_compress& operator=(zstd_compress&&) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false in case of errors in compression. Compression can't be used then.
  // NB: data_in can be consumed partially. All the remaining data should be sent to end_stream then
  bool update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when compression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when compression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return _impl != nullptr; }

  explicit operator bool() const noexcept { return _impl != nullptr; }

 private:
  class impl;
  std::unique_ptr<impl> _impl;
  bool _is_finished = false;
};

}  // namespace server

#endif
