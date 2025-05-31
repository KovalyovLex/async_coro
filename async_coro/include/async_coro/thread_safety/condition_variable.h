#pragma once

#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <condition_variable>

namespace async_coro {

// Replacement for std::mutex to work with clang thread safety analysis
class condition_variable : public std::condition_variable {
  using super = std::condition_variable;

 public:
  using super::wait;

  template <class T>
  void wait(unique_lock<T>& lock) CORO_THREAD_REQUIRES(lock) {
    super::wait(static_cast<std::unique_lock<std::mutex>&>(lock));
  }

  template <class T, class TPredicate>
  void wait(unique_lock<T>& lock, TPredicate&& predicate) CORO_THREAD_REQUIRES(lock) {
    super::wait(static_cast<std::unique_lock<std::mutex>&>(lock), std::forward<TPredicate>(predicate));
  }

  using super::wait_for;

  template <class T, class TRep, class TPeriod>
  std::cv_status wait_for(unique_lock<T>& lock, const std::chrono::duration<TRep, TPeriod>& time) CORO_THREAD_REQUIRES(lock) {
    return super::wait_for(static_cast<std::unique_lock<std::mutex>&>(lock), time);
  }

  template <class T, class TRep, class TPeriod, class TPredicate>
  bool wait_for(unique_lock<T>& lock, const std::chrono::duration<TRep, TPeriod>& time, TPredicate&& predicate) CORO_THREAD_REQUIRES(lock) {
    return super::wait_for(static_cast<std::unique_lock<std::mutex>&>(lock), time, std::forward<TPredicate>(predicate));
  }

  using super::wait_until;

  template <class T, class TClock, class TDuration>
  std::cv_status wait_until(unique_lock<T>& lock, const std::chrono::time_point<TClock, TDuration>& time) CORO_THREAD_REQUIRES(lock) {
    return super::wait_until(static_cast<std::unique_lock<std::mutex>&>(lock), time);
  }

  template <class T, class TClock, class TDuration, class TPredicate>
  bool wait_until(unique_lock<T>& lock, const std::chrono::time_point<TClock, TDuration>& time, TPredicate&& predicate) CORO_THREAD_REQUIRES(lock) {
    return super::wait_until(static_cast<std::unique_lock<std::mutex>&>(lock), time, std::forward<TPredicate>(predicate));
  }
};

}  // namespace async_coro
