#pragma once

#include <async_coro/config.h>
#include <async_coro/unique_function.h>
#include <concurrentqueue.h>

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace async_coro {
class working_queue3 {
  using task_id = size_t;

 public:
  // use function with Small Function Optimization buffer of 3 pointers
  using task_function = unique_function<void(), sizeof(void*) * 3>;

  static inline constexpr uint32_t bucket_size_default =
      static_cast<uint32_t>(-1);

  working_queue3() = default;
  working_queue3(const working_queue3&) = delete;
  working_queue3(working_queue3&&) = delete;
  ~working_queue3();

  working_queue3& operator=(const working_queue3&) = delete;
  working_queue3& operator=(working_queue3&&) = delete;

  /// @brief Plan function for execution on worker thread
  /// @param f function to execute
  /// @return task id to monitor accomplishment
  void execute(task_function f);

  /// @brief Execute Fx with values from range [begin, end). Execution will be performed on worker threads and in current thread
  /// @tparam Fx lambda with signature void(*It)
  /// @tparam It Random access iterator
  /// @param f function to execute
  /// @param begin iterator on beginning or range
  /// @param end iterator of end
  /// @param bucket_size how work will be shrinked. By default all range (end - begin) splited on num_threads + 1 chunks and executes in parralel
  template <typename Fx, std::random_access_iterator It>
    requires(std::invocable<Fx, decltype(*std::declval<It>())>)
  void parallel_for(const Fx& f, It begin, It end,
                    uint32_t bucket_size = bucket_size_default);

  /// @brief Seting up num of worker threads and create all of them or stops
  /// @param num number of threads to set. If num > num_threads - spawns new threads, otherwise stops extra threads
  void set_num_threads(uint32_t num);

  /// @brief Returns current number or worker threads
  /// @return num_threads
  uint32_t get_num_threads() const noexcept { return _num_alive_threads.load(std::memory_order::acquire); }

  /// @brief Checks it current thread belogs to worker threads
  /// @return true if current thread is worker
  bool is_current_thread_worker() const noexcept;

 private:
  void start_up_threads();

 private:
  mutable std::mutex _threads_mutex;
  std::condition_variable _sleep_variable;
  moodycamel::ConcurrentQueue<std::pair<task_function, task_id>> _tasks;
  std::vector<std::thread> _threads;  // guarded by _threads_mutex
  uint32_t _num_threads = 0;          // guarded by _threads_mutex
  std::atomic<task_id> _current_id = 0;
  std::atomic<uint32_t> _num_alive_threads = 0;
  std::atomic<uint32_t> _num_threads_to_destroy = 0;
  std::atomic<uint32_t> _num_sleeping_threads = 0;
};

template <typename Fx, std::random_access_iterator It>
  requires(std::invocable<Fx, decltype(*std::declval<It>())>)
void working_queue3::parallel_for(const Fx& f, It begin, It end,
                                  uint32_t bucket_size) {
  const auto size = end - begin;
  ASYNC_CORO_ASSERT((long long)size < (long long)bucket_size_default);

  using difference_t = typename std::iterator_traits<It>::difference_type;

  if (bucket_size == bucket_size_default) {
    // equal destribution, plan work for num threads + 1
    const auto num_workers =
        _num_alive_threads.load(std::memory_order::acquire) + 1;
    bucket_size = static_cast<uint32_t>(size) / num_workers;
  }

  std::atomic<uint32_t> num_not_finished = 0;
  uint32_t rest = static_cast<uint32_t>(size);
  task_id wait_id = 0;
  for (auto it = begin; it != end;) {
    const auto step = std::min(rest, bucket_size);
    rest -= step;
    const auto end_chuk_it = it + static_cast<difference_t>(step);
    num_not_finished.fetch_add(1, std::memory_order::relaxed);
    wait_id = _current_id.fetch_add(1, std::memory_order::relaxed);
    _tasks.enqueue(std::pair<task_function, task_id>(
        [it, end_chuk_it, &f, &num_not_finished]() mutable {
          for (; it != end_chuk_it; ++it) {
            std::invoke(f, *it);
          }
          num_not_finished.fetch_sub(1, std::memory_order::release);
        },
        wait_id));
    it = end_chuk_it;
  }

  if (_num_sleeping_threads.load(std::memory_order::relaxed) != 0) {
    _sleep_variable.notify_all();
  }

  // do work in this thread
  std::pair<task_function, task_id> task_pair;
  while (_tasks.try_dequeue(task_pair)) {
    if (task_pair.second > wait_id) {
      // push back this task and finish awaiting
      _tasks.enqueue(std::move(task_pair));
      break;
    }

    auto to_execute = std::move(task_pair.first);

    to_execute();
    to_execute = nullptr;  // destroy function earlier

    if (task_pair.second == wait_id) {
      break;
    }
  }

  // wait till all precesses finish
  auto to_await = num_not_finished.load(std::memory_order::acquire);
  while (to_await > 0) {
    std::this_thread::yield();
    to_await = num_not_finished.load(std::memory_order::acquire);
  }
}
}  // namespace async_coro
