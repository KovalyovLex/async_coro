#pragma once

#include <async_coro/config.h>

#include <array>
#include <atomic>
#include <thread>
#include <type_traits>

namespace async_coro {

template <typename T, uint32_t BlockSize = 64>
class atomic_queue {
  struct task_chunk {
    alignas(alignof(T)) std::array<char[sizeof(T)], BlockSize> values;
    std::atomic<task_chunk*> next = nullptr;
    std::atomic<uint32_t> begin = 0;
    std::atomic<uint32_t> end = 0;
    std::atomic<uint32_t> num_used = 0;
    std::atomic<int32_t> free_protect = 0;
    std::array<std::atomic_bool, BlockSize> values_sync;
    std::atomic_bool create_next_crit = false;

    template <typename... U>
    void push(atomic_queue& q, U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
      auto cur_index = num_used.fetch_add(1, std::memory_order_relaxed);
      if (cur_index < values.size()) {
        new (&values[cur_index]) T(std::forward<U>(v)...);
        end.fetch_add(1, std::memory_order_relaxed);
        free_protect.fetch_sub(1, std::memory_order_relaxed);
        values_sync[cur_index].store(true, std::memory_order_release);
      } else {
        // need to use next chunk
        num_used.fetch_sub(1, std::memory_order_relaxed);
        auto* next_created = q.create_next_chunk(this);
        free_protect.fetch_sub(1, std::memory_order_relaxed);

        auto protect = next_created->free_protect.load(std::memory_order_relaxed);
        do {
          if (protect < 0) {
            // this chunk was freed try next
            next_created = q._head_push.load(std::memory_order_acquire);
            ASYNC_CORO_ASSERT(next_created != nullptr);
            protect = next_created->free_protect.load(std::memory_order_relaxed);
          }
        } while (!next_created->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order_relaxed));

        next_created->push(q, std::forward<U>(v)...);
      }
    }

    bool try_pop(T& v, atomic_queue& q, bool& need_release) noexcept(std::is_nothrow_move_assignable_v<T>) {
      auto cur_begin = begin.load(std::memory_order_relaxed);
      if (cur_begin < values.size()) {
        const auto cur_end = end.load(std::memory_order_relaxed);
        do {
          if (cur_begin >= cur_end) {
            // no values
            return false;
          }
        } while (!begin.compare_exchange_weak(cur_begin, cur_begin + 1, std::memory_order_relaxed));

        while (!values_sync[cur_begin].load(std::memory_order_acquire)) {
        }
        // syncronization here guaranteed by related values_sync variable in release-acquire ordering
        v = std::move(reinterpret_cast<T&>(values[cur_begin]));
        values_sync[cur_begin].store(false, std::memory_order_relaxed);
        return true;
      } else {
        auto* cur_next = next.load(std::memory_order_relaxed);
        if (cur_next) {
          // current chunk is over
          return q.on_chunk_depleted(v, this, cur_next, need_release);
        }
        return false;
      }
    }

    bool has_value() const noexcept {
      const auto cur_begin = begin.load(std::memory_order_relaxed);
      const auto cur_end = end.load(std::memory_order_relaxed);
      if (cur_begin < values.size()) {
        return cur_begin < cur_end;
      } else {
        auto* cur_next = next.load(std::memory_order_relaxed);
        return cur_next && cur_next->has_value();
      }
    }
  };

  bool on_chunk_depleted(T& v, task_chunk* c, task_chunk* cur_next, bool& need_release) noexcept {
    // head will be next
    task_chunk* expected = c;
    if (!_head_pop.compare_exchange_strong(expected, cur_next, std::memory_order_release, std::memory_order_relaxed)) {
      if (expected == cur_next) {
        // freed by someone else
        need_release = false;
        c->free_protect.fetch_sub(1, std::memory_order_relaxed);
        return this->try_pop(v);
      }
      return false;
    }
    // only one thread can pass here

    need_release = false;

    // wait till other threads finish push\pop and block future work
    int32_t expected_prots = 1;
    while (!c->free_protect.compare_exchange_strong(expected_prots, -1, std::memory_order_relaxed)) {
      expected_prots = 1;
      std::this_thread::yield();
    }

    // push c to list of free chunks
    expected = _free_chain.load(std::memory_order_acquire);

    ASYNC_CORO_ASSERT(c->next.load(std::memory_order_relaxed) == cur_next);

    c->next.store(expected, std::memory_order_relaxed);

    while (!_free_chain.compare_exchange_strong(expected, c, std::memory_order_release, std::memory_order_relaxed)) {
      c->next.store(expected, std::memory_order_relaxed);
    }

    return this->try_pop(v);
  }

