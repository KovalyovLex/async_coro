#if WIN_IOCP_ENABLED

#include <async_coro/await/await_callback.h>
#include <server/io/file_open_mode.h>
#include <server/io/iocp/iocp_file.h>
#include <server/utils/expected.h>

#include <cstring>
#include <utility>
#include <vector>

namespace server::io {

// ============================================================================
// Constructor / Destructor / Move semantics
// ============================================================================

iocp_file::iocp_file(iocp_reactor& reactor, file_handle_t file_descriptor) noexcept
    : _reactor(reactor),
      _fd(file_descriptor) {
  // The fd is already opened via IOCP in iocp_file::open_coro.
}

iocp_file::~iocp_file() noexcept {
  close_sync();
}

iocp_file::iocp_file(iocp_file&& other) noexcept
    : _reactor(other._reactor),
      _fd(std::exchange(other._fd, invalid_file_handle)),
      _seek_cur(std::exchange(other._seek_cur, 0)) {
}

iocp_file& iocp_file::operator=(iocp_file&& other) noexcept {
  if (this != &other) {
    // Explicitly destroy current object (closes file handle via destructor).
    this->~iocp_file();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) iocp_file(std::move(other));
  }
  return *this;
}

void iocp_file::close_sync() noexcept {
  if (is_closed()) {
    return;
  }

  auto val = _reactor.close_file(std::exchange(_fd, invalid_file_handle));

  ASYNC_CORO_ASSERT(val);
}

// ============================================================================
// open_coro
// ============================================================================

async_coro::task<expected<iocp_file, std::string>> iocp_file::open_coro(iocp_reactor& reactor, std::string path, file_open_mode mode) noexcept {  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters): reactor lifetime guaranteed by iocp_file owner

  auto result = co_await async_coro::await_callback_with_result<expected<file_handle_t, std::string>>([&](auto cont) {
    reactor.submit_open(path.c_str(), mode, std::move(cont));
  });

  if (!result) {
    co_return expected<iocp_file, std::string>{unexpect, std::move(result.error())};
  }

  co_return iocp_file{reactor, result.value()};
}

// ============================================================================
// read
// ============================================================================

async_coro::task<expected<size_t, std::string>> iocp_file::read(std::span<std::byte> buffer) {
  if (is_closed()) {
    co_return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_read = 0;
  auto current_buffer = buffer;

  while (total_bytes_read < buffer.size()) {
    // Submit async read operation.
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, std::string>>([this, &current_buffer](auto cont) {
      _reactor.submit_read(_fd, _seek_cur, current_buffer, std::move(cont));
    });

    if (!result) {
      co_return expected<size_t, std::string>{unexpect, std::move(result.error())};
    }

    const auto read = result.value();
    if (read == 0) {
      break;  // EOF reached
    }
    _seek_cur += read;
    total_bytes_read += read;
    current_buffer = current_buffer.subspan(read);
  }

  co_return total_bytes_read;
}

// ============================================================================
// write
// ============================================================================

async_coro::task<expected<void, std::string>> iocp_file::write(std::span<const std::byte> data) {
  if (is_closed()) {
    co_return expected<void, std::string>{unexpect, "File is closed"};
  }

  size_t total_bytes_written = 0;
  auto current_data = data;

  while (total_bytes_written < data.size()) {
    // Submit async write operation.
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

// ============================================================================
// flush
// ============================================================================

expected<void, std::string> iocp_file::flush() noexcept {
  if (is_closed()) {
    return expected<void, std::string>{unexpect, "File is closed"};
  }

  return _reactor.flush(_fd);
}

// ============================================================================
// close
// ============================================================================

expected<void, std::string> iocp_file::close() noexcept {
  if (is_closed()) {
    return expected<void, std::string>{};
  }

  return _reactor.close_file(std::exchange(_fd, invalid_file_handle));
}

// ============================================================================
// Query methods
// ============================================================================

expected<size_t, std::string> iocp_file::get_size() const noexcept {
  if (is_closed()) {
    return expected<size_t, std::string>{unexpect, "File is closed"};
  }

  LARGE_INTEGER file_size{};

  if (!GetFileSizeEx(_fd, &file_size)) {
    return expected<size_t, std::string>{unexpect, "GetFileSizeEx failed"};
  }

  return static_cast<size_t>(file_size.QuadPart);
}

expected<uint64_t, std::string> iocp_file::seek(uint64_t offset) noexcept {
  if (is_closed()) {
    return expected<uint64_t, std::string>{unexpect, "File is closed"};
  }

  _seek_cur = offset;
  return _seek_cur;
}

async_coro::task<expected<std::vector<std::byte>, std::string>> iocp_file::read_all() {
  if (is_closed()) {
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

  auto result = co_await read(buffer);
  if (!result) {
    co_return expected<std::vector<std::byte>, std::string>{unexpect, std::move(result).error()};
  }

  co_return std::move(buffer);
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
