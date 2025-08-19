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

/**
 * @brief A thread safe queue that uses atomics for a fast path.
 * In case when there is no preallocated values it will fall back to using spin lock mutex to allocate a new bank of values.
 * This queue is designed to be used in multi producer/multi consumer scenarios.
 * @tparam T The type of the values to be stored in the queue.
 * @tparam BlockSize The number of values to be preallocated in a single bank.
 */
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
  /**
   * @brief Construct a new atomic queue object
   */
  atomic_queue() noexcept {}

  /**
   * @brief Destroy the atomic queue object and all the values that are still in it.
   */
  ~atomic_queue() noexcept {
    unique_lock lock{_value_mutex};

    auto head = _head.exchange(nullptr, std::memory_order::relaxed);
    _last = nullptr;

    while (head) {
      std::destroy_at(std::addressof(head->val.value));
      head = head->next;
    }
  }

  /**
   * @brief Pushes a new value to the queue.
   * In case when there is no preallocated values it will allocate a new bank of values.
   * This can cause a small overhead.
   * @tparam U The types of the arguments to be forwarded to the constructor of T.
   * @param v The arguments to be forwarded to the constructor of T.
   */
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

    _head.store(head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  /**
   * @brief Tries to push a new value to the queue.
   * This method will not allocate a new bank of values if there are no preallocated values.
   * @tparam U The types of the arguments to be forwarded to the constructor of T.
   * @param v The arguments to be forwarded to the constructor of T.
   * @return true if the value was pushed successfully, false otherwise.
   */
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

    _head.store(head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  /**
   * @brief Tries to pop a value from the queue.
   * @param v The reference to the value where the popped value will be moved to.
   * @return true if a value was popped successfully, false otherwise.
   */
  bool try_pop(T& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
    value* head;

    {
      // we use common lock here as head->next can change unexpectedly
      unique_lock lock{_value_mutex};

      head = _head.load(std::memory_order::relaxed);
      if (head) {
        _head.store(head->next, std::memory_order::relaxed);
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

  /**
   * @brief Checks if the queue has any values.
   * @return true if the queue has any values, false otherwise.
   */
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
