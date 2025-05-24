#pragma once

// Enable thread safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.
#if defined(__clang__)
#define CORO_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define CORO_THREAD_ANNOTATION_ATTRIBUTE__(x)  // no-op
#endif

#define COTHREAD_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define COTHREAD_SCOPED_CAPABILITY \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define COTHREAD_GUARDED_BY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define COTHREAD_PT_GUARDED_BY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define COTHREAD_ACQUIRED_BEFORE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define COTHREAD_ACQUIRED_AFTER(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define COTHREAD_REQUIRES(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define COTHREAD_REQUIRES_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define COTHREAD_ACQUIRE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define COTHREAD_ACQUIRE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define COTHREAD_RELEASE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define COTHREAD_RELEASE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define COTHREAD_RELEASE_GENERIC(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

#define COTHREAD_TRY_ACQUIRE(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define COTHREAD_TRY_ACQUIRE_SHARED(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define COTHREAD_EXCLUDES(...) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define COTHREAD_ASSERT_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define COTHREAD_ASSERT_SHARED_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define COTHREAD_RETURN_CAPABILITY(x) \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define COTHREAD_NO_THREAD_SAFETY_ANALYSIS \
  CORO_THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)
