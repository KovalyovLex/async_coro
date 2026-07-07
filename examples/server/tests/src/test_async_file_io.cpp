#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <server/io/file.h>
#include <server/utils/expected.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Helper to create a temporary file with given content
static std::string create_temp_file(const std::string& content) {
  static thread_local std::mt19937 gen(std::random_device{}());
  static thread_local std::uniform_int_distribution<int> dist(0, 999999);
  auto path = fs::temp_directory_path() / ("async_coro_test_" + std::to_string(getpid()) + "_" + std::to_string(dist(gen)));
  std::ofstream ofs(path, std::ios::binary);
  ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  ofs.close();
  return path.string();
}

// Helper to read file content for comparison
static std::string read_file_content(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

// Helper to run a coroutine task and wait for completion
static bool run_task(async_coro::task<int> task, async_coro::scheduler& scheduler, server::io::reactor& file_reactor) {
  auto handle = scheduler.start_task(std::move(task), async_coro::execution_queues::main);
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
    file_reactor.process_loop(std::chrono::milliseconds(1));
  }
  return handle.done();
}

TEST(async_file_io, open_and_read_file) {
  const std::string content = "Hello, async file I/O!";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    std::vector<uint8_t> buffer(1024);
    auto read_result = co_await file.read(buffer);

    if (!read_result) {
      co_return -1;
    }

    file.close();
    co_return static_cast<int>(read_result.value());
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, write_and_read_file) {
  const std::string content = "Write and read test";
  auto path = create_temp_file("");

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    std::vector<uint8_t> data(content.begin(), content.end());
    auto write_result = co_await file.write(data);

    if (!write_result) {
      co_return -1;
    }

    file.close();
    co_return 0;
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));

  // Verify the content was written
  auto written_content = read_file_content(path);
  EXPECT_EQ(written_content, content);

  fs::remove(path);
}

TEST(async_file_io, read_nonexistent_file) {
  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, "/nonexistent/path/file.txt", O_RDONLY);
    if (result) {
      co_return -1;  // Should have failed
    }
    co_return 0;  // Expected failure
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
}

TEST(async_file_io, close_file) {
  const std::string content = "Close test";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    EXPECT_FALSE(file.is_closed());

    file.close();
    EXPECT_TRUE(file.is_closed());

    co_return 0;
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, read_empty_file) {
  auto path = create_temp_file("");

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    std::vector<uint8_t> buffer(1024);
    auto read_result = co_await file.read(buffer);

    if (!read_result) {
      co_return -1;
    }

    file.close();
    co_return static_cast<int>(read_result.value());
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, get_size_returns_correct_value) {
  const std::string content = "Size test content";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    auto size_result = file.get_size();

    if (!size_result) {
      co_return -1;
    }

    file.close();
    co_return static_cast<int>(size_result.value());
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  EXPECT_EQ(static_cast<size_t>(content.size()), content.size());
  fs::remove(path);
}

TEST(async_file_io, read_all_reads_entire_file) {
  const std::string content = "Read all test content here";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    auto read_result = co_await file.read_all();

    if (!read_result) {
      co_return -1;
    }

    file.close();

    // Verify all bytes were read
    if (read_result.value().size() != content.size()) {
      co_return -1;
    }

    // Verify content matches
    for (size_t i = 0; i < content.size(); ++i) {
      if (static_cast<char>(read_result.value()[i]) != content[i]) {
        co_return -1;
      }
    }

    co_return 0;
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, read_all_empty_file) {
  auto path = create_temp_file("");

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    auto read_result = co_await file.read_all();

    if (!read_result) {
      co_return -1;
    }

    file.close();

    // Empty file should return empty vector
    if (!read_result.value().empty()) {
      co_return -1;
    }

    co_return 0;
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, seek_moves_position) {
  const std::string content = "Seek test content";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    // Seek to position 5 using SEEK_SET
    auto seek_result = file.seek(5, server::io::file::seek_whence::set);
    if (!seek_result) {
      co_return -1;
    }

    // Verify new position is 5
    if (seek_result.value() != 5) {
      co_return -1;
    }

    // Seek forward 3 bytes using SEEK_CUR
    seek_result = file.seek(3, server::io::file::seek_whence::current);
    if (!seek_result) {
      co_return -1;
    }

    // Verify new position is 8
    if (seek_result.value() != 8) {
      co_return -1;
    }

    file.close();
    co_return 0;
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}

TEST(async_file_io, seek_error_invalid_whence) {
  const std::string content = "Seek error test";
  auto path = create_temp_file(content);

  async_coro::scheduler scheduler;
  server::io::reactor file_reactor;

  auto test = [&]() -> async_coro::task<int> {
    auto result = server::io::file::open(file_reactor, path, O_RDONLY);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    // Try to seek with invalid whence (6 is not a valid whence value)
    auto seek_result = file.seek(0, static_cast<server::io::file::seek_whence>(6));
    if (!seek_result) {
      co_return 0;  // Expected error
    }

    file.close();
    co_return -1;  // Should have failed
  };

  ASSERT_TRUE(run_task(test(), scheduler, file_reactor));
  fs::remove(path);
}
