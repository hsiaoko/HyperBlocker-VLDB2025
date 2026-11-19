#ifndef HYPERDEDUCE_ENGINE_ENGINE_H_
#define HYPERDEDUCE_ENGINE_ENGINE_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hyperdeduce/backend/backend.h"
#include "hyperdeduce/base/status.h"
#include "hyperdeduce/catalog/catalog.h"
#include "hyperdeduce/catalog/row_key.h"
#include "hyperdeduce/input/rule_frontend.h"
#include "hyperdeduce/io/csv.h"
#include "hyperdeduce/logic/program.h"
#include "hyperdeduce/parser/ast.h"
#include "hyperdeduce/plan/physical_plan.h"
#include "hyperdeduce/plan/stats.h"

namespace hyperdeduce {

// The library facade. Load CSV relations, add Datalog rules, build an
// inspectable execution plan, then evaluate to a fixpoint on a chosen backend.
// The engine owns the catalog and all mutation; backends stay pure.
class Engine {
public:
  Engine() = default;

  Status LoadCsv(const std::string &path, const std::string &name,
                 const CsvOptions &options = {});

  // Convenience: parse Datalog text.
  Status AddRules(const std::string &datalog);

  // Unified input path: any registered frontend parses source into the shared
  // AST. The catalog is supplied to the context automatically for frontends
  // that need relation schemas (e.g. YAML).
  Status AddRules(const std::string &source, const RuleFrontend &frontend,
                  RuleInputContext context = {});

  // Validates the program, infers derived schemas, and compiles a plan per rule.
  // Returns the rendered plans so a caller can inspect the chosen join order.
  StatusOr<std::string> BuildPlan();

  // Evaluates every stratum to a fixpoint using the given backend.
  Status Execute(Backend *backend);

  const Relation *GetRelation(const std::string &name) const;

  // Names of derived (IDB) relations, available after BuildPlan or Execute.
  std::vector<std::string> IdbNames() const;

  const Catalog &catalog() const { return catalog_; }

private:
  Status EnsurePlanned();
  Status InferIdbSchemas();
  Status EvaluateStratum(Backend *backend,
                         const std::vector<std::string> &stratum);
  const PhysicalOp *DeltaPlan(int rule_index, int relational_ordinal);

  Catalog catalog_;
  std::vector<Rule> rules_;
  std::unique_ptr<Program> program_;
  Statistics stats_;
  std::vector<std::unique_ptr<PhysicalOp>> full_plans_;
  std::map<std::pair<int, int>, std::unique_ptr<PhysicalOp>> delta_plans_;
  std::unordered_map<std::string, std::unordered_set<RowKey, KeyHash>> idb_index_;
  bool planned_ = false;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_ENGINE_ENGINE_H_