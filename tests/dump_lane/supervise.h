#pragma once

// `mpt_dump_lane --supervise [--crash-report FILE] <the lane's other arguments>` (M4-04): run the lane
// as a child process under a supervisor that reports a crash the child cannot report itself.
//
// On Windows the supervisor starts the child as its debugger (DEBUG_ONLY_THIS_PROCESS). A /GS stack-
// cookie failure is a fail-fast (__fastfail, 0xC0000409): it skips every handler in the process, but
// the kernel still hands it to an attached debugger. The supervisor then stops the child, walks the
// faulting thread's stack (DbgHelp's StackWalk64 over the child's unwind data, which also covers game
// coroutine stacks) and names each frame as module+offset plus the nearest symbol: from the image's
// COFF symbol table when it has one (the MP4 module, /DEBUG:SYMTAB), else from DbgHelp (a PDB or the
// exports). Every other exception the child does not handle (second chance) is reported the same way;
// first-chance exceptions go back to the child untouched, so MP4's copy trap and the in-process hooks
// (crash_report.h) work as before. An exit with code 3 (the in-process abort/terminate hooks) or with
// an NTSTATUS code also gets a report, without a stack. The breadcrumbs (crash_report.h) are in memory
// the supervisor shares with the child, so the report has the frame, phase, overlay, seed, board and
// last checkpoint even when the child is gone.
//
// The report holds numbers, module file names (never their folders) and symbol names: no game text and
// no paths. It goes to stdout and, with --crash-report FILE, to FILE (outside the repository, like
// every path the lane writes). FILE is removed when the run starts and written only on a crash, so it
// never describes an older run. The supervisor returns the child's exit code.
//
// The debugger changes nothing the game sees: the child gets _NO_DEBUG_HEAP=1, so its heap is the same
// as an unsupervised run's, and nothing in the runtime reads IsDebuggerPresent.
//
// On Linux, `--supervise` runs the lane in-process: the stack protector's failure path,
// __stack_chk_fail, is the lane's own (stack_chk_fail.cpp) and writes the same breadcrumbs and the
// failing function to stderr and --crash-report FILE. Compile-only until the Linux runtime work is
// unblocked (phase-1.md M4-U2).
//
// `--crash-selftest MODULE gs|av|abort` (no dump needed) loads a synthetic stub module
// (tests/mp4_loader/overrun_stub.cpp), notes made-up breadcrumbs and makes the stub crash: a stack
// buffer overrun caught by /GS or the stack protector, a write through a null pointer, or abort().
// tests/dump_lane/crash_report_check.cmake runs it under --supervise and checks the report.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "crash_report.h"
#include "path_guard.h"

#if defined(_WIN32) && defined(_M_X64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// clang-format off
#include <dbghelp.h>
#include <shellapi.h>
// clang-format on

#include <algorithm>
#include <map>
#elif !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace mpt::dump_lane {

// The --crash-report FILE of this run, for the in-process Linux report (stack_chk_fail.cpp). Fixed
// size: the failure path cannot allocate.
inline char g_crash_report_path[1024] = {};

