
#include <server/utils/zlib_decompress.h>

#if SERVER_HAS_ZLIB
#include <async_coro/config.h>
#include <zlib.h>

#include <cstring>

namespace server {

class zlib_decompress::impl {
 public:
  impl() noexcept  // NOLINT(*init*)
      : stream() {
  }

  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;

  ~impl() noexcept {
    ::inflateEnd(&stream);
  }

  static std::unique_ptr<impl> make_impl(zlib::compression_method method, zlib::window_bits window_bits) {
    auto impl_ptr = std::make_unique<impl>();

    int window = method == zlib::compression_method::deflate ? -window_bits.value : window_bits.value;

    if (inflateInit2(&impl_ptr->stream, window) != Z_OK) {
      impl_ptr.reset();
    }

    return impl_ptr;
  }

  z_stream stream;
};

zlib_decompress::zlib_decompress() noexcept = default;

zlib_decompress::zlib_decompress(zlib::compression_method method, zlib::window_bits window_bits)
    : _impl(impl::make_impl(method, window_bits)) {  // NOLINT(*init)
}

zlib_decompress::zlib_decompress(zlib_decompress&&) noexcept = default;
zlib_decompress& zlib_decompress::operator=(zlib_decompress&&) noexcept = default;

bool zlib_decompress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  auto& stream = _impl->stream;

  if (_is_finished) {
    _is_finished = false;
    if (::inflateReset(&stream) != Z_OK) {
      return false;
    }
  }

  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data_in.data()));  // NOLINT(*cast*)
  stream.avail_in = static_cast<uInt>(data_in.size());
  stream.next_out = reinterpret_cast<Bytef*>(data_out.data());  // NOLINT(*cast*)
  stream.avail_out = static_cast<uInt>(data_out.size());

  const auto ret = ::inflate(&stream, Z_NO_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - stream.avail_in);
  data_out = data_out.subspan(data_out.size() - stream.avail_out);

  return true;
}

bool zlib_decompress::flush_impl(std::span<const std::byte>& data_in, std::span<std::byte>& data_out, bool& finished) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  auto& stream = _impl->stream;

  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data_in.data()));  // NOLINT(*cast*)
  stream.avail_in = static_cast<uInt>(data_in.size());
  stream.next_out = reinterpret_cast<Bytef*>(data_out.data());  // NOLINT(*cast*)
  stream.avail_out = static_cast<uInt>(data_out.size());

  const auto ret = ::inflate(&stream, Z_SYNC_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    return false;
  }

  if ((ret == Z_STREAM_END) ||
      ((ret == Z_BUF_ERROR || ret == Z_OK) && stream.avail_in == 0 && stream.avail_out == data_out.size() && stream.avail_out > 0)) {
    // stream finished
    finished = true;
  }

  data_in = data_in.subspan(data_in.size() - stream.avail_in);
  data_out = data_out.subspan(data_out.size() - stream.avail_out);

  return !finished;
}

bool zlib_decompress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  return flush_impl(data_in, data_out, _is_finished);
}

bool zlib_decompress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  bool finished = false;
  return flush_impl(data_in, data_out, finished);
}

zlib_decompress::~zlib_decompress() noexcept = default;

}  // namespace server

#endif
