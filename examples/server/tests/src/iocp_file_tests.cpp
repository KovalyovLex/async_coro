#include <server/io/iocp_file.h>

#if WIN_IOCP_ENABLED

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>
#include <server/io/iocp_reactor.h>

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "utils/io_helpers.h"
#include "utils/temp_file.h"

namespace fs = std::filesystem;

TEST(iocp_file_tests, open_and_close) {
  const std::string content = "Hello, IOCP!";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);
    EXPECT_FALSE(file.is_closed());
    EXPECT_NE(file.get_fd(), server::io::invalid_file_handle);

    auto close_result = file.close();
    if (!close_result) {
      co_return -1;
    }

    EXPECT_TRUE(file.is_closed());
    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, read_file_content) {
  const std::string content = "Test content for IOCP read";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
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

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, write_file_content) {
  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(
        reactor, path, server::io::file_open_mode::write | server::io::file_open_mode::create | server::io::file_open_mode::trunc);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    const std::string_view write_content = "Written via IOCP";

    auto write_result = co_await file.write(std::as_bytes(std::span{write_content}));
    if (!write_result) {
      EXPECT_TRUE(write_result) << write_result.error();
      co_return -1;
    }

    auto flush_result = file.flush();
    if (!flush_result) {
      EXPECT_TRUE(flush_result) << flush_result.error();
      co_return -1;
    }

    auto close_result = file.close();
    if (!close_result) {
      EXPECT_TRUE(close_result) << close_result.error();
      co_return -1;
    }

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));

  // Verify the file was written correctly
  std::ifstream in(path, std::ios::binary);
  std::string read_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(read_content, "Written via IOCP");

  fs::remove(path);
}

TEST(iocp_file_tests, flush_file) {
  const std::string content = "Data to flush";
  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(
        reactor, path, server::io::file_open_mode::write | server::io::file_open_mode::create);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    const std::string_view write_content = "Data to flush";

    auto write_result = co_await file.write(std::as_bytes(std::span{write_content}));
    if (!write_result) {
      EXPECT_TRUE(write_result) << write_result.error();
      co_return -1;
    }

    // Flush to ensure data is persisted to disk
    auto flush_result = file.flush();
    if (!flush_result) {
      EXPECT_TRUE(flush_result) << flush_result.error();
      co_return -1;
    }

    // Close the file
    auto close_result = file.close();
    if (!close_result) {
      EXPECT_TRUE(close_result) << close_result.error();
      co_return -1;
    }

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));

  // Re-open and verify the file content was persisted
  auto re_path = test_utils::create_temp_file("");
  fs::remove(re_path);

  async_coro::scheduler scheduler2;
  auto reactor_result2 = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result2) << "Failed to create second IOCP reactor";
  auto& reactor2 = *reactor_result2;

  auto verify_test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor2, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto data_result = co_await file.read_all();
    if (!data_result) {
      co_return -1;
    }

    const auto& data = data_result.value();
    std::string read_content(data.size(), '\0');
    for (size_t i = 0; i < data.size(); ++i) {
      read_content[i] = static_cast<char>(static_cast<unsigned char>(data[i]));
    }
    EXPECT_EQ(read_content, content);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(verify_test(), scheduler2, reactor2));
  fs::remove(path);
}

TEST(iocp_file_tests, read_partial_buffer) {
  const std::string_view content = "12345";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    // Read into a buffer larger than the file content
    std::array<char, 64> buffer{};
    auto read_result = co_await file.read(std::as_writable_bytes(std::span{buffer}));
    if (!read_result) {
      EXPECT_TRUE(read_result) << read_result.error();
      co_return -1;
    }

    // Only file-sized data should be returned
    EXPECT_EQ(read_result.value(), content.size());

    // Verify only the first content.size() bytes are correct
    std::string read_str(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read_result.value()));
    EXPECT_EQ(read_str, content);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, open_nonexistent_file_fails) {
  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(
        reactor, "/nonexistent/path/to/file/that/does/not/exist.txt", server::io::file_open_mode::read);

    // Should fail to open a nonexistent file for reading
    if (result) {
      co_return -1;
    }

    EXPECT_FALSE(result.error().empty());
    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_file_tests, move_constructor) {
  const std::string content = "Move constructor test";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file1 = std::move(*result);
    EXPECT_FALSE(file1.is_closed());
    EXPECT_NE(file1.get_fd(), server::io::invalid_file_handle);

    // Move-construct another iocp_file
    auto file2 = std::move(file1);

    // Original should be closed after move
    EXPECT_TRUE(file1.is_closed());
    EXPECT_EQ(file1.get_fd(), server::io::invalid_file_handle);

    // New one should work
    EXPECT_FALSE(file2.is_closed());
    EXPECT_NE(file2.get_fd(), server::io::invalid_file_handle);

    // Verify we can read from the moved-to file
    auto data_result = co_await file2.read_all();
    if (!data_result) {
      co_return -1;
    }

    const auto& data = data_result.value();
    EXPECT_EQ(data.size(), content.size());

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, multiple_writes) {
  using namespace std::literals::string_view_literals;

  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(
        reactor, path, server::io::file_open_mode::write | server::io::file_open_mode::create);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    // Write data in multiple chunks
    constexpr std::array chunks = {"Hello, "sv, "world! "sv, "This is "sv, "a multi-chunk "sv, "write test."sv};

    for (size_t i = 0; i < chunks.size(); ++i) {
      auto write_result = co_await file.write(std::as_bytes(std::span{chunks[i]}));
      if (!write_result) {
        EXPECT_TRUE(write_result) << write_result.error();
        co_return -1;
      }
    }

    // Flush to ensure all data is persisted
    auto flush_result = file.flush();
    if (!flush_result) {
      co_return -1;
    }

    auto close_result = file.close();
    if (!close_result) {
      co_return -1;
    }

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));

  // Read back and verify all content
  std::ifstream in(path, std::ios::binary);
  std::string read_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(read_content, "Hello, world! This is a multi-chunk write test.");

  fs::remove(path);
}

TEST(iocp_file_tests, get_file_size) {
  const std::string content = "Size test content for IOCP";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
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

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, seek_and_read) {
  const std::string content = "0123456789ABCDEF";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
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
    std::array<char, 4> buffer;
    auto read_result = co_await file.read(std::as_writable_bytes(std::span{buffer}));
    if (!read_result) {
      co_return -1;
    }
    EXPECT_EQ(read_result.value(), 4);

    std::string read_str(buffer.begin(), buffer.end());
    EXPECT_EQ(read_str, "5678");

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, read_empty_file) {
  auto path = test_utils::create_temp_file("");

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto data_result = co_await file.read_all();
    if (!data_result) {
      co_return -1;
    }
    EXPECT_EQ(data_result.value().size(), 0U);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

TEST(iocp_file_tests, read_closed_file) {
  const std::string content = "Test";
  auto path = test_utils::create_temp_file(content);

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    auto result = co_await server::io::iocp_file::open_coro(reactor, path, server::io::file_open_mode::read);
    if (!result) {
      co_return -1;
    }

    auto file = std::move(*result);

    auto close_result = file.close();
    if (!close_result) {
      co_return -1;
    }

    EXPECT_TRUE(file.is_closed());

    std::array<std::byte, 10> buffer;
    auto read_result = co_await file.read(buffer);
    if (read_result) {
      co_return -1;
    }
    EXPECT_EQ(read_result.error(), "File is closed");

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
  fs::remove(path);
}

#endif  // WIN_IOCP_ENABLED
