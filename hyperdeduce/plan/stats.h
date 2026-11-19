#ifndef HYPERDEDUCE_PLAN_STATS_H_
#define HYPERDEDUCE_PLAN_STATS_H_

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/catalog/catalog.h"

namespace hyperdeduce {

struct RelationStats {
  std::size_t rows = 0;
  std::vector<std::size_t> distinct; // distinct cell count per column
};

// Data-aware planning input: row counts and per-column distinct counts. The
// optimizer uses these to estimate selection selectivity and join output sizes.
class Statistics {
public:
  void Compute(const Catalog &catalog);

  const RelationStats *Find(const std::string &name) const;

private:
  std::unordered_map<std::string, RelationStats> stats_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PLAN_STATS_H_