#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/hardware_interference_size.h>
#include <async_coro/internal/virtual_tagged_ptr.h>
#include <async_coro/thread_safety/spin_lock_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/warnings.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace async_coro {

template <typename T, std::uint32_t BlockSize = 64>
class atomic_stack {
  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4324)

  struct alignas(std::hardware_constructive_interference_size) value_holder {
    // using union to not initialize value by default
    union {
      T value;
    };
    std::atomic<value_holder*> next;

    value_holder() noexcept {}
    value_holder(const value_holder&) = delete;
    value_holder(value_holder&&) = delete;
    ~value_holder() noexcept {}
  };

  ASYNC_CORO_WARNINGS_MSVC_POP

  struct values_array {
    values_array(std::uint32_t index)
        : free_index(index) {}

    std::array<value_holder, BlockSize> values;
    std::atomic<std::uint32_t> free_index;
  };

 public:
  atomic_stack() noexcept {}

  ~atomic_stack() noexcept {
    auto head1 = _values.load(std::memory_order::relaxed);
    _values.store({nullptr, 0}, std::memory_order::relaxed);

    while (head1.ptr) {
      std::destroy_at(&head1.ptr->value);

      head1.ptr = head1.ptr->next.load(std::memory_order::acquire);
    }

    {
      unique_lock lock{_banks_mutex};
      _free_bank.store(nullptr, std::memory_order::relaxed);
      _additional_banks.clear();
    }

    ASYNC_CORO_ASSERT(_values.load(std::memory_order::acquire).ptr == nullptr);
  }

  template <typename... U>
  void push(U&&... v) {
    auto free_tagged = _free_chain.load(std::memory_order::relaxed);

    while (free_tagged.ptr) {
      decltype(free_tagged) next_tagged = {free_tagged.ptr->next.load(std::memory_order::acquire), free_tagged.tag + 1};

      if (_free_chain.compare_exchange_strong(free_tagged, next_tagged, std::memory_order::relaxed)) {
        free_tagged.tag++;

        ASYNC_CORO_ASSERT(next_tagged.ptr == free_tagged.ptr->next.load(std::memory_order::relaxed));
        break;
      }
    }

    if (!free_tagged.ptr) [[unlikely]] {
      // get block from bank

      values_array* free_bank = _free_bank.load(std::memory_order::acquire);
      ASYNC_CORO_ASSERT(free_bank != nullptr);

      auto index = free_bank->free_index.fetch_add(1, std::memory_order::relaxed);

      if (index >= BlockSize) {
        unique_lock lock{_banks_mutex};

        // try check for free bank one more time
        free_bank = _free_bank.load(std::memory_order::acquire);
        ASYNC_CORO_ASSERT(free_bank != nullptr);

        index = free_bank->free_index.fetch_add(1, std::memory_order::relaxed);
        if (index < BlockSize) {
          free_tagged.ptr = &free_bank->values[index];
          free_tagged.tag = 0;
        } else {
          // allocate new bank
          _additional_banks.emplace_back(std::make_unique<values_array>(1));

          auto old_bank = free_bank;

          free_bank = _additional_banks.back().get();

          free_tagged.ptr = &free_bank->values[0];
          free_tagged.tag = 0;

          while (!_free_bank.compare_exchange_weak(old_bank, free_bank, std::memory_order::release)) {
          }
        }
      } else {
        free_tagged.ptr = &free_bank->values[index];
        free_tagged.tag = 0;
      }
    }

    new (&free_tagged.ptr->value) T{std::forward<U>(v)...};

    auto old_head = _values.load(std::memory_order::relaxed);

    free_tagged.ptr->next.store(old_head.ptr, std::memory_order::release);
    free_tagged.tag = old_head.tag + 1;

    while (!_values.compare_exchange_strong(old_head, free_tagged, std::memory_order::relaxed)) {
      free_tagged.ptr->next.store(old_head.ptr, std::memory_order::relaxed);
      free_tagged.tag = old_head.tag + 1;
    }

    ASYNC_CORO_ASSERT(old_head.ptr != free_tagged.ptr);
  }

  template <typename... U>
  bool try_push(U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
    auto free_tagged = _free_chain.load(std::memory_order::relaxed);

    while (free_tagged.ptr) {
      decltype(free_tagged) next_tagged = {free_tagged.ptr->next.load(std::memory_order::acquire), free_tagged.tag + 1};

      if (_free_chain.compare_exchange_strong(free_tagged, next_tagged, std::memory_order::relaxed)) {
        free_tagged.tag++;

        ASYNC_CORO_ASSERT(next_tagged.ptr == free_tagged.ptr->next.load(std::memory_order::relaxed));
        break;
      }
    }

    if (!free_tagged.ptr) {
      return false;
    }

    new (&free_tagged.ptr->value) T{std::forward<U>(v)...};

    auto old_head = _values.load(std::memory_order::relaxed);

    free_tagged.ptr->next.store(old_head.ptr, std::memory_order::release);
    free_tagged.tag = old_head.tag + 1;

    while (!_values.compare_exchange_strong(old_head, free_tagged, std::memory_order::relaxed)) {
      free_tagged.ptr->next.store(old_head.ptr, std::memory_order::relaxed);
      free_tagged.tag = old_head.tag + 1;
    }

    ASYNC_CORO_ASSERT(old_head.ptr != free_tagged.ptr);

    return true;
  }

  bool try_pop(T& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
    auto value_tagged = _values.load(std::memory_order::relaxed);
    while (value_tagged.ptr) {
      decltype(value_tagged) next_tagged = {value_tagged.ptr->next.load(std::memory_order::acquire), value_tagged.tag + 1};

      if (_values.compare_exchange_strong(value_tagged, next_tagged, std::memory_order::relaxed)) {
        value_tagged.tag++;

        ASYNC_CORO_ASSERT(next_tagged.ptr == value_tagged.ptr->next.load(std::memory_order::relaxed));
        break;
      }
    }

    if (!value_tagged.ptr) {
      return false;
    }

    v = std::move(value_tagged.ptr->value);
    std::destroy_at(&value_tagged.ptr->value);

    auto prev_free = _free_chain.load(std::memory_order::relaxed);

    value_tagged.ptr->next.store(prev_free.ptr, std::memory_order::release);
    value_tagged.tag = prev_free.tag + 1;

    while (!_free_chain.compare_exchange_strong(prev_free, value_tagged, std::memory_order::relaxed)) {
      value_tagged.ptr->next.store(prev_free.ptr, std::memory_order::relaxed);
      value_tagged.tag = prev_free.tag + 1;
    }

    ASYNC_CORO_ASSERT(prev_free.ptr != value_tagged.ptr);

    return true;
  }

  bool has_value() const noexcept {
    auto head = _values.load(std::memory_order::relaxed);
    return head.ptr != nullptr;
  }

 private:
  using value_ptr = internal::virtual_tagged_ptr<value_holder>;

  value_ptr _values = nullptr;
  std::atomic<values_array*> _free_bank = &_head_bank;
  values_array _head_bank{0};
  spin_lock_mutex _banks_mutex;
  std::vector<std::unique_ptr<values_array>> _additional_banks CORO_THREAD_GUARDED_BY(_banks_mutex);
  value_ptr _free_chain = nullptr;
};

};  // namespace async_coro
