#pragma once

#include <async_coro/config.h>
#include <async_coro/thread_safety/spin_lock_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <array>
#include <atomic>
#include <memory>
#include <type_traits>
#include <vector>

namespace async_coro {

template <typename T, uint32_t BlockSize = 64>
class atomic_queue {
  union union_store {
    T value;
    char tmp_byte;

    union_store() noexcept {}
    ~union_store() noexcept {}
  };

  struct value {
    union_store val;
    value* next;
  };

  struct values_bank {
    std::array<value, BlockSize> values;

    values_bank() noexcept {
      value* prev_value = nullptr;
      for (auto& val : values) {
        if (prev_value) {
          prev_value->next = &val;
        }
        prev_value = &val;
      }
      prev_value->next = nullptr;
    }
  };

 public:
  atomic_queue() noexcept {}

  ~atomic_queue() noexcept {
    unique_lock lock{_value_mutex};

    auto head = _head.load(std::memory_order::relaxed);
    _head.store(nullptr, std::memory_order::relaxed);
    _last = nullptr;

    while (head) {
      std::destroy_at(std::addressof(head->val.value));
      head = head->next;
    }
  }

  template <typename... U>
  void push(U&&... v) {
    value* head;

    {
      unique_lock lock{_free_value_mutex};

      if (!_free_value) {
        allocate_new_bank();
      }

      head = _free_value;
      _free_value = head->next;
    }

    new (std::addressof(head->val.value)) T{std::forward<U>(v)...};
    head->next = nullptr;

    unique_lock lock{_value_mutex};

    value* expected_to_set = nullptr;
    _head.compare_exchange_strong(expected_to_set, head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  template <typename... U>
  bool try_push(U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
    value* head;

    {
      unique_lock lock{_free_value_mutex};

      if (!_free_value) {
        return false;
      }

      head = _free_value;
      _free_value = head->next;
    }

    new (std::addressof(head->val.value)) T{std::forward<U>(v)...};
    head->next = nullptr;

    unique_lock lock{_value_mutex};

    value* expected_to_set = nullptr;
    _head.compare_exchange_strong(expected_to_set, head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  bool try_pop(T& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
    value* head;

    {
      // we use common lock here as  head->next can change unexpectedly
      unique_lock lock{_value_mutex};

      head = _head.load(std::memory_order::relaxed);
      if (head) {
        if (!_head.compare_exchange_strong(head, head->next, std::memory_order::relaxed)) {
          ASYNC_CORO_ASSERT(false);
        }
        if (_last == head) {
          _last = head->next;
        }
      } else {
        return false;
      }
    }

    v = std::move(head->val.value);
    std::destroy_at(std::addressof(head->val.value));

    {
      unique_lock lock{_free_value_mutex};
      head->next = _free_value;
      _free_value = head;
    }

    return true;
  }

  bool has_value() const noexcept {
    return _head.load(std::memory_order::relaxed) != nullptr;
  }

 private:
  inline void allocate_new_bank() CORO_THREAD_REQUIRES(_free_value_mutex) {
    _additional_banks.emplace_back(std::make_unique<values_bank>());
    _free_value = std::addressof(_additional_banks.back()->values[0]);
  }

 private:
  spin_lock_mutex _free_value_mutex;
  value* _free_value CORO_THREAD_GUARDED_BY(_free_value_mutex) = nullptr;

  values_bank _head_bank;

  spin_lock_mutex _value_mutex;
  value* _last CORO_THREAD_GUARDED_BY(_value_mutex) = nullptr;

  std::vector<std::unique_ptr<values_bank>> _additional_banks CORO_THREAD_GUARDED_BY(_free_value_mutex);

  // moved to end to minify false sharing effects with other atomics
  std::atomic<value*> _head = nullptr;
};

};  // namespace async_coro
