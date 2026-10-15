#include "durable_file.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mpt::shell {
namespace {

std::uint64_t process_id() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

// <path>.tmp-<pid>-<n>: unique per process and call.
std::filesystem::path temp_name_for(const std::filesystem::path& path) {
  static std::atomic<std::uint64_t> counter{0};
  std::filesystem::path temp = path;
  temp += ".tmp-" + std::to_string(process_id()) + "-" + std::to_string(counter.fetch_add(1));
  return temp;
}

#ifdef _WIN32

std::string last_error_text() { return "Windows error " + std::to_string(GetLastError()); }

Result write_synced(const std::filesystem::path& temp, std::span<const std::byte> bytes) {
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return Result::failure("cannot create " + display_path(temp) + ": " + last_error_text());
  std::size_t done = 0;
  bool ok = true;
  while (ok && done < bytes.size()) {
    const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 30));
    DWORD written = 0;
    ok = WriteFile(file, bytes.data() + done, chunk, &written, nullptr) != 0 && written == chunk;
    done += written;
  }
  ok = ok && FlushFileBuffers(file) != 0;
  const std::string error = ok ? std::string() : last_error_text();
  CloseHandle(file);
  if (!ok) return Result::failure("cannot write " + display_path(temp) + ": " + error);
  return Result::success();
}

Result replace_file(const std::filesystem::path& temp, const std::filesystem::path& path) {
  if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return Result::failure("cannot replace " + display_path(path) + ": " + last_error_text());
  }
  return Result::success();
}

#else

std::string errno_text() { return std::error_code(errno, std::generic_category()).message(); }

Result write_synced(const std::filesystem::path& temp, std::span<const std::byte> bytes) {
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) return Result::failure("cannot create " + display_path(temp) + ": " + errno_text());
  std::size_t done = 0;
  bool ok = true;
  while (ok && done < bytes.size()) {
    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n < 0 && errno == EINTR) continue;
    ok = n > 0;
    if (ok) done += static_cast<std::size_t>(n);
  }
  ok = ok && ::fsync(fd) == 0;
  const std::string error = ok ? std::string() : errno_text();
  ::close(fd);
  if (!ok) return Result::failure("cannot write " + display_path(temp) + ": " + error);
  return Result::success();
}

Result replace_file(const std::filesystem::path& temp, const std::filesystem::path& path) {
  if (::rename(temp.c_str(), path.c_str()) != 0) {
    return Result::failure("cannot replace " + display_path(path) + ": " + errno_text());
  }
  // Make the rename itself durable.
  const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir >= 0) {
    ::fsync(dir);
    ::close(dir);
  }
  return Result::success();
}

#endif

}  // namespace

std::string display_path(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}


Result write_file_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return Result::failure("cannot create " + display_path(path.parent_path()) + ": " + ec.message());

  const std::filesystem::path temp = temp_name_for(path);
  if (Result written = write_synced(temp, bytes); !written) {
    std::filesystem::remove(temp, ec);
    return written;
  }
  if (Result replaced = replace_file(temp, path); !replaced) {
    std::filesystem::remove(temp, ec);
    return replaced;
  }
  return Result::success();
}

}  // namespace mpt::shell
