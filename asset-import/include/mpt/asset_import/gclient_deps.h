#pragma once

// Reads a gclient DEPS file (Dawn's dependency list) without running it.
//
// DEPS is Python, but in practice it uses a small literal subset: assignments of
// strings, dicts, lists, True/False/None, '+' concatenation, Var('name') and
// Str('...'), plus "{name}" placeholders inside strings. This parser understands
// exactly that subset and fails on anything else, so an upstream format change
// stops the fetch with a message instead of being misread. Nothing is executed,
// and `fetch_dawn_dependencies.py` is never run (ADR 0009, amendment after P1-10).

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mpt::asset_import {

struct GclientDep {
  std::string url;     // with vars expanded; empty for CIPD packages
  std::string commit;  // the 40-hex commit after '@', or empty if the URL has none
  bool cipd = false;   // a CIPD package, not a git repository
};

// Parses DEPS text into its `deps` entries, keyed by path.
std::optional<std::map<std::string, GclientDep>> parse_gclient_deps(std::string_view text, std::string& error);

}  // namespace mpt::asset_import