  task_chunk* create_next_chunk(task_chunk* c) noexcept {
    // TODO: actially this method can throw exception because of new

    auto* next_created = c->next.load(std::memory_order_relaxed);
    if (next_created == nullptr) {
      // only one thread allowed to create next chunk
      bool expected_zone = false;
      while (!c->create_next_crit.compare_exchange_weak(expected_zone, true, std::memory_order_relaxed)) {
        expected_zone = false;
        std::this_thread::yield();
      }

      // this is crit zone only for current chunk but few threads can create different chunks at same time so _head_push and _free_chain need syncronization

      next_created = c->next.load(std::memory_order_acquire);
      if (next_created) {
        c->create_next_crit.store(false, std::memory_order_relaxed);
        return next_created;
      }

      auto free = _free_chain.load(std::memory_order_acquire);
      if (free == nullptr) {
        // create new one
        free = new task_chunk();
      } else {
        auto next = free->next.load(std::memory_order_relaxed);
        while (!_free_chain.compare_exchange_strong(free, next, std::memory_order_release, std::memory_order_relaxed)) {
          ASYNC_CORO_ASSERT(free != nullptr);
          next = free->next.load(std::memory_order_relaxed);
        }

        free->next.store(nullptr, std::memory_order_relaxed);
        free->begin.store(0, std::memory_order_relaxed);
        free->end.store(0, std::memory_order_relaxed);
        free->num_used.store(0, std::memory_order_relaxed);
        free->free_protect.store(0, std::memory_order_relaxed);
      }

      c->next.store(free, std::memory_order_release);

      task_chunk* expected = c;
      while (!_head_push.compare_exchange_strong(expected, free, std::memory_order_release, std::memory_order_relaxed)) {
      }

      c->create_next_crit.store(false, std::memory_order_relaxed);
      return free;
    } else {
      return next_created;
    }
  }

 public:
  atomic_queue()
      : _head_pop(new task_chunk()), _free_chain(new task_chunk()) {
    _free_chain.load(std::memory_order_relaxed)->free_protect.store(-1, std::memory_order_relaxed);
    _head_push.store(_head_pop.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }

  ~atomic_queue() noexcept {
    auto head1 = _head_pop.load(std::memory_order_acquire);
    auto head2 = _free_chain.load(std::memory_order_acquire);

    ASYNC_CORO_ASSERT(head1 != nullptr);
    ASYNC_CORO_ASSERT(head2 != nullptr);

    while (!_head_pop.compare_exchange_strong(head1, nullptr, std::memory_order_release, std::memory_order_relaxed)) {
    }
    while (!_free_chain.compare_exchange_strong(head2, nullptr, std::memory_order_release, std::memory_order_relaxed)) {
    }
    _head_push.store(nullptr, std::memory_order_release);

    while (head1) {
      auto* next = head1->next.load(std::memory_order_acquire);
      head1->next.store(nullptr, std::memory_order_release);

      delete head1;

      head1 = next;
    }

    while (head2) {
      auto* next = head2->next.load(std::memory_order_acquire);
      head2->next.store(nullptr, std::memory_order_release);

      delete head2;

      head2 = next;
    }
  }

  template <typename... U>
  void push(U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
    auto head = _head_push.load(std::memory_order_acquire);
    ASYNC_CORO_ASSERT(head != nullptr);
    auto protect = head->free_protect.load(std::memory_order_relaxed);
    do {
      if (protect < 0) {
        // this chunk was freed try next
        head = _head_push.load(std::memory_order_acquire);
        ASYNC_CORO_ASSERT(head != nullptr);
        protect = head->free_protect.load(std::memory_order_relaxed);
      }
    } while (!head->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order_relaxed));

    head->push(*this, std::forward<U>(v)...);
  }

  bool try_pop(T& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
    auto head = _head_pop.load(std::memory_order_acquire);
    ASYNC_CORO_ASSERT(head != nullptr);
    auto protect = head->free_protect.load(std::memory_order_relaxed);
    do {
      if (protect < 0) {
        // this chunk was freed try next
        head = _head_pop.load(std::memory_order_acquire);
        ASYNC_CORO_ASSERT(head != nullptr);
        protect = head->free_protect.load(std::memory_order_relaxed);
      }
    } while (!head->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order_relaxed));

    // release of free_protect inside try_pop
    bool need_release = true;
    auto ans = head->try_pop(v, *this, need_release);
    if (need_release) [[likely]] {
      head->free_protect.fetch_sub(1, std::memory_order_relaxed);
    }
    return ans;
  }

  bool has_value() const noexcept {
    auto head = _head_pop.load(std::memory_order_relaxed);
    ASYNC_CORO_ASSERT(head != nullptr);
    return head->has_value();
  }

 private:
  std::atomic<task_chunk*> _head_pop;
  std::atomic<task_chunk*> _head_push;
  std::atomic<task_chunk*> _free_chain;
};
};  // namespace async_coro
