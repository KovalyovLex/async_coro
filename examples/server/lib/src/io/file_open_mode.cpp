#include <server/io/file_open_mode.h>

#if WIN_SOCKET

#else
#include <fcntl.h>
#endif

namespace server::io {

#if WIN_SOCKET

void mode_to_win_flags(file_open_mode mode, DWORD& access, DWORD& creation) noexcept {
  access = 0;
  creation = 0;

  if ((mode & file_open_mode::read) == file_open_mode::read) {
    access |= GENERIC_READ;
  }
  if ((mode & file_open_mode::write) == file_open_mode::write) {
    access |= GENERIC_WRITE;
  }

  if ((mode & file_open_mode::append) == file_open_mode::append) {
    // Append mode: write-only, open existing
    access = GENERIC_WRITE;
    creation = OPEN_EXISTING;
  } else if ((mode & file_open_mode::create) == file_open_mode::create) {
    creation = ((mode & file_open_mode::trunc) == file_open_mode::trunc) ? CREATE_ALWAYS : OPEN_ALWAYS;
  } else if ((mode & file_open_mode::trunc) == file_open_mode::trunc) {
    creation = TRUNCATE_EXISTING;
  } else {
    creation = OPEN_EXISTING;
  }

  // If neither read nor write is specified, default to read
  if (access == 0) {
    access |= GENERIC_READ;
  }
}

#else

int mode_to_posix_flags(file_open_mode mode) noexcept {
  int flags = 0;
  constexpr auto rw_mode = file_open_mode::read & file_open_mode::write;

  // NOLINTBEGIN(*-signed-bitwise*)
  if ((mode & rw_mode) == rw_mode) {
    flags |= O_RDWR;
  } else if ((mode & file_open_mode::write) == file_open_mode::write) {
    flags |= O_WRONLY;
  } else {
    flags |= O_RDONLY;
  }

  if ((mode & file_open_mode::create) == file_open_mode::create) {
    flags |= O_CREAT;
  }
  if ((mode & file_open_mode::trunc) == file_open_mode::trunc) {
    flags |= O_TRUNC;
  }
  if ((mode & file_open_mode::append) == file_open_mode::append) {
    flags |= O_APPEND;
  }
  // NOLINTEND(*-signed-bitwise*)

  return flags;
}

#endif

}  // namespace server::io
