#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>

namespace async_coro {

void task_base::on_child_coro_added(base_handle& parent, base_handle& child) {
  parent.get_scheduler().on_child_coro_added(parent, child, {});
}

}  // namespace async_coro