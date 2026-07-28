
#include <async_coro/await/await_callback.h>
#include <server/io/file.h>
#include <server/io/io_config.h>
#include <server/utils/expected.h>

#if !WIN_SOCKET
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace server::io {

bool file::would_block() noexcept {
#if WIN_SOCKET
  // On Windows, file I/O errors are reported via GetLastError, not errno.
  // Non-blocking mode is not supported for files on Windows, so this should
  // not be called for file handles.
  return false;
#else
  const int err_code = errno;
  return err_code == EAGAIN || err_code == EWOULDBLOCK;
#endif
}

file::file(reactor& reactor, file_handle_t file_descriptor, size_t index) noexcept  // NOLINT(*-swappable*)
    : _reactor(reactor),
      _fd(file_descriptor),
      _index(index) {
  // The fd is already added to the reactor in file::open
}

expected<file, std::string> file::open(reactor& reactor, const std::string& path, file_open_mode mode) noexcept {
#if WIN_SOCKET
  DWORD access = 0;
  DWORD creation = 0;
  mode_to_win_flags(mode, access, creation);

  // Convert path to wide string for CreateFileW
  const int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
  if (wide_len <= 0) {
    return expected<file, std::string>{unexpect, "Failed to convert path to wide string"};
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
    return expected<file, std::string>{unexpect, "CreateFile failed with error code " + std::to_string(error)};
  }

  size_t index = reactor.add_fd(handle);
  if (index == static_cast<size_t>(-1)) {
    close_file(handle);
    return expected<file, std::string>{unexpect, "Failed to add fd to reactor"};
  }

  return file{reactor, handle, index};
#else
  int posix_mode = mode_to_posix_flags(mode) | O_NONBLOCK;
  file_handle_t file_descriptor = ::open(path.c_str(), posix_mode, static_cast<int>(0644));  // NOLINT(*vararg*)
  if (file_descriptor == invalid_file_handle) {
    return expected<file, std::string>{unexpect, std::string(strerror(errno))};
  }

  size_t index = reactor.add_fd(file_descriptor);
  if (index == static_cast<size_t>(-1)) {
    close_file(file_descriptor);
    return expected<file, std::string>{unexpect, "Failed to add fd to reactor"};
  }

  return file{reactor, file_descriptor, index};
#endif
}

async_coro::task<expected<size_t, std::string>> file::read(std::span<uint8_t> buffer) {
  if (_fd == invalid_file_handle) {
    co_return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_read = 0;
  auto current_buffer = buffer;

  while (total_bytes_read < buffer.size()) {
#if WIN_SOCKET
    DWORD bytes_read = 0;
    BOOL result = ::ReadFile(_fd, current_buffer.data(), static_cast<DWORD>(current_buffer.size()), &bytes_read, nullptr);

    if (result && bytes_read > 0) {
      total_bytes_read += bytes_read;
      current_buffer = current_buffer.subspan(bytes_read);
      continue;
    }

    if (!result && bytes_read == 0) {
      // End of file
      co_return total_bytes_read;
    }

    if (!result) {
      const DWORD error = GetLastError();
      // ERROR_HANDLE_EOF means end of file
      if (error == ERROR_HANDLE_EOF) {
        co_return total_bytes_read;
      }
      co_return expected<size_t, std::string>{unexpect, "ReadFile failed with error code " + std::to_string(error)};
    }

    // bytes_read > 0 but result is false - shouldn't happen, treat as error
    co_return expected<size_t, std::string>{unexpect, "ReadFile returned success with no bytes"};
#else
    ssize_t bytes_read = ::read(_fd, current_buffer.data(), current_buffer.size());

    if (bytes_read > 0) {
      total_bytes_read += static_cast<size_t>(bytes_read);
      current_buffer = current_buffer.subspan(bytes_read);
      continue;
    }

    if (bytes_read == 0) {
      // End of file
      co_return total_bytes_read;
    }

    // Error or EAGAIN/EWOULDBLOCK
    if (!file::would_block()) {
      co_return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
    }

    // Yield to let the reactor process events, then retry
    auto result = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor.continue_after_receive_data(_fd, _index, std::move(cont));
    });

    if (result == reactor::connection_state::closed) {
      co_return expected<size_t, std::string>{unexpect, "File was closed"};
    }
#endif
  }

  co_return total_bytes_read;
}