namespace supervise_detail {

// The file name of a path, without its folders: reports never carry the user's paths.
inline std::string base_name(const std::string& path) {
  const std::size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline bool write_report_file(const std::filesystem::path& file, const std::string& report) {
  std::FILE* f = nullptr;
#if defined(_WIN32)
  if (_wfopen_s(&f, file.c_str(), L"wb") != 0) f = nullptr;
#else
  f = std::fopen(file.c_str(), "wb");
#endif
  if (f == nullptr) return false;
  const bool ok = std::fwrite(report.data(), 1, report.size(), f) == report.size();
  return std::fclose(f) == 0 && ok;
}

#if defined(_WIN32) && defined(_M_X64)

// The nearest function symbol at or below an RVA, from an image file's COFF symbol table.
class CoffSymbols {
 public:
  explicit CoffSymbols(const std::wstring& image) { load(image); }

  // "name+0xoff", or "" when the image has no symbol at or below `rva` in an executable section.
  std::string at(std::uint32_t rva) const {
    auto it = std::upper_bound(symbols_.begin(), symbols_.end(), rva,
                               [](std::uint32_t v, const Symbol& s) { return v < s.rva; });
    if (it == symbols_.begin()) return {};
    --it;
    char off[32];
    std::snprintf(off, sizeof off, "+0x%x", rva - it->rva);
    return it->name + off;
  }

 private:
  struct Symbol {
    std::uint32_t rva;
    std::string name;
  };
  std::vector<Symbol> symbols_;

  void load(const std::wstring& image) {
    HANDLE file = CreateFileW(image.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    std::vector<unsigned char> bytes;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1ll << 31)) {
      bytes.resize(static_cast<std::size_t>(size.QuadPart));
      DWORD read = 0;
      if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
        bytes.clear();
    }
    CloseHandle(file);
    parse(bytes);
  }

  void parse(const std::vector<unsigned char>& b) {
    auto fits = [&](std::size_t at, std::size_t n) { return at <= b.size() && n <= b.size() - at; };
    if (!fits(0, sizeof(IMAGE_DOS_HEADER))) return;
    IMAGE_DOS_HEADER dos;
    std::memcpy(&dos, b.data(), sizeof dos);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return;
    const std::size_t nt = static_cast<std::size_t>(dos.e_lfanew);
    if (!fits(nt, 4 + sizeof(IMAGE_FILE_HEADER))) return;
    DWORD signature = 0;
    std::memcpy(&signature, b.data() + nt, 4);
    if (signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_FILE_HEADER fh;
    std::memcpy(&fh, b.data() + nt + 4, sizeof fh);
    if (fh.PointerToSymbolTable == 0 || fh.NumberOfSymbols == 0) return;
    const std::size_t sections_at = nt + 4 + sizeof fh + fh.SizeOfOptionalHeader;
    if (!fits(sections_at, std::size_t{fh.NumberOfSections} * sizeof(IMAGE_SECTION_HEADER))) return;
    std::vector<IMAGE_SECTION_HEADER> sections(fh.NumberOfSections);
    std::memcpy(sections.data(), b.data() + sections_at, sections.size() * sizeof(IMAGE_SECTION_HEADER));
    const std::size_t table = fh.PointerToSymbolTable;
    const std::size_t count = fh.NumberOfSymbols;
    if (!fits(table, count * IMAGE_SIZEOF_SYMBOL)) return;
    const std::size_t strings = table + count * IMAGE_SIZEOF_SYMBOL;
    std::uint32_t strings_size = 0;
    if (fits(strings, 4)) std::memcpy(&strings_size, b.data() + strings, 4);
    for (std::size_t i = 0; i < count; ++i) {
      IMAGE_SYMBOL s;
      std::memcpy(&s, b.data() + table + i * IMAGE_SIZEOF_SYMBOL, IMAGE_SIZEOF_SYMBOL);
      const std::size_t aux = s.NumberOfAuxSymbols;
      const bool function = ISFCN(s.Type);
      const bool placed = s.SectionNumber > 0 && static_cast<std::size_t>(s.SectionNumber) <= sections.size();
      const bool storage = s.StorageClass == IMAGE_SYM_CLASS_EXTERNAL || s.StorageClass == IMAGE_SYM_CLASS_STATIC;
      if (function && placed && storage) {
        const IMAGE_SECTION_HEADER& sec = sections[static_cast<std::size_t>(s.SectionNumber) - 1];
        if ((sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
          std::string name;
          if (s.N.Name.Short != 0) {
            name.assign(reinterpret_cast<const char*>(s.N.ShortName), strnlen(reinterpret_cast<const char*>(s.N.ShortName), 8));
          } else if (s.N.Name.Long >= 4 && s.N.Name.Long < strings_size && fits(strings, strings_size)) {
            const char* p = reinterpret_cast<const char*>(b.data() + strings + s.N.Name.Long);
            name.assign(p, strnlen(p, strings_size - s.N.Name.Long));
          }
          if (!name.empty()) symbols_.push_back({sec.VirtualAddress + s.Value, std::move(name)});
        }
      }
      i += aux;
    }
    std::sort(symbols_.begin(), symbols_.end(), [](const Symbol& x, const Symbol& y) { return x.rva < y.rva; });
  }
};

inline std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// One frame of a report: the address as module+offset and its nearest symbol.
struct Namer {
  HANDLE process;
  std::map<std::wstring, CoffSymbols> coff;  // per image file

  struct Name {
    std::string module;  // file name only; "" outside any module
    DWORD64 offset = 0;
    std::string symbol;  // "" when none is known
  };

  Name name(DWORD64 pc) {
    Name n;
    IMAGEHLP_MODULEW64 info{};
    info.SizeOfStruct = sizeof info;
    if (!SymGetModuleInfoW64(process, pc, &info)) return n;
    const std::wstring image = info.ImageName;
    n.module = base_name(narrow(image));
    n.offset = pc - info.BaseOfImage;
    auto it = coff.find(image);
    if (it == coff.end()) it = coff.emplace(image, CoffSymbols(image)).first;
    n.symbol = it->second.at(static_cast<std::uint32_t>(n.offset));
    if (n.symbol.empty()) {
      alignas(SYMBOL_INFOW) unsigned char buffer[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)] = {};
      auto* sym = reinterpret_cast<SYMBOL_INFOW*>(buffer);
      sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
      sym->MaxNameLen = 255;
      DWORD64 displacement = 0;
      if (SymFromAddrW(process, pc, &displacement, sym)) {
        char off[32];
        std::snprintf(off, sizeof off, "+0x%llx", static_cast<unsigned long long>(displacement));
        n.symbol = narrow(std::wstring(sym->Name, sym->NameLen)) + off;
      }
    }
    return n;
  }

  std::string text(DWORD64 pc) {
    const Name n = name(pc);
    char line[64];
    if (n.module.empty()) {
      std::snprintf(line, sizeof line, "0x%016llx (no module)", static_cast<unsigned long long>(pc));
      return line;
    }
    std::snprintf(line, sizeof line, "+0x%llx", static_cast<unsigned long long>(n.offset));
    return n.module + line + (n.symbol.empty() ? "" : " " + n.symbol);
  }
};

// Whether a frame is the failure machinery rather than the code that failed: the system and C runtime
// DLLs, and the /GS and range-check reporters every image links statically.
inline bool is_failure_machinery(const Namer::Name& n) {
  std::string m = n.module;
  for (char& c : m) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  for (const char* dll : {"ntdll.dll", "kernelbase.dll", "kernel32.dll", "ucrtbase.dll", "ucrtbased.dll"})
    if (m == dll) return true;
  if (m.rfind("vcruntime", 0) == 0 || m.rfind("msvcp", 0) == 0) return true;
  for (const char* sym : {"__report_gsfailure", "__security_check_cookie", "__report_rangecheckfailure",
                          "__report_securityfailure", "__GSHandlerCheck", "_invoke_watson", "__fastfail"})
    if (n.symbol.rfind(sym, 0) == 0 && (n.symbol.size() == std::strlen(sym) || n.symbol[std::strlen(sym)] == '+'))
      return true;
  return false;
}

inline const char* status_name(DWORD code) {
  switch (code) {
    case STATUS_STACK_BUFFER_OVERRUN: return "STATUS_STACK_BUFFER_OVERRUN (fail-fast)";
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case 0xE06D7363u: return "unhandled C++ exception";
    case 0xC0000374u: return "STATUS_HEAP_CORRUPTION";
    default: return "exception";
  }
}

// The __fastfail codes (winnt.h FAST_FAIL_*) a crash in the lane is likely to carry.
inline const char* fail_fast_name(ULONG_PTR code) {
  switch (code) {
    case 0: return "legacy /GS violation";
    case 2: return "stack cookie check failure (/GS)";
    case 3: return "corrupt list entry";
    case 4: return "incorrect stack";
    case 5: return "invalid argument";
    case 7: return "fatal app exit (abort)";
    case 8: return "range check failure";
    case 10: return "CFG indirect call check failure";
    case 13: return "invalid fiber switch";
    case 14: return "invalid set of context";
    case 18: return "invalid jump buffer";
    case 19: return "mrdata modified";
    default: return "other";
  }
}

// The report on a stopped child: status, breadcrumbs, the faulting address and the stack.
inline std::string exception_report(HANDLE process, DWORD thread_id, const EXCEPTION_RECORD& er, bool first_chance,
                                    const CrashCrumbs& crumbs) {
  std::string out = "dump lane crash report v1\n";
  char line[512];
  std::snprintf(line, sizeof line, "status 0x%08lx %s%s", static_cast<unsigned long>(er.ExceptionCode),
                status_name(er.ExceptionCode), first_chance ? "" : ", not handled");
  out += line;
  if (er.ExceptionCode == STATUS_STACK_BUFFER_OVERRUN && er.NumberParameters >= 1) {
    std::snprintf(line, sizeof line, ", fail-fast code %llu %s",
                  static_cast<unsigned long long>(er.ExceptionInformation[0]), fail_fast_name(er.ExceptionInformation[0]));
    out += line;
  } else if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
    const ULONG_PTR kind = er.ExceptionInformation[0];
    std::snprintf(line, sizeof line, ", %s 0x%016llx", kind == 0 ? "read" : kind == 1 ? "write" : "execute",
                  static_cast<unsigned long long>(er.ExceptionInformation[1]));
    out += line;
  }
  out += "\n";
  char crumb_text[512];
  format_crumbs(crumbs, crumb_text, sizeof crumb_text);
  out += crumb_text;

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  const bool symbols = SymInitializeW(process, nullptr, TRUE) != FALSE;
  Namer namer{process, {}};
  const DWORD64 fault = reinterpret_cast<DWORD64>(er.ExceptionAddress);
  out += "fault " + namer.text(fault) + "\n";

  std::vector<DWORD64> frames;
  HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, thread_id);
  if (thread != nullptr && symbols) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &ctx)) {
      STACKFRAME64 sf{};
      sf.AddrPC.Offset = ctx.Rip;
      sf.AddrPC.Mode = AddrModeFlat;
      sf.AddrFrame.Offset = ctx.Rbp;
      sf.AddrFrame.Mode = AddrModeFlat;
      sf.AddrStack.Offset = ctx.Rsp;
      sf.AddrStack.Mode = AddrModeFlat;
      for (int depth = 0; depth < 64; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctx, nullptr, &SymFunctionTableAccess64,
                         &SymGetModuleBase64, nullptr))
          break;
        if (sf.AddrPC.Offset == 0) break;
        if (!frames.empty() && frames.back() == sf.AddrPC.Offset && depth > 1) break;
        frames.push_back(sf.AddrPC.Offset);
      }
    }
  }
  if (thread != nullptr) CloseHandle(thread);
  // The first frame outside the failure machinery: for a /GS failure, the function whose cookie broke.
  for (const DWORD64 pc : frames) {
    if (!is_failure_machinery(namer.name(pc))) {
      out += "culprit " + namer.text(pc) + "\n";
      break;
    }
  }
  out += "stack\n";
  if (frames.empty()) out += "  (no stack: the thread's context could not be read)\n";
  for (std::size_t i = 0; i < frames.size(); ++i) {
    std::snprintf(line, sizeof line, "  #%-2zu ", i);
    out += line + namer.text(frames[i]) + "\n";
  }
  if (symbols) SymCleanup(process);
  out += "end of crash report\n";
  return out;
}

