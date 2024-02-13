#pragma once

#include <async_coro/internal/always_false.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace async_coro {
namespace internal {
template <size_t SFOBuffer, typename T>
class function_impl_call {
  static_assert(
      always_false<T>::value,
      "move_only_function only accepts function types as template arguments.");
};

template <size_t Size>
union small_buffer {
  static_assert(Size >= sizeof(void*), "Size of buffer to small");

  void* fx;
  char mem[Size];

  small_buffer() noexcept {}
  explicit small_buffer(std::nullptr_t) noexcept {
    std::memset(&mem[0], 0, Size);
  }
  small_buffer(const small_buffer& other) noexcept {
    std::memcpy(&mem[0], &other.mem[0], Size);
  }
  ~small_buffer() noexcept {}

  small_buffer& operator=(std::nullptr_t) noexcept {
    std::memset(&mem[0], 0, Size);
    return *this;
  }
};

template <size_t SFOBuffer, typename R, typename... TArgs>
class function_impl_call<SFOBuffer, R(TArgs...)> {
 protected:
  using small_buffer = small_buffer<SFOBuffer>;

  using t_invoke_f = R (*)(small_buffer&, TArgs&&...);

  inline static constexpr bool is_noexcept_invoke = false;

  template <typename Fx>
  using is_invocable = std::is_invocable_r<R, Fx, TArgs...>;

  function_impl_call() noexcept {}

  function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr),
        _buffer(nullptr) {
  }

  function_impl_call(function_impl_call&&) noexcept = default;

