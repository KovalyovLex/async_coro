
#include <server/utils/zlib_compress.h>

#include <cstdlib>

#if SERVER_HAS_ZLIB

#include <async_coro/config.h>
#include <zlib.h>

#include <cstring>
#include <memory>

namespace server {

static_assert(zlib::compression_level{}.value == Z_DEFAULT_COMPRESSION);

class zlib_compress::impl {
 public:
  impl() noexcept  // NOLINT(*init*)
      : stream() {
  }

  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;

  ~impl() noexcept {
    ::deflateEnd(&stream);
  }

  static std::unique_ptr<impl> make_impl(zlib::compression_method method, zlib::compression_level compression_level, zlib::window_bits window_bits, zlib::memory_level memory_level) {
    auto impl_ptr = std::make_unique<impl>();

    int window = method == zlib::compression_method::deflate ? -window_bits.value : window_bits.value;

    if (deflateInit2(&impl_ptr->stream, compression_level.value, Z_DEFLATED, window, memory_level.value, Z_DEFAULT_STRATEGY) != Z_OK) {
      impl_ptr.reset();
    }

    return impl_ptr;
  }

  z_stream stream;
};

zlib_compress::zlib_compress() noexcept = default;

zlib_compress::zlib_compress(zlib::compression_config conf)
    : _impl(impl::make_impl(conf.method, conf.compression_level, conf.window_bits, conf.memory_level)) {
}

zlib_compress::zlib_compress(zlib_compress&&) noexcept = default;
zlib_compress& zlib_compress::operator=(zlib_compress&&) noexcept = default;

bool zlib_compress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  auto& stream = _impl->stream;

  if (_is_finished) {
    _is_finished = false;
    if (::deflateReset(&stream) != Z_OK) {
      return false;
    }
  }

  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data_in.data()));  // NOLINT(*cast*)
  stream.avail_in = static_cast<uInt>(data_in.size());
  stream.next_out = reinterpret_cast<Bytef*>(data_out.data());  // NOLINT(*cast*)
  stream.avail_out = static_cast<uInt>(data_out.size());

  const auto ret = ::deflate(&stream, Z_NO_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - stream.avail_in);
  data_out = data_out.subspan(data_out.size() - stream.avail_out);

  return true;
}

bool zlib_compress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  auto& stream = _impl->stream;

  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data_in.data()));  // NOLINT(*cast*)
  stream.avail_in = static_cast<uInt>(data_in.size());
  stream.next_out = reinterpret_cast<Bytef*>(data_out.data());  // NOLINT(*cast*)
  stream.avail_out = static_cast<uInt>(data_out.size());

  const auto ret = ::deflate(&stream, Z_FINISH);
  if (ret == Z_STREAM_ERROR) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - stream.avail_in);
  data_out = data_out.subspan(data_out.size() - stream.avail_out);

  if (ret == Z_STREAM_END) {
    _is_finished = true;
  }

  return !_is_finished;
}

bool zlib_compress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);
  ASYNC_CORO_ASSERT(data_out.size() > 6);

  auto& stream = _impl->stream;

  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data_in.data()));  // NOLINT(*cast*)
  stream.avail_in = static_cast<uInt>(data_in.size());
  stream.next_out = reinterpret_cast<Bytef*>(data_out.data());  // NOLINT(*cast*)
  stream.avail_out = static_cast<uInt>(data_out.size());

  const auto ret = ::deflate(&stream, Z_SYNC_FLUSH);
  if (ret == Z_STREAM_ERROR) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - stream.avail_in);
  data_out = data_out.subspan(data_out.size() - stream.avail_out);

  return stream.avail_out == 0;
}

zlib_compress::~zlib_compress() noexcept = default;

}  // namespace server

#endif