inline std::string exit_report(DWORD code, const CrashCrumbs& crumbs) {
  std::string out = "dump lane crash report v1\n";
  char line[256];
  if (code == 3)
    std::snprintf(line, sizeof line, "status exit code 3 (abort, std::terminate or a CRT invalid parameter; the "
                                     "in-process report on stderr has the stack)\n");
  else
    std::snprintf(line, sizeof line, "status exit code 0x%08lx with no exception the supervisor saw\n",
                  static_cast<unsigned long>(code));
  out += line;
  char crumb_text[512];
  format_crumbs(crumbs, crumb_text, sizeof crumb_text);
  out += crumb_text;
  out += "end of crash report\n";
  return out;
}

// Quoting for one argument of a command line that CommandLineToArgvW reads back unchanged.
inline void append_quoted(std::wstring& line, const std::wstring& arg) {
  if (!line.empty()) line += L' ';
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    line += arg;
    return;
  }
  line += L'"';
  for (std::size_t i = 0;; ++i) {
    std::size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      line.append(backslashes * 2, L'\\');
      break;
    }
    if (arg[i] == L'"') {
      line.append(backslashes * 2 + 1, L'\\');
      line += L'"';
    } else {
      line.append(backslashes, L'\\');
      line += arg[i];
    }
  }
  line += L'"';
}

