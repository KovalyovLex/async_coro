#include <gtest/gtest.h>
#include <server/io/file_open_mode.h>
#include <server/io/sync_file.h>
#include <server/utils/expected.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "utils/temp_file.h"

namespace fs = std::filesystem;

TEST(sync_file_io, open_and_read_file) {
  const std::string content = "Hello, synchronous file I/O!";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result) << "Failed to open file: " << result.error();

  auto file = std::move(*result);
  std::vector<uint8_t> buffer(1024);
  auto read_result = file.read(buffer);

  ASSERT_TRUE(read_result) << "Failed to read: " << read_result.error();
  EXPECT_EQ(read_result.value(), content.size());

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, write_and_read_file) {
  const std::string content = "Write and read test";
  auto path = test_utils::create_temp_file("");

  auto open_result = server::io::sync_file::open(
      path,
      server::io::file_open_mode::write | server::io::file_open_mode::create | server::io::file_open_mode::trunc);
  ASSERT_TRUE(open_result) << "Failed to open file for writing: " << open_result.error();

  auto file = std::move(*open_result);
  std::vector<uint8_t> data(content.begin(), content.end());
  auto write_result = file.write(data);

  ASSERT_TRUE(write_result) << "Failed to write: " << write_result.error();
  auto flush_result1 = file.flush();
  ASSERT_TRUE(flush_result1);
  file.close();

  // Verify the content was written
  auto written_content = test_utils::read_file_content(path);
  EXPECT_EQ(written_content, content);

  fs::remove(path);
}

TEST(sync_file_io, read_nonexistent_file) {
  auto result = server::io::sync_file::open("/nonexistent/path/file.txt", server::io::file_open_mode::read);
  ASSERT_FALSE(result) << "Should have failed to open nonexistent file";
}

TEST(sync_file_io, close_file) {
  const std::string content = "Close test";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  EXPECT_FALSE(file.is_closed());

  file.close();
  EXPECT_TRUE(file.is_closed());

  // Reading after close should fail
  std::vector<uint8_t> buffer(1024);
  auto read_result = file.read(buffer);
  ASSERT_FALSE(read_result);

  fs::remove(path);
}

