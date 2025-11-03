#include <async_coro/utils/set_thread_name.h>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000.
  LPCSTR szName;     // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;     // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

#else

#include <pthread.h>

#endif

namespace async_coro {

void set_thread_name(std::thread& thread, const std::string& name) {
  if (name.empty()) {
    return;
  }

#ifdef _WIN32
  DWORD threadId = ::GetThreadId(static_cast<HANDLE>(thread.native_handle()));

  constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = name.c_str();
  info.dwThreadID = threadId;
  info.dwFlags = 0;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#else

#ifdef __APPLE__
  (void)thread;
  ::pthread_setname_np(name.c_str());
#else
  auto handle = thread.native_handle();
  ::pthread_setname_np(handle, name.c_str());
#endif

#endif
}

}  // namespace async_coro
