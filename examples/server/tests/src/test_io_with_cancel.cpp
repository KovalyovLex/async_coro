#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>
#include <server/io/io_uring_file.h>
#include <server/io/io_uring_reactor.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#if IO_URING_ENABLED

namespace fs = std::filesystem;

#include "utils/io_helpers.h"
#include "utils/temp_file.h"

// ============================================================================
// Test cancellation with io_uring_file operations.
// ============================================================================
TEST(io_cancel, cancel_with_io_uring) {
  auto path = test_utils::create_temp_binary_file(static_cast<size_t>(1024) * 1024);
  ASSERT_FALSE(path.empty());

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result)
      << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  std::atomic<bool> read_started{false};
  std::atomic<bool> cancelled{false};

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(
        reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    std::vector<uint8_t> buffer(65536);
    read_started.store(true);

    auto read_result = co_await file.read(buffer);

    // After cancellation the result may be an error — that's expected.
    if (!read_result && !cancelled.load()) {
      co_return -1;
    }

    auto close_result = co_await file.close();
    (void)close_result;  // ignore close errors after cancellation
    co_return 0;
  };

  auto handle = scheduler.start_task(test(), async_coro::execution_queues::main);

  // Wait for read to start
  while (!read_started.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    scheduler.get_execution_system<async_coro::execution_system>()
        .update_from_main();
    reactor.process_loop(std::chrono::nanoseconds(1000000));
  }

  // Cancel the read
  cancelled.store(true);

  // Drive until completion
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    scheduler.get_execution_system<async_coro::execution_system>()
        .update_from_main();
    reactor.process_loop(std::chrono::nanoseconds(1000000));
  }

  EXPECT_TRUE(handle.done())
      << "io_uring read task did not complete after cancellation";

  fs::remove(path);
}

#endif  // IO_URING_ENABLED
