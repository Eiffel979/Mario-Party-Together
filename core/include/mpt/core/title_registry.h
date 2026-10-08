#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "mpt/core/title_adapter.h"
#include "mpt/core/title_runtime.h"

namespace mpt {

// Maps each TitleId to the factories that build its runtime and adapter.
// Runtimes register here; core never names a concrete title implementation.
class TitleRegistry {
 public:
  using RuntimeFactory = std::function<std::unique_ptr<ITitleRuntime>()>;
  using AdapterFactory = std::function<std::unique_ptr<ITitleAdapter>(ITitleRuntime&)>;

  // Fails if `title` is already registered.
  Result add(TitleId title, RuntimeFactory runtime, AdapterFactory adapter);

  bool contains(TitleId title) const;
  std::vector<TitleId> titles() const;  // sorted by TitleId

  std::unique_ptr<ITitleRuntime> createRuntime(TitleId title) const;
  std::unique_ptr<ITitleAdapter> createAdapter(TitleId title, ITitleRuntime& runtime) const;

 private:
  struct Entry {
    RuntimeFactory runtime;
    AdapterFactory adapter;
  };
  std::map<TitleId, Entry> entries_;
};

}  // namespace mpt