TEST(sync_file_io, read_empty_file) {
  auto path = test_utils::create_temp_file("");

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  std::vector<uint8_t> buffer(1024);
  auto read_result = file.read(buffer);

  ASSERT_TRUE(read_result);
  EXPECT_EQ(read_result.value(), static_cast<size_t>(0));

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, get_size_returns_correct_value) {
  const std::string content = "Size test content";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  auto size_result = file.get_size();

  ASSERT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), content.size());

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, read_all_reads_entire_file) {
  const std::string content = "Read all test content - this is a longer string to verify complete reading.";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  auto read_all_result = file.read_all();

  ASSERT_TRUE(read_all_result);
  EXPECT_EQ(read_all_result.value().size(), content.size());

  // Verify content matches
  std::string actual_content(
      reinterpret_cast<const char*>(read_all_result.value().data()),
      read_all_result.value().size());
  EXPECT_EQ(actual_content, content);

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, seek_moves_position) {
  const std::string content = "0123456789ABCDEFGHIJ";  // 20 bytes
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Seek to position 5
  auto seek_result = file.seek(5, server::io::sync_file::seek_whence::set);
  ASSERT_TRUE(seek_result);
  EXPECT_EQ(seek_result.value(), static_cast<off_t>(5));

  // Read remaining content
  std::vector<uint8_t> buffer(20);
  auto read_result = file.read(buffer);
  ASSERT_TRUE(read_result);
  EXPECT_EQ(read_result.value(), static_cast<size_t>(15));  // 20 - 5 = 15

  std::string actual_content(buffer.begin(), buffer.begin() + read_result.value());
  EXPECT_EQ(actual_content, "56789ABCDEFGHIJ");

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, seek_from_current) {
  const std::string content = "0123456789ABCDEFGHIJ";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Read first 5 bytes
  std::vector<uint8_t> buffer(5);
  auto read_result = file.read(buffer);
  ASSERT_TRUE(read_result);
  EXPECT_EQ(read_result.value(), static_cast<size_t>(5));

  // Seek forward 3 bytes from current position (now at 8)
  auto seek_result = file.seek(3, server::io::sync_file::seek_whence::current);
  ASSERT_TRUE(seek_result);
  EXPECT_EQ(seek_result.value(), static_cast<off_t>(8));

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, seek_from_end) {
  const std::string content = "0123456789ABCDEFGHIJ";  // 20 bytes
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Seek to last 5 bytes from end
  auto seek_result = file.seek(-5, server::io::sync_file::seek_whence::end);
  ASSERT_TRUE(seek_result);
  EXPECT_EQ(seek_result.value(), static_cast<off_t>(15));

  // Read those 5 bytes
  std::vector<uint8_t> buffer(10);
  auto read_result = file.read(buffer);
  ASSERT_TRUE(read_result);
  EXPECT_EQ(read_result.value(), static_cast<size_t>(5));

  std::string actual_content(buffer.begin(), buffer.begin() + read_result.value());
  EXPECT_EQ(actual_content, "FGHIJ");

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, seek_error_invalid_offset) {
  const std::string content = "Test";
  auto path = test_utils::create_temp_file(content);

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Seek beyond end should still work (creates sparse region)
  auto seek_result = file.seek(100, server::io::sync_file::seek_whence::set);
  // This may succeed or fail depending on platform - just verify it returns expected
  if (seek_result) {
    EXPECT_EQ(seek_result.value(), static_cast<off_t>(100));
  }

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, write_multiple_chunks) {
  auto path = test_utils::create_temp_file("");

  auto result = server::io::sync_file::open(
      path,
      server::io::file_open_mode::write | server::io::file_open_mode::create | server::io::file_open_mode::trunc);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Write in multiple chunks
  std::string chunk1 = "Hello, ";
  std::string chunk2 = "world! ";
  std::string chunk3 = "This is a test.";

  auto write1 = file.write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(chunk1.data()), chunk1.size()});
  ASSERT_TRUE(write1);

  auto write2 = file.write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(chunk2.data()), chunk2.size()});
  ASSERT_TRUE(write2);

  auto write3 = file.write(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(chunk3.data()), chunk3.size()});
  ASSERT_TRUE(write3);

  auto flush_result = file.flush();
  ASSERT_TRUE(flush_result);
  file.close();

  // Verify total content
  auto written_content = test_utils::read_file_content(path);
  EXPECT_EQ(written_content, "Hello, world! This is a test.");

  fs::remove(path);
}

TEST(sync_file_io, get_size_after_write) {
  const std::string content = "Size after write test";
  auto path = test_utils::create_temp_file("");

  auto result = server::io::sync_file::open(
      path,
      server::io::file_open_mode::write | server::io::file_open_mode::create | server::io::file_open_mode::trunc);
  ASSERT_TRUE(result);

  auto file = std::move(*result);

  // Size should be 0 initially
  auto size_before = file.get_size();
  ASSERT_TRUE(size_before);
  EXPECT_EQ(size_before.value(), static_cast<size_t>(0));

  // Write data
  std::vector<uint8_t> data(content.begin(), content.end());
  auto write_result = file.write(data);
  ASSERT_TRUE(write_result);

  // Flush to ensure size is updated
  auto flush_result2 = file.flush();
  ASSERT_TRUE(flush_result2);

  // Size should now match written content
  auto size_after = file.get_size();
  ASSERT_TRUE(size_after);
  EXPECT_EQ(size_after.value(), content.size());

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, read_all_empty_file) {
  auto path = test_utils::create_temp_file("");

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  auto read_all_result = file.read_all();

  ASSERT_TRUE(read_all_result);
  EXPECT_TRUE(read_all_result.value().empty());

  file.close();
  fs::remove(path);
}

TEST(sync_file_io, read_closed_file) {
  auto path = test_utils::create_temp_file("test");

  auto result = server::io::sync_file::open(path, server::io::file_open_mode::read);
  ASSERT_TRUE(result);

  auto file = std::move(*result);
  file.close();

  // Reading closed file should fail
  std::vector<uint8_t> buffer(1024);
  auto read_result = file.read(buffer);
  ASSERT_FALSE(read_result);
  EXPECT_EQ(read_result.error(), "File is closed");

  fs::remove(path);
}