 public:
  R operator()(TArgs... args) const {
    if (_invoke == nullptr) [[unlikely]] {
      std::abort();
    }
    return _invoke(_buffer, std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;
  mutable small_buffer _buffer;
};

template <size_t SFOBuffer, typename R, typename... TArgs>
class function_impl_call<SFOBuffer, R(TArgs...) noexcept> {
 protected:
  using small_buffer = small_buffer<SFOBuffer>;

  using t_invoke_f = R (*)(small_buffer&, TArgs&&...) noexcept;

  inline static constexpr bool is_noexcept_invoke = true;

  template <typename Fx>
  using is_invocable = std::is_nothrow_invocable_r<R, Fx, TArgs...>;

  function_impl_call() noexcept {}

  function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr),
        _buffer(nullptr) {
  }

  function_impl_call(function_impl_call&&) noexcept = default;

 public:
  R operator()(TArgs... args) const noexcept {
    if (_invoke == nullptr) [[unlikely]] {
      std::abort();
    }
    return _invoke(_buffer, std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;
  mutable small_buffer _buffer;
};
}  // namespace internal

template <typename FTy, size_t SFOBuffer = sizeof(void*)>
class move_only_function : public internal::function_impl_call<SFOBuffer, FTy> {
  using super = internal::function_impl_call<SFOBuffer, FTy>;

  enum deinit_op {
    action_move,
    action_destroy,
  };

  using t_move_or_destroy_f = void (*)(move_only_function& self,
                                       move_only_function* other,
                                       deinit_op op) noexcept;

  template <typename Fx>
  inline static constexpr bool is_small_f =
      sizeof(Fx) <= SFOBuffer && alignof(Fx) <= alignof(void*);

  template <typename Fx>
  inline static constexpr bool is_noexecept_init =
      is_small_f<std::remove_cvref_t<Fx>> &&
      std::is_nothrow_constructible_v<Fx, Fx&&>;

  struct no_init {};

  move_only_function(no_init) noexcept {}

 public:
  move_only_function() noexcept : super(nullptr), _move_or_destroy(nullptr) {}

  move_only_function(std::nullptr_t) noexcept : move_only_function() {}

  move_only_function(const move_only_function&) = delete;

  move_only_function(move_only_function&& other) noexcept
      : super(std::move(other)), _move_or_destroy(other._move_or_destroy) {
    if (_move_or_destroy) {
      _move_or_destroy(*this, &other, action_move);
    }
    other.clear();
  }

  template <typename Fx,
            typename = std::enable_if_t<super::is_invocable<Fx>::value>>
  move_only_function(Fx&& func) noexcept(is_noexecept_init<Fx>)
      : move_only_function(no_init{}) {
    init(std::forward<Fx>(func));
  }

  ~move_only_function() noexcept {
    if (_move_or_destroy) {
      _move_or_destroy(*this, nullptr, action_destroy);
    }
  }

  move_only_function& operator=(const move_only_function&) const = delete;

  move_only_function& operator=(move_only_function&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    clear();

    if (other._move_or_destroy) {
      other._move_or_destroy(*this, &other, action_move);
    } else {
      this->_buffer = std::exchange(other._buffer, nullptr);
    }

    this->_invoke = std::exchange(other._invoke, nullptr);
    _move_or_destroy = std::exchange(other._move_or_destroy, nullptr);

    return *this;
  }

  move_only_function& operator=(std::nullptr_t) noexcept {
    clear();
    return *this;
  }

  template <typename Fx, typename = std::enable_if_t<
                             super::is_invocable<Fx>::value &&
                             !std::is_same_v<Fx, move_only_function>>>
  move_only_function& operator=(Fx&& func) noexcept(is_noexecept_init<Fx>) {
    clear();
    init(std::forward<Fx>(func));
    return *this;
  }

  explicit operator bool() const noexcept { return this->_invoke != nullptr; }

  using super::operator();

 private:
  template <typename Fx>
  void init(Fx&& func) noexcept(is_noexecept_init<Fx>) {
    static_assert(std::is_nothrow_destructible_v<Fx>, "lambda should have noexcept destructor");

    using TFunc = std::remove_cvref_t<Fx>;
    constexpr bool is_small_function = is_small_f<TFunc>;

    this->_invoke = make_invoke<TFunc>(static_cast<super::t_invoke_f>(nullptr));

    if constexpr (is_small_function) {
      // small object
      if constexpr (!std::is_trivially_destructible_v<TFunc> ||
                    !std::is_trivially_move_constructible_v<TFunc>) {
        this->_move_or_destroy =
            +[](move_only_function& self, move_only_function* other, deinit_op op) noexcept {
              if (op == action_destroy) {
                if constexpr (!std::is_trivially_destructible_v<TFunc>) {
                  auto* fx = reinterpret_cast<TFunc*>(&self._buffer.mem[0]);
                  std::destroy_at(fx);
                }
                self._buffer = nullptr;
              } else {
                // action_move
                if constexpr (!std::is_trivially_move_constructible_v<TFunc>) {
                  static_assert(std::is_nothrow_move_constructible_v<TFunc>, "lambda should have noexcept move contructor");

                  auto* to = reinterpret_cast<TFunc*>(&self._buffer.mem[0]);
                  auto& from = reinterpret_cast<TFunc&>(other->_buffer.mem[0]);
                  new (to) TFunc(std::move(from));
                  other->_buffer = nullptr;
                }
              }
            };
      } else {
        this->_move_or_destroy = nullptr;
      }

      new (&this->_buffer.mem[0]) Fx(std::forward<Fx>(func));
    } else {
      // large function
      _move_or_destroy = +[](move_only_function& self, move_only_function* other, auto op) noexcept {
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
      };
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

  template <typename TFunc, typename R, typename... TArgs>
  static auto make_invoke(R (*)(typename super::small_buffer&, TArgs...)) noexcept {
    return static_cast<super::t_invoke_f>(
        [](super::small_buffer& buffer, TArgs&&... args) noexcept(super::is_noexcept_invoke) {
          if constexpr (is_small_f<TFunc>) {
            auto& fx_t = reinterpret_cast<TFunc&>(buffer.mem[0]);
            return std::invoke(fx_t, std::forward<TArgs>(args)...);
          } else {
            auto& fx_t = *static_cast<TFunc*>(buffer.fx);
            return std::invoke(fx_t, std::forward<TArgs>(args)...);
          }
        });
  }

 private:
  t_move_or_destroy_f _move_or_destroy;
};

template <typename R, class... TArgs>
move_only_function(R(TArgs...)) -> move_only_function<R(TArgs...)>;

namespace internal {
template <typename Fx, typename = void>
struct deduce_signature {};  // can't deduce signature when &_Fx::operator() is missing, inaccessible, or ambiguous

template <typename T>
struct deduce_signature_impl {
  static_assert(
      always_false<T>::value,
      "move_only_function cant deduce signature.");
};

template <typename R, typename T, typename... TArgs>
struct deduce_signature_impl<R (T::*)(TArgs...) const> {
  using type = R(TArgs...);
};

template <typename R, typename T, typename... TArgs>
struct deduce_signature_impl<R (T::*)(TArgs...)> {
  using type = R(TArgs...);
};

template <typename R, typename... TArgs>
struct deduce_signature_impl<R (*)(TArgs...)> {
  using type = R(TArgs...);
};

template <typename R, typename T, typename... TArgs>
struct deduce_signature_impl<R (T::*)(TArgs...) const noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename R, typename T, typename... TArgs>
struct deduce_signature_impl<R (T::*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename R, typename... TArgs>
struct deduce_signature_impl<R (*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename Fx>
struct deduce_signature<Fx, std::void_t<decltype(&Fx::operator())>> {
  using type = deduce_signature_impl<decltype(&Fx::operator())>::type;
};

}  // namespace internal

template <typename Fx>
move_only_function(Fx) -> move_only_function<typename internal::deduce_signature<Fx>::type>;

}  // namespace async_coro
