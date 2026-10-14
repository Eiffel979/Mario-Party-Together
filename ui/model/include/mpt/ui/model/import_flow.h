#pragma once

// The "add your game dump" flow (ADR 0010 §4, P1-23): identify, build with progress,
// then a VerifiedDump from asset_import::make_verified, or a specific rejection (E11).
//
// The slow work (hashing a 1.4 GB disc, the local build) runs on a worker thread. The
// worker only posts events; poll() applies them on the main thread, so every accessor
// here is main-thread only. Plain C++: no RmlUi, no SDL.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

#include "mpt/asset_import/build.h"
#include "mpt/asset_import/importer.h"
#include "mpt/core/dump.h"
#include "mpt/core/title_id.h"

namespace mpt::ui {

// What the flow needs from asset-import. Called on the worker thread, one call at a time.
class ImportBackend {
 public:
  virtual ~ImportBackend() = default;
  virtual asset_import::IdentifyReport identify(const std::filesystem::path& path) = 0;
  // Empty when this build can build `title`, otherwise the reason it cannot.
  virtual std::string cannot_build(TitleId title) = 0;
  virtual std::optional<asset_import::BuildOutcome> build(const asset_import::IdentifyReport& report,
                                                          const std::filesystem::path& path,
                                                          const asset_import::ProgressCallback& progress,
                                                          const std::function<bool()>& cancelled,
                                                          std::string& error) = 0;
};

// Why a dump was not accepted. Each kind has its own heading; the message under it is
// asset-import's specific text (E11) or the build error.
enum class RejectKind {
  not_a_dump,           // unreadable, or not an N64/GameCube/Wii image at all
  compressed,           // RVZ, WIA, GCZ, WBFS, CISO (D9: plain .iso only)
  unsupported_version,  // a known revision or region that is not supported yet
  not_verified,         // not the known-good dump: bad, trimmed, altered or unknown
  cannot_run,           // verified, but this build has no runtime or recipe for it
  build_failed,
  cancelled,
  changed,              // re-checking a library entry: the file is no longer that dump
};

struct Rejection {
  RejectKind kind = RejectKind::not_a_dump;
  std::string heading;  // short, for the screen title
  std::string message;  // the specific reason
};

// Sorts an identify report that has no match into a rejection.
Rejection classify(const asset_import::IdentifyReport& report);
std::string heading_for(RejectKind kind);

struct ImportProgress {
  std::string stage;  // "Checking the dump", then asset-import's build stages
  std::size_t stage_index = 0;
  std::size_t stage_count = 0;
  double fraction = 0.0;  // within the stage, 0..1
  std::string detail;

  // Overall 0..1, for one progress bar.
  double overall() const;
};

struct ImportResult {
  VerifiedDump dump;
  asset_import::KnownDump known;  // region, revision and the display name come from here
  std::filesystem::path path;     // the user's file
  bool reused_build = false;
};

class ImportFlow {
 public:
  enum class State { idle, identifying, building, cancelling, done, rejected };

  // `run_inline` runs the work on the calling thread inside start() (tests). Otherwise a
  // worker thread does it and poll() picks up the results.
  explicit ImportFlow(ImportBackend& backend, bool run_inline = false);
  ~ImportFlow();  // cancels and joins the worker
  ImportFlow(const ImportFlow&) = delete;
  ImportFlow& operator=(const ImportFlow&) = delete;

  // Starts on `path`. With `expected_sha256` (re-checking a library entry), a different
  // dump is rejected as `changed`. Ignored while busy().
  void start(const std::filesystem::path& path, std::string expected_sha256 = {});
  // Asks the build to stop. Identification of a large file finishes first.
  void cancel();
  // Applies the worker's events. True if anything changed.
  bool poll();
  // Back to idle once done or rejected.
  void reset();

  State state() const { return state_; }
  bool busy() const { return state_ == State::identifying || state_ == State::building || state_ == State::cancelling; }
  const std::filesystem::path& path() const { return path_; }
  const ImportProgress& progress() const { return progress_; }
  // Set once identification matched a supported dump ("Mario Party 4 (USA, Rev 0)").
  const std::optional<asset_import::KnownDump>& identified() const { return identified_; }
  const std::optional<Rejection>& rejection() const { return rejection_; }
  const std::optional<ImportResult>& result() const { return result_; }

 private:
  struct Identified {
    asset_import::KnownDump known;
  };
  struct Progressed {
    asset_import::BuildProgress progress;
  };
  struct Finished {
    std::variant<ImportResult, Rejection> outcome;
  };
  using Event = std::variant<Identified, Progressed, Finished>;

  void work(std::filesystem::path path, std::string expected_sha256);
  void post(Event event);
  void join();

  ImportBackend& backend_;
  bool run_inline_;
  std::thread worker_;
  std::atomic<bool> cancel_{false};
  std::mutex mutex_;
  std::deque<Event> events_;

  State state_ = State::idle;
  std::filesystem::path path_;
  ImportProgress progress_;
  std::optional<asset_import::KnownDump> identified_;
  std::optional<Rejection> rejection_;
  std::optional<ImportResult> result_;
};

// The backend the launcher uses: asset-import's Importer and BuildCache with the shell's
// recipe for each title.
class AssetImportBackend final : public ImportBackend {
 public:
  using RecipeFactory = std::function<std::unique_ptr<asset_import::BuildRecipe>(TitleId)>;
  AssetImportBackend(std::filesystem::path known_dumps, std::filesystem::path cache_dir, RecipeFactory recipe);

  asset_import::IdentifyReport identify(const std::filesystem::path& path) override;
  std::string cannot_build(TitleId title) override;
  std::optional<asset_import::BuildOutcome> build(const asset_import::IdentifyReport& report,
                                                  const std::filesystem::path& path,
                                                  const asset_import::ProgressCallback& progress,
                                                  const std::function<bool()>& cancelled, std::string& error) override;

 private:
  std::filesystem::path known_dumps_;
  std::filesystem::path cache_dir_;
  RecipeFactory recipe_;
};

}  // namespace mpt::ui
