#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/internal/is_invocable_by_signature.h>
#include <async_coro/utils/always_false.h>
#include <async_coro/utils/passkey.h>
#include <async_coro/utils/unique_function_storage.h>

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro {
namespace internal {

template <std::size_t SFOSize, typename TFunc, typename T>
class function_impl_call {
  static_assert(always_false<T>::value,
                "unique_function only accepts function types as template arguments, "
                "with possibly const noexcept qualifiers.");
};

template <std::size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...) const> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...);

  static constexpr bool is_noexcept_invoke = false;

  function_impl_call() noexcept = default;

  explicit function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  explicit function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) const {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<const TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

  R move_to_storage_and_call(unique_function_storage<SFOSize>& store, TArgs... args) {
    ASYNC_CORO_ASSERT(_invoke);

    const auto invoke = std::exchange(this->_invoke, nullptr);

    store = std::move(*static_cast<TFunc*>(this));

    return invoke(store.get_buffer(passkey{this}), std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;  // NOLINT(*-non-private-member-*)
};

template <std::size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...)> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...);

  static constexpr bool is_noexcept_invoke = false;

  function_impl_call() noexcept = default;

  explicit function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  explicit function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

  R move_to_storage_and_call(unique_function_storage<SFOSize>& store, TArgs... args) {
    ASYNC_CORO_ASSERT(_invoke);

    const auto invoke = std::exchange(this->_invoke, nullptr);

    store = std::move(*static_cast<TFunc*>(this));

    return invoke(store.get_buffer(passkey{this}), std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;  // NOLINT(*-non-private-member-*)
};

template <std::size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...) noexcept> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...) noexcept;

  static constexpr bool is_noexcept_invoke = true;

  function_impl_call() noexcept = default;

  explicit function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  explicit function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) const noexcept {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<const TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

  R move_to_storage_and_call(unique_function_storage<SFOSize>& store, TArgs... args) noexcept {
    ASYNC_CORO_ASSERT(_invoke);

    const auto invoke = std::exchange(this->_invoke, nullptr);
    const auto funcPtr = static_cast<TFunc*>(this);
    const auto moveOrDestroy = funcPtr->_move_or_destroy;

    store = std::move(*funcPtr);

    return invoke(store.get_buffer(passkey{this}), std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;  // NOLINT(*-non-private-member-*)
};

template <std::size_t SFOSize, typename TFunc, typename R, typename... TArgs>
class function_impl_call<SFOSize, TFunc, R(TArgs...) const noexcept> {
 protected:
  using t_small_buffer = small_buffer<SFOSize>;

  using t_invoke_f = R (*)(t_small_buffer&, TArgs&&...) noexcept;

  static constexpr bool is_noexcept_invoke = true;

  function_impl_call() noexcept = default;

  explicit function_impl_call(std::nullptr_t) noexcept
      : _invoke(nullptr) {
  }

  explicit function_impl_call(t_invoke_f invoke) noexcept : _invoke(invoke) {}

 public:
  R operator()(TArgs... args) const noexcept {
    ASYNC_CORO_ASSERT(_invoke);

    return _invoke(static_cast<const TFunc*>(this)->_buffer, std::forward<TArgs>(args)...);
  }

  R move_to_storage_and_call(unique_function_storage<SFOSize>& store, TArgs... args) noexcept {
    ASYNC_CORO_ASSERT(_invoke);

    const auto invoke = std::exchange(this->_invoke, nullptr);
    const auto funcPtr = static_cast<TFunc*>(this);
    const auto moveOrDestroy = funcPtr->_move_or_destroy;

    store = std::move(*funcPtr);

    return invoke(store.get_buffer(passkey{this}), std::forward<TArgs>(args)...);
  }

 protected:
  t_invoke_f _invoke;  // NOLINT(*-non-private-member-*)
};

}  // namespace internal

/**
 * @brief A move-only function wrapper with customizable small object optimization.
 *
 * This class serves as an analog to `std::move_only_function`, optimized for internal
 * object size and performance. It allows customization of the buffer size used for
 * small object optimization (SOO), enabling efficient storage of small callable objects
 * directly within the function wrapper without dynamic memory allocation.
 *
 * @tparam FTy The function signature, e.g., `R(Args...)`, where `R` is the return type
 *             and `Args...` are the parameter types.
 * @tparam SFOSize The size (in bytes) of the internal buffer used for small object optimization.
 *                 Defaults to `sizeof(void*) * 2`.
 */
template <typename FTy, std::size_t SFOSize = sizeof(void*) * 2>
class unique_function : private internal::function_impl_call<SFOSize, unique_function<FTy, SFOSize>, FTy>,
                        private unique_function_storage<SFOSize> {
  using super = internal::function_impl_call<SFOSize, unique_function<FTy, SFOSize>, FTy>;
  using storage = unique_function_storage<SFOSize>;

  friend super;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<FTy, Fx>;

  using t_small_buffer = typename super::t_small_buffer;

  using t_move_or_destroy_f = typename t_small_buffer::t_move_or_destroy_f;

  template <typename Fx>
  static constexpr bool is_small_f =
      sizeof(Fx) <= SFOSize && alignof(Fx) <= alignof(t_small_buffer);

  template <typename Fx>
  static constexpr bool is_noexcept_init =
      is_small_f<std::remove_cvref_t<Fx>> &&
      std::is_nothrow_constructible_v<Fx, Fx&&>;

  class no_init {};

  explicit unique_function(no_init /*tag*/) noexcept : storage(typename storage::no_init{}) {}

 public:
  unique_function() noexcept : super(nullptr), storage() {}

  unique_function(std::nullptr_t) noexcept : unique_function() {}  // NOLINT(*-explicit*)

  unique_function(const unique_function&) = delete;

  unique_function(unique_function&& other) noexcept
      : super(other._invoke),
        storage(std::move(other)) {
    other._invoke = nullptr;
  }

  template <typename Fx>
    requires(is_invocable<Fx>::value && !std::is_same_v<std::remove_cvref_t<Fx>, unique_function>)
  unique_function(Fx&& func) noexcept(is_noexcept_init<Fx>)  // NOLINT(*-explicit*)
      : unique_function(no_init{}) {
    init(std::forward<Fx>(func));
  }

  ~unique_function() noexcept = default;

  unique_function& operator=(const unique_function&) const = delete;

  unique_function& operator=(unique_function&& other) noexcept {
    *static_cast<storage*>(this) = std::move(other);

    this->_invoke = std::exchange(other._invoke, nullptr);

    return *this;
  }

  unique_function& operator=(std::nullptr_t) noexcept {
    this->clear();
    return *this;
  }

  template <typename Fx>
    requires(is_invocable<Fx>::value && !std::is_same_v<Fx, unique_function>)
  unique_function& operator=(Fx&& func) noexcept(is_noexcept_init<Fx>) {
    this->clear();
    init(std::forward<Fx>(func));
    return *this;
  }

  explicit operator bool() const noexcept { return this->_invoke != nullptr; }

  using super::operator();

  using super::move_to_storage_and_call;

 private:
  template <typename Fx>
  void init(Fx&& func) noexcept(is_noexcept_init<Fx>) {  // NOLINT
    static_assert(std::is_nothrow_destructible_v<Fx>, "lambda should have noexcept destructor");

    using TFunc = std::remove_cvref_t<Fx>;
    constexpr bool is_small_function = is_small_f<TFunc>;

    this->_invoke = make_invoke<TFunc>(static_cast<typename super::t_invoke_f>(nullptr));

    if constexpr (is_small_function) {
      // small object
      if constexpr (!std::is_trivially_destructible_v<TFunc> ||
                    !std::is_trivially_move_constructible_v<TFunc>) {
        this->_move_or_destroy = static_cast<t_move_or_destroy_f>(
            [](t_small_buffer& self, t_small_buffer* other, internal::deinit_op operation) noexcept {
              if (operation == internal::deinit_op::destroy) {
                if constexpr (!std::is_trivially_destructible_v<TFunc>) {
                  auto* fx_ptr = reinterpret_cast<TFunc*>(&self.mem[0]);  // NOLINT(*reinterpret-cast)
                  std::destroy_at(fx_ptr);
                }
                self = nullptr;
              } else {
                // action_move
                if constexpr (!std::is_trivially_move_constructible_v<TFunc>) {
                  static_assert(std::is_nothrow_move_constructible_v<TFunc>, "lambda should have noexcept move constructor");

                  auto* to_ptr = reinterpret_cast<TFunc*>(&self.mem[0]);  // NOLINT(*reinterpret-cast)
                  auto& from = reinterpret_cast<TFunc&>(other->mem[0]);   // NOLINT(*reinterpret-cast)
                  new (to_ptr) TFunc(std::move(from));
                  std::destroy_at(&from);
                  *other = nullptr;
                }
              }
            });
      } else {
        this->_move_or_destroy = nullptr;
      }

      new (&this->_buffer.mem[0]) TFunc(std::forward<Fx>(func));
    } else {
      // large function
      this->_move_or_destroy = static_cast<t_move_or_destroy_f>(
          [](t_small_buffer& self, t_small_buffer* other, internal::deinit_op operation) noexcept {
            if (operation == internal::deinit_op::destroy) {
              if (self.fx) {
                auto* fx_ptr = static_cast<TFunc*>(self.fx);
                delete fx_ptr;  // NOLINT(*-owning-memory)
                self.fx = nullptr;
              }
            } else {
              // action_move
              self.fx = std::exchange(other->fx, nullptr);
            }
          });
      this->_buffer.fx = new TFunc(std::forward<Fx>(func));  // NOLINT(*-owning-memory)
    }
  }

  void clear() noexcept {
    storage::clear();
    this->_invoke = nullptr;
  }

  template <typename TFunc, typename R, typename... TArgs>
  static auto make_invoke(R (* /*func*/)(t_small_buffer&, TArgs...)) noexcept {
    return static_cast<typename super::t_invoke_f>(
        [](t_small_buffer& buffer, TArgs&&... args) noexcept(super::is_noexcept_invoke) {
          if constexpr (is_small_f<TFunc>) {
            auto& fx_t = reinterpret_cast<TFunc&>(buffer.mem[0]);  // NOLINT(*reinterpret-cast)
            return fx_t(std::forward<TArgs>(args)...);
          } else {
            auto& fx_t = *static_cast<TFunc*>(buffer.fx);
            return fx_t(std::forward<TArgs>(args)...);
          }
        });
  }
};

template <typename R, class... TArgs>
unique_function(R(TArgs...)) -> unique_function<R(TArgs...)>;

template <typename Fx>
unique_function(Fx) -> unique_function<typename internal::deduce_function_signature<Fx>::type>;

}  // namespace async_coro
