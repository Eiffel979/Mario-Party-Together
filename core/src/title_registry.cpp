#include "mpt/core/title_registry.h"

#include <string>

namespace mpt {

Result TitleRegistry::add(TitleId title, RuntimeFactory runtime, AdapterFactory adapter) {
  if (!runtime || !adapter) {
    return Result::failure("title " + std::string(to_string(title)) + " registered without factories");
  }
  const bool inserted = entries_.emplace(title, Entry{std::move(runtime), std::move(adapter)}).second;
  if (!inserted) return Result::failure("title " + std::string(to_string(title)) + " is already registered");
  return Result::success();
}

bool TitleRegistry::contains(TitleId title) const { return entries_.contains(title); }

std::vector<TitleId> TitleRegistry::titles() const {
  std::vector<TitleId> out;
  out.reserve(entries_.size());
  for (const auto& [title, entry] : entries_) out.push_back(title);
  return out;
}

std::unique_ptr<ITitleRuntime> TitleRegistry::createRuntime(TitleId title) const {
  const auto it = entries_.find(title);
  return it == entries_.end() ? nullptr : it->second.runtime();
}

std::unique_ptr<ITitleAdapter> TitleRegistry::createAdapter(TitleId title, ITitleRuntime& runtime) const {
  const auto it = entries_.find(title);
  return it == entries_.end() ? nullptr : it->second.adapter(runtime);
}

}  // namespace mpt
