#include <server/io/file_open_mode.h>
#include <server/io/sync_file.h>
#include <server/utils/expected.h>

#include <cerrno>
#include <cstring>
#include <string>

#if !WIN_SOCKET
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace server::io {

sync_file::sync_file(file_handle_t file_descriptor) noexcept  // NOLINT(*-swappable*)
    : _fd(file_descriptor) {
}

expected<sync_file, std::string> sync_file::open(const std::string& path, file_open_mode mode) noexcept {
#if WIN_SOCKET
  DWORD access = 0;
  DWORD creation = 0;
  mode_to_win_flags(mode, access, creation);

  // Convert path to wide string for CreateFileW
  const int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
  if (wide_len <= 0) {
    return expected<sync_file, std::string>{unexpect, "Failed to convert path to wide string"};
  }

  std::vector<wchar_t> wide_path(static_cast<size_t>(wide_len) + 1, 0);
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), wide_path.data(), wide_len);

  HANDLE handle = ::CreateFileW(
      wide_path.data(),
      access,
      0,        // no sharing by default
      nullptr,  // default security
      creation,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return expected<sync_file, std::string>{unexpect, "CreateFile failed with error code " + std::to_string(error)};
  }

  return sync_file{handle};
#else
  int posix_mode = mode_to_posix_flags(mode);
  file_handle_t file_descriptor = ::open(path.c_str(), posix_mode, static_cast<int>(0644));  // NOLINT(*vararg*)
  if (file_descriptor == invalid_file_handle) {
    return expected<sync_file, std::string>{unexpect, std::string(strerror(errno))};
  }

  return sync_file{file_descriptor};
#endif
}

