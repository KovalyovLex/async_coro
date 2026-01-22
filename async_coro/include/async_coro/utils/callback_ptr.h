#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/utils/always_false.h>
#include <async_coro/utils/callback_base_ptr.h>
#include <async_coro/utils/callback_fwd.h>

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace async_coro {

// RAII holder of callback
template <typename TFunc>
class callback_ptr {
  static_assert(always_false<TFunc>::value,
                "callback_ptr only accepts function types as template arguments");
};

// RAII holder of callback with atomic pointer (safe to share state)
template <typename TFunc>
class callback_atomic_ptr {
  static_assert(always_false<TFunc>::value,
                "callback_ptr_atomic only accepts function types as template arguments");
};

// Callback_base_ptr with ability to execute callback
template <typename R, typename... TArgs>
class callback_ptr<R(TArgs...)> : public callback_base_ptr<false> {
  friend callback_atomic_ptr<R(TArgs...)>;

  using super = callback_base_ptr<false>;

 public:
  using callback_t = callback<R(TArgs...)>;

  constexpr callback_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  constexpr explicit callback_ptr(callback_t* clb) noexcept : super(clb) {}

  constexpr explicit callback_ptr(callback<R(TArgs...) noexcept>* clb) noexcept : super(clb) {}

  void assign_to_no_init(callback_ptr&& other) noexcept {
    super::assign_to_no_init(std::move(other));
  }

  void assign_to_no_init(callback_ptr<R(TArgs...) noexcept>&& other) noexcept {
    super::assign_to_no_init(std::move(other));
  }

  auto try_execute_and_destroy(TArgs... args) {
    using result_type = std::conditional_t<std::is_void_v<typename callback_t::return_type>, void, std::optional<typename callback_t::return_type>>;

    auto* clb = this->release();

    if (clb != nullptr) {
      return clb->execute_and_destroy(std::forward<TArgs>(args)...);
    }

    if constexpr (std::is_void_v<result_type>) {
      return;
    } else {
      return result_type{};
    }
  }

  callback_t::return_type execute_and_destroy(TArgs... args) {
    auto* clb = this->release();

    ASYNC_CORO_ASSERT(clb != nullptr);

    return clb->execute_and_destroy(std::forward<TArgs>(args)...);
  }

  callback_t::return_type execute(TArgs... args) const {
    auto* clb = static_cast<callback_t*>(_clb);

    ASYNC_CORO_ASSERT(clb != nullptr);

    return clb->execute(std::forward<TArgs>(args)...);
  }

  void reset(callback_t* clb = nullptr) {
    super::reset(clb);
  }

  void reset(callback<R(TArgs...) noexcept>* clb) {
    super::reset(clb);
  }

  callback_t* release() noexcept {
    return static_cast<callback_t*>(super::release());
  }

  explicit operator callback_atomic_ptr<R(TArgs...)>() && noexcept;

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
  static callback_ptr allocate(Fx&& func) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Fx>, Fx&&>);
};

// Callback_base_ptr with ability to execute callback
template <typename R, typename... TArgs>
class callback_ptr<R(TArgs...) noexcept> : public callback_base_ptr<true> {
  friend callback_atomic_ptr<R(TArgs...)>;

  using super = callback_base_ptr<true>;

 public:
  using callback_t = callback<R(TArgs...) noexcept>;

  constexpr callback_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  constexpr explicit callback_ptr(callback_t* clb) noexcept : super(clb) {}

  void assign_to_no_init(callback_ptr&& other) noexcept {
    super::assign_to_no_init(std::move(other));
  }

  auto try_execute_and_destroy(TArgs... args) noexcept {
    using result_type = std::conditional_t<std::is_void_v<typename callback_t::return_type>, void, std::optional<typename callback_t::return_type>>;

    auto* clb = this->release();

    if (clb != nullptr) {
      return clb->execute_and_destroy(std::forward<TArgs>(args)...);
    }

    if constexpr (std::is_void_v<result_type>) {
      return;
    } else {
      return result_type{};
    }
  }

  callback_t::return_type execute_and_destroy(TArgs... args) noexcept {
    auto* clb = this->release();

    ASYNC_CORO_ASSERT(clb != nullptr);

    return clb->execute_and_destroy(std::forward<TArgs>(args)...);
  }

  callback_t::return_type execute(TArgs... args) const noexcept {
    auto* clb = static_cast<callback_t*>(_clb);

    ASYNC_CORO_ASSERT(clb != nullptr);

    return clb->execute(std::forward<TArgs>(args)...);
  }

