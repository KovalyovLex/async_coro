#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace async_coro {

namespace internal {

enum class deinit_op {
  move,
  destroy,
};

template <size_t Size>
union small_buffer {
  static_assert(Size >= sizeof(void*), "Size of buffer to small");

  using t_move_or_destroy_f = void (*)(small_buffer& self,
                                       small_buffer* other,
                                       internal::deinit_op op) noexcept;

  void* fx;
  std::byte mem[Size];

  small_buffer() noexcept {}
  explicit small_buffer(std::nullptr_t) noexcept
      : fx(nullptr) {}
  small_buffer(const small_buffer& other) noexcept {
    std::memcpy(&mem[0], &other.mem[0], Size);
  }
  ~small_buffer() noexcept {}

  small_buffer& operator=(std::nullptr_t) noexcept {
    fx = nullptr;
    return *this;
  }

  void swap_and_reset(small_buffer& other) noexcept {
    std::memcpy(&mem[0], &other.mem[0], Size);
    other.fx = nullptr;
  }
};

template <size_t SFOSize, typename TFunc, typename T>
class function_impl_call;

}  // namespace internal

/**
 * @brief Move-only storage for erased-function internals of unique_function.
 *
 * This class acts as an RAII wrapper for a type-erased callable object,
 * decoupled from any specific function signature. It manages the lifetime
 * of the stored function object and handles its proper destruction.
 *
 * Typically used to implement custom move-only function wrappers with
 * small object optimization (SOO).
 *
 * @tparam SFOSize The size (in bytes) of the internal buffer used for
 *                 small object optimization. Defaults to `sizeof(void*)`.
 */
template <size_t SFOSize = sizeof(void*)>
class unique_function_storage {
  using t_small_buffer = internal::small_buffer<SFOSize>;
  using t_move_or_destroy_f = typename t_small_buffer::t_move_or_destroy_f;

 protected:
  struct no_init {};

  unique_function_storage(no_init) noexcept {}

 public:
  unique_function_storage() noexcept : _move_or_destroy(nullptr), _buffer(nullptr) {}

  unique_function_storage(std::nullptr_t) noexcept : unique_function_storage() {}

  unique_function_storage(const unique_function_storage&) = delete;

  unique_function_storage(unique_function_storage&& other) noexcept
      : _move_or_destroy(other._move_or_destroy), _buffer(std::move(other._buffer)) {
    if (_move_or_destroy) {
      _move_or_destroy(this->_buffer, &other._buffer, internal::deinit_op::move);
    }
    other.clear();
  }

  ~unique_function_storage() noexcept {
    if (_move_or_destroy) {
      _move_or_destroy(this->_buffer, nullptr, internal::deinit_op::destroy);
    }
  }

  unique_function_storage& operator=(const unique_function_storage&) const = delete;

  unique_function_storage& operator=(unique_function_storage&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    clear();

    if (other._move_or_destroy) {
      other._move_or_destroy(this->_buffer, &other._buffer, internal::deinit_op::move);
    } else {
      this->_buffer.swap_and_reset(other._buffer);
    }

    _move_or_destroy = std::exchange(other._move_or_destroy, nullptr);

    return *this;
  }

  unique_function_storage& operator=(std::nullptr_t) noexcept {
    clear();
    return *this;
  }

 public:
  template <typename TFunc, typename T>
  t_small_buffer& get_buffer(internal::passkey<internal::function_impl_call<SFOSize, TFunc, T>>) {
    return _buffer;
  }

 protected:
  void clear() noexcept {
    if (_move_or_destroy) {
      _move_or_destroy(this->_buffer, nullptr, internal::deinit_op::destroy);
    } else {
      this->_buffer = nullptr;
    }
    _move_or_destroy = nullptr;
  }

 protected:
  t_move_or_destroy_f _move_or_destroy;
  mutable t_small_buffer _buffer;
};

}  // namespace async_coro
