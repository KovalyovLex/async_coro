#include <async_coro/config.h>
#include <async_coro/execution_system.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000.
  LPCSTR szName;     // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;     // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

#else

#include <pthread.h>

#endif

namespace async_coro {

execution_system::execution_system(const execution_system_config& config, const execution_queue_mark max_queue)
    : _main_thread_id(std::this_thread::get_id()),
      _main_thread_mask(config.main_thread_allowed_tasks),
      _num_workers(static_cast<std::uint32_t>(config.worker_configs.size())),
      _max_q(max_queue) {
  std::atomic<std::size_t> num_threads_to_wait_start = 0;

  // NOLINTBEGIN(*-avoid-c-arrays)
  _thread_data = std::make_unique<worker_thread_data[]>(_num_workers);

  _tasks_queues = std::make_unique<task_queue[]>(max_queue.get_value() + 1);
  // NOLINTEND(*-avoid-c-arrays)

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    const auto& worker_config = config.worker_configs[i];
    auto& thread_data = _thread_data[i];

    for (uint8_t q_id = 0; q_id <= max_queue.get_value(); q_id++) {
      execution_thread_mask mask{execution_queue_mark{q_id}};
      if (mask.allowed(worker_config.allowed_tasks)) {
        auto& task_q = _tasks_queues[q_id];
        thread_data.task_queues.push_back(std::addressof(task_q.queue));
        task_q.workers_data.push_back(std::addressof(thread_data));
      }
    }

    thread_data.mask = worker_config.allowed_tasks;
    thread_data.num_loops_before_sleep = worker_config.num_loops_before_sleep;

    if (!thread_data.task_queues.empty()) {
      num_threads_to_wait_start.fetch_add(1, std::memory_order::relaxed);
    }
  }

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    auto& thread_data = _thread_data[i];

    if (!thread_data.task_queues.empty()) {
      const auto& worker_config = config.worker_configs[i];

      thread_data.thread = std::thread([this, &thread_data, &num_threads_to_wait_start]() -> void {
        if (num_threads_to_wait_start.fetch_sub(1, std::memory_order::release) == 1) {
          num_threads_to_wait_start.notify_one();
        }

        worker_loop(thread_data);
      });
      set_thread_name(thread_data.thread, worker_config.name);
    }
  }

  for (uint8_t q_id = 0; q_id <= max_queue.get_value(); q_id++) {
    execution_thread_mask mask{execution_queue_mark{q_id}};
    if (mask.allowed(_main_thread_mask)) {
      _main_thread_queues.push_back(std::addressof(_tasks_queues[q_id].queue));
    }
  }

  // start timer thread for delayed tasks
  num_threads_to_wait_start.fetch_add(1, std::memory_order::relaxed);

  _timer_thread = std::thread([this, &num_threads_to_wait_start]() {
    if (num_threads_to_wait_start.fetch_sub(1, std::memory_order::release) == 1) {
      num_threads_to_wait_start.notify_one();
    }

    timer_loop();
  });
  set_thread_name(_timer_thread, "delayed_tasks_loop");

  auto num_started = num_threads_to_wait_start.load(std::memory_order::acquire);
  while (num_started != 0) {
    num_threads_to_wait_start.wait(num_started, std::memory_order::relaxed);
    num_started = num_threads_to_wait_start.load(std::memory_order::acquire);
  }
}

execution_system::~execution_system() noexcept {
  _is_stopping.store(true, std::memory_order::release);

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    _thread_data[i].notifier.notify();
  }

  // stop timer thread
  _delayed_cv.notify_one();
  if (_timer_thread.joinable()) {
    _timer_thread.join();
  }

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    if (_thread_data[i].thread.joinable()) {
      _thread_data[i].thread.join();
    }
  }
}

delayed_task_id execution_system::plan_execution_after(task_function func, execution_queue_mark execution_queue,
                                                       std::chrono::steady_clock::time_point when) {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());
  if (!func) [[unlikely]] {
    return {};
  }

  // if time is now or in the past, push directly
  if (when <= std::chrono::steady_clock::now()) {
    plan_execution(std::move(func), execution_queue);
    return {};
  }

  bool need_notify = false;
  t_task_id task_id = 0;
  {
    unique_lock lock(_delayed_mutex);

    task_id = _delayed_task_id++;
    if (_delayed_task_id == 0) [[unlikely]] {
      _delayed_task_id = 1;
    }

    _delayed_tasks.emplace_back(std::move(func), when, execution_queue, task_id);
    std::ranges::push_heap(_delayed_tasks, std::greater<delayed_task>{});

    // notify only if our value goes on top of the heap
    need_notify = _delayed_tasks.front().id == task_id;
  }

  if (need_notify) {
    _delayed_cv.notify_one();
  }

  return {.task_id = task_id};
}

