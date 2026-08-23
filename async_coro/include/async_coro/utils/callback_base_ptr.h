#pragma once

#include <async_coro/config.h>
#include <async_coro/utils/callback_fwd.h>

#include <atomic>
#include <cstddef>
#include <utility>

namespace async_coro {

template <bool Noexcept>
class callback_base_atomic_ptr;

// Owning RAII class for owning callback
// Base pointer type can only be destroyed and provide pointer like functions
template <bool Noexcept>
class callback_base_ptr {
  friend callback_base_atomic_ptr<Noexcept>;

 public:
  callback_base_ptr() noexcept = default;
  callback_base_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  explicit callback_base_ptr(callback_base<Noexcept>* clb) noexcept : _clb(clb) {}
  explicit callback_base_ptr(callback_base<true>* clb) noexcept
    requires(!Noexcept)
      : _clb(clb) {}

  callback_base_ptr(const callback_base_ptr&) = delete;
  constexpr callback_base_ptr(callback_base_ptr&& other) noexcept
      : _clb(std::exchange(other._clb, nullptr)) {}

  callback_base_ptr& operator=(const callback_base_ptr&) = delete;
  callback_base_ptr& operator=(callback_base_ptr&& other) noexcept {
    if (&other == this) {
      return *this;
    }

    reset();

    _clb = std::exchange(other._clb, nullptr);

    return *this;
  }

  constexpr bool operator==(const callback_base_ptr& other) const noexcept {
    return _clb == other._clb;
  }

  constexpr bool operator==(const callback_base_atomic_ptr<Noexcept>& other) const noexcept;

  constexpr bool operator==(callback_base<Noexcept>* clb) const noexcept {
    return _clb == clb;
  }

  constexpr explicit operator bool() const noexcept {
    return _clb != nullptr;
  }

  explicit operator callback_base_atomic_ptr<Noexcept>() && noexcept {
    return callback_base_atomic_ptr<Noexcept>(this->release());
  }

  ~callback_base_ptr() noexcept {
    reset();
  }

  void assign_to_no_init(callback_base_ptr&& other) noexcept {  // NOLINT(*not-moved*)
    ASYNC_CORO_ASSERT(_clb == nullptr);

    _clb = std::exchange(other._clb, nullptr);
  }

  void assign_to_no_init(callback_base_ptr<true>&& other) noexcept  // NOLINT(*not-moved*)
    requires(!Noexcept)
  {
    ASYNC_CORO_ASSERT(_clb == nullptr);

    _clb = std::exchange(other._clb, nullptr);
  }

  void reset(callback_base<Noexcept>* clb = nullptr) noexcept {
    if (auto* old = std::exchange(_clb, clb)) {
      old->destroy();
    }
  }

  void reset(callback_base<true>* clb) noexcept
    requires(!Noexcept)
  {
    if (auto* old = std::exchange(_clb, clb)) {
      old->destroy();
    }
  }

  callback_base<Noexcept>* release() noexcept {
    return std::exchange(_clb, nullptr);
  }

 protected:
  callback_base<Noexcept>* _clb = nullptr;
};

// Thread safe variant of callback_base_ptr
template <bool Noexcept>
class callback_base_atomic_ptr {
  friend callback_base_ptr<Noexcept>;

 public:
  callback_base_atomic_ptr() noexcept = default;
  callback_base_atomic_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)

  explicit callback_base_atomic_ptr(callback_base<Noexcept>* clb) noexcept : _clb(clb) {}
  explicit callback_base_atomic_ptr(callback_base<true>* clb) noexcept
    requires(!Noexcept)
      : _clb(clb) {}

  callback_base_atomic_ptr(const callback_base_atomic_ptr&) = delete;
  constexpr callback_base_atomic_ptr(callback_base_atomic_ptr&& other) noexcept
      : _clb(other._clb.exchange(nullptr, std::memory_order::relaxed)) {}

  callback_base_atomic_ptr& operator=(const callback_base_atomic_ptr&) = delete;
  callback_base_atomic_ptr& operator=(callback_base_atomic_ptr&& other) noexcept {
    if (&other == this) {
      return *this;
    }

    if (auto* clb = _clb.exchange(other._clb.exchange(nullptr, std::memory_order::relaxed), std::memory_order::relaxed)) {
      clb->destroy();
    }

    return *this;
  }

  constexpr bool operator==(const callback_base_atomic_ptr& other) const noexcept {
    return _clb.load(std::memory_order::relaxed) == other._clb.load(std::memory_order::relaxed);
  }

  constexpr bool operator==(const callback_base_ptr<Noexcept>& other) const noexcept {
    return _clb.load(std::memory_order::relaxed) == other._clb;
  }

  constexpr bool operator==(callback_base<Noexcept>* clb) const noexcept {
    return _clb.load(std::memory_order::relaxed) == clb;
  }

  constexpr explicit operator bool() const noexcept {
    return _clb.load(std::memory_order::relaxed) != nullptr;
  }

  ~callback_base_atomic_ptr() noexcept {
    reset();
  }

  void assign_to_no_init(callback_base_atomic_ptr&& other, std::memory_order order = std::memory_order::relaxed) noexcept {  // NOLINT(*not-moved*)
    ASYNC_CORO_ASSERT_VARIABLE auto* clb = _clb.exchange(other._clb.exchange(nullptr, order), order);

    ASYNC_CORO_ASSERT(clb == nullptr);
  }

  void assign_to_no_init(callback_base_atomic_ptr<true>&& other, std::memory_order order = std::memory_order::relaxed) noexcept  // NOLINT(*not-moved*)
    requires(!Noexcept)
  {
    ASYNC_CORO_ASSERT_VARIABLE auto* clb = _clb.exchange(other._clb.exchange(nullptr, order), order);

    ASYNC_CORO_ASSERT(clb == nullptr);
  }

  void reset(callback_base<Noexcept>* clb = nullptr, std::memory_order order = std::memory_order::relaxed) noexcept {
    if (auto* old = _clb.exchange(clb, order)) {
      old->destroy();
    }
  }

  void reset(callback_base<true>* clb, std::memory_order order = std::memory_order::relaxed) noexcept
    requires(!Noexcept)
  {
    if (auto* old = _clb.exchange(clb, order)) {
      old->destroy();
    }
  }

  callback_base<Noexcept>* release(std::memory_order order = std::memory_order::relaxed) noexcept {
    return _clb.exchange(nullptr, order);
  }

 protected:
  std::atomic<callback_base<Noexcept>*> _clb = nullptr;
};

template <bool Noexcept>
constexpr bool callback_base_ptr<Noexcept>::operator==(const callback_base_atomic_ptr<Noexcept>& other) const noexcept {
  return _clb == other._clb.load(std::memory_order::relaxed);
}
}  // namespace async_coro
