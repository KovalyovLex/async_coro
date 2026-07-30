#pragma once

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace test_utils {

/**
 * @brief Generate a unique temporary file path in the system temp directory.
 *
 * Uses the process ID and a random number to avoid collisions between
 * concurrent test runs.
 *
 * @return  Absolute path string for a unique temp file.
 */
inline std::string generate_temp_path() {
  static thread_local std::mt19937 gen(std::random_device{}());
  static thread_local std::uniform_int_distribution<int> dist(0, 999999);

#if defined(_WIN32) || defined(_WIN64)
  const auto pid = _getpid();
#else
  const auto pid = getpid();
#endif

  return std::filesystem::temp_directory_path().string() +
         "/async_coro_test_" + std::to_string(pid) +
         "_" + std::to_string(dist(gen));
}

/**
 * @brief Create a temporary file with given content.
 *
 * @param content  Data to write into the file.
 * @return         Absolute path to the created temp file.
 */
inline std::string create_temp_file(std::string_view content) {
  auto path = generate_temp_path();

  std::ofstream ofs(path, std::ios::binary);
  if (!content.empty()) {
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  ofs.close();
  return path;
}

/**
 * @brief Create a temporary file filled with random binary data.
 *
 * @param size_bytes  Number of bytes to write (default 64 KiB).
 * @return            Absolute path to the created temp file.
 */
inline std::string create_temp_binary_file(size_t size_bytes = 65536) {
  auto path = generate_temp_path();

  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    return {};
  }

  std::vector<uint8_t> chunk(4096);
  std::mt19937 local_gen(std::random_device{}());
  for (size_t written = 0; written < size_bytes;) {
    std::ranges::generate(chunk, [&local_gen]() {
      return static_cast<uint8_t>(local_gen() % 256);
    });
    size_t to_write = std::min(chunk.size(), size_bytes - written);
    ofs.write(reinterpret_cast<const char*>(chunk.data()),
              static_cast<std::streamsize>(to_write));
    written += to_write;
  }
  ofs.close();
  return path;
}

/**
 * @brief Read entire file content into a string.
 *
 * @param path  Path to the file to read.
 * @return      File contents as std::string.
 */
inline std::string read_file_content(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

/**
 * @brief Generate deterministic test data for a given index.
 *
 * Returns a string of exactly `size` bytes where each byte is derived
 * from (index + byte_position) % 256, making it easy to verify integrity.
 */
inline std::string generate_test_data(size_t size, int index) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((index + static_cast<int>(i)) % 256);
  }
  return data;
}

}  // namespace test_utils
