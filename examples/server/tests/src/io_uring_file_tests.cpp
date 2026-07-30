#include <server/io/io_uring_file.h>

#if IO_URING_ENABLED

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>
#include <server/io/io_uring_reactor.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "utils/io_helpers.h"
#include "utils/temp_file.h"

namespace fs = std::filesystem;

TEST(io_uring_file_tests, open_and_close) {
  const std::string content = "Hello, io_uring!";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    EXPECT_FALSE(file.is_closed());
    EXPECT_GT(file.get_fd(), 0);

    auto close_result = co_await file.close();
    if (!close_result) {
      co_return -1;
    }

    EXPECT_TRUE(file.is_closed());
    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(io_uring_file_tests, read_file_content) {
  const std::string content = "Test content for io_uring read";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto data_result = co_await file.read_all();
    if (!data_result) {
      co_return -1;
    }

    const auto& data = data_result.value();
    EXPECT_EQ(data.size(), content.size());

    // Convert std::byte to char for comparison
    std::string read_content(data.size(), '\0');
    for (size_t i = 0; i < data.size(); ++i) {
      read_content[i] = static_cast<char>(static_cast<unsigned char>(data[i]));
    }
    EXPECT_EQ(read_content, content);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(io_uring_file_tests, write_to_file) {
  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::write | server::io::file_open_mode::create | server::io::file_open_mode::trunc);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    const std::string write_content = "Written via io_uring";
    std::vector<uint8_t> data(write_content.begin(), write_content.end());

    auto write_result = co_await file.write(data);
    if (!write_result) {
      co_return -1;
    }

    auto flush_result = co_await file.flush();
    if (!flush_result) {
      co_return -1;
    }

    auto close_result = co_await file.close();
    if (!close_result) {
      co_return -1;
    }

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));

  // Verify the file was written correctly
  std::ifstream in(path, std::ios::binary);
  std::string read_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(read_content, "Written via io_uring");

  fs::remove(path);
}

TEST(io_uring_file_tests, get_file_size) {
  const std::string content = "Size test content";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto size_result = file.get_size();
    if (!size_result) {
      co_return -1;
    }

    EXPECT_EQ(size_result.value(), content.size());

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(io_uring_file_tests, seek_and_read) {
  const std::string content = "0123456789ABCDEF";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    // Seek to position 5
    auto seek_result = file.seek(5);
    if (!seek_result) {
      co_return -1;
    }
    EXPECT_EQ(seek_result.value(), 5);

    // Read 4 bytes from position 5
    std::vector<uint8_t> buffer(4);
    auto read_result = co_await file.read(buffer);
    if (!read_result) {
      co_return -1;
    }
    EXPECT_EQ(read_result.value(), 4);

    std::string read_str(buffer.begin(), buffer.end());
    EXPECT_EQ(read_str, "5678");

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(io_uring_file_tests, read_empty_file) {
  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto data_result = co_await file.read_all();
    if (!data_result) {
      co_return -1;
    }
    EXPECT_EQ(data_result.value().size(), 0u);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(io_uring_file_tests, read_closed_file) {
  const std::string content = "Test";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::io_uring_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create io_uring reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::io_uring_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto close_result = co_await file.close();
    if (!close_result) {
      co_return -1;
    }

    EXPECT_TRUE(file.is_closed());

    std::vector<uint8_t> buffer(10);
    auto read_result = co_await file.read(buffer);
    if (read_result) {
      co_return -1;
    }
    EXPECT_EQ(read_result.error(), "File is closed");

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_io_uring(test(), scheduler, reactor));
  fs::remove(path);
}

#endif
