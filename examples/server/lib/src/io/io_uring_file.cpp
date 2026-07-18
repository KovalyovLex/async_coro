#if IO_URING_ENABLED

#include <async_coro/await/await_callback.h>
#include <fcntl.h>
#include <server/io/io_uring_file.h>
#include <server/utils/expected.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace server::io {

bool io_uring_file::would_block(int err) noexcept {
#if WIN_SOCKET
  return err == WSAEWOULDBLOCK || err == WSAEAGAIN;
#else
  return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

io_uring_file::io_uring_file(io_uring_reactor& reactor, int fd) noexcept  // NOLINT(*-swappable*)
    : _reactor(reactor),
      _fd(fd) {
  // The fd is already opened via io_uring in io_uring_file::open
}

async_coro::task<expected<io_uring_file, std::string>> io_uring_file::open_coro(io_uring_reactor& reactor, std::string path, int flags, int mode) noexcept {
  auto result = co_await async_coro::await_callback_with_result<expected<int, std::string>>([&](auto cont) {
    reactor.submit_open(path.c_str(), flags, mode, std::move(cont));
  });

  if (!result) {
    co_return expected<io_uring_file, std::string>{unexpect, std::move(result.error())};
  }

  co_return io_uring_file{reactor, result.value()};
}

async_coro::task<expected<size_t, std::string>> io_uring_file::read(std::span<uint8_t> buffer) {
  if (_fd == -1) {
    co_return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_read = 0;
  auto current_buffer = buffer;

  while (total_bytes_read < buffer.size()) {
    // Submit async read operation
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, std::string>>([this, &current_buffer](auto cont) {
      _reactor.submit_read(_fd, _seek_cur, current_buffer, std::move(cont));
    });

    if (!result) {
      co_return expected<size_t, std::string>{unexpect, std::move(result.error())};
    }

    const auto read = result.value();
    _seek_cur += read;
    total_bytes_read += read;
    current_buffer = current_buffer.subspan(read);
  }

  co_return total_bytes_read;
}

async_coro::task<expected<void, std::string>> io_uring_file::write(std::span<const uint8_t> data) {
  if (_fd == -1) {
    co_return expected<void, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_written = 0;
  auto current_data = data;

  while (total_bytes_written < data.size()) {
    // Submit async write operation
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, std::string>>([this, &current_data](auto cont) {
      _reactor.submit_write(_fd, _seek_cur, current_data, std::move(cont));
    });

    if (!result) {
      co_return expected<void, std::string>{unexpect, std::move(result.error())};
    }

    const auto written = result.value();
    _seek_cur += written;
    total_bytes_written += written;
    current_data = current_data.subspan(written);
  }

  co_return expected<void, std::string>{};
}

async_coro::task<expected<void, std::string>> io_uring_file::flush() {
  if (_fd == -1) {
    co_return expected<void, std::string>{unexpect, "File is closed"};
  }

  auto result = co_await async_coro::await_callback_with_result<expected<void, std::string>>([this](auto cont) {
    _reactor.submit_fsync(_fd, std::move(cont));
  });

  co_return std::move(result);
}

async_coro::task<expected<void, std::string>> io_uring_file::close() {
  if (_fd == -1) {
    co_return expected<void, std::string>{};
  }

  auto result = co_await async_coro::await_callback_with_result<expected<void, std::string>>([this](auto cont) {
    _reactor.submit_close(_fd, std::move(cont));
  });

  _fd = -1;

  co_return std::move(result);
}

bool io_uring_file::is_closed() const noexcept {
  return _fd == -1;
}

int io_uring_file::get_fd() const noexcept {
  return _fd;
}

expected<size_t, std::string> io_uring_file::get_size() const {
  if (_fd == -1) {
    return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  struct stat stat_buf;
  if (::fstat(_fd, &stat_buf) != 0) {
    return expected<size_t, std::string>{unexpect, std::string(strerror(errno))};
  }

  return static_cast<size_t>(stat_buf.st_size);
}

expected<off_t, std::string> io_uring_file::seek(off_t offset) {
  if (_fd == -1) {
    return expected<off_t, std::string>{unexpect, "File is closed"};
  }

  _seek_cur = static_cast<size_t>(offset);

  return static_cast<off_t>(_seek_cur);
}

async_coro::task<expected<std::vector<std::byte>, std::string>> io_uring_file::read_all() {
  if (_fd == -1) {
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

  auto result = co_await read(std::span<uint8_t>{reinterpret_cast<uint8_t*>(buffer.data()), file_size});
  if (!result) {
    co_return expected<std::vector<std::byte>, std::string>{unexpect, std::move(result).error()};
  }

  co_return std::move(buffer);
}

}  // namespace server::io

#endif
