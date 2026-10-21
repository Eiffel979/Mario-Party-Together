#pragma once

// Renames that survive Windows' transient file locks.
//
// On Windows a directory cannot be renamed while any process holds a handle on a file
// inside it. Defender real-time scanning and the search indexer briefly open files that
// were just written, so renaming a freshly built folder into place intermittently fails
// with ERROR_ACCESS_DENIED or ERROR_SHARING_VIOLATION even though nothing is wrong.
//
// rename_with_retry() keeps the rename atomic (it never falls back to a copy, so a build
// is either fully in place or not at all) and only retries those transient errors, with
// bounded exponential backoff. Every other error fails at once. On POSIX it is a plain
// std::filesystem::rename, since POSIX renames do not fail on open files.

#include <chrono>
#include <filesystem>
#include <functional>
#include <system_error>

namespace mpt::asset_import {

// Bounds for the retries. The defaults wait 10, 20, 40, 80, 160 ms and then 250 ms per
// retry, for at most 20 retries: about 4 s of waiting in total before giving up. Scanner
// holds are usually a few to a few tens of milliseconds, so most renames succeed on the
// first or second retry; the long tail covers a scan of a large file on a slow disk. A
// real permission problem therefore costs at most about 4 s before it is reported.
struct RenameRetryPolicy {
  int max_retries = 20;
  std::chrono::milliseconds first_delay{10};
  std::chrono::milliseconds max_delay{250};
};

// True for the errors a transient handle on a file inside the folder causes on Windows:
// permission_denied (ERROR_ACCESS_DENIED maps to it) and, on Windows, the Win32 codes
// ERROR_ACCESS_DENIED (5), ERROR_SHARING_VIOLATION (32) and ERROR_LOCK_VIOLATION (33).
bool is_transient_rename_error(const std::error_code& ec);

// Renames from to to. On Windows, retries transient errors per the default policy; on
// POSIX, a single rename. Leaves ec clear on success, or set to the last error.
void rename_with_retry(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);

// The retry loop itself, on every platform, with the rename and the sleep injectable so
// tests can simulate a scanner holding a file without depending on its timing.
using RenameFunction =
    std::function<void(const std::filesystem::path&, const std::filesystem::path&, std::error_code&)>;
using SleepFunction = std::function<void(std::chrono::milliseconds)>;

void rename_with_retry(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec,
                       const RenameRetryPolicy& policy, const RenameFunction& rename, const SleepFunction& sleep);

}  // namespace mpt::asset_import
