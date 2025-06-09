#include <atomic>
#include <new>

namespace mem_hook {
inline std::atomic_size_t num_allocated = 0;
}

#if defined(__clang__) && defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MEM_HOOKS_ENABLED 0
#else
#define MEM_HOOKS_ENABLED 1
#endif
#else
#define MEM_HOOKS_ENABLED 1
#endif

#if MEM_HOOKS_ENABLED

[[nodiscard]] void* operator new(std::size_t count);

[[nodiscard]] void* operator new(std::size_t count, const std::nothrow_t& tag) noexcept;

void operator delete(void* ptr) noexcept;

void operator delete(void* ptr, const std::nothrow_t& tag) noexcept;

void operator delete(void* ptr, std::size_t size) noexcept;

#endif
