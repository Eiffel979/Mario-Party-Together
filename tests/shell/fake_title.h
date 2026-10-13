#pragma once

// Test-only shell registration (P1-06): FakeRuntime + FakeAdapter as "mp4", with a
// synthetic VerifiedDump so no real dump is ever needed. Used by mpt_shell_fake and by
// the replay generator for the shell smoke tests.

#include <memory>

#include "../support/fake_adapter.h"
#include "../support/fake_runtime.h"
#include "titles.h"

namespace mpt::test {

inline VerifiedDump fake_shell_dump() {
  VerifiedDump dump;
  dump.title = TitleId::mp4;
  dump.region = "USA";
  dump.revision = "1.0";
  dump.sha256 = "f00df00df00df00df00df00df00df00df00df00df00df00df00df00df00df00d";
  return dump;
}

inline shell::ShellTitle fake_shell_title() {
  shell::ShellTitle title;
  title.id = TitleId::mp4;
  title.runtime = [] { return std::make_unique<FakeRuntime>(); };
  title.adapter = &FakeAdapter::create;
  title.synthetic_dump = fake_shell_dump();
  return title;
}

}  // namespace mpt::test
