#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace async_coro {
namespace internal {

uint32_t get_key_for_type(std::type_index type);

template <class T>
struct key_static {
  static inline const uint32_t id_for_t = get_key_for_type(typeid(T));
};

}  // namespace internal

struct data_key {
  uint32_t id;

  auto operator<=>(const data_key&) const noexcept = default;

  template <class T>
  static data_key get_key_for_class() noexcept {
    return internal::key_static<T>::id_for_t;
  }
};

/** @brief Thread local data used in execution system.
 *
 * Every execution callback can use it to access some thread local data specific only for execution system.
 * This thread local data will be allocated on heap with first access
 * This class is not thread safe
 */
class executor_data {
  using destroyer_func_t = void (*)(void*) noexcept;
  using data_ptr = std::unique_ptr<void, destroyer_func_t>;
  using creator_func_t = data_ptr (*)();

 public:
  executor_data() noexcept;
  executor_data(const executor_data&) = delete;
  executor_data(executor_data&&) noexcept;
  executor_data& operator=(const executor_data&) = delete;
  executor_data& operator=(executor_data&&) noexcept;

  ~executor_data() noexcept;

  template <class T>
    requires(std::is_nothrow_destructible_v<T>)
  T& get_data() const {
    return *static_cast<T*>(find_data(
        data_key::get_key_for_class<T>(),
        +[]() {
          return data_ptr{
              new T(),
              +[](void* data) noexcept {
                delete static_cast<T*>(data);  // NOLINT(*owning-memory)
              }};
        }));
  }

  [[nodiscard]] std::thread::id get_owning_thread() const noexcept { return _owning_thread; }

  void set_owning_thread(std::thread::id thread_id) noexcept { _owning_thread = thread_id; }

 private:
  void* find_data(data_key key, creator_func_t creator) const;

 private:
  struct store {
    data_key key;
    data_ptr data;
  };

  std::thread::id _owning_thread;
  mutable std::vector<store> _data;
};

}  // namespace async_coro