inline int run_supervised(const std::vector<std::wstring>& child_args, const std::filesystem::path& report_file) {
  // The shared breadcrumbs, zeroed; the child writes them (crash_report.h).
  wchar_t mapping_name[96];
  std::swprintf(mapping_name, 96, L"Local\\mpt_dump_lane_crumbs_%lu", static_cast<unsigned long>(GetCurrentProcessId()));
  HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(CrashCrumbs), mapping_name);
  if (mapping == nullptr) {
    std::printf("dump lane: --supervise could not create the shared breadcrumbs (error %lu)\n", GetLastError());
    return 1;
  }
  auto* crumbs = static_cast<CrashCrumbs*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(CrashCrumbs)));
  if (crumbs == nullptr) {
    CloseHandle(mapping);
    std::printf("dump lane: --supervise could not map the shared breadcrumbs\n");
    return 1;
  }
  std::memset(crumbs, 0, sizeof *crumbs);
  SetEnvironmentVariableW(L"MPT_DUMP_LANE_CRASH_CRUMBS", mapping_name);
  SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1");  // the heap an unsupervised run gets

  wchar_t exe[MAX_PATH * 4];
  const DWORD exe_length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
  std::wstring line;
  append_quoted(line, std::wstring(exe, exe_length));
  for (const std::wstring& a : child_args) append_quoted(line, a);

  std::fflush(stdout);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe, line.data(), nullptr, nullptr, TRUE, DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &si, &pi)) {
    std::printf("dump lane: --supervise could not start the lane (error %lu)\n", GetLastError());
    UnmapViewOfFile(crumbs);
    CloseHandle(mapping);
    return 1;
  }
  DebugSetProcessKillOnExit(TRUE);  // if the supervisor dies, so does the child

  std::string report;
  bool loader_breakpoint_seen = false;
  DWORD exit_code = 1;
  for (bool done = false; !done;) {
    DEBUG_EVENT ev{};
    if (!WaitForDebugEvent(&ev, INFINITE)) {
      std::printf("dump lane: --supervise lost the lane (error %lu)\n", GetLastError());
      break;
    }
    DWORD reply = DBG_CONTINUE;
    switch (ev.dwDebugEventCode) {
      case EXCEPTION_DEBUG_EVENT: {
        const EXCEPTION_RECORD& er = ev.u.Exception.ExceptionRecord;
        const bool first = ev.u.Exception.dwFirstChance != 0;
        if (er.ExceptionCode == EXCEPTION_BREAKPOINT && first && !loader_breakpoint_seen) {
          loader_breakpoint_seen = true;  // the loader's breakpoint once the process starts
          break;
        }
        // A fail-fast reaches only the debugger, in either chance; anything else is fatal only
        // once the child has had its chance to handle it.
        if ((!first || er.ExceptionCode == STATUS_STACK_BUFFER_OVERRUN) && report.empty())
          report = exception_report(pi.hProcess, ev.dwThreadId, er, first, *crumbs);
        reply = DBG_EXCEPTION_NOT_HANDLED;
        break;
      }
      case CREATE_PROCESS_DEBUG_EVENT:
        if (ev.u.CreateProcessInfo.hFile != nullptr) CloseHandle(ev.u.CreateProcessInfo.hFile);
        break;
      case LOAD_DLL_DEBUG_EVENT:
        if (ev.u.LoadDll.hFile != nullptr) CloseHandle(ev.u.LoadDll.hFile);
        break;
      case EXIT_PROCESS_DEBUG_EVENT:
        if (ev.dwProcessId == pi.dwProcessId) {
          exit_code = ev.u.ExitProcess.dwExitCode;
          done = true;
        }
        break;
      default:
        break;
    }
    ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, reply);
  }
  if (report.empty() && (exit_code == 3 || exit_code >= 0x80000000u)) report = exit_report(exit_code, *crumbs);
  if (!report.empty()) {
    std::fputs(report.c_str(), stdout);
    if (!report_file.empty())
      std::printf("dump lane: crash report %s\n",
                  write_report_file(report_file, report) ? "written to --crash-report" : "could NOT be written");
  }
  std::fflush(stdout);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  UnmapViewOfFile(crumbs);
  CloseHandle(mapping);
  return static_cast<int>(exit_code);
}