expected<size_t, std::string> sync_file::read(std::span<uint8_t> buffer) {
  if (_fd == invalid_file_handle) {
    return expected<size_t, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  DWORD bytes_read = 0;
  BOOL result = ::ReadFile(_fd, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);

  if (result && bytes_read > 0) {
    return static_cast<size_t>(bytes_read);
  }

  if (!result) {
    const DWORD error = GetLastError();
    return expected<size_t, std::string>{unexpect, "ReadFile failed with error code " + std::to_string(error)};
  }

  // bytes_read == 0 means EOF
  return static_cast<size_t>(0);
#else
  ssize_t bytes_read = ::read(_fd, buffer.data(), buffer.size());

  if (bytes_read > 0) {
    return static_cast<size_t>(bytes_read);
  }

  if (bytes_read == 0) {
    // End of file
    return static_cast<size_t>(0);
  }

  // Error occurred
  return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
#endif
}

expected<void, std::string> sync_file::write(std::span<const uint8_t> data) {
  if (_fd == invalid_file_handle) {
    return expected<void, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  DWORD bytes_written = 0;
  BOOL result = ::WriteFile(_fd, data.data(), static_cast<DWORD>(data.size()), &bytes_written, nullptr);

  if (result && static_cast<size_t>(bytes_written) == data.size()) {
    return expected<void, std::string>{};
  }

  if (!result) {
    const DWORD error = GetLastError();
    return expected<void, std::string>{unexpect, "WriteFile failed with error code " + std::to_string(error)};
  }

  // Partial write
  return expected<void, std::string>{unexpect, "Partial write: " + std::to_string(bytes_written) + " of " + std::to_string(data.size()) + " bytes"};
#else
  ssize_t bytes_written = ::write(_fd, data.data(), data.size());

  if (bytes_written > 0 && static_cast<size_t>(bytes_written) == data.size()) {
    return expected<void, std::string>{};
  }

  if (bytes_written == 0) {
    return expected<void, std::string>{unexpect, "write() returned 0 bytes"};
  }

  // Partial write or error
  if (static_cast<size_t>(bytes_written) < data.size()) {
    return expected<void, std::string>{unexpect, "Partial write: " + std::to_string(bytes_written) + " of " + std::to_string(data.size()) + " bytes"};
  }

  return expected<void, std::string>{unexpect, std::string(strerror(errno))};
#endif
}

expected<void, std::string> sync_file::flush() const {
  if (_fd == invalid_file_handle) {
    return expected<void, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  if (::FlushFileBuffers(_fd)) {
    return expected<void, std::string>{};
  }
  const DWORD error = GetLastError();
  return expected<void, std::string>{unexpect, "FlushFileBuffers failed with error code " + std::to_string(error)};
#else
  if (::fsync(_fd) == 0) {
    return expected<void, std::string>{};
  }

  return expected<void, std::string>{unexpect, std::string(strerror(errno))};
#endif
}

void sync_file::close() noexcept {
  if (_fd != invalid_file_handle) {
#if WIN_SOCKET
    ::CloseHandle(_fd);
#else
    ::close(_fd);
#endif
    _fd = invalid_file_handle;
  }
}

expected<size_t, std::string> sync_file::get_size() const {
  if (_fd == invalid_file_handle) {
    return expected<size_t, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  LARGE_INTEGER size{};
  if (::GetFileSizeEx(_fd, &size)) {
    return static_cast<size_t>(size.QuadPart);
  }
  return expected<size_t, std::string>{unexpect, "GetFileSizeEx failed"};
#else
  struct stat stat_buf{};
  if (::fstat(_fd, &stat_buf) != 0) {
    return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
  }

  return static_cast<size_t>(stat_buf.st_size);
#endif
}

expected<off_t, std::string> sync_file::seek(off_t offset, seek_whence whence) const {
  if (_fd == invalid_file_handle) {
    return expected<off_t, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  LARGE_INTEGER distance{};
  LARGE_INTEGER new_position{};
  DWORD win_seek_origin;

  switch (whence) {
    case seek_whence::set:
      win_seek_origin = FILE_BEGIN;
      break;
    case seek_whence::current:
      win_seek_origin = FILE_CURRENT;
      break;
    case seek_whence::end:
      win_seek_origin = FILE_END;
      break;
    default:
      win_seek_origin = FILE_CURRENT;
      break;
  }

  distance.QuadPart = offset;
  if (::SetFilePointerEx(_fd, distance, &new_position, win_seek_origin)) {
    return static_cast<off_t>(new_position.QuadPart);
  }

  const DWORD error = GetLastError();
  return expected<off_t, std::string>{unexpect, "SetFilePointerEx failed with error code " + std::to_string(error)};
#else
  int posix_whence;
  switch (whence) {
    case seek_whence::set:
      posix_whence = SEEK_SET;
      break;
    case seek_whence::current:
      posix_whence = SEEK_CUR;
      break;
    case seek_whence::end:
      posix_whence = SEEK_END;
      break;
    default:
      posix_whence = SEEK_SET;
      break;
  }

  off_t new_offset = ::lseek(_fd, offset, posix_whence);
  if (new_offset == static_cast<off_t>(-1)) {
    return expected<off_t, std::string>{unexpect, std::string(strerror(errno))};
  }

  return new_offset;
#endif
}

expected<std::vector<std::byte>, std::string> sync_file::read_all() {
  if (_fd == invalid_file_handle) {
    return expected<std::vector<std::byte>, std::string>{unexpect, "File is closed"};
  }

  auto size_result = get_size();
  if (!size_result) {
    return expected<std::vector<std::byte>, std::string>{unexpect, std::move(size_result).error()};
  }

  const size_t file_size = size_result.value();
  if (file_size == 0) {
    return std::vector<std::byte>{};
  }

  std::vector<std::byte> buffer;
  buffer.resize(file_size);

  size_t total_bytes_read = 0;
  auto current_buffer = std::span<std::byte>{buffer.data(), buffer.size()};

  while (total_bytes_read < file_size) {
#if WIN_SOCKET
    DWORD bytes_read = 0;
    BOOL result = ::ReadFile(_fd, current_buffer.data(), static_cast<DWORD>(current_buffer.size()), &bytes_read, nullptr);

    if (!result) {
      const DWORD error = GetLastError();
      return expected<std::vector<std::byte>, std::string>{unexpect, "ReadFile failed with error code " + std::to_string(error)};
    }

    if (bytes_read == 0) {
      // EOF reached before reading entire file
      buffer.resize(total_bytes_read);
      break;
    }

    total_bytes_read += bytes_read;
    current_buffer = current_buffer.subspan(bytes_read);
#else
    ssize_t bytes_read = ::read(_fd, current_buffer.data(), current_buffer.size());

    if (bytes_read > 0) {
      total_bytes_read += static_cast<size_t>(bytes_read);
      current_buffer = current_buffer.subspan(bytes_read);
      continue;
    }

    if (bytes_read == 0) {
      // EOF reached before reading entire file
      buffer.resize(total_bytes_read);
      break;
    }

    return expected<std::vector<std::byte>, std::string>{unexpect, std::string(strerror(errno))};
#endif
  }

  return buffer;
}

}  // namespace server::io
