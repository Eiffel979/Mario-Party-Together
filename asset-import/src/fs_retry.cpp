#include "mpt/asset_import/fs_retry.h"

#include <algorithm>
#include <thread>

namespace mpt::asset_import {

namespace fs = std::filesystem;

bool is_transient_rename_error(const std::error_code& ec) {
  if (!ec) return false;
  if (ec == std::errc::permission_denied) return true;
#ifdef _WIN32
  // Win32 codes, spelled out to keep <windows.h> out of this file. (On POSIX,
  // system_category values are errno values, where 5, 32 and 33 mean other things.)
  constexpr int kErrorAccessDenied = 5;
  constexpr int kErrorSharingViolation = 32;
  constexpr int kErrorLockViolation = 33;
  if (ec.category() == std::system_category()) {
    const int value = ec.value();
    return value == kErrorAccessDenied || value == kErrorSharingViolation || value == kErrorLockViolation;
  }
#endif
  return false;
}

void rename_with_retry(const fs::path& from, const fs::path& to, std::error_code& ec, const RenameRetryPolicy& policy,
                       const RenameFunction& rename, const SleepFunction& sleep) {
  std::chrono::milliseconds delay = policy.first_delay;
  for (int retry = 0;; ++retry) {
    ec.clear();
    rename(from, to, ec);
    if (!ec || !is_transient_rename_error(ec) || retry >= policy.max_retries) return;
    sleep(delay);
    delay = std::min(delay * 2, policy.max_delay);
  }
}

void rename_with_retry(const fs::path& from, const fs::path& to, std::error_code& ec) {
#ifdef _WIN32
  rename_with_retry(
      from, to, ec, RenameRetryPolicy{},
      [](const fs::path& a, const fs::path& b, std::error_code& e) { fs::rename(a, b, e); },
      [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); });
#else
  fs::rename(from, to, ec);
#endif
}

}  // namespace mpt::asset_import