  void reset(callback_t* clb = nullptr) noexcept {
    super::reset(clb);
  }

  callback_t* release() noexcept {
    return static_cast<callback_t*>(super::release());
  }

  explicit operator callback_atomic_ptr<R(TArgs...) noexcept>() && noexcept;

  template <class Fx>
    requires(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
  static callback_ptr allocate(Fx&& func) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Fx>, Fx&&>);
};

// Thread safe variant of callback_ptr
template <typename R, typename... TArgs>
class callback_atomic_ptr<R(TArgs...)> : public callback_base_atomic_ptr<false> {
  friend callback_ptr<R(TArgs...)>;

  using super = callback_base_atomic_ptr<false>;

 public:
  using callback_t = callback<R(TArgs...)>;

  constexpr callback_atomic_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  constexpr callback_atomic_ptr(callback_ptr<R(TArgs...)> other) noexcept : super(other.release()) {}           // NOLINT(*explicit*)
  constexpr callback_atomic_ptr(callback_ptr<R(TArgs...) noexcept> other) noexcept : super(other.release()) {}  // NOLINT(*explicit*)

  constexpr explicit callback_atomic_ptr(callback_t* clb) noexcept : super(clb) {}
  constexpr explicit callback_atomic_ptr(callback<R(TArgs...) noexcept>* clb) noexcept : super(clb) {}

  void assign_to_no_init(callback_atomic_ptr&& other, std::memory_order order = std::memory_order::relaxed) noexcept {
    super::assign_to_no_init(std::move(other), order);
  }

  void assign_to_no_init(callback_atomic_ptr<R(TArgs...) noexcept>&& other, std::memory_order order = std::memory_order::relaxed) noexcept {
    super::assign_to_no_init(std::move(other), order);
  }

  auto try_execute_and_destroy(TArgs... args) {
    using result_type = std::conditional_t<std::is_void_v<typename callback_t::return_type>, void, std::optional<typename callback_t::return_type>>;

    auto* clb = this->release(std::memory_order::acquire);

    if (clb != nullptr) {
      return clb->execute_and_destroy(std::forward<TArgs>(args)...);
    }

    if constexpr (std::is_void_v<result_type>) {
      return;
    } else {
      return result_type{};
    }
  }

  auto execute_and_destroy(TArgs... args) {
    return try_execute_and_destroy(std::forward<TArgs>(args)...);
  }

  callback_t::return_type execute(TArgs... args) const {
    auto* clb = static_cast<callback_t*>(_clb.load(std::memory_order::acquire));

    ASYNC_CORO_ASSERT(_clb != nullptr);

    return clb->execute(std::forward<TArgs>(args)...);
  }

  void reset(callback_t* clb = nullptr, std::memory_order order = std::memory_order::relaxed) {
    super::reset(clb, order);
  }

  void reset(callback<R(TArgs...) noexcept>* clb, std::memory_order order = std::memory_order::relaxed) {
    super::reset(clb, order);
  }

  callback_t* release(std::memory_order order = std::memory_order::relaxed) noexcept {
    return static_cast<callback_t*>(super::release(order));
  }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
  static callback_atomic_ptr allocate(Fx&& func) noexcept {
    return callback_atomic_ptr{callback_ptr<R(TArgs...)>::allocate(std::forward(func))};
  }
};

// Thread safe variant of callback_ptr
template <typename R, typename... TArgs>
class callback_atomic_ptr<R(TArgs...) noexcept> : public callback_base_atomic_ptr<true> {
  friend callback_ptr<R(TArgs...)>;

  using super = callback_base_atomic_ptr<true>;

 public:
  using callback_t = callback<R(TArgs...) noexcept>;

  constexpr callback_atomic_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  constexpr callback_atomic_ptr(callback_ptr<R(TArgs...) noexcept> other) noexcept : super(other.release()) {}  // NOLINT(*explicit*)

  constexpr explicit callback_atomic_ptr(callback_t* clb) noexcept : super(clb) {}

  void assign_to_no_init(callback_atomic_ptr&& other, std::memory_order order = std::memory_order::relaxed) noexcept {
    super::assign_to_no_init(std::move(other), order);
  }

  auto try_execute_and_destroy(TArgs... args) noexcept {
    using result_type = std::conditional_t<std::is_void_v<typename callback_t::return_type>, void, std::optional<typename callback_t::return_type>>;

    auto* clb = this->release(std::memory_order::acquire);

    if (clb != nullptr) {
      return clb->execute_and_destroy(std::forward<TArgs>(args)...);
    }

    if constexpr (std::is_void_v<result_type>) {
      return;
    } else {
      return result_type{};
    }
  }

