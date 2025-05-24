#include <async_coro/base_handle.h>
#include <async_coro/internal/thread_safety/unique_lock.h>
#include <async_coro/scheduler.h>

#include <algorithm>
#include <thread>

namespace async_coro {

scheduler::scheduler() noexcept {
  // store this thread as main
  _main_thread = std::this_thread::get_id();
}

scheduler::~scheduler() {
  unique_lock lock{_mutex};
  auto coros = std::move(_managed_coroutines);
  _is_destroying = true;
  lock.unlock();

  for (auto& coro : coros) {
    if (coro) {
      coro->try_free_task_impl();
    }
  }
}

bool scheduler::is_current_thread_fits(execution_thread thread) noexcept {
  if (thread == execution_thread::main) {
    return _main_thread == std::this_thread::get_id();
  } else if (thread == execution_thread::worker) {
    return _queue.is_current_thread_worker();
  }
  return false;
}

void scheduler::update() {
  ASYNC_CORO_ASSERT(_main_thread == std::this_thread::get_id());

  if (!_update_tasks.empty()) {
    for (size_t i = 0; i < _update_tasks.size(); i++) {
      _update_tasks[i](*this);
    }
    _update_tasks.clear();
  }

  if (_has_synchronized_tasks.load(std::memory_order::acquire)) {
    {
      unique_lock lock{_task_mutex};
      _update_tasks.swap(_update_tasks_synchronized);
      _has_synchronized_tasks.store(false, std::memory_order::release);
    }

    for (size_t i = 0; i < _update_tasks.size(); i++) {
      _update_tasks[i](*this);
    }
    _update_tasks.clear();
  }
}

void scheduler::continue_execution_impl(base_handle& handle_impl) {
  ASYNC_CORO_ASSERT(handle_impl.is_current_thread_same());

  handle_impl._state = coroutine_state::running;
  handle_impl._handle.resume();
  if (handle_impl._state == coroutine_state::running) [[likely]] {
    handle_impl._state = handle_impl._handle.done() ? coroutine_state::finished
                                                    : coroutine_state::suspended;
  }

  if (handle_impl._state == coroutine_state::finished) {
    if (auto* parent = handle_impl.get_parent(); parent && parent->_state == coroutine_state::suspended) {
      // wake up parent coroutine
      continue_execution(*handle_impl._parent);
    } else if (!parent) {
      // cleanup coroutine
      {
        // remove from managed
        unique_lock lock{_mutex};
        auto it = std::find(_managed_coroutines.begin(), _managed_coroutines.end(), &handle_impl);
        ASYNC_CORO_ASSERT(it != _managed_coroutines.end());
        if (it != _managed_coroutines.end()) {
          if (*it != _managed_coroutines.back()) {
            std::swap(*it, _managed_coroutines.back());
          }
          _managed_coroutines.resize(_managed_coroutines.size() - 1);
        }
      }
      handle_impl.try_free_task_impl();
    }
  }
}

void scheduler::plan_continue_on_thread(base_handle& handle_impl, execution_thread thread) {
  ASYNC_CORO_ASSERT(handle_impl._scheduler == this);

  if (thread == execution_thread::main) {
    if (std::this_thread::get_id() == _main_thread) {
      _update_tasks.emplace_back(
          [handle_base = &handle_impl](scheduler& sched) {
            handle_base->_execution_thread = std::this_thread::get_id();
            sched.continue_execution_impl(*handle_base);
          });
    } else {
      unique_lock lock{_task_mutex};
      _update_tasks_synchronized.emplace_back(
          [handle_base = &handle_impl](scheduler& sched) {
            handle_base->_execution_thread = std::this_thread::get_id();
            sched.continue_execution_impl(*handle_base);
          });
      _has_synchronized_tasks.store(true, std::memory_order::release);
    }
  } else {
    _queue.execute([this, handle_base = &handle_impl]() {
      handle_base->_execution_thread = std::this_thread::get_id();
      this->continue_execution_impl(*handle_base);
    });
  }
}

void scheduler::add_coroutine(base_handle& handle_impl,
                              execution_thread thread) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl._state == coroutine_state::created);
  ASYNC_CORO_ASSERT(handle_impl._handle);

  {
    unique_lock lock{_mutex};

    if (_is_destroying) {
      // if we are in destructor no way to run this coroutine
      handle_impl.try_free_task_impl();
      return;
    }

    _managed_coroutines.push_back(&handle_impl);
  }

  handle_impl._scheduler = this;

  if (is_current_thread_fits(thread)) {
    // start execution immediately if we in right thread

    handle_impl._execution_thread = std::this_thread::get_id();
    continue_execution_impl(handle_impl);
  } else {
    change_thread(handle_impl, thread);
  }
}

void scheduler::continue_execution(base_handle& handle_impl) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread != std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl._state == coroutine_state::suspended);

  if (handle_impl.is_current_thread_same()) {
    // start execution immediately if we in right thread
    continue_execution_impl(handle_impl);
  } else {
    if (handle_impl._execution_thread == _main_thread) {
      plan_continue_on_thread(handle_impl, execution_thread::main);
    } else {
      plan_continue_on_thread(handle_impl, execution_thread::worker);
    }
  }
}

void scheduler::plan_continue_execution(base_handle& handle_impl) noexcept {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread != std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl._state == coroutine_state::suspended);

  if (handle_impl._execution_thread == _main_thread) {
    plan_continue_on_thread(handle_impl, execution_thread::main);
  } else {
    plan_continue_on_thread(handle_impl, execution_thread::worker);
  }
}
void scheduler::change_thread(base_handle& handle_impl,
                              execution_thread thread) {
  ASYNC_CORO_ASSERT(!is_current_thread_fits(thread));

  plan_continue_on_thread(handle_impl, thread);
}

void scheduler::on_child_coro_added(base_handle& parent, base_handle& child) {
  ASYNC_CORO_ASSERT(parent._scheduler == this);
  ASYNC_CORO_ASSERT(child._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(child._state == coroutine_state::created);

  child._scheduler = this;
  child._execution_thread = parent._execution_thread;
  child.set_parent(parent);
  child._state = coroutine_state::suspended;

  // start execution of internal coroutine
  continue_execution_impl(child);
}

}  // namespace async_coro
