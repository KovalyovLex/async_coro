#include <atomic>
#include <new>

namespace mem_hook
{
	inline std::atomic_size_t num_allocated = 0;
}

void* operator new (std::size_t count);

void operator delete (void* ptr) noexcept;
