#include "mpt/ui/model/import_flow.h"

#include <algorithm>
#include <utility>

#include "mpt/asset_import/dump_format.h"
#include "mpt/asset_import/known_dumps.h"
#include "mpt/asset_import/process.h"

namespace mpt::ui {

namespace ai = asset_import;

std::string heading_for(RejectKind kind) {
  switch (kind) {
    case RejectKind::not_a_dump: return "That file is not a game dump";
    case RejectKind::compressed: return "Compressed disc images are not supported yet";
    case RejectKind::unsupported_version: return "This version is not supported yet";
    case RejectKind::not_verified: return "That dump could not be verified";
    case RejectKind::cannot_run: return "This build cannot run that game yet";
    case RejectKind::build_failed: return "The local build failed";
    case RejectKind::cancelled: return "Import cancelled";
    case RejectKind::changed: return "That file has changed";
  }
  return "That dump was not accepted";
}

Rejection classify(const ai::IdentifyReport& report) {
  Rejection r;
  if (report.format == ai::DumpFormat::unknown) {
    r.kind = RejectKind::not_a_dump;
  } else if (ai::is_compressed_container(report.format)) {
    r.kind = RejectKind::compressed;
  } else if (report.unsupported) {
    r.kind = RejectKind::unsupported_version;
  } else {
    r.kind = RejectKind::not_verified;
  }
  r.heading = heading_for(r.kind);
  r.message = report.message.empty() ? "This dump is not in the known-good list." : report.message;
  return r;
}

double ImportProgress::overall() const {
  // Stage 0 is identification; the build's stages follow it.
  if (stage_count == 0) return std::clamp(fraction, 0.0, 1.0);
  const double done = static_cast<double>(stage_index) + std::clamp(fraction, 0.0, 1.0);
  return std::clamp(done / static_cast<double>(stage_count), 0.0, 1.0);
}

ImportFlow::ImportFlow(ImportBackend& backend, bool run_inline) : backend_(backend), run_inline_(run_inline) {}

ImportFlow::~ImportFlow() {
  cancel_ = true;
  backend_.interrupt();
  join();
}

void ImportFlow::join() {
  if (worker_.joinable()) worker_.join();
}

void ImportFlow::start(const std::filesystem::path& path, std::string expected_sha256) {
  if (busy()) return;
  join();
  {
    const std::lock_guard lock(mutex_);
    events_.clear();
  }
  cancel_ = false;
  state_ = State::identifying;
  path_ = path;
  progress_ = ImportProgress{"Checking the dump", 0, 0, 0.0, {}};
  identified_.reset();
  rejection_.reset();
  result_.reset();
  if (run_inline_) {
    work(path, std::move(expected_sha256));
  } else {
    worker_ = std::thread([this, path, sha = std::move(expected_sha256)]() mutable { work(path, std::move(sha)); });
  }
}

void ImportFlow::cancel() {
  if (!busy()) return;
  cancel_ = true;
  state_ = State::cancelling;
  backend_.interrupt();
}

void ImportFlow::reset() {
  if (busy()) return;
  join();
  state_ = State::idle;
  progress_ = {};
  identified_.reset();
  rejection_.reset();
  result_.reset();
}

void ImportFlow::post(Event event) {
  const std::lock_guard lock(mutex_);
  events_.push_back(std::move(event));
}

bool ImportFlow::poll() {
  std::deque<Event> events;
  {
    const std::lock_guard lock(mutex_);
    events.swap(events_);
  }
  for (Event& event : events) {
    if (auto* identified = std::get_if<Identified>(&event)) {
      identified_ = identified->known;
      if (state_ == State::identifying) state_ = State::building;
    } else if (auto* progressed = std::get_if<Progressed>(&event)) {
      const ai::BuildProgress& p = progressed->progress;
      // Identification is stage 0 of the bar; the build's stages come after it.
      progress_ = ImportProgress{p.stage, p.stage_index + 1, p.stage_count + 1, p.fraction, p.detail};
    } else if (auto* finished = std::get_if<Finished>(&event)) {
      if (auto* result = std::get_if<ImportResult>(&finished->outcome)) {
        result_ = std::move(*result);
        state_ = State::done;
      } else {
        rejection_ = std::get<Rejection>(std::move(finished->outcome));
        state_ = State::rejected;
      }
    }
  }
  if (!events.empty() && !busy()) join();
  return !events.empty();
}

void ImportFlow::work(std::filesystem::path path, std::string expected_sha256) {
  const auto reject = [this](RejectKind kind, std::string message) {
    post(Finished{Rejection{kind, heading_for(kind), std::move(message)}});
  };

  const ai::IdentifyReport report = backend_.identify(path);
  if (!report.match) {
    post(Finished{classify(report)});
    return;
  }
  const ai::KnownDump& known = *report.match;
  if (!expected_sha256.empty() && report.sha256 != expected_sha256) {
    reject(RejectKind::changed, "This file is now a different dump from the one you added. Add it again.");
    return;
  }
  post(Identified{known});
  if (cancel_) {
    reject(RejectKind::cancelled, "Nothing was built.");
    return;
  }
  if (std::string why = backend_.cannot_build(known.title); !why.empty()) {
    reject(RejectKind::cannot_run, std::move(why));
    return;
  }

  std::string error;
  const ai::ProgressCallback progress = [this](const ai::BuildProgress& p) { post(Progressed{p}); };
  const std::function<bool()> cancelled = [this] { return cancel_.load(); };
  std::optional<ai::BuildOutcome> outcome = backend_.build(report, path, progress, cancelled, error);
  if (!outcome) {
    if (cancel_) {
      reject(RejectKind::cancelled, "The build was stopped. Nothing was kept.");
    } else {
      reject(RejectKind::build_failed, error.empty() ? std::string("The build did not finish.") : error);
    }
    return;
  }
  std::optional<VerifiedDump> verified = ai::make_verified(report, *outcome, error);
  if (!verified) {
    reject(RejectKind::build_failed, error);
    return;
  }
  post(Finished{ImportResult{std::move(*verified), known, path, outcome->reused}});
}

// ---------------------------------------------------------------------------------------

AssetImportBackend::AssetImportBackend(std::filesystem::path known_dumps, std::filesystem::path cache_dir,
                                       RecipeFactory recipe)
    : known_dumps_(std::move(known_dumps)), cache_dir_(std::move(cache_dir)), recipe_(std::move(recipe)) {}

ai::IdentifyReport AssetImportBackend::identify(const std::filesystem::path& path) {
  std::string error;
  std::optional<ai::KnownDumps> known = ai::KnownDumps::load(known_dumps_, error);
  if (!known) {
    ai::IdentifyReport report;
    report.message = "The list of known dumps could not be read: " + error;
    return report;
  }
  const ai::Importer importer(std::move(*known));
  return importer.identify(path);
}

std::string AssetImportBackend::cannot_build(TitleId title) {
  const std::string name(ai::title_display_name(title));
  if (!recipe_ || !recipe_(title)) {
    return name + " is a verified dump, but this build of Mario Party Together has no runtime for it yet.";
  }
  return {};
}

std::optional<ai::BuildOutcome> AssetImportBackend::build(const ai::IdentifyReport& report,
                                                          const std::filesystem::path& path,
                                                          const ai::ProgressCallback& progress,
                                                          const std::function<bool()>& cancelled,
                                                          std::string& error) {
  const std::unique_ptr<ai::BuildRecipe> recipe = recipe_ ? recipe_(report.match->title) : nullptr;
  if (!recipe) {
    error = "This build has no local build recipe for that game.";
    return std::nullopt;
  }
  std::optional<ai::DumpForBuild> dump = ai::dump_for_build(report, path, error);
  if (!dump) return std::nullopt;
  ai::SystemProcessRunner runner;
  ai::BuildCache cache(cache_dir_, runner);
  ai::BuildRequest request;
  request.dump = std::move(*dump);
  request.progress = progress;
  request.cancelled = cancelled;
  return cache.build(*recipe, request, error);
}

}  // namespace mpt::ui
