
#include <server/utils/zstd_compress.h>

#if SERVER_HAS_ZSTD

#include <async_coro/config.h>
#include <zstd.h>

#include <cstring>
#include <memory>

namespace server {

class zstd_compress::impl {
 public:
  impl() = delete;
  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;
  ~impl() = delete;

  ZSTD_CCtx* as_context() noexcept {
    return reinterpret_cast<ZSTD_CCtx*>(this);  // NOLINT(*reinterpret-cast)
  }

  static std::unique_ptr<impl, deleter> make_impl(zstd::compression_level compression_level, zstd::window_log window_log) {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (cctx == nullptr) {
      return {};
    }

    auto ptr = std::unique_ptr<impl, deleter>(reinterpret_cast<impl*>(cctx));  // NOLINT(*reinterpret-cast)

    size_t err = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compression_level.value);
    if (ZSTD_isError(err) != 0U) {
      return {};
    }

    if (window_log.value != 0) {
      err = ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, window_log.value);
      if (ZSTD_isError(err) != 0U) {
        return {};
      }
    }

    return ptr;
  }

  static std::unique_ptr<impl, deleter> make_impl_with_dict(std::span<const std::byte> dictionary, zstd::compression_level compression_level, zstd::window_log window_log) {
    auto impl_ptr = make_impl(compression_level, window_log);
    if (!impl_ptr) {
      return {};
    }

    // Load dictionary
    size_t err = ZSTD_CCtx_loadDictionary(impl_ptr->as_context(), dictionary.data(), dictionary.size());
    if (ZSTD_isError(err) != 0U) {
      return {};
    }

    return impl_ptr;
  }
};

void zstd_compress::deleter::operator()(zstd_compress::impl* ptr) const noexcept {
  if (ptr != nullptr) {
    ZSTD_freeCCtx(ptr->as_context());
  }
}

zstd_compress::zstd_compress() noexcept = default;

zstd_compress::zstd_compress(zstd::compression_config conf)
    : _impl(impl::make_impl(conf.compression_level, conf.window_log)) {
}

zstd_compress::zstd_compress(std::span<const std::byte> dictionary, zstd::compression_config conf)
    : _impl(impl::make_impl_with_dict(dictionary, conf.compression_level, conf.window_log)) {
}

zstd_compress::zstd_compress(zstd_compress&&) noexcept = default;
zstd_compress& zstd_compress::operator=(zstd_compress&&) noexcept = default;

bool zstd_compress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  if (_is_finished) {
    _is_finished = false;
    // Reset context for new compression
    size_t err = ZSTD_CCtx_reset(_impl->as_context(), ZSTD_reset_session_only);
    if (ZSTD_isError(err) != 0U) {
      return false;
    }
  }

  ZSTD_inBuffer input{data_in.data(), data_in.size(), 0};
  ZSTD_outBuffer output{data_out.data(), data_out.size(), 0};

  const size_t err = ZSTD_compressStream2(_impl->as_context(), &output, &input, ZSTD_e_continue);

  if (ZSTD_isError(err) != 0U) {
    return false;
  }

  data_in = data_in.subspan(input.pos);
  data_out = data_out.subspan(output.pos);

  return true;
}

bool zstd_compress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  ZSTD_inBuffer input{data_in.data(), data_in.size(), 0};
  ZSTD_outBuffer output{data_out.data(), data_out.size(), 0};

  const size_t remaining = ZSTD_compressStream2(_impl->as_context(), &output, &input, ZSTD_e_end);

  if (ZSTD_isError(remaining) != 0U) {
    return false;
  }

  data_in = data_in.subspan(input.pos);
  data_out = data_out.subspan(output.pos);

  if (remaining == 0) {
    _is_finished = true;
  }

  return !_is_finished;
}

bool zstd_compress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  ZSTD_inBuffer input{data_in.data(), data_in.size(), 0};
  ZSTD_outBuffer output{data_out.data(), data_out.size(), 0};

  const size_t remaining = ZSTD_compressStream2(_impl->as_context(), &output, &input, ZSTD_e_flush);

  if (ZSTD_isError(remaining) != 0U) {
    return false;
  }

  data_in = data_in.subspan(input.pos);
  data_out = data_out.subspan(output.pos);

  return remaining > 0;
}

zstd_compress::~zstd_compress() noexcept = default;

}  // namespace server

#endif
