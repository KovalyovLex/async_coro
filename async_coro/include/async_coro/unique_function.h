#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/always_false.h>
#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/internal/is_invocable_by_signature.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro {
namespace internal {

template <size_t Size>
union small_buffer {
  static_assert(Size >= sizeof(void*), "Size of buffer to small");

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
class function_impl_call {
  static_assert(always_false<T>::value,
                "unique_function only accepts function types as template arguments, "
                "with possibly noexcept qualifiers.");
};

template <size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...)> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...);

  inline static constexpr bool is_noexcept_invoke = false;

  function_impl_call() noexcept {}

  function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) const {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<const TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;
};

template <size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...) noexcept> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...) noexcept;

  inline static constexpr bool is_noexcept_invoke = true;

  function_impl_call() noexcept {}

  function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) const noexcept {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<const TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;
};
}  // namespace internal

template <typename FTy, size_t SFOSize = sizeof(void*)>
class unique_function : public internal::function_impl_call<SFOSize, unique_function<FTy, SFOSize>, FTy> {
  using super = internal::function_impl_call<SFOSize, unique_function<FTy, SFOSize>, FTy>;

  friend super;

  enum deinit_op {
    action_move,
    action_destroy,
  };

  using t_move_or_destroy_f = void (*)(unique_function& self,
                                       unique_function* other,
                                       deinit_op op) noexcept;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<FTy, Fx>;

  using t_small_buffer = typename super::t_small_buffer;

  template <typename Fx>
  inline static constexpr bool is_small_f =
      sizeof(Fx) <= SFOSize && alignof(Fx) <= alignof(t_small_buffer);

  template <typename Fx>
  inline static constexpr bool is_noexcept_init =
      is_small_f<std::remove_cvref_t<Fx>> &&
      std::is_nothrow_constructible_v<Fx, Fx&&>;

  struct no_init {};

  unique_function(no_init) noexcept {}

 public:
  unique_function() noexcept : super(nullptr), _move_or_destroy(nullptr), _buffer(nullptr) {}

  unique_function(std::nullptr_t) noexcept : unique_function() {}

  unique_function(const unique_function&) = delete;

  unique_function(unique_function&& other) noexcept
      : super(other._invoke), _move_or_destroy(other._move_or_destroy), _buffer(std::move(other._buffer)) {
    if (_move_or_destroy) {
      _move_or_destroy(*this, &other, action_move);
    }
    other.clear();
  }

  template <typename Fx,
            typename = std::enable_if_t<is_invocable<Fx>::value>>
  unique_function(Fx&& func) noexcept(is_noexcept_init<Fx>)
      : unique_function(no_init{}) {
    init(std::forward<Fx>(func));
  }

  ~unique_function() noexcept {
    if (_move_or_destroy) {
      _move_or_destroy(*this, nullptr, action_destroy);
    }
  }

  unique_function& operator=(const unique_function&) const = delete;

  unique_function& operator=(unique_function&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    clear();

    if (other._move_or_destroy) {
      other._move_or_destroy(*this, &other, action_move);
    } else {
      this->_buffer.swap_and_reset(other._buffer);
    }

    this->_invoke = std::exchange(other._invoke, nullptr);
    _move_or_destroy = std::exchange(other._move_or_destroy, nullptr);

    return *this;
  }

  unique_function& operator=(std::nullptr_t) noexcept {
    clear();
    return *this;
  }

  template <typename Fx, typename = std::enable_if_t<
                             is_invocable<Fx>::value &&
                             !std::is_same_v<Fx, unique_function>>>
  unique_function& operator=(Fx&& func) noexcept(is_noexcept_init<Fx>) {
    clear();
    init(std::forward<Fx>(func));
    return *this;
  }

  explicit operator bool() const noexcept { return this->_invoke != nullptr; }

  using super::operator();

 private:
  template <typename Fx>
  void init(Fx&& func) noexcept(is_noexcept_init<Fx>) {
    static_assert(std::is_nothrow_destructible_v<Fx>, "lambda should have noexcept destructor");

    using TFunc = std::remove_cvref_t<Fx>;
    constexpr bool is_small_function = is_small_f<TFunc>;

    this->_invoke = make_invoke<TFunc>(static_cast<typename super::t_invoke_f>(nullptr));

    if constexpr (is_small_function) {
      // small object
      if constexpr (!std::is_trivially_destructible_v<TFunc> ||
                    !std::is_trivially_move_constructible_v<TFunc>) {
        this->_move_or_destroy = static_cast<t_move_or_destroy_f>(
            [](unique_function& self, unique_function* other, deinit_op op) noexcept {
              if (op == action_destroy) {
                if constexpr (!std::is_trivially_destructible_v<TFunc>) {
                  auto* fx = reinterpret_cast<TFunc*>(&self._buffer.mem[0]);
                  std::destroy_at(fx);
                }
                self._buffer = nullptr;
              } else {
                // action_move
                if constexpr (!std::is_trivially_move_constructible_v<TFunc>) {
                  static_assert(std::is_nothrow_move_constructible_v<TFunc>, "lambda should have noexcept move constructor");

                  auto* to = reinterpret_cast<TFunc*>(&self._buffer.mem[0]);
                  auto& from = reinterpret_cast<TFunc&>(other->_buffer.mem[0]);
                  new (to) TFunc(std::move(from));
                  other->_buffer = nullptr;
                }
              }
            });
      } else {
        this->_move_or_destroy = nullptr;
      }

      new (&this->_buffer.mem[0]) Fx(std::forward<Fx>(func));
    } else {
      // large function
      _move_or_destroy = static_cast<t_move_or_destroy_f>(
          [](unique_function& self, unique_function* other, auto op) noexcept {
            if (op == action_destroy) {
              if (self._buffer.fx) {
                auto* fx = static_cast<TFunc*>(self._buffer.fx);
                delete fx;
                self._buffer.fx = nullptr;
              }
            } else {
              // action_move
              self._buffer.fx = std::exchange(other->_buffer.fx, nullptr);
            }
          });
      this->_buffer.fx = new TFunc(std::forward<Fx>(func));
    }
  }

  void clear() noexcept {
    if (_move_or_destroy) {
      _move_or_destroy(*this, nullptr, action_destroy);
    } else {
      this->_buffer = nullptr;
    }
    _move_or_destroy = nullptr;
    this->_invoke = nullptr;
  }

  static t_small_buffer& get_buffer(const super* self) noexcept {
    return static_cast<const unique_function*>(self)->_buffer;
  }

  template <typename TFunc, typename R, typename... TArgs>
  static auto make_invoke(R (*)(t_small_buffer&, TArgs...)) noexcept {
    return static_cast<typename super::t_invoke_f>(
        [](t_small_buffer& buffer, TArgs&&... args) noexcept(super::is_noexcept_invoke) {
          if constexpr (is_small_f<TFunc>) {
            auto& fx_t = reinterpret_cast<TFunc&>(buffer.mem[0]);
            return fx_t(std::forward<TArgs>(args)...);
          } else {
            auto& fx_t = *static_cast<TFunc*>(buffer.fx);
            return fx_t(std::forward<TArgs>(args)...);
          }
        });
  }

 private:
  t_move_or_destroy_f _move_or_destroy;
  mutable t_small_buffer _buffer;
};

template <typename R, class... TArgs>
unique_function(R(TArgs...)) -> unique_function<R(TArgs...)>;

template <typename Fx>
unique_function(Fx) -> unique_function<typename internal::deduce_function_signature<Fx>::type>;

}  // namespace async_coro
