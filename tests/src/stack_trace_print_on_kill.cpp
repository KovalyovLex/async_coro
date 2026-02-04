#include "stack_trace_print_on_kill.h"

#if defined(__linux__)
#include <cxxabi.h>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <unwind.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <ostream>
#include <string_view>

// NOLINTBEGIN(*array-index*, *vararg)

struct stack_write_buffer {  // NOLINT(*member-init*)

  void append_string(std::string_view str) noexcept {
    const auto count = std::min(_buffer.size() - _end, str.size());

    std::memmove(&_buffer[_end], str.data(), count);
    _end += count;
  }

  std::string_view get_result() noexcept {
    return {_buffer.data(), _end};
  }

 private:
  size_t _end = 0;
  std::array<char, 20UL * 1024U> _buffer;
};

struct backtrace_state {
  void **current = nullptr;
  void **end = nullptr;
};

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *context, void *arg) {
  auto *state = reinterpret_cast<backtrace_state *>(arg);

  const uintptr_t pc = _Unwind_GetIP(context);
  if (pc != 0) {
    if (state->current == state->end) {
      return _URC_END_OF_STACK;
    }
    *state->current++ = reinterpret_cast<void *>(pc);  // NOLINT(*int-to-ptr*)
  }

  return _URC_NO_REASON;
}

static int get_backtrace(void **buffer, size_t buffer_size) {
  backtrace_state state = {.current = buffer, .end = buffer + buffer_size};
  _Unwind_Backtrace(unwind_callback, &state);

  size_t frame_count = 0;
  while (frame_count < buffer_size) {
    if (buffer[frame_count] == nullptr) {
      break;
    }
    frame_count++;
  }

  return (int)frame_count;
}

static void print_backtrace(stack_write_buffer &buffer) {
  std::array<void *, 64> frames{};
  std::array<char, 1300> buf;  // NOLINT(*init*)

  // int frame_count = backtrace(frames.data(), frames.size());

  int frame_count = get_backtrace(frames.data(), frames.size());

  for (int i = 0; i < frame_count; ++i) {
    Dl_info info;
    if (dladdr(frames[i], &info) != 0) {
      const auto offset = (static_cast<const char *>(frames[i]) - static_cast<const char *>(info.dli_fbase));

      if (info.dli_sname != nullptr) {
        int status = 0;
        std::array<char, 1024> demangle_buf;  // NOLINT(*init*)
        size_t size = demangle_buf.size();
        char *dem = abi::__cxa_demangle(info.dli_sname, demangle_buf.data(), &size, &status);
        const char *symname = (status == 0 && dem != nullptr) ? dem : info.dli_sname;

        int n = snprintf(buf.data(), buf.size(), "  %02d: %p  %s + 0x%lx (%s)\n",
                         i, frames[i], info.dli_fname != nullptr ? info.dli_fname : "?",
                         (unsigned long)offset, symname);
        if (n > 0) {
          std::string_view str{buf.data(), (size_t)n};
          buffer.append_string(str);
        }

        // free allocated string
        if (dem != demangle_buf.data()) {
          free(dem);
        }
      } else {
        int n = snprintf(buf.data(), buf.size(), "  %02d: %p  %s + 0x%lx (unknown)\n",
                         i, frames[i], info.dli_fname != nullptr ? info.dli_fname : "?",
                         (unsigned long)offset);
        if (n > 0) {
          std::string_view str{buf.data(), (size_t)n};
          buffer.append_string(str);
        }
      }
    } else {
      int n = snprintf(buf.data(), buf.size(), "  %02d: %p  ?(unknown)\n",
                       i, frames[i]);
      if (n > 0) {
        std::string_view str{buf.data(), (size_t)n};
        buffer.append_string(str);
      }
    }
  }
}

static void sigusr2_handler(int /*signo*/, siginfo_t * /*info*/, void * /*ucontext*/) {
  stack_write_buffer buffer{};

  std::array<char, 256> hdr{};
  const auto tid = (pid_t)syscall(SYS_gettid);
  int n = snprintf(hdr.data(), hdr.size(), "=== [RECEIVED SIGUSR2] Backtrace for thread %d ===", (int)tid);
  if (n > 0) {
    buffer.append_string({"\n"});
    buffer.append_string({hdr.data(), (size_t)n});
    buffer.append_string({"\n"});
  }
  print_backtrace(buffer);

  std::cout << buffer.get_result() << std::endl;
}

static void interrupt_handler(int signo) {
  const auto pid = getpid();
  const auto *signal_name = (signo == SIGINT) ? "SIGINT" : ((signo == SIGTERM) ? "SIGTERM" : "SIGNAL");  // NOLINT

  std::cout << "[RECEIVED " << signal_name << "] Sending SIGUSR2 to all threads in process " << (int)pid << std::endl;

  // Send SIGUSR2 to all threads (iterate /proc/self/task)
  DIR *d = opendir("/proc/self/task");
  if (d != nullptr) {
    struct dirent *ent = nullptr;
    int thread_count = 0;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.') {
        continue;
      }
      const auto tid = (pid_t)atoi(&ent->d_name[0]);
      if (tid <= 0) {
        continue;
      }
      const auto ret = (int)syscall(SYS_tgkill, pid, tid, SIGUSR2);
      thread_count++;

      std::cout << "[SIGNAL SENT] SIGUSR2 -> TID " << tid << " (result=" << ret << ")" << std::endl;
    }
    closedir(d);

    std::cout << "[SIGNAL BROADCAST] Sent SIGUSR2 to " << thread_count << " threads" << std::endl;
  }

  // give threads a moment to print their traces
  std::cout << "[HANDLER] Sleeping 1 second to allow threads to print backtraces..." << std::endl;

  sleep(1);

  std::cout << "[HANDLER] Restoring default handler and re-raising " << signal_name << std::endl;

  signal(signo, SIG_DFL);
  raise(signo);
}

static void install_backtrace_handler() {
  struct sigaction sa{};
  sa.sa_sigaction = sigusr2_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, nullptr);

  std::cout << "[HANDLER INSTALL] SIGUSR2 handler installed" << std::endl;
}

static void install_interrupt_handlers() {
  struct sigaction sa{};
  sa.sa_handler = interrupt_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  std::cout << "[HANDLER INSTALL] SIGINT and SIGTERM handlers installed" << std::endl;
}

void utils::install_backtrace_handler_on_kill() {
  // Install handlers early so all threads inherit the SIGUSR2 handler.
  install_backtrace_handler();
  install_interrupt_handlers();
}

// NOLINTEND(*array-index*, *vararg)

#else

void utils::install_backtrace_handler_on_kill() {
}

#endif
