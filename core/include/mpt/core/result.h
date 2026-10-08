#pragma once

#include <string>
#include <utility>

namespace mpt {

// Success-or-error value. C++20 has no std::expected, so this stays deliberately small.
class [[nodiscard]] Result {
 public:
  static Result success() { return Result{}; }
  static Result failure(std::string message) { return Result{std::move(message)}; }

  bool ok() const { return !failed_; }
  explicit operator bool() const { return ok(); }
  const std::string& error() const { return error_; }

 private:
  Result() = default;
  explicit Result(std::string message) : failed_(true), error_(std::move(message)) {}

  bool failed_ = false;
  std::string error_;
};

}  // namespace mpt
