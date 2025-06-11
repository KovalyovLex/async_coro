#include <async_coro/config.h>
#include <async_coro/execution_system.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

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
      _num_workers(static_cast<uint32_t>(config.worker_configs.size())),
      _max_q(max_queue) {
  _thread_data = std::make_unique<worker_thread_data[]>(_num_workers);

  _tasks_queues = std::make_unique<task_queue[]>(max_queue.get_value() + 1);

  for (uint32_t i = 0; i < _num_workers; i++) {
    auto& worker_config = config.worker_configs[i];
    auto& thread_data = _thread_data[i];

    for (uint8_t q = 0; q <= max_queue.get_value(); q++) {
      execution_thread_mask mask{execution_queue_mark{q}};
      if (mask.allowed(worker_config.allowed_tasks)) {
        auto& task_q = _tasks_queues[q];
        thread_data.task_queues.push_back(std::addressof(task_q.queue));
        task_q.workers_data.push_back(std::addressof(thread_data));
      }
    }

    thread_data.mask = worker_config.allowed_tasks;

    if (!thread_data.task_queues.empty()) {
      thread_data.thread = std::thread([this, &thread_data]() {
        worker_loop(thread_data);
      });
      set_thread_name(thread_data.thread, worker_config.name);
    }
  }

  for (uint8_t q = 0; q <= max_queue.get_value(); q++) {
    execution_thread_mask mask{execution_queue_mark{q}};
    if (mask.allowed(_main_thread_mask)) {
      _main_thread_queues.push_back(std::addressof(_tasks_queues[q].queue));
    }
  }
}

execution_system::~execution_system() noexcept {
  _is_stopping.store(true, std::memory_order::release);

  for (uint32_t i = 0; i < _num_workers; i++) {
    _thread_data[i].notifier.notify();
  }

  for (uint32_t i = 0; i < _num_workers; i++) {
    if (_thread_data[i].thread.joinable()) {
      _thread_data[i].thread.join();
    }
  }
}

void execution_system::plan_execution(task_function f, execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());
  if (!f) [[unlikely]] {
    return;
  }

  auto& task_q = _tasks_queues[execution_queue.get_value()];
  task_q.queue.push(std::move(f));

  for (auto* worker : task_q.workers_data) {
    worker->notifier.notify();
  }
}

void execution_system::execute_or_plan_execution(task_function f, execution_queue_mark execution_queue) {
  if (!f) [[unlikely]] {
    return;
  }

  if (execution_system::is_current_thread_fits(execution_queue)) {
    f();
    return;
  }

  // plan execution
  auto& task_q = _tasks_queues[execution_queue.get_value()];
  task_q.queue.push(std::move(f));

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

  for (uint32_t i = 0; i < _num_workers; i++) {
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

  task_function f;

  // trying to execute one task from each q
  for (auto* task_q : _main_thread_queues) {
    if (task_q->try_pop(f)) {
      f();
      f = nullptr;
    }
  }
}

uint32_t execution_system::get_num_workers_for_queue(execution_queue_mark execution_queue) const noexcept {
  ASYNC_CORO_ASSERT(execution_queue.get_value() <= _max_q.get_value());

  auto& task_q = _tasks_queues[execution_queue.get_value()];

  return static_cast<uint32_t>(task_q.workers_data.size()) + (_main_thread_mask.allowed(execution_queue) ? 1 : 0);
}

void execution_system::worker_loop(worker_thread_data& data) {
  std::uint32_t num_empty_loops = 0;

  while (!_is_stopping.load(std::memory_order::relaxed)) {
    task_function f;
    bool is_empty_loop = true;
    for (auto* task_q : data.task_queues) {
      if (task_q->try_pop(f)) {
        is_empty_loop = false;
        f();
        f = nullptr;

        if (_is_stopping.load(std::memory_order::relaxed)) {
          break;
        }
      }
    }

    if (is_empty_loop) {
      if (++num_empty_loops > 10) {
        data.notifier.sleep();
        num_empty_loops = 0;
      }
    }
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
