
#include <server/utils/brotli_decompress.h>

#if SERVER_HAS_BROTLI

#include <async_coro/config.h>
#include <brotli/decode.h>

#include <cstring>
#include <memory>

namespace server {

class brotli_decompress::impl {
 public:
  impl() noexcept = delete;
  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;

  ~impl() noexcept {
    ::BrotliDecoderDestroyInstance(as_decoder_state());
  }

  BrotliDecoderState* as_decoder_state() noexcept {
    return reinterpret_cast<BrotliDecoderState*>(this);  // NOLINT(*reinterpret-cast)
  }

  static std::unique_ptr<impl> make_impl(brotli::window_bits window_bits) {
    auto* state = ::BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (state == nullptr) {
      return {};
    }

    return std::unique_ptr<impl>{reinterpret_cast<impl*>(state)};  // NOLINT(*reinterpret-cast)
  }
};

brotli_decompress::brotli_decompress() noexcept = default;

brotli_decompress::brotli_decompress(brotli::window_bits window_bits)
    : _impl(impl::make_impl(window_bits)) {
}

brotli_decompress::brotli_decompress(brotli_decompress&&) noexcept = default;
brotli_decompress& brotli_decompress::operator=(brotli_decompress&&) noexcept = default;

bool brotli_decompress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  if (_is_finished) {
    _is_finished = false;
    // Can't reset brotli decoder, need to create new instance
    // For now, just mark as invalid
    return false;
  }

  const auto* input_data = reinterpret_cast<const uint8_t*>(data_in.data());  // NOLINT(*reinterpret-cast)
  size_t input_size = data_in.size();
  auto* output_data = reinterpret_cast<uint8_t*>(data_out.data());  // NOLINT(*reinterpret-cast)
  size_t output_size = data_out.size();

  const auto result = ::BrotliDecoderDecompressStream(_impl->as_decoder_state(), &input_size, &input_data, &output_size, &output_data, nullptr);

  if (result == BROTLI_DECODER_RESULT_ERROR) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - input_size);
  data_out = data_out.subspan(data_out.size() - output_size);

  return true;
}

bool brotli_decompress::flush_impl(std::span<const std::byte>& data_in, std::span<std::byte>& data_out, bool& finished) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  const auto* input_data = reinterpret_cast<const uint8_t*>(data_in.data());  // NOLINT(*reinterpret-cast)
  size_t input_size = data_in.size();
  auto* output_data = reinterpret_cast<uint8_t*>(data_out.data());  // NOLINT(*reinterpret-cast)
  size_t output_size = data_out.size();

  const auto result = ::BrotliDecoderDecompressStream(_impl->as_decoder_state(), &input_size, &input_data, &output_size, &output_data, nullptr);

  if (result == BROTLI_DECODER_RESULT_ERROR) {
    return false;
  }

  if (result == BROTLI_DECODER_RESULT_SUCCESS) {
    finished = true;
  }

  data_in = data_in.subspan(data_in.size() - input_size);
  data_out = data_out.subspan(data_out.size() - output_size);

  return !finished;
}

bool brotli_decompress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  return flush_impl(data_in, data_out, _is_finished);
}

bool brotli_decompress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  bool finished = false;
  return flush_impl(data_in, data_out, finished);
}

brotli_decompress::~brotli_decompress() noexcept = default;

}  // namespace server

#endif
