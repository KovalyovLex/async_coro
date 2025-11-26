#pragma once

#include <server/utils/has_brotli.h>

#if SERVER_HAS_BROTLI
#include <server/utils/brotli_compression_constants.h>

#include <memory>
#include <span>

namespace server {

class brotli_compress {
 public:
  brotli_compress() noexcept;
  explicit brotli_compress(brotli::compression_level compression_level = {}, brotli::window_bits window_bits = {}, brotli::lgblock lgblock = {});
  brotli_compress(const brotli_compress&) = delete;
  brotli_compress(brotli_compress&&) noexcept;

  ~brotli_compress() noexcept;

  brotli_compress& operator=(const brotli_compress&) = delete;
  brotli_compress& operator=(brotli_compress&&) noexcept;

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
