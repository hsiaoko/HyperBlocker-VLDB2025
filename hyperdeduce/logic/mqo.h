#ifndef HYPERDEDUCE_LOGIC_MQO_H_
#define HYPERDEDUCE_LOGIC_MQO_H_

#include <vector>

#include "hyperdeduce/catalog/catalog.h"
#include "hyperdeduce/parser/ast.h"

namespace hyperdeduce {

// Multiple-query optimization by common-subexpression sharing. Rules that share
// an identical all-EDB relational body get a synthetic shared base rule
// (__shared_k :- relational body) materialized once, and each original rule is
// rewritten to scan that base plus its own builtin/similarity atoms. The
// dependency graph then evaluates the shared base before its dependents, so the
// common join runs once instead of once per rule.
//
// Only all-EDB bodies are shared, so the base is non-recursive and constant
// across fixpoint rounds. Rules whose relational body references a derived
// predicate are left untouched. Transforms rules in place; a no-op when no group
// of two or more rules shares a body.
void ApplyMqo(std::vector<Rule> &rules, const Catalog &catalog);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_LOGIC_MQO_H_