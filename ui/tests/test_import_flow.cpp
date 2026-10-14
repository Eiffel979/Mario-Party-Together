// The import flow against a fake backend: synthetic reports and builds only, no dump.

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "mpt/ui/model/import_flow.h"

using namespace mpt;
using namespace mpt::ui;
namespace ai = mpt::asset_import;
namespace fs = std::filesystem;

namespace {

const std::string kSha(64, 'e');

ai::KnownDump known_mp4() {
  ai::KnownDump k;
  k.title = TitleId::mp4;
  k.region = "USA";
  k.revision = "Rev 0";
  k.supported = true;
  k.sha256 = kSha;
  k.size = 1234;
  return k;
}

class FakeBackend final : public ImportBackend {
 public:
  ai::IdentifyReport report;
  std::string cannot;
  bool build_fails = false;
  bool wait_for_cancel = false;
  fs::path build_dir;
  int builds = 0;

  ai::IdentifyReport identify(const fs::path&) override { return report; }
  std::string cannot_build(TitleId) override { return cannot; }
  std::optional<ai::BuildOutcome> build(const ai::IdentifyReport&, const fs::path&, const ai::ProgressCallback& progress,
                                        const std::function<bool()>& cancelled, std::string& error) override {
    ++builds;
    progress(ai::BuildProgress{"Compiling", 0, 2, 0.5, "half"});
    if (wait_for_cancel) {
      while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      error = "cancelled";
      return std::nullopt;
    }
    if (build_fails) {
      error = "the compiler was not found";
      return std::nullopt;
    }
    progress(ai::BuildProgress{"Finishing", 1, 2, 1.0, ""});
    ai::BuildOutcome outcome;
    outcome.build_dir = build_dir;
    outcome.manifest.dump_sha256 = kSha;
    return outcome;
  }
};

fs::path fake_build_dir() {
  const fs::path dir = fs::temp_directory_path() / "mpt_ui_model_tests" / "build";
  fs::create_directories(dir);
  std::ofstream(dir / "manifest.json") << "{}";
  return dir;
}

ai::IdentifyReport matched() {
  ai::IdentifyReport r;
  r.format = ai::DumpFormat::gc_disc;
  r.sha256 = kSha;
  r.match = known_mp4();
  return r;
}

}  // namespace

TEST_CASE("a verified dump goes identify -> build -> done with a VerifiedDump from make_verified") {
  FakeBackend backend;
  backend.report = matched();
  backend.build_dir = fake_build_dir();
  ImportFlow flow(backend, /*run_inline=*/true);
  flow.start("disc.iso");
  CHECK(flow.state() == ImportFlow::State::identifying);  // events not applied yet
  CHECK(flow.poll());
  REQUIRE(flow.state() == ImportFlow::State::done);
  REQUIRE(flow.result());
  CHECK(flow.result()->dump.title == TitleId::mp4);
  CHECK(flow.result()->dump.build_dir == backend.build_dir);
  CHECK(flow.result()->path == "disc.iso");
  CHECK(flow.identified()->region == "USA");
  CHECK(flow.progress().stage == "Finishing");
  CHECK(flow.progress().overall() == doctest::Approx(1.0));
}

TEST_CASE("each kind of rejection has its own heading and keeps asset-import's message (E11)") {
  FakeBackend backend;
  ImportFlow flow(backend, true);

  SUBCASE("not a dump") {
    backend.report.format = ai::DumpFormat::unknown;
    backend.report.message = "Not a recognized N64, GameCube or Wii dump.";
    flow.start("x.bin");
    flow.poll();
    REQUIRE(flow.rejection());
    CHECK(flow.rejection()->kind == RejectKind::not_a_dump);
    CHECK(flow.rejection()->message == backend.report.message);
  }
  SUBCASE("compressed container") {
    backend.report.format = ai::DumpFormat::rvz;
    backend.report.message = "rvz images are not supported yet";
    flow.start("x.rvz");
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::compressed);
  }
  SUBCASE("known but unsupported revision") {
    backend.report.format = ai::DumpFormat::gc_disc;
    backend.report.unsupported = known_mp4();
    backend.report.message = "This is Mario Party 4 (USA, Rev 1). This revision is not supported yet.";
    flow.start("x.iso");
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::unsupported_version);
    CHECK(flow.rejection()->heading == heading_for(RejectKind::unsupported_version));
  }
  SUBCASE("bad or truncated") {
    backend.report.format = ai::DumpFormat::gc_disc;
    backend.report.message = "probably truncated or trimmed";
    flow.start("x.iso");
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::not_verified);
  }
  SUBCASE("no runtime in this build") {
    backend.report = matched();
    backend.cannot = "no runtime for it yet";
    flow.start("x.iso");
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::cannot_run);
    CHECK(backend.builds == 0);
  }
  SUBCASE("build failure") {
    backend.report = matched();
    backend.build_fails = true;
    flow.start("x.iso");
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::build_failed);
    CHECK(flow.rejection()->message == "the compiler was not found");
  }
  SUBCASE("re-checking a library entry whose file changed") {
    backend.report = matched();
    flow.start("x.iso", std::string(64, 'f'));
    flow.poll();
    CHECK(flow.rejection()->kind == RejectKind::changed);
  }
  CHECK(flow.state() == ImportFlow::State::rejected);
  flow.reset();
  CHECK(flow.state() == ImportFlow::State::idle);
}

TEST_CASE("the worker thread reports progress and can be cancelled") {
  FakeBackend backend;
  backend.report = matched();
  backend.wait_for_cancel = true;
  ImportFlow flow(backend);
  flow.start("x.iso");
  CHECK(flow.busy());
  // Wait until the build has started (its first progress event arrives).
  for (int i = 0; i < 2000 && flow.progress().stage != "Compiling"; ++i) {
    flow.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(flow.progress().stage == "Compiling");
  CHECK(flow.state() == ImportFlow::State::building);
  flow.cancel();
  CHECK(flow.state() == ImportFlow::State::cancelling);
  for (int i = 0; i < 2000 && flow.busy(); ++i) {
    flow.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(flow.rejection());
  CHECK(flow.rejection()->kind == RejectKind::cancelled);
}

TEST_CASE("destroying the flow mid-build stops and joins the worker") {
  FakeBackend backend;
  backend.report = matched();
  backend.wait_for_cancel = true;
  {
    ImportFlow flow(backend);
    flow.start("x.iso");
  }
  CHECK(true);  // reaching here means the destructor did not hang
}
