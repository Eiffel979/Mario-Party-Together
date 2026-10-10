#include "mpt/core/session.h"

#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace mpt {

namespace {

// Titles with a live Session in this process. Session spawns no threads; the mutex only
// keeps the set safe if callers start sessions from different threads.
std::mutex g_live_mutex;
std::set<TitleId> g_live_titles;

std::string title_name(TitleId title) { return std::string(to_string(title)); }

}  // namespace

std::optional<Session::TitleLease> Session::TitleLease::acquire(TitleId title) {
  const std::lock_guard lock(g_live_mutex);
  if (!g_live_titles.insert(title).second) return std::nullopt;
  return TitleLease(title);
}

Session::TitleLease::TitleLease(TitleLease&& other) noexcept : title_(std::exchange(other.title_, std::nullopt)) {}

Session::TitleLease::~TitleLease() {
  if (!title_) return;
  const std::lock_guard lock(g_live_mutex);
  g_live_titles.erase(*title_);
}

Session::Session(TitleLease lease, std::unique_ptr<ITitleRuntime> runtime, std::unique_ptr<ITitleAdapter> adapter)
    : lease_(std::move(lease)), runtime_(std::move(runtime)), adapter_(std::move(adapter)) {}

Session::~Session() {
  // Explicit, although member order already guarantees it: adapter before runtime.
  adapter_.reset();
  runtime_.reset();
}

std::variant<std::unique_ptr<Session>, Result> Session::start(const TitleRegistry& registry,
                                                              const SessionConfig& config) {
  const TitleId title = config.dump.title;
  if (!registry.contains(title)) return Result::failure("title " + title_name(title) + " is not registered");

  // Reserve the title before creating anything: a second runtime instance may already
  // corrupt process-global state when it is constructed.
  std::optional<TitleLease> lease = TitleLease::acquire(title);
  if (!lease) return Result::failure("a session of " + title_name(title) + " is already running in this process");

  std::unique_ptr<ITitleRuntime> runtime = registry.createRuntime(title);
  if (!runtime) return Result::failure("the runtime factory for " + title_name(title) + " returned null");
  if (runtime->id() != title) {
    return Result::failure("the runtime registered for " + title_name(title) + " reports " + title_name(runtime->id()));
  }
  if (Result booted = runtime->boot(config.dump, config.runtime); !booted) {
    return Result::failure("boot failed: " + booted.error());
  }

  std::unique_ptr<ITitleAdapter> adapter = registry.createAdapter(title, *runtime);
  if (!adapter) return Result::failure("the adapter factory for " + title_name(title) + " returned null");
  if (config.rules) {
    if (Result applied = adapter->applyRuleset(*config.rules); !applied) {
      return Result::failure("ruleset rejected: " + applied.error());
    }
  }

  std::unique_ptr<Session> session(new Session(std::move(*lease), std::move(runtime), std::move(adapter)));
  session->director_.resync(0, session->adapter_->readParty());
  return session;
}

Result Session::step(const InputFrame& inputs) {
  if (inputs.frame != frame_) {
    return Result::failure("input for frame " + std::to_string(inputs.frame) + " given at frame " +
                           std::to_string(frame_));
  }
  const std::uint64_t generation_before = runtime_->saveMediaGeneration();
  runtime_->stepFrame(inputs);
  ++frame_;
  if (runtime_->saveMediaGeneration() != generation_before) pending_save_media_ = runtime_->saveMedia();
  director_.observe(inputs.frame, adapter_->readParty());
  return Result::success();
}

Result Session::loadState(const SaveState& state) {
  if (Result loaded = runtime_->loadState(state); !loaded) return loaded;
  frame_ = state.frame;
  // The restored generation is the new baseline: the next step() compares against it,
  // and nothing here counts as a write (ADR 0004 rule 10).
  director_.resync(state.frame, adapter_->readParty());
  return Result::success();
}

std::optional<SaveMedia> Session::takeSaveMedia() { return std::exchange(pending_save_media_, std::nullopt); }

}  // namespace mpt
