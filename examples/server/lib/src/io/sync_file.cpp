#include <server/core/error.h>
#include <server/io/file_open_mode.h>
#include <server/io/io_config.h>
#include <server/io/sync_file.h>
#include <server/io/utils.h>
#include <server/utils/expected.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#if !WIN_SOCKET
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace server::io {

sync_file::sync_file(file_handle_t file_descriptor) noexcept  // NOLINT(*-swappable*)
    : _fd(file_descriptor) {
}

sync_file::sync_file(sync_file&& other) noexcept
    : _fd(std::exchange(other._fd, invalid_file_handle)) {
}

sync_file& sync_file::operator=(sync_file&& other) noexcept {
  _fd = std::exchange(other._fd, invalid_file_handle);
  return *this;
}

sync_file::~sync_file() noexcept {
  close();
}

expected<sync_file, core::error> sync_file::open(const std::string& path, file_open_mode mode) noexcept {
#if WIN_SOCKET
  DWORD access = 0;
  DWORD creation = 0;
  mode_to_win_flags(mode, access, creation);

  // Convert path to wide string for CreateFileW
  const int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
  if (wide_len <= 0) {
    return expected<sync_file, core::error>{unexpect, core::error_type::path_conversion_failed};
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
    return expected<sync_file, core::error>{unexpect, core::error_type::open_failed, static_cast<int>(error)};
  }

  return sync_file{handle};
#else
  int posix_mode = mode_to_posix_flags(mode);
  constexpr int default_open_mode = 0644;                                                                 // NOLINT(cppcoreguidelines-avoid-magic-numbers)
  file_handle_t file_descriptor = ::open(path.c_str(), posix_mode, static_cast<int>(default_open_mode));  // NOLINT(*vararg*)
  if (file_descriptor == invalid_file_handle) {
    return expected<sync_file, core::error>{unexpect, core::error_type::open_failed, errno};
  }

  return sync_file{file_descriptor};
#endif
}

expected<size_t, core::error> sync_file::read(std::span<std::byte> buffer) const {
  if (_fd == invalid_file_handle) {
    return expected<size_t, core::error>{unexpect, core::error_type::file_closed};
  }

#if WIN_SOCKET
  DWORD bytes_read = 0;
  BOOL result = ::ReadFile(_fd, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);

  if (result && bytes_read > 0) {
    return static_cast<size_t>(bytes_read);
  }

  if (!result) {
    const DWORD error = GetLastError();
    return expected<size_t, core::error>{unexpect, core::error_type::read_failed, static_cast<int>(error)};
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
  return expected<size_t, core::error>{unexpect, core::error_type::read_failed, errno};
#endif
}

expected<void, core::error> sync_file::write(std::span<const std::byte> data) const {
  if (_fd == invalid_file_handle) {
    return expected<void, core::error>{unexpect, core::error_type::file_closed};
  }

#if WIN_SOCKET
  DWORD bytes_written = 0;
  BOOL result = ::WriteFile(_fd, data.data(), static_cast<DWORD>(data.size()), &bytes_written, nullptr);

  if (result && static_cast<size_t>(bytes_written) == data.size()) {
    return expected<void, core::error>{};
  }

  if (!result) {
    const DWORD error = GetLastError();
    return expected<void, core::error>{unexpect, core::error_type::write_failed, static_cast<int>(error)};
  }

  // Partial write
  return expected<void, core::error>{unexpect, core::error_type::partial_write, static_cast<int>(bytes_written)};
#else
  ssize_t bytes_written = ::write(_fd, data.data(), data.size());

  if (bytes_written > 0 && static_cast<size_t>(bytes_written) == data.size()) {
    return expected<void, core::error>{};
  }

  if (bytes_written == 0) {
    return expected<void, core::error>{unexpect, core::error_type::write_zero_bytes};
  }

  // Partial write or error
  if (static_cast<size_t>(bytes_written) < data.size()) {
    return expected<void, core::error>{unexpect, core::error_type::partial_write, static_cast<int>(bytes_written)};
  }

  return expected<void, core::error>{unexpect, core::error_type::write_failed, errno};
#endif
}

expected<void, core::error> sync_file::flush() const {
  if (_fd == invalid_file_handle) {
    return expected<void, core::error>{unexpect, core::error_type::file_closed};
  }

#if WIN_SOCKET
  if (::FlushFileBuffers(_fd)) {
    return expected<void, core::error>{};
  }
  const DWORD error = GetLastError();
  return expected<void, core::error>{unexpect, core::error_type::flush_failed, static_cast<int>(error)};
#else
  if (::fsync(_fd) == 0) {
    return expected<void, core::error>{};
  }

  return expected<void, core::error>{unexpect, core::error_type::flush_failed, errno};
#endif
}

void sync_file::close() noexcept {
  if (_fd != invalid_file_handle) {
    close_file(_fd);
    _fd = invalid_file_handle;
  }
}

expected<size_t, core::error> sync_file::get_size() const {
  if (_fd == invalid_file_handle) {
    return expected<size_t, core::error>{unexpect, core::error_type::file_closed};
  }

#if WIN_SOCKET
  LARGE_INTEGER size{};
  if (::GetFileSizeEx(_fd, &size)) {
    return static_cast<size_t>(size.QuadPart);
  }
  return expected<size_t, core::error>{unexpect, core::error_type::get_size_failed};
#else
  struct stat stat_buf{};
  if (::fstat(_fd, &stat_buf) != 0) {
    return expected<size_t, core::error>{unexpect, core::error_type::get_size_failed, errno};
  }

  return static_cast<size_t>(stat_buf.st_size);
#endif
}

expected<off_t, core::error> sync_file::seek(off_t offset, seek_whence whence) const {
  if (_fd == invalid_file_handle) {
    return expected<off_t, core::error>{unexpect, core::error_type::file_closed};
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
  return expected<off_t, core::error>{unexpect, core::error_type::set_file_pointer_failed, static_cast<int>(error)};
#else
  int posix_whence = SEEK_SET;
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
    return expected<off_t, core::error>{unexpect, core::error_type::set_file_pointer_failed, errno};
  }

  return new_offset;
#endif
}

expected<std::vector<std::byte>, core::error> sync_file::read_all() {  // NOLINT(readability-make-member-function-const): modifies file state by reading data
  if (_fd == invalid_file_handle) {
    return expected<std::vector<std::byte>, core::error>{unexpect, core::error_type::file_closed};
  }

  auto size_result = get_size();
  if (!size_result) {
    return expected<std::vector<std::byte>, core::error>{unexpect, std::move(size_result).error()};
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
      return expected<std::vector<std::byte>, core::error>{unexpect, core::error_type::read_failed, static_cast<int>(error)};
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

    return expected<std::vector<std::byte>, core::error>{unexpect, core::error_type::read_failed, errno};
#endif
  }

  return buffer;
}

}  // namespace server::io
