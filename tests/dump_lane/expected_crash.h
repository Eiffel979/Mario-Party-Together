#pragma once
// For the lane's self-tests that crash, abort or fail fast on purpose (`--crash-selftest`,
// `--guard-reload-selftest`): a Windows Debug CRT would stop such a run on a modal dialog ("abort()
// has been called", an assertion, Windows Error Reporting), which blocks ctest on the desktop until
// someone clicks it. Called only once a self-test has been asked for, never for a normal lane run, so
// a real assertion anywhere else still stops on its dialog. The messages still go to stderr.
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mpt::dump_lane {

inline void expect_crash_without_dialog() {
#if defined(_WIN32) && defined(_DEBUG)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
}

}  // namespace mpt::dump_lane
