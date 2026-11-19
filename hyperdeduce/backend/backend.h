#ifndef HYPERDEDUCE_BACKEND_BACKEND_H_
#define HYPERDEDUCE_BACKEND_BACKEND_H_

#include <string>
#include <unordered_map>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/catalog/dictionary.h"
#include "hyperdeduce/catalog/relation.h"
#include "hyperdeduce/plan/physical_plan.h"

namespace hyperdeduce {

// Read-only snapshots of the relations a plan scans. The engine fills this
// before each evaluation step; a backend never mutates the catalog through it.
// Each name maps to a full relation and, for derived predicates, a delta.
class ExecutionContext {
public:
  explicit ExecutionContext(const Dictionary *dictionary)
      : dictionary_(dictionary) {}

  void AddRelation(const std::string &name, const Relation *full,
                   const Relation *delta) {
    relations_[name] = Entry{full, delta};
  }

  const Relation *GetRelation(const std::string &name, bool delta) const {
    auto it = relations_.find(name);
    if (it == relations_.end()) {
      return nullptr;
    }
    if (delta && it->second.delta != nullptr) {
      return it->second.delta;
    }
    return it->second.full;
  }

  const Dictionary &dictionary() const { return *dictionary_; }

private:
  struct Entry {
    const Relation *full = nullptr;
    const Relation *delta = nullptr;
  };
  std::unordered_map<std::string, Entry> relations_;
  const Dictionary *dictionary_;
};

// A backend interprets a physical plan against input snapshots and returns the
// derived tuples. It is a pure function: no catalog mutation, no global state.
// The CPU backend uses TBB; the GPU backend uses CUDA across devices. The
// fixpoint driver is backend-agnostic.
class Backend {
public:
  virtual ~Backend() = default;

  virtual std::string name() const = 0;

  virtual StatusOr<Relation> Execute(const PhysicalOp &plan,
                                     const ExecutionContext &context) = 0;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BACKEND_BACKEND_H_