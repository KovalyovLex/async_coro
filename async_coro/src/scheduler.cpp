#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

namespace async_coro {

scheduler::scheduler()
    : scheduler(std::make_unique<execution_system>(execution_system_config{})) {
}

scheduler::scheduler(i_execution_system::ptr system) noexcept
    : _execution_system(std::move(system)) {
  ASYNC_CORO_ASSERT(_execution_system);
}

scheduler::~scheduler() {
  unique_lock lock{_mutex};
  auto coros = std::move(_managed_coroutines);
  _is_destroying = true;
  lock.unlock();

  for (auto& coro : coros) {
    if (coro) {
      coro->on_task_freed_by_scheduler();
    }
  }
}

bool scheduler::is_current_thread_fits(execution_queue_mark execution_queue) noexcept {
  return _execution_system->is_current_thread_fits(execution_queue);
}

void scheduler::continue_execution_impl(base_handle& handle_impl) {
  ASYNC_CORO_ASSERT(handle_impl.is_current_thread_same());

  handle_impl.set_coroutine_state(coroutine_state::running);
  handle_impl._handle.resume();

  const auto state = handle_impl.get_coroutine_state();

  ASYNC_CORO_ASSERT(state != coroutine_state::running);

  if (state == coroutine_state::waiting_switch) {
    change_execution_queue(handle_impl, handle_impl._execution_queue);
  } else if (state == coroutine_state::finished) {
    if (auto* parent = handle_impl.get_parent(); parent && parent->get_coroutine_state() == coroutine_state::suspended) {
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
      handle_impl.on_task_freed_by_scheduler();
    }
  }
}

void scheduler::plan_continue_on_thread(base_handle& handle_impl, execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(handle_impl._scheduler == this);

  _execution_system->plan_execution(
      [this, handle_base = &handle_impl, execution_queue]() {
        handle_base->_execution_thread = std::this_thread::get_id();
        handle_base->_execution_queue = execution_queue;
        this->continue_execution_impl(*handle_base);
      },
      execution_queue);
}

void scheduler::add_coroutine(base_handle& handle_impl,
                              execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl.get_coroutine_state() == coroutine_state::created);
  ASYNC_CORO_ASSERT(handle_impl._handle);

  {
    unique_lock lock{_mutex};

    if (_is_destroying) {
      // if we are in destructor no way to run this coroutine
      handle_impl.on_task_freed_by_scheduler();
      return;
    }

    _managed_coroutines.push_back(&handle_impl);
  }

  handle_impl._scheduler = this;

  if (is_current_thread_fits(execution_queue)) {
    // start execution immediately if we in right thread

    handle_impl._execution_thread = std::this_thread::get_id();
    handle_impl._execution_queue = execution_queue;
    continue_execution_impl(handle_impl);
  } else {
    change_execution_queue(handle_impl, execution_queue);
  }
}

void scheduler::continue_execution(base_handle& handle_impl) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread != std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl.get_coroutine_state() == coroutine_state::suspended);

  if (handle_impl.is_current_thread_same()) {
    // start execution immediately if we in right thread
    continue_execution_impl(handle_impl);
  } else {
    plan_continue_on_thread(handle_impl, handle_impl._execution_queue);
  }
}

void scheduler::change_execution_queue(base_handle& handle_impl,
                                       execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(!is_current_thread_fits(execution_queue));

  plan_continue_on_thread(handle_impl, execution_queue);
}

void scheduler::on_child_coro_added(base_handle& parent, base_handle& child, internal::passkey<task_base>) {
  ASYNC_CORO_ASSERT(parent._scheduler == this);
  ASYNC_CORO_ASSERT(child._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(child.get_coroutine_state() == coroutine_state::created);

  child._scheduler = this;
  child._execution_thread = parent._execution_thread;
  child._execution_queue = parent._execution_queue;
  child.set_parent(parent);
  child.set_coroutine_state(coroutine_state::suspended);

  // start execution of internal coroutine
  continue_execution_impl(child);
}

}  // namespace async_coro