async_coro::task<expected<void, std::string>> file::write(std::span<const uint8_t> data) {
  if (_fd == invalid_file_handle) {
    co_return expected<void, std::string>{unexpect, "File is closed"};
  }

#if WIN_SOCKET
  // On Windows, files are blocking by default. Use WriteFile directly.
  DWORD bytes_written = 0;
  BOOL result = ::WriteFile(_fd, data.data(), static_cast<DWORD>(data.size()), &bytes_written, nullptr);

  if (result && static_cast<size_t>(bytes_written) == data.size()) {
    co_return expected<void, std::string>{};
  }

  if (!result) {
    const DWORD error = GetLastError();
    co_return expected<void, std::string>{unexpect, "WriteFile failed with error code " + std::to_string(error)};
  }

  // Partial write - shouldn't normally happen for files
  if (static_cast<size_t>(bytes_written) < data.size()) {
    co_return expected<void, std::string>{unexpect, "Partial write: " + std::to_string(bytes_written) + " of " + std::to_string(data.size()) + " bytes"};
  }

  co_return expected<void, std::string>{};
#else
  while (!data.empty()) {
    ssize_t bytes_written = ::write(_fd, data.data(), data.size());

    if (bytes_written > 0) {
      data = data.subspan(bytes_written);
      continue;
    }

    // Guard against write() returning 0 (undefined behavior on non-blocking fd)
    if (bytes_written == 0) {
      co_return expected<void, std::string>{unexpect, "write() returned 0 bytes"};
    }

    // Error or EAGAIN/EWOULDBLOCK
    if (!file::would_block()) {
      co_return expected<void, std::string>{unexpect, std::string(strerror(errno))};
    }

    // Yield to let the reactor process events, then retry
    auto result = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor.continue_after_sent_data(_fd, _index, std::move(cont));
    });

    if (result == reactor::connection_state::closed) {
      co_return expected<void, std::string>{unexpect, "File was closed"};
    }
  }

  co_return expected<void, std::string>{};
#endif
}

expected<void, std::string> file::flush() const {
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

void file::close() noexcept {
  if (_fd != invalid_file_handle) {
    _reactor.remove_fd(_fd, _index);
    close_file(_fd);
    _fd = invalid_file_handle;
    _index = static_cast<size_t>(-1);
  }
}

expected<size_t, std::string> file::get_size() const {
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

expected<off_t, std::string> file::seek(off_t offset, seek_whence whence) const {
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

async_coro::task<expected<std::vector<std::byte>, std::string>> file::read_all() {
  if (_fd == invalid_file_handle) {
    co_return expected<std::vector<std::byte>, std::string>{unexpect, "File is closed"};
  }

  auto size_result = get_size();
  if (!size_result) {
    co_return expected<std::vector<std::byte>, std::string>{unexpect, std::move(size_result).error()};
  }

  const size_t file_size = size_result.value();
  if (file_size == 0) {
    co_return std::vector<std::byte>{};
  }

  std::vector<std::byte> buffer;
  buffer.resize(file_size);

  size_t total_bytes_read = 0;
  auto current_buffer = std::span<std::byte>{buffer.data(), buffer.size()};

  while (total_bytes_read < file_size) {
#if WIN_SOCKET
    DWORD bytes_read = 0;
    BOOL result = ::ReadFile(_fd, current_buffer.data(), static_cast<DWORD>(current_buffer.size()), &bytes_read, nullptr);

    if (result && bytes_read > 0) {
      total_bytes_read += bytes_read;
      current_buffer = current_buffer.subspan(bytes_read);
      continue;
    }

    if (!result && bytes_read == 0) {
      // End of file
      co_return std::move(buffer);
    }

    if (!result) {
      const DWORD error = GetLastError();
      if (error == ERROR_HANDLE_EOF) {
        co_return std::move(buffer);
      }
      co_return expected<std::vector<std::byte>, std::string>{unexpect, "ReadFile failed with error code " + std::to_string(error)};
    }

    co_return expected<std::vector<std::byte>, std::string>{unexpect, "ReadFile returned success with no bytes"};
#else
    ssize_t bytes_read = ::read(_fd, current_buffer.data(), current_buffer.size());

    if (bytes_read > 0) {
      total_bytes_read += static_cast<size_t>(bytes_read);
      current_buffer = current_buffer.subspan(bytes_read);
      continue;
    }

    if (bytes_read == 0) {
      // End of file
      co_return std::move(buffer);
    }

    // Error or EAGAIN/EWOULDBLOCK
    if (!file::would_block()) {
      co_return expected<std::vector<std::byte>, std::string>{unexpect, std::string(strerror(errno))};
    }

    // Yield to let the reactor process events, then retry
    auto result = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor.continue_after_receive_data(_fd, _index, std::move(cont));
    });

    if (result == reactor::connection_state::closed) {
      co_return expected<std::vector<std::byte>, std::string>{unexpect, "File was closed"};
    }
#endif
  }

  co_return std::move(buffer);
}

}  // namespace server::io
