#ifndef HYPERDEDUCE_LOGIC_PROGRAM_H_
#define HYPERDEDUCE_LOGIC_PROGRAM_H_

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/parser/ast.h"

namespace hyperdeduce {

struct PredicateInfo {
  std::string name;
  std::size_t arity = 0;
  bool is_idb = false;     // derived by some rule head
  bool recursive = false;  // in a nontrivial SCC or has a self dependency
};

// A validated Datalog program. BuildProgram checks safety and arity, classifies
// predicates as base (EDB) or derived (IDB), and groups derived predicates into
// evaluation strata using the strongly connected components of the dependency
// graph. Strata are ordered so a predicate's dependencies come first.
class Program {
public:
  const std::vector<Rule> &rules() const { return rules_; }

  const std::vector<PredicateInfo> &predicates() const { return predicates_; }

  // Each inner vector is one stratum (one SCC). Order is dependencies first.
  const std::vector<std::vector<std::string>> &strata() const { return strata_; }

  const PredicateInfo *FindPredicate(const std::string &name) const;

  std::vector<const Rule *> RulesForHead(const std::string &name) const;

private:
  friend StatusOr<Program> BuildProgram(std::vector<Rule> rules);

  std::vector<Rule> rules_;
  std::vector<PredicateInfo> predicates_;
  std::vector<std::vector<std::string>> strata_;
  std::unordered_map<std::string, std::size_t> predicate_index_;
};

// Validates and analyzes rules. Returns an error on an unsafe rule, an arity
// conflict, a builtin in a head, or a variable that no positive body atom binds.
StatusOr<Program> BuildProgram(std::vector<Rule> rules);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_LOGIC_PROGRAM_H_