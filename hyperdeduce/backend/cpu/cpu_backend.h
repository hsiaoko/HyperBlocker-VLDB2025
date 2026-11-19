#ifndef HYPERDEDUCE_BACKEND_CPU_CPU_BACKEND_H_
#define HYPERDEDUCE_BACKEND_CPU_CPU_BACKEND_H_

#include <string>

#include "hyperdeduce/backend/backend.h"

namespace hyperdeduce {

// Interprets a physical plan on the CPU. Scan copies, filter masks, hash-join
// probes, and projections run through TBB across cores. Deduplication uses a
// hash set. The backend holds no state between calls.
class CpuBackend : public Backend {
public:
  std::string name() const override { return "cpu"; }

  StatusOr<Relation> Execute(const PhysicalOp &plan,
                             const ExecutionContext &context) override;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BACKEND_CPU_CPU_BACKEND_H_