
#include <server/utils/zstd_decompress.h>

#if SERVER_HAS_ZSTD

#include <async_coro/config.h>
#include <zstd.h>

#include <cstring>
#include <memory>

namespace server {

class zstd_decompress::impl {
 public:
  impl() = delete;
  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;
  ~impl() = delete;

  ZSTD_DCtx* as_context() noexcept {
    return reinterpret_cast<ZSTD_DCtx*>(this);  // NOLINT(*reinterpret-cast)
  }

  static std::unique_ptr<impl, deleter> make_impl(zstd::window_log window_log) {
    auto* dctx = ZSTD_createDCtx();
    if (dctx == nullptr) {
      return {};
    }

    auto ptr = std::unique_ptr<impl, deleter>(reinterpret_cast<impl*>(dctx));  // NOLINT(*reinterpret-cast)

    if (window_log.value != 0) {
      size_t err = ZSTD_DCtx_setParameter(dctx, ZSTD_d_windowLogMax, window_log.value);
      if (ZSTD_isError(err) != 0U) {
        return {};
      }
    }

    return ptr;
  }

  static std::unique_ptr<impl, deleter> make_impl_with_dict(std::span<const std::byte> dictionary, zstd::window_log window_log) {
    auto impl_ptr = make_impl(window_log);
    if (!impl_ptr) {
      return {};
    }

    // Load dictionary
    size_t err = ZSTD_DCtx_loadDictionary(impl_ptr->as_context(), dictionary.data(), dictionary.size());
    if (ZSTD_isError(err) != 0U) {
      return {};
    }

    return impl_ptr;
  }
};

void zstd_decompress::deleter::operator()(zstd_decompress::impl* ptr) const noexcept {
  if (ptr != nullptr) {
    ZSTD_freeDCtx(ptr->as_context());
  }
}

zstd_decompress::zstd_decompress() noexcept = default;

zstd_decompress::zstd_decompress(zstd::window_log window_log)
    : _impl(impl::make_impl(window_log)) {
}

zstd_decompress::zstd_decompress(std::span<const std::byte> dictionary, zstd::window_log window_log)
    : _impl(impl::make_impl_with_dict(dictionary, window_log)) {
}

zstd_decompress::zstd_decompress(zstd_decompress&&) noexcept = default;
zstd_decompress& zstd_decompress::operator=(zstd_decompress&&) noexcept = default;

bool zstd_decompress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  if (_is_finished) {
    _is_finished = false;
    _depleted = false;
    // Reset context for new decompression
    size_t err = ZSTD_DCtx_reset(_impl->as_context(), ZSTD_reset_session_only);
    if (ZSTD_isError(err) != 0U) {
      return false;
    }
  }

  ZSTD_inBuffer input{data_in.data(), data_in.size(), 0};
  ZSTD_outBuffer output{data_out.data(), data_out.size(), 0};

  const auto remaining = ZSTD_decompressStream(_impl->as_context(), &output, &input);

  if (ZSTD_isError(remaining) != 0U) {
    return false;
  }

  if (remaining == 0) {
    _depleted = true;
  }

  data_in = data_in.subspan(input.pos);
  data_out = data_out.subspan(output.pos);

  return true;
}

bool zstd_decompress::flush_impl(std::span<const std::byte>& data_in, std::span<std::byte>& data_out, bool& finished) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  ZSTD_inBuffer input{data_in.data(), data_in.size(), 0};
  ZSTD_outBuffer output{data_out.data(), data_out.size(), 0};

  const auto remaining = ZSTD_decompressStream(_impl->as_context(), &output, &input);

  if (ZSTD_isError(remaining) != 0U) {
    return false;
  }

  if (remaining == 0 || (_depleted && data_in.size() == 0)) {
    _depleted = true;
    finished = true;
  }

  data_in = data_in.subspan(input.pos);
  data_out = data_out.subspan(output.pos);

  return !finished;
}

bool zstd_decompress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  return flush_impl(data_in, data_out, _is_finished);
}

bool zstd_decompress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  bool finished = false;
  return flush_impl(data_in, data_out, finished);
}

zstd_decompress::~zstd_decompress() noexcept = default;

}  // namespace server

#endif
