
#if IO_URING_ENABLED

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>
#include <server/io/uring/io_uring_file.h>
#include <server/io/uring/io_uring_reactor.h>

#include <cstddef>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#include "utils/io_helpers.h"
#include "utils/temp_file.h"

// ============================================================================
// Tests concurrent operations using io_uring_file.
// Verifies io_uring backend handles multiple concurrent file operations correctly.
// ============================================================================
TEST(concurrent_io, concurrent_with_io_uring) {
  constexpr int num_files = 5;
  constexpr size_t data_size = 256;

  // Create temp files.
  std::vector<std::string> file_paths;
  file_paths.reserve(num_files);

  for (int i = 0; i < num_files; ++i) {
    file_paths.push_back(test_utils::create_temp_file(test_utils::generate_test_data(data_size, i)));
  }

  async_coro::scheduler scheduler;

  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& io_uring_reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // --- io_uring backend: open and read all files concurrently ---
    std::vector<server::io::io_uring_file> iuring_files;
    iuring_files.reserve(num_files);

    for (int i = 0; i < num_files; ++i) {
      auto result = co_await server::io::io_uring_file::open_coro(
          io_uring_reactor, file_paths[static_cast<size_t>(i)], server::io::file_open_mode::read);
      if (!result) {
        co_return -1;
      }
      iuring_files.push_back(std::move(*result));
    }

    int successes = 0;
    for (int i = 0; i < num_files; ++i) {
      std::vector<std::byte> buffer(data_size);
      auto read_result = co_await iuring_files[static_cast<size_t>(i)].read(buffer);
      auto close_result = co_await iuring_files[static_cast<size_t>(i)].close();

      if (read_result && close_result && read_result.value() == data_size) {
        ++successes;
      } else {
        co_return -1;
      }
    }

    co_return successes == num_files ? 0 : -1;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, io_uring_reactor));

  // Cleanup
  for (const auto& p : file_paths) {
    fs::remove(p);
  }
}

#endif  // IO_URING_ENABLED
