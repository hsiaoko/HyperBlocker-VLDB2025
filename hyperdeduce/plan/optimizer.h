#ifndef HYPERDEDUCE_PLAN_OPTIMIZER_H_
#define HYPERDEDUCE_PLAN_OPTIMIZER_H_

#include <memory>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/catalog/catalog.h"
#include "hyperdeduce/parser/ast.h"
#include "hyperdeduce/plan/physical_plan.h"
#include "hyperdeduce/plan/stats.h"

namespace hyperdeduce {

// Compiles one rule into a physical operator tree. The planner resolves every
// variable to a column index, pushes constant arguments into scan selections,
// and chooses a data-aware join order from the statistics.
//
// delta_relational_index selects which relational body atom scans its delta
// instead of its full relation, for semi-naive evaluation. Pass -1 to scan every
// relation in full. The index counts relational body atoms in source order.
StatusOr<std::unique_ptr<PhysicalOp>> BuildRulePlan(
    const Rule &rule, const Catalog &catalog, const Statistics &stats,
    Dictionary &dictionary, int delta_relational_index = -1);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PLAN_OPTIMIZER_H_