#endif  // _WIN32 && _M_X64

}  // namespace supervise_detail

// Called first thing in main(). With --supervise (Windows x64): runs the lane as a supervised child
// and returns its exit code. Otherwise, or on other platforms, removes --supervise and --crash-report
// FILE from argv (the in-process report uses the file) and returns nullopt so main() goes on.
inline std::optional<int> supervise_if_asked(int& argc, char** argv, const std::filesystem::path& repo_root) {
  bool supervise = false;
  std::filesystem::path report_file;
  int kept = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--supervise") {
      supervise = true;
    } else if (arg == "--crash-report" && i + 1 < argc) {
      report_file = std::filesystem::path(argv[++i]);  // the ANSI argv on Windows
    } else {
      argv[kept++] = argv[i];
    }
  }
  if (!supervise && report_file.empty()) return std::nullopt;
  const GuardedPath guarded[] = {{"--crash-report", report_file}};
  if (const std::string refused = refuse_inside(guarded, repo_root); !refused.empty()) {
    std::printf("dump lane: %s\n", refused.c_str());
    return 1;
  }
  std::error_code ec;
  if (!report_file.empty()) std::filesystem::remove(report_file, ec);  // never an older run's report
#if defined(_WIN32) && defined(_M_X64)
  if (!supervise) {
    std::puts("dump lane: --crash-report needs --supervise on Windows");
    return 2;
  }
  // The child's arguments are the wide originals (non-ASCII paths survive), minus the two above.
  int wargc = 0;
  LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  std::vector<std::wstring> child;
  for (int i = 1; wargv != nullptr && i < wargc; ++i) {
    const std::wstring a = wargv[i];
    if (a == L"--supervise") continue;
    if (a == L"--crash-report" && i + 1 < wargc) {
      ++i;
      continue;
    }
    child.push_back(a);
  }
  if (wargv != nullptr) LocalFree(wargv);
  return supervise_detail::run_supervised(child, report_file);
