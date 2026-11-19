#ifndef HYPERDEDUCE_PLAN_PHYSICAL_PLAN_H_
#define HYPERDEDUCE_PLAN_PHYSICAL_PLAN_H_

#include <memory>
#include <string>
#include <vector>

#include "hyperdeduce/base/types.h"

namespace hyperdeduce {

enum class OpKind { kScan, kFilter, kHashJoin, kSimilarityJoin, kProject, kDedup };

// One column of a working relation: the variable it binds and its type.
struct Column {
  std::string var;
  DataType type = DataType::kString;
};

// A builtin argument is either a working column or a constant cell.
struct FilterArg {
  bool is_constant = false;
  int column = -1;
  Cell constant = 0;
  DataType type = DataType::kString;
};

// A projection output column is either an input column or a constant.
struct ProjectColumn {
  bool is_constant = false;
  int index = -1;
  Cell constant = 0;
  Column output;
};

// A physical operator tree. The planner resolves every variable to a column
// index, so a backend only executes primitives: scan a relation, filter rows by
// a builtin, hash-join two inputs, project columns, deduplicate. The plan is
// data, which is what lets Explain() print it and any backend interpret it.
struct PhysicalOp {
  OpKind kind = OpKind::kScan;
  std::vector<Column> output_schema;
  std::vector<std::unique_ptr<PhysicalOp>> inputs;
  double est_rows = 0;

  // kScan: read source_columns from the source relation into output_schema.
  std::string source_relation;
  bool delta = false;
  std::vector<int> source_columns;

  // kFilter: keep rows where builtin(filter_args) holds.
  std::string builtin;
  std::vector<FilterArg> filter_args;

  // kHashJoin: equi-join inputs[0] and inputs[1] on these column indices. Empty
  // keys mean a cross product. The output keeps all left columns plus the right
  // columns that are not join keys.
  std::vector<int> left_keys;
  std::vector<int> right_keys;

  // kSimilarityJoin: a binary theta-join whose condition is measure(left_key,
  // right_key) >= sim_threshold. A sibling of kHashJoin, not a filter. The
  // output keeps all left columns plus all right columns (the two string keys
  // are not equated, so both remain).
  std::string measure;
  double sim_threshold = 0;
  int left_key = -1;
  int right_key = -1;

  // kProject: produce project_columns from the single input.
  std::vector<ProjectColumn> project_columns;
};

const char *OpKindName(OpKind kind);

// Renders the plan tree with estimated row counts, one operator per line.
std::string Explain(const PhysicalOp &op);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PLAN_PHYSICAL_PLAN_H_