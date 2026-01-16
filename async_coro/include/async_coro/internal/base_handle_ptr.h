#pragma once

#include <cstddef>
#include <utility>

namespace async_coro {
class base_handle;

class base_handle_ptr {
 public:
  base_handle_ptr() noexcept = default;
  explicit base_handle_ptr(base_handle* handle) noexcept;
  base_handle_ptr(std::nullptr_t) noexcept {}  // NOLINT(*explicit*)
  base_handle_ptr(const base_handle_ptr& other) = delete;
  base_handle_ptr(base_handle_ptr&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)) {}

  base_handle_ptr& operator=(const base_handle_ptr& other) = delete;
  base_handle_ptr& operator=(base_handle_ptr&& other) noexcept {
    if (_handle == other._handle) {
      return *this;
    }

    reset(nullptr);
    _handle = std::exchange(other._handle, nullptr);
    return *this;
  }

  ~base_handle_ptr() noexcept;

  [[nodiscard]] base_handle_ptr copy() const noexcept {
    return base_handle_ptr{get()};
  }

  void reset(base_handle* handle = nullptr) noexcept;

  explicit operator bool() const noexcept {
    return _handle != nullptr;
  }

  base_handle& operator*() const noexcept {
    return *_handle;
  }

  base_handle* operator->() const noexcept {
    return _handle;
  }

  [[nodiscard]] base_handle* get() const noexcept {
    return _handle;
  }

  void swap(base_handle_ptr& other) noexcept {
    std::swap(_handle, other._handle);
  }

  bool operator==(const base_handle_ptr& other) const noexcept {
    return _handle == other._handle;
  }

  bool operator==(const base_handle* handle) const noexcept {
    return _handle == handle;
  }

 private:
  base_handle* _handle = nullptr;
};

}  // namespace async_coro