#else
  argc = kept;
  argv[argc] = nullptr;
  if (supervise) std::puts("dump lane: --supervise runs in-process on this platform (__stack_chk_fail reports)");
  const std::string path = report_file.string();
  if (path.size() < sizeof g_crash_report_path) std::memcpy(g_crash_report_path, path.c_str(), path.size() + 1);
  return std::nullopt;
#endif
}

// `--crash-selftest MODULE gs|av|abort`: crash inside a synthetic stub module on purpose (see the top
// of this file). Returns nullopt when not asked, else an exit code (only on a bad request: a working
// self-test does not return).
inline std::optional<int> crash_selftest_if_asked(int argc, char** argv) {
  if (argc < 2 || std::strcmp(argv[1], "--crash-selftest") != 0) return std::nullopt;
  if (argc != 4) {
    std::puts("usage: mpt_dump_lane [--supervise [--crash-report FILE]] --crash-selftest MODULE gs|av|abort");
    return 2;
  }
  const std::string kind_name = argv[3];
  const int kind = kind_name == "gs" ? 0 : kind_name == "av" ? 1 : kind_name == "abort" ? 2 : -1;
  if (kind < 0) {
    std::printf("dump lane: unknown --crash-selftest kind '%s' (gs, av or abort)\n", kind_name.c_str());
    return 2;
  }
  using Crash = int (*)(int, int);
  Crash crash = nullptr;
#if defined(_WIN32)
  const std::filesystem::path module(argv[2]);
  HMODULE handle = LoadLibraryW(module.c_str());
  if (handle != nullptr) crash = reinterpret_cast<Crash>(reinterpret_cast<void*>(GetProcAddress(handle, "mpt_overrun_stub_crash")));
#else
  void* handle = dlopen(argv[2], RTLD_NOW);
  if (handle != nullptr)
    crash = reinterpret_cast<Crash>(reinterpret_cast<std::uintptr_t>(dlsym(handle, "mpt_overrun_stub_crash")));
#endif
  if (crash == nullptr) {
    std::puts("dump lane: --crash-selftest could not load the stub module or its mpt_overrun_stub_crash");
    return 1;
  }
  // Made-up breadcrumbs, so the check can see every field reach the report.
  crash_note_run("selftest", 7);
  crash_note_party("minigame", "selftestDll", 3, 4, 20);
  crash_note_checkpoint(1200, 0x0123456789abcdefull);
  crash_note_frame(1234);
  std::printf("dump lane: crash self-test, kind %s\n", kind_name.c_str());
  std::fflush(stdout);
  const int r = crash(kind, 64);
  std::printf("dump lane: the stub returned %d instead of crashing\n", r);
  return 1;
}

}  // namespace mpt::dump_lane
