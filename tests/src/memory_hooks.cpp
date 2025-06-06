#include "memory_hooks.h"

#include <cassert>
#include <cstdlib>

#if MEM_HOOKS_ENABLED

void* operator new(std::size_t count) {
  mem_hook::num_allocated += count + sizeof(std::size_t);
  auto mem =
      static_cast<std::size_t*>(std::malloc(count + sizeof(std::size_t)));
  *mem = count;
  return mem + 1;
}

void* operator new(std::size_t count, const std::nothrow_t&) noexcept {
  return operator new(count);
}

void operator delete(void* ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }

  auto mem = static_cast<std::size_t*>(ptr) - 1;
  mem_hook::num_allocated -= *mem + sizeof(std::size_t);
  std::free(mem);
}

void operator delete(void* ptr, std::size_t) noexcept {
  return operator delete(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  return operator delete(ptr);
}

#endif