  auto execute_and_destroy(TArgs... args) noexcept {
    return try_execute_and_destroy(std::forward<TArgs>(args)...);
  }

  callback_t::return_type execute(TArgs... args) const noexcept {
    auto* clb = static_cast<callback_t*>(_clb.load(std::memory_order::acquire));

    ASYNC_CORO_ASSERT(clb != nullptr);

    return clb->execute(std::forward<TArgs>(args)...);
  }

  void reset(callback_t* clb = nullptr, std::memory_order order = std::memory_order::relaxed) noexcept {
    super::reset(clb, order);
  }

  callback_t* release(std::memory_order order = std::memory_order::relaxed) noexcept {
    return static_cast<callback_t*>(super::release(order));
  }

  template <class Fx>
    requires(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
  static callback_atomic_ptr allocate(Fx&& func) noexcept {
    return callback_atomic_ptr{callback_ptr<R(TArgs...) noexcept>::allocate(std::forward(func))};
  }
};

template <typename R, typename... TArgs>
inline callback_ptr<R(TArgs...)>::operator callback_atomic_ptr<R(TArgs...)>() && noexcept {
  auto* clb = std::exchange(_clb, nullptr);
  return callback_atomic_ptr<R(TArgs...)>{clb};
}

template <typename R, typename... TArgs>
inline callback_ptr<R(TArgs...) noexcept>::operator callback_atomic_ptr<R(TArgs...) noexcept>() && noexcept {
  auto* clb = std::exchange(_clb, nullptr);
  return callback_atomic_ptr<R(TArgs...)>{clb};
}

template <class Fx, class TCallback>
class callback_on_heap final : public TCallback {
 public:
  template <class FxRef>
    requires(!std::is_convertible_v<FxRef &&, callback_on_heap> && std::is_convertible_v<FxRef &&, Fx>)
  explicit callback_on_heap(FxRef&& func) noexcept(std::is_nothrow_constructible_v<Fx, FxRef&&>)  // NOLINT(*not-moved*)
      : TCallback(&execute),
        _fx(std::forward<FxRef>(func)) {}

 private:
  static void execute(internal::callback_execute_command& cmd, callback_base<TCallback::is_noexcept>& clb) noexcept(TCallback::is_noexcept) {
    auto& self = static_cast<callback_on_heap&>(clb);

    if (cmd.execute == internal::callback_execute_type::destroy) {
      delete &self;  // NOLINT(*owning*)
    } else {
      auto& args = cmd.get_arguments<typename TCallback::execute_signature>();

      if constexpr (std::is_void_v<typename TCallback::return_type>) {
        std::apply(
            [&self](auto&&... t_args) noexcept(TCallback::is_noexcept) {
              return self._fx(std::forward<decltype(t_args)>(t_args)...);
            },
            std::move(args.args.get_value()));
      } else {
        args.set_result(std::apply(
            [&self](auto&&... t_args) noexcept(TCallback::is_noexcept) {
              return self._fx(std::forward<decltype(t_args)>(t_args)...);
            },
            std::move(args.args.get_value())));
      }

      if (cmd.execute == internal::callback_execute_type::execute_and_destroy) {
        delete &self;  // NOLINT(*owning*)
      }
    }
  }

 private:
  Fx _fx;
};

template <typename R, typename... TArgs>
template <class Fx>
  requires(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
inline callback_ptr<R(TArgs...) noexcept> callback_ptr<R(TArgs...) noexcept>::allocate(Fx&& func) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Fx>, Fx&&>) {
  auto ptr = new callback_on_heap<std::remove_cvref_t<Fx>, callback_t>{std::forward<Fx>(func)};  // NOLINT(*owning*)

  return callback_ptr{ptr};
}

template <typename R, typename... TArgs>
template <class Fx>
  requires(internal::is_runnable<std::remove_cvref_t<Fx>, R, TArgs...>)
inline callback_ptr<R(TArgs...)> callback_ptr<R(TArgs...)>::allocate(Fx&& func) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<Fx>, Fx&&>) {
  auto ptr = new callback_on_heap<std::remove_cvref_t<Fx>, callback_t>{std::forward<Fx>(func)};  // NOLINT(*owning*)

  return callback_ptr{ptr};
}

}  // namespace async_coro
