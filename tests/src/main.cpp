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
#include <ctime>

// NOLINTBEGIN(*array-index*, *vararg)

static void write_all(const char *buf, size_t len) {
  while (len > 0) {
    ssize_t n = write(STDOUT_FILENO, buf, len);
    if (n <= 0) {
      break;
    }
    buf += n;
    len -= n;
  }
}

static void log_message(const char *msg) {
  std::array<char, 256> timestamp_buf{};
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);
  strftime(timestamp_buf.data(), timestamp_buf.size(), "%Y-%m-%d %H:%M:%S", tm_info);

  std::array<char, 512> full_msg{};
  const auto pid = getpid();
  const auto tid = (pid_t)syscall(SYS_gettid);
  snprintf(full_msg.data(), full_msg.size(), "[%s] PID=%d TID=%d: %s\n",
           timestamp_buf.data(), (int)pid, (int)tid, msg);
  write_all(full_msg.data(), strlen(full_msg.data()));
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
        write_all(buf.data(), (size_t)n);
      }
      free(dem);
    } else {
      if (symbols != nullptr && symbols[i] != nullptr) {
        write_all(symbols[i], strlen(symbols[i]));
        write_all("\n", 1);
      }
    }
  }

  free(static_cast<void *>(symbols));
}

static void sigusr2_handler(int /*signo*/, siginfo_t * /*info*/, void * /*ucontext*/) {
  std::array<char, 256> hdr{};
  const auto tid = (pid_t)syscall(SYS_gettid);
  int n = snprintf(hdr.data(), hdr.size(), "=== [RECEIVED SIGUSR2] Backtrace for thread %d ===", (int)tid);
  if (n > 0) {
    write_all("\n", 1);
    write_all(hdr.data(), (size_t)n);
    write_all("\n", 1);
  }
  print_backtrace_to_stderr();
}

static void interrupt_handler(int signo) {
  std::array<char, 512> msg{};
  const auto pid = getpid();
  const auto *signal_name = (signo == SIGINT) ? "SIGINT" : ((signo == SIGTERM) ? "SIGTERM" : "SIGNAL");  // NOLINT

  snprintf(msg.data(), msg.size(), "[RECEIVED %s] Sending SIGUSR2 to all threads in process %d",
           signal_name,
           (int)pid);
  log_message(msg.data());

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
      std::array<char, 256> thread_msg{};
      snprintf(thread_msg.data(), thread_msg.size(), "[SIGNAL SENT] SIGUSR2 -> TID %d (result=%d)",
               (int)tid, ret);
      log_message(thread_msg.data());
    }
    closedir(d);

    std::array<char, 256> count_msg{};
    snprintf(count_msg.data(), count_msg.size(), "[SIGNAL BROADCAST] Sent SIGUSR2 to %d threads", thread_count);
    log_message(count_msg.data());
  }

  // give threads a moment to print their traces
  log_message("[HANDLER] Sleeping 1 second to allow threads to print backtraces...");
  sleep(1);

  // restore default and re-raise to allow normal termination
  std::array<char, 256> exit_msg{};

  snprintf(exit_msg.data(), exit_msg.size(), "[HANDLER] Restoring default handler and re-raising %s", signal_name);
  log_message(exit_msg.data());
  signal(signo, SIG_DFL);
  raise(signo);
}

static void install_backtrace_handler() {
  struct sigaction sa{};
  sa.sa_sigaction = sigusr2_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR2, &sa, nullptr);
  log_message("[HANDLER INSTALL] SIGUSR2 handler installed");
}

static void install_interrupt_handlers() {
  struct sigaction sa{};
  sa.sa_handler = interrupt_handler;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  log_message("[HANDLER INSTALL] SIGINT and SIGTERM handlers installed");
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
