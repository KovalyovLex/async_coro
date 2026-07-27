
#include <async_coro/await/await_callback.h>
#include <fcntl.h>
#include <server/io/file.h>
#include <server/socket_layer/socket_config.h>
#include <server/utils/expected.h>
#include <sys/stat.h>

#if !WIN_SOCKET
#include <unistd.h>
#endif

#include <cerrno>

namespace server::io {

bool file::would_block(int err) noexcept {
#if WIN_SOCKET
  return err == WSAEWOULDBLOCK || err == WSAEAGAIN;
#else
  return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

file::file(reactor& reactor, socket_type file_descriptor, size_t index) noexcept  // NOLINT(*-swappable*)
    : _reactor(reactor),
      _fd(file_descriptor),
      _index(index) {
  // The fd is already added to the reactor in file::open
}

expected<file, std::string> file::open(reactor& reactor, const std::string& path, int mode, int permissions) noexcept {
  socket_type file_descriptor = ::open(path.c_str(), mode, permissions);  // NOLINT(*vararg*)
  if (file_descriptor == invalid_socket_id) {
    return expected<file, std::string>{unexpect, std::string(strerror(errno))};
  }

  // Set non-blocking mode for async I/O
#if !WIN_SOCKET
  const auto flags = ::fcntl(file_descriptor, F_GETFL, 0);
  if (flags < 0 || ::fcntl(file_descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {  // NOLINT(*-signed*, *vararg*)
    socket_layer::close_socket(file_descriptor);
    return expected<file, std::string>{unexpect, "Failed to set non-blocking mode"};
  }
#endif

  size_t index = reactor.add_fd(file_descriptor);
  if (index == static_cast<size_t>(-1)) {
    socket_layer::close_socket(file_descriptor);
    return expected<file, std::string>{unexpect, "Failed to add fd to reactor"};
  }

  return file{reactor, file_descriptor, index};
}

async_coro::task<expected<size_t, std::string>> file::read(std::span<uint8_t> buffer) {
  if (_fd == invalid_socket_id) {
    co_return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_read = 0;
  auto current_buffer = buffer;

  while (total_bytes_read < buffer.size()) {
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
    if (!file::would_block(errno)) {
      co_return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
    }

    // Yield to let the reactor process events, then retry
    auto result = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor.continue_after_receive_data(_fd, _index, std::move(cont));
    });

    if (result == reactor::connection_state::closed) {
      co_return expected<size_t, std::string>{unexpect, "File was closed"};
    }
  }

  co_return total_bytes_read;
}

async_coro::task<expected<void, std::string>> file::write(std::span<const uint8_t> data) {
  if (_fd == invalid_socket_id) {
    co_return expected<void, std::string>{unexpect, "File is closed"};
  }

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
    if (!file::would_block(errno)) {
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
}

expected<void, std::string> file::flush() const {
  if (_fd == invalid_socket_id) {
    return expected<void, std::string>{unexpect, "File is closed"};
  }

  if (::fsync(_fd) == 0) {
    return expected<void, std::string>{};
  }

  return expected<void, std::string>{unexpect, std::string(strerror(errno))};
}

void file::close() noexcept {
  if (_fd != invalid_socket_id) {
    ::close(_fd);
    _reactor.remove_fd(_fd, _index);
    _fd = invalid_socket_id;
    _index = static_cast<size_t>(-1);
  }
}

bool file::is_closed() const noexcept {
  return _fd == invalid_socket_id;
}

int file::get_fd() const noexcept {
  return static_cast<int>(_fd);
}

int file::map_whence(seek_whence whence) noexcept {
  switch (whence) {
    case seek_whence::set:
      return SEEK_SET;
    case seek_whence::current:
      return SEEK_CUR;
    case seek_whence::end:
      return SEEK_END;
  }
  return SEEK_SET;  // fallback
}

expected<size_t, std::string> file::get_size() const {
  if (_fd == invalid_socket_id) {
    return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  struct stat stat_buf{};
  if (::fstat(_fd, &stat_buf) != 0) {
    return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
  }

  return static_cast<size_t>(stat_buf.st_size);
}

expected<off_t, std::string> file::seek(off_t offset, seek_whence whence) const {
  if (_fd == invalid_socket_id) {
    return expected<off_t, std::string>{unexpect, "File is closed"};
  }

  const int posix_whence = map_whence(whence);
  off_t new_offset = ::lseek(_fd, offset, posix_whence);
  if (new_offset == static_cast<off_t>(-1)) {
    return expected<off_t, std::string>{unexpect, std::string(strerror(errno))};
  }

  return new_offset;
}

async_coro::task<expected<std::vector<std::byte>, std::string>> file::read_all() {
  if (_fd == invalid_socket_id) {
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
    if (!file::would_block(errno)) {
      co_return expected<std::vector<std::byte>, std::string>{unexpect, std::string(strerror(errno))};
    }

    // Yield to let the reactor process events, then retry
    auto result = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor.continue_after_receive_data(_fd, _index, std::move(cont));
    });

    if (result == reactor::connection_state::closed) {
      co_return expected<std::vector<std::byte>, std::string>{unexpect, "File was closed"};
    }
  }

  co_return std::move(buffer);
}

}  // namespace server::io
