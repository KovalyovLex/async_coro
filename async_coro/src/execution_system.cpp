#include <async_coro/config.h>
#include <async_coro/execution_system.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/utils/set_thread_name.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace async_coro {

execution_system::execution_system(const execution_system_config& config, const execution_queue_mark max_queue)
    : _main_thread_mask(config.main_thread_allowed_tasks),
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
        thread_data.data.set_owning_thread(std::this_thread::get_id());

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
  {
    unique_lock lock(_delayed_mutex);
    _delayed_tasks.clear();
  }
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

    // function will be destroyed without lock
    auto func = std::move(task_it->func);
    lock.unlock();

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
    if (worker->notifier.notify()) {
      // leave others in sleeping state
      return;
    }
  }
}

void execution_system::execute_or_plan_execution(task_function func, execution_queue_mark execution_queue, const executor_data& curent_data) {
  if (!func) [[unlikely]] {
    return;
  }
  const auto current_thread = curent_data.get_owning_thread();
  if (execution_system::is_thread_fits(execution_queue, current_thread)) {
    func(curent_data);
    return;
  }

  // plan execution
  auto& task_q = _tasks_queues[execution_queue.get_value()];
  task_q.queue.push(std::move(func));

  for (auto* worker : task_q.workers_data) {
    if (worker->notifier.notify()) {
      // leave others in sleeping state
      return;
    }
  }
}

bool execution_system::is_thread_fits(execution_queue_mark execution_queue, std::thread::id thread_id) const noexcept {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());

  if (_main_thread_data.get_owning_thread() == thread_id) {
    if (_main_thread_mask.allowed(execution_queue)) {
      return true;
    }
  }

  for (std::uint32_t i = 0; i < _num_workers; i++) {
    if (_thread_data[i].thread.get_id() == thread_id) {
      if (_thread_data[i].mask.allowed(execution_queue)) {
        return true;
      }
      break;
    }
  }

  return false;
}

void execution_system::update_from_main() {
  ASYNC_CORO_ASSERT(_main_thread_data.get_owning_thread() == std::this_thread::get_id());

  task_function func;

  // trying to execute one task from each q
  for (auto* task_q : _main_thread_queues) {
    if (task_q->try_pop(func)) {
      func(_main_thread_data);
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
        func(data.data);
        func = nullptr;

        if (_is_stopping.load(std::memory_order::relaxed)) [[unlikely]] {
          break;
        }
      }
    }

    if (is_empty_loop && ++num_empty_loops > data.num_loops_before_sleep) {
      if (_is_stopping.load(std::memory_order::relaxed)) [[unlikely]] {
        break;
      }
      data.notifier.sleep();
      num_empty_loops = 0;
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
        const auto time = top.when;  // top may be freed and wait_until may do checks with this variable on spurious wakeup
        _delayed_cv.wait_until(lock, time);
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
      if (worker->notifier.notify()) {
        // leave others in sleeping state
        break;
      }
    }

    lock.lock();
  }
}

}  // namespace async_coro
