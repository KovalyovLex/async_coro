
#include <server/utils/brotli_compress.h>

#if SERVER_HAS_BROTLI

#include <async_coro/config.h>
#include <brotli/encode.h>

#include <cstring>
#include <memory>

namespace server {

class brotli_compress::impl {
 public:
  impl() noexcept = delete;
  impl(const impl&) = delete;
  impl(impl&&) = delete;
  impl& operator=(const impl&) = delete;
  impl& operator=(impl&&) = delete;
  ~impl() = delete;

  BrotliEncoderState* as_encoder_state() noexcept {
    return reinterpret_cast<BrotliEncoderState*>(this);  // NOLINT(*reinterpret-cast)
  }

  static std::unique_ptr<impl, deleter> make_impl(brotli::compression_level compression_level, brotli::window_bits window_bits, brotli::lgblock lgblock) {
    auto* state = ::BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    if (state == nullptr) {
      return {};
    }

    auto ptr = std::unique_ptr<impl, deleter>{reinterpret_cast<impl*>(state)};  // NOLINT(*reinterpret-cast)

    ::BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(compression_level.value));
    ::BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, static_cast<uint32_t>(window_bits.value));

    if (lgblock.value != 0) {
      ::BrotliEncoderSetParameter(state, BROTLI_PARAM_LGBLOCK, static_cast<uint32_t>(lgblock.value));
    }

    return ptr;
  }
};

void brotli_compress::deleter::operator()(brotli_compress::impl* ptr) const noexcept {
  if (ptr != nullptr) {
    ::BrotliEncoderDestroyInstance(ptr->as_encoder_state());
  }
}

brotli_compress::brotli_compress() noexcept = default;

brotli_compress::brotli_compress(brotli::window_bits window_bits, brotli::compression_level compression_level, brotli::lgblock lgblock)
    : _impl(impl::make_impl(compression_level, window_bits, lgblock)) {
}

brotli_compress::brotli_compress(brotli_compress&&) noexcept = default;
brotli_compress& brotli_compress::operator=(brotli_compress&&) noexcept = default;

bool brotli_compress::update_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }

  if (_is_finished) {
    _is_finished = false;
    // Can't reset brotli encoder, need to create new instance
    // For now, just mark as invalid
    return false;
  }

  const auto* input_data = reinterpret_cast<const uint8_t*>(data_in.data());  // NOLINT(*reinterpret-cast)
  size_t input_size = data_in.size();
  auto* output_data = reinterpret_cast<uint8_t*>(data_out.data());  // NOLINT(*reinterpret-cast)
  size_t output_size = data_out.size();

  const auto success = ::BrotliEncoderCompressStream(_impl->as_encoder_state(), BROTLI_OPERATION_PROCESS, &input_size, &input_data, &output_size, &output_data, nullptr);

  if (success == 0) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - input_size);
  data_out = data_out.subspan(data_out.size() - output_size);

  return true;
}

bool brotli_compress::end_stream(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  const auto* input_data = reinterpret_cast<const uint8_t*>(data_in.data());  // NOLINT(*reinterpret-cast)
  size_t input_size = data_in.size();
  auto* output_data = reinterpret_cast<uint8_t*>(data_out.data());  // NOLINT(*reinterpret-cast)
  size_t output_size = data_out.size();

  const auto success = ::BrotliEncoderCompressStream(_impl->as_encoder_state(), BROTLI_OPERATION_FINISH, &input_size, &input_data, &output_size, &output_data, nullptr);

  if (success == 0) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - input_size);
  data_out = data_out.subspan(data_out.size() - output_size);

  if (::BrotliEncoderIsFinished(_impl->as_encoder_state()) != 0) {
    _is_finished = true;
  }

  return !_is_finished;
}

bool brotli_compress::flush(std::span<const std::byte>& data_in, std::span<std::byte>& data_out) noexcept {
  if (!_impl) {
    return false;
  }
  ASYNC_CORO_ASSERT(!_is_finished);

  const auto* input_data = reinterpret_cast<const uint8_t*>(data_in.data());  // NOLINT(*reinterpret-cast)
  size_t input_size = data_in.size();
  auto* output_data = reinterpret_cast<uint8_t*>(data_out.data());  // NOLINT(*reinterpret-cast)
  size_t output_size = data_out.size();

  const auto success = BrotliEncoderCompressStream(_impl->as_encoder_state(), BROTLI_OPERATION_EMIT_METADATA, &input_size, &input_data, &output_size, &output_data, nullptr);

  if (success == 0) {
    return false;
  }

  data_in = data_in.subspan(data_in.size() - input_size);
  data_out = data_out.subspan(data_out.size() - output_size);

  return output_size == 0;
}

brotli_compress::~brotli_compress() noexcept = default;

}  // namespace server

#endif