bool execution_system::cancel_execution(const delayed_task_id& task_id) {
  if (task_id.task_id == 0) {
    return false;
  }

  unique_lock lock(_delayed_mutex);

  const auto task_it = std::ranges::find_if(_delayed_tasks, [t_id = task_id.task_id](const delayed_task& task) {
    return task.id == t_id;
  });

  if (task_it != _delayed_tasks.end()) {
    task_it->cancel_execution = true;
    return true;
  }
  return false;
}

void execution_system::plan_execution(task_function func, execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());
  if (!func) [[unlikely]] {
    return;
  }

  auto& task_q = _tasks_queues[execution_queue.get_value()];
  task_q.queue.push(std::move(func));

  for (auto* worker : task_q.workers_data) {
    worker->notifier.notify();
  }
}

void execution_system::execute_or_plan_execution(task_function func, execution_queue_mark execution_queue) {
  if (!func) [[unlikely]] {
    return;
  }

  if (execution_system::is_current_thread_fits(execution_queue)) {
    func();
    return;
  }

  // plan execution
  auto& task_q = _tasks_queues[execution_queue.get_value()];
  task_q.queue.push(std::move(func));

  for (auto* worker : task_q.workers_data) {
    worker->notifier.notify();
  }
}

bool execution_system::is_current_thread_fits(execution_queue_mark execution_queue) const noexcept {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());

  const auto current_thread_id = std::this_thread::get_id();
  if (_main_thread_id == current_thread_id) {
    if (_main_thread_mask.allowed(execution_queue)) {
      return true;
    }
  }

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    if (_thread_data[i].thread.get_id() == current_thread_id) {
      if (_thread_data[i].mask.allowed(execution_queue)) {
        return true;
      }
      break;
    }
  }

  return false;
}

void execution_system::update_from_main() {
  ASYNC_CORO_ASSERT(_main_thread_id == std::this_thread::get_id());

  task_function func;

  // trying to execute one task from each q
  for (auto* task_q : _main_thread_queues) {
    if (task_q->try_pop(func)) {
      func();
      func = nullptr;
    }
  }
}

std::uint32_t execution_system::get_num_workers_for_queue(execution_queue_mark execution_queue) const noexcept {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());

  auto& task_q = _tasks_queues[execution_queue.get_value()];

  return static_cast<std::uint32_t>(task_q.workers_data.size()) + (_main_thread_mask.allowed(execution_queue) ? 1 : 0);
}

void execution_system::worker_loop(worker_thread_data& data) {
  std::size_t num_empty_loops = 0;

  while (!_is_stopping.load(std::memory_order::relaxed)) {
    data.notifier.reset_notification();

    task_function func;
    bool is_empty_loop = true;
    for (auto* task_q : data.task_queues) {
      if (task_q->try_pop(func)) {
        is_empty_loop = false;
        func();
        func = nullptr;

        if (_is_stopping.load(std::memory_order::relaxed)) {
          break;
        }
      }
    }

    if (is_empty_loop) {
      if (++num_empty_loops > data.num_loops_before_sleep) {
        data.notifier.sleep();
        num_empty_loops = 0;
      }
    }
  }
}

void execution_system::timer_loop() {
  unique_lock lock(_delayed_mutex);
  while (!_is_stopping.load(std::memory_order::relaxed)) {
    if (_delayed_tasks.empty()) {
      _delayed_cv.wait(lock);
      continue;
    }

    {
      auto now = std::chrono::steady_clock::now();
      auto& top = _delayed_tasks.front();
      if (!top.cancel_execution && top.when > now) {
        _delayed_cv.wait_until(lock, top.when);
        continue;
      }
    }

    std::ranges::pop_heap(_delayed_tasks, std::greater<delayed_task>{});

    if (_delayed_tasks.back().cancel_execution) {
      _delayed_tasks.pop_back();
      continue;
    }

    auto& target_task_q = _tasks_queues[_delayed_tasks.back().queue.get_value()];
    task_function func{std::move(_delayed_tasks.back().func)};

    _delayed_tasks.pop_back();

    lock.unlock();

    ASYNC_CORO_ASSERT(func);

    target_task_q.queue.push(std::move(func));

    for (auto* worker : target_task_q.workers_data) {
      worker->notifier.notify();
    }

    lock.lock();
  }
}

void execution_system::set_thread_name(std::thread& thread, const std::string& name) {
  if (name.empty()) {
    return;
  }

#ifdef _WIN32
  DWORD threadId = ::GetThreadId(static_cast<HANDLE>(thread.native_handle()));

  constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = name.c_str();
  info.dwThreadID = threadId;
  info.dwFlags = 0;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#else

#ifdef __APPLE__
  (void)thread;
  pthread_setname_np(name.c_str());
#else
  auto handle = thread.native_handle();
  pthread_setname_np(handle, name.c_str());
#endif

#endif
}

}  // namespace async_coro
