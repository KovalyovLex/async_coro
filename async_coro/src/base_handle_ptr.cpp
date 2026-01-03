#include <async_coro/base_handle.h>
#include <async_coro/internal/base_handle_ptr.h>

namespace async_coro {

void base_handle_ptr::reset(base_handle* handle) noexcept {
  if (_handle == handle) {
    return;
  }

  if (_handle != nullptr) {
    _handle->dec_num_owners();
  }

  _handle = handle;

  if (_handle != nullptr) {
    _handle->inc_num_owners();
  }
}

base_handle_ptr::base_handle_ptr(base_handle* handle) noexcept
    : _handle(handle) {
  if (_handle != nullptr) {
    _handle->inc_num_owners();
  }
}

base_handle_ptr::~base_handle_ptr() noexcept {
  if (_handle != nullptr) {
    _handle->dec_num_owners();
    //_handle = nullptr;
  }
}

}  // namespace async_coro
