#include "mpt/asset_import/process.h"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mpt::asset_import {
namespace {

namespace fs = std::filesystem;

std::string getenv_string(const char* name) {
#ifdef _WIN32
  // The wide API keeps non-ASCII PATH entries intact; convert back to UTF-8.
  const std::wstring wide_name(name, name + std::char_traits<char>::length(name));
  const DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
  if (size == 0) return {};
  std::wstring value(size, L'\0');
  const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
  value.resize(length);
  const std::u8string utf8 = fs::path(value).u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
  const char* value = std::getenv(name);
  return value ? value : "";
#endif
}

fs::path from_utf8(std::string_view text) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}  // namespace

std::optional<fs::path> find_program(std::string_view name) {
  if (name.empty()) return std::nullopt;
  const fs::path as_path = from_utf8(name);
  std::error_code ec;
  if (as_path.has_parent_path()) {
    if (fs::is_regular_file(as_path, ec)) return as_path;
    return std::nullopt;
  }
#ifdef _WIN32
  constexpr char kSeparator = ';';
  std::vector<std::string> extensions{""};
  if (!as_path.has_extension()) {
    std::string pathext = getenv_string("PATHEXT");
    if (pathext.empty()) pathext = ".COM;.EXE;.BAT;.CMD";
    std::size_t start = 0;
    while (start <= pathext.size()) {
      const std::size_t end = std::min(pathext.find(kSeparator, start), pathext.size());
      if (end > start) extensions.push_back(pathext.substr(start, end - start));
      start = end + 1;
    }
    extensions.erase(extensions.begin());  // an extensionless file is not runnable on Windows
  }
#else
  constexpr char kSeparator = ':';
  const std::vector<std::string> extensions{""};
#endif
  const std::string path_var = getenv_string("PATH");
  std::size_t start = 0;
  while (start <= path_var.size()) {
    const std::size_t end = std::min(path_var.find(kSeparator, start), path_var.size());
    const std::string dir = path_var.substr(start, end - start);
    start = end + 1;
    if (dir.empty()) continue;
    for (const std::string& ext : extensions) {
      const fs::path candidate = from_utf8(dir) / from_utf8(std::string(name) + ext);
      if (!fs::is_regular_file(candidate, ec)) continue;
#ifndef _WIN32
      if (::access(candidate.c_str(), X_OK) != 0) continue;
#endif
      return candidate;
    }
  }
  return std::nullopt;
}

#ifdef _WIN32
namespace {

std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// Quotes one argument so CommandLineToArgvW / the MSVC runtime read it back unchanged.
void append_quoted(std::wstring& command_line, const std::wstring& arg) {
  if (!command_line.empty()) command_line.push_back(L' ');
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    command_line += arg;
    return;
  }
  command_line.push_back(L'"');
  for (std::size_t i = 0;; ++i) {
    std::size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      command_line.append(backslashes * 2, L'\\');
      break;
    }
    if (arg[i] == L'"') {
      command_line.append(backslashes * 2 + 1, L'\\');
    } else {
      command_line.append(backslashes, L'\\');
    }
    command_line.push_back(arg[i]);
  }
  command_line.push_back(L'"');
}

std::wstring upper(std::wstring text) {
  for (wchar_t& c : text) c = static_cast<wchar_t>(towupper(c));
  return text;
}

// The inherited environment with `overrides` applied, as a CREATE_UNICODE_ENVIRONMENT block.
std::wstring environment_block(const std::map<std::string, std::string>& overrides) {
  std::map<std::wstring, std::wstring> vars;  // upper-cased name → "Name=value"
  if (wchar_t* block = GetEnvironmentStringsW()) {
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
      const std::wstring entry(p);
      const std::size_t eq = entry.find(L'=', 1);  // names like "=C:" start with '='
      if (eq == std::wstring::npos) continue;
      vars[upper(entry.substr(0, eq))] = entry;
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto& [name, value] : overrides) vars[upper(widen(name))] = widen(name) + L"=" + widen(value);
  std::wstring out;
  for (const auto& [key, entry] : vars) {
    out += entry;
    out.push_back(L'\0');
  }
  out.push_back(L'\0');
  return out;
}

}  // namespace

