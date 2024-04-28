#include <atomic>
#include <new>

namespace mem_hook {
inline std::atomic_size_t num_allocated = 0;
}

[[nodiscard]] void* operator new(std::size_t count);

[[nodiscard]] void* operator new(std::size_t count, const std::nothrow_t& tag) noexcept;

void operator delete(void* ptr) noexcept;

void operator delete(void* ptr, const std::nothrow_t& tag) noexcept;

void operator delete(void* ptr, std::size_t size) noexcept;
