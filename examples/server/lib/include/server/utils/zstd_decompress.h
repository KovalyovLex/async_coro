#pragma once

#include <server/utils/has_zstd.h>

#if SERVER_HAS_ZSTD

#include <server/utils/zstd_compression_constants.h>

#include <memory>
#include <span>

namespace server {

class zstd_decompress {
 public:
  zstd_decompress() noexcept;
  explicit zstd_decompress(zstd::window_log window_log = {});

  // Constructor with optional dictionary
  explicit zstd_decompress(std::span<const std::byte> dictionary, zstd::window_log window_log = {});

  zstd_decompress(const zstd_decompress&) = delete;
  zstd_decompress(zstd_decompress&&) noexcept;

  ~zstd_decompress() noexcept;

  zstd_decompress& operator=(const zstd_decompress&) = delete;
  zstd_decompress& operator=(zstd_decompress&&) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans)
  // Returns false in case of errors in decompression. Decompression can't be used then.
  // NB: data_in can be consumed partially. All the remaining data should be sent to end_stream then
  bool update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when decompression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumed or written data from them (shifts right beginning of spans).
  // Returns false when decompression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return _impl != nullptr; }

  explicit operator bool() const noexcept { return _impl != nullptr; }

 private:
  bool flush_impl(std::span<const std::byte>& data_in, std::span<std::byte>& data_out, bool& finished) noexcept;

 private:
  class impl;
  std::unique_ptr<impl> _impl;
  bool _is_finished = false;
};

}  // namespace server

#endif
