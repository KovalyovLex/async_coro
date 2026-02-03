#include <gtest/gtest.h>

#if defined(__linux__)
#include <cxxabi.h>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// NOLINTBEGIN(*array-index*, *vararg)

static void write_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, buf, len);
    if (n <= 0) {
      break;
    }
    buf += n;
    len -= n;
  }
}

static void print_backtrace_to_stderr() {
  std::array<void *, 64> frames{};
  std::array<char, 1024> buf{};

  int frame_count = backtrace(frames.data(), frames.size());
  char **symbols = backtrace_symbols(frames.data(), frame_count);

  for (int i = 0; i < frame_count; ++i) {
    Dl_info info;
    if (dladdr(frames[i], &info) != 0 && info.dli_sname != nullptr) {
      int status = 0;
      char *dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
      const char *symname = (status == 0 && dem != nullptr) ? dem : info.dli_sname;

      const auto offset = (static_cast<const char *>(frames[i]) - static_cast<const char *>(info.dli_saddr));
      int n = snprintf(buf.data(), buf.size(), "  %02d: %p  %s + 0x%lx (%s)\n",
                       i, frames[i], info.dli_fname != nullptr ? info.dli_fname : "?",
                       (unsigned long)offset, symname);
      if (n > 0) {
        write_all(STDERR_FILENO, buf.data(), (size_t)n);
      }
      free(dem);
    } else {
      if (symbols != nullptr && symbols[i] != nullptr) {
        write_all(STDERR_FILENO, symbols[i], strlen(symbols[i]));
        write_all(STDERR_FILENO, "\n", 1);
      }
    }
  }

  free(static_cast<void *>(symbols));
}

static void sigusr2_handler(int /*signo*/, siginfo_t * /*info*/, void * /*ucontext*/) {
  std::array<char, 128> hdr{};

  const auto tid = (pid_t)syscall(SYS_gettid);
  int n = snprintf(hdr.data(), hdr.size(), "=== Backtrace for thread %d ===\n", (int)tid);
  if (n > 0) {
    write_all(STDERR_FILENO, hdr.data(), (size_t)n);
  }
  print_backtrace_to_stderr();
}

static void install_backtrace_handler() {
  struct sigaction sa{};
  sa.sa_sigaction = sigusr2_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, nullptr);
}

static void interrupt_handler(int signo) {
  // Send SIGUSR2 to all threads (iterate /proc/self/task)
  pid_t pid = getpid();
  DIR *d = opendir("/proc/self/task");
  if (d != nullptr) {
    struct dirent *ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.') {
        continue;
      }
      const auto tid = (pid_t)atoi(&ent->d_name[0]);
      if (tid <= 0) {
        continue;
      }
      syscall(SYS_tgkill, pid, tid, SIGUSR2);
    }
    closedir(d);
  }

  // give threads a moment to print their traces
  sleep(1);

  // restore default and re-raise to allow normal termination
  signal(signo, SIG_DFL);
  raise(signo);
}

static void install_interrupt_handlers() {
  struct sigaction sa{};
  sa.sa_handler = interrupt_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

// NOLINTEND(*array-index*, *vararg)

#endif

int main(int argc, char **argv) {
#if defined(__linux__)
  // Install handlers early so all threads inherit the SIGUSR2 handler.
  install_backtrace_handler();
  install_interrupt_handlers();
#endif

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
