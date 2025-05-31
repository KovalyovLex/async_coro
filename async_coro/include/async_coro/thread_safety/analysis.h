#pragma once

// Enable thread safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.
#if defined(__clang__)
#define CORO_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define CORO_THREAD_ANNOTATION_ATTRIBUTE__(x)  // no-op
#endif

#define CORO_THREAD_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define CORO_THREAD_SCOPED_CAPABILITY \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define CORO_THREAD_GUARDED_BY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define CORO_THREAD_PT_GUARDED_BY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define CORO_THREAD_ACQUIRED_BEFORE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define CORO_THREAD_ACQUIRED_AFTER(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define CORO_THREAD_REQUIRES(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define CORO_THREAD_REQUIRES_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define CORO_THREAD_ACQUIRE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define CORO_THREAD_ACQUIRE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define CORO_THREAD_RELEASE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define CORO_THREAD_RELEASE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define CORO_THREAD_RELEASE_GENERIC(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

#define CORO_THREAD_TRY_ACQUIRE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define CORO_THREAD_TRY_ACQUIRE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define CORO_THREAD_EXCLUDES(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define CORO_THREAD_ASSERT_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define CORO_THREAD_ASSERT_SHARED_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define CORO_THREAD_RETURN_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define CORO_THREAD_NO_THREAD_SAFETY_ANALYSIS \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)
