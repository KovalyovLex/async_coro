#include <async_coro/callback.h>

namespace async_coro {

void callback_base::destroy() noexcept {
  if (_deleter) {
    _deleter(this);
  }
}

void callback_base::default_deleter(callback_base* ptr) noexcept {
  delete ptr;
}

}  // namespace async_coro