ProcessResult SystemProcessRunner::run(const ProcessRequest& request) {
  ProcessResult result;
  if (request.argv.empty()) return result;
  const auto program = find_program(request.argv[0]);
  if (!program) {
    result.output = "Program not found: " + request.argv[0];
    return result;
  }

  std::wstring command_line;
  append_quoted(command_line, program->wstring());
  for (std::size_t i = 1; i < request.argv.size(); ++i) append_quoted(command_line, widen(request.argv[i]));

  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
    result.output = "Could not create a pipe.";
    return result;
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
  HANDLE null_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                               nullptr);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_in;
  startup.hStdOutput = write_end;
  startup.hStdError = write_end;
  PROCESS_INFORMATION info{};

  std::wstring env;
  if (!request.env.empty()) env = environment_block(request.env);
  const std::wstring cwd = request.cwd.empty() ? std::wstring{} : request.cwd.wstring();
  const BOOL created = CreateProcessW(program->wstring().c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | (env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
                                      env.empty() ? nullptr : env.data(), cwd.empty() ? nullptr : cwd.c_str(),
                                      &startup, &info);
  CloseHandle(write_end);
  if (null_in != INVALID_HANDLE_VALUE) CloseHandle(null_in);
  if (!created) {
    CloseHandle(read_end);
    result.output = "Could not start " + request.argv[0] + " (error " + std::to_string(GetLastError()) + ").";
    return result;
  }
  result.started = true;

  char buffer[4096];
  DWORD n = 0;
  while (ReadFile(read_end, buffer, sizeof(buffer), &n, nullptr) && n > 0) result.output.append(buffer, n);
  CloseHandle(read_end);
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(info.hProcess, &code);
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  result.exit_code = static_cast<int>(code);
  return result;
}

#else

ProcessResult SystemProcessRunner::run(const ProcessRequest& request) {
  ProcessResult result;
  if (request.argv.empty()) return result;
  const auto program = find_program(request.argv[0]);
  if (!program) {
    result.output = "Program not found: " + request.argv[0];
    return result;
  }

  // Build everything the child needs before fork(): only async-signal-safe calls after it.
  const std::string program_path = program->string();
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(program_path.c_str()));
  for (std::size_t i = 1; i < request.argv.size(); ++i) argv.push_back(const_cast<char*>(request.argv[i].c_str()));
  argv.push_back(nullptr);

  std::map<std::string, std::string> vars;
  for (char** e = environ; e && *e; ++e) {
    const std::string entry(*e);
    const std::size_t eq = entry.find('=');
    if (eq != std::string::npos) vars[entry.substr(0, eq)] = entry;
  }
  for (const auto& [name, value] : request.env) vars[name] = name + "=" + value;
  std::vector<std::string> env_storage;
  for (const auto& [name, entry] : vars) env_storage.push_back(entry);
  std::vector<char*> envp;
  for (std::string& entry : env_storage) envp.push_back(entry.data());
  envp.push_back(nullptr);
  const std::string cwd = request.cwd.string();

  int out_pipe[2];
  int exec_pipe[2];  // closes on a successful exec; carries errno if exec fails
  if (pipe(out_pipe) != 0) return result;
  if (pipe(exec_pipe) != 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    return result;
  }
  fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);

  const pid_t pid = fork();
  if (pid == 0) {
    close(out_pipe[0]);
    close(exec_pipe[0]);
    const int null_in = open("/dev/null", O_RDONLY);
    if (null_in >= 0) dup2(null_in, 0);
    dup2(out_pipe[1], 1);
    dup2(out_pipe[1], 2);
    if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
      const int err = errno;
      (void)!write(exec_pipe[1], &err, sizeof(err));
      _exit(127);
    }
    execve(argv[0], argv.data(), envp.data());
    const int err = errno;
    (void)!write(exec_pipe[1], &err, sizeof(err));
    _exit(127);
  }
  close(out_pipe[1]);
  close(exec_pipe[1]);
  if (pid < 0) {
    close(out_pipe[0]);
    close(exec_pipe[0]);
    return result;
  }

  int exec_errno = 0;
  const bool exec_failed = read(exec_pipe[0], &exec_errno, sizeof(exec_errno)) == sizeof(exec_errno);
  close(exec_pipe[0]);
  char buffer[4096];
  for (ssize_t n; (n = read(out_pipe[0], buffer, sizeof(buffer))) != 0;) {
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    result.output.append(buffer, static_cast<std::size_t>(n));
  }
  close(out_pipe[0]);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (exec_failed) {
    result.output = "Could not start " + request.argv[0] + " (errno " + std::to_string(exec_errno) + ").";
    return result;
  }
  result.started = true;
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
  return result;
}

#endif

}  // namespace mpt::asset_import
