#pragma once

#include <server/utils/has_zlib.h>

#if SERVER_HAS_ZLIB
#include <server/utils/zlib_compression_constants.h>

#include <memory>
#include <span>

namespace server {

class zlib_compress {
 public:
  zlib_compress() noexcept;
  explicit zlib_compress(zlib::compression_method method, zlib::window_bits window_bits = {}, zlib::compression_level compression_level = {}, zlib::memory_level memory_level = {});
  zlib_compress(const zlib_compress&) = delete;
  zlib_compress(zlib_compress&&) noexcept;

  ~zlib_compress() noexcept;

  zlib_compress& operator=(const zlib_compress&) = delete;
  zlib_compress& operator=(zlib_compress&&) noexcept;

  // Modifies spans removing consumend or written data from them (shifts right beginning of spans).
  // Returns false in case of errors in compression. Compression can't be used then.
  // NB: data_in can be consumed partially. All the remaining data should be send to end_stream then
  bool update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumend or written data from them (shifts right beginning of spans).
  // Returns false when compression ends or error happened.
  // Returns true if more output buffer space needed. Should be called in a while loop then.
  bool end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept;

  // Modifies spans removing consumend or written data from them (shifts right beginning of spans).
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
