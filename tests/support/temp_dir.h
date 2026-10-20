#pragma once

// Temporary folders for tests that never collide across processes: ctest -j, two build
// trees, or other worktrees testing on the same machine all share the system temp folder.
// The name holds the process ID (unique among live processes) and a per-process counter.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mpt::test {

inline std::uint64_t process_id() {
#ifdef _WIN32
  return static_cast<std::uint64_t>(_getpid());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

// <system temp>/<prefix>_<pid>_<n>, created empty.
inline std::filesystem::path make_temp_dir(const std::string& prefix) {
  static std::atomic<std::uint64_t> counter{0};
  const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                    (prefix + "_" + std::to_string(process_id()) + "_" + std::to_string(counter++));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

// A make_temp_dir() folder removed again when the object goes out of scope.
class ScopedTempDir {
 public:
  explicit ScopedTempDir(const std::string& prefix) : path_(make_temp_dir(prefix)) {}
  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  ScopedTempDir(const ScopedTempDir&) = delete;
  ScopedTempDir& operator=(const ScopedTempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace mpt::test
