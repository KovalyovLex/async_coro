#pragma once

#include <server/utils/has_brotli.h>

#if SERVER_HAS_BROTLI

#include <server/utils/brotli_compression_constants.h>

#include <memory>
#include <span>

namespace server {

class brotli_decompress {
 public:
  brotli_decompress() noexcept;
  explicit brotli_decompress(bool init_decompressor);
  brotli_decompress(const brotli_decompress&) = delete;
  brotli_decompress(brotli_decompress&&) noexcept;

  ~brotli_decompress() noexcept;

  brotli_decompress& operator=(const brotli_decompress&) = delete;
  brotli_decompress& operator=(brotli_decompress&&) noexcept;

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
  struct deleter {
    void operator()(impl*) const noexcept;
  };
  std::unique_ptr<impl, deleter> _impl;
  bool _is_finished = false;
};

}  // namespace server

#endif
