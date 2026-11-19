#include "hyperdeduce/plan/optimizer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "hyperdeduce/builtin/builtin.h"

namespace hyperdeduce {
namespace {

Cell ConstantToCell(const Constant &constant, Dictionary &dictionary) {
  switch (constant.type) {
  case DataType::kInt64:
    return Int64ToCell(constant.int_value);
  case DataType::kDouble:
    return DoubleToCell(constant.double_value);
  case DataType::kString:
    return dictionary.Intern(constant.text);
  }
  return 0;
}

int FindColumn(const std::vector<Column> &schema, const std::string &var) {
  for (std::size_t i = 0; i < schema.size(); ++i) {
    if (schema[i].var == var) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

double FloorRows(double rows) { return std::max(rows, 1.0); }

// A compiled body atom: its scan subtree, the variable schema it produces, and
// the source statistics used to estimate join selectivity.
struct AtomPlan {
  const Atom *atom = nullptr;
  std::unique_ptr<PhysicalOp> op;
  std::vector<Column> schema;
  std::unordered_set<std::string> vars;
  std::unordered_map<std::string, int> var_source_column;
  const RelationStats *stats = nullptr;
  double est = 1;
};

// A similarity atom is a binary join edge between the atoms binding var_x and
// var_y, with a similarity theta rather than equality.
struct SimAtom {
  const BuiltinSpec *spec = nullptr;
  std::string var_x;
  std::string var_y;
  double threshold = 0;
  bool consumed = false;
};

// Similarity joins are expensive per pair; a default selectivity keeps the
// optimizer preferring cheap equality joins first (equalities-first emerges).
constexpr double kSimilaritySelectivity = 0.1;

std::unique_ptr<PhysicalOp> MakeUnitScan() {
  auto scan = std::make_unique<PhysicalOp>();
  scan->kind = OpKind::kScan;
  scan->source_relation = ""; // the unit producer: one row, zero columns
  scan->est_rows = 1;
  return scan;
}

StatusOr<AtomPlan> CompileAtom(const Atom &atom, const Catalog &catalog,
                               const Statistics &stats, Dictionary &dictionary,
                               bool delta,
                               std::unordered_map<std::string, DataType> *var_types) {
  const Relation *relation = catalog.GetRelation(atom.predicate);
  if (relation == nullptr) {
    return Status::Error("unknown relation in rule body: " + atom.predicate);
  }
  if (relation->num_columns() != atom.terms.size()) {
    return Status::Error("atom arity does not match relation: " + atom.predicate);
  }
  if (atom.terms.empty()) {
    return Status::Error("0-ary relations are not supported in evaluation: " +
                         atom.predicate);
  }

  AtomPlan plan;
  plan.atom = &atom;
  plan.stats = stats.Find(atom.predicate);

  auto scan = std::make_unique<PhysicalOp>();
  scan->kind = OpKind::kScan;
  scan->source_relation = atom.predicate;
  scan->delta = delta;
  scan->output_schema.resize(atom.terms.size());
  scan->source_columns.resize(atom.terms.size());

  std::unordered_map<std::string, int> first_occurrence;
  std::vector<int> keep_columns;
  std::vector<std::unique_ptr<PhysicalOp>> filters;
  double selectivity = 1.0;

  for (std::size_t j = 0; j < atom.terms.size(); ++j) {
    const Term &term = atom.terms[j];
    const DataType source_type = relation->column(j).type;
    scan->source_columns[j] = static_cast<int>(j);
    if (term.is_variable) {
      scan->output_schema[j] = Column{term.variable, source_type};
      plan.var_source_column[term.variable] = static_cast<int>(j);
      auto existing = var_types->find(term.variable);
      if (existing == var_types->end()) {
        var_types->emplace(term.variable, source_type);
      }
      auto first = first_occurrence.find(term.variable);
      if (first == first_occurrence.end()) {
        first_occurrence[term.variable] = static_cast<int>(j);
        keep_columns.push_back(static_cast<int>(j));
        plan.vars.insert(term.variable);
      } else {
        auto filter = std::make_unique<PhysicalOp>();
        filter->kind = OpKind::kFilter;
        filter->builtin = "=";
        filter->filter_args.push_back(FilterArg{false, first->second, 0, source_type});
        filter->filter_args.push_back(FilterArg{false, static_cast<int>(j), 0, source_type});
        filters.push_back(std::move(filter));
      }
    } else {
      scan->output_schema[j] = Column{"", source_type};
      const Cell cell = ConstantToCell(term.constant, dictionary);
      auto filter = std::make_unique<PhysicalOp>();
      filter->kind = OpKind::kFilter;
      filter->builtin = "=";
      filter->filter_args.push_back(FilterArg{false, static_cast<int>(j), 0, source_type});
      filter->filter_args.push_back(
          FilterArg{true, -1, cell, term.constant.type});
      filters.push_back(std::move(filter));
      if (plan.stats != nullptr && plan.stats->distinct[j] > 0) {
        selectivity /= static_cast<double>(plan.stats->distinct[j]);
      }
    }
  }

  const double source_rows =
      plan.stats != nullptr ? static_cast<double>(plan.stats->rows) : 0.0;
  scan->est_rows = FloorRows(source_rows);

  // Stack the constant and repeated-variable selections above the scan.
  std::unique_ptr<PhysicalOp> node = std::move(scan);
  double filtered_rows = FloorRows(source_rows * selectivity);
  for (auto &filter : filters) {
    filter->output_schema = node->output_schema;
    filter->est_rows = filtered_rows;
    filter->inputs.push_back(std::move(node));
    node = std::move(filter);
  }

  // Project down to the unique variable columns.
  auto project = std::make_unique<PhysicalOp>();
  project->kind = OpKind::kProject;
  project->est_rows = filtered_rows;
  for (int col : keep_columns) {
    ProjectColumn pc;
    pc.is_constant = false;
    pc.index = col;
    pc.output = node->output_schema[col];
    project->project_columns.push_back(pc);
    plan.schema.push_back(node->output_schema[col]);
  }
  project->output_schema = plan.schema;
  project->inputs.push_back(std::move(node));
  plan.op = std::move(project);
  plan.est = filtered_rows;
  return plan;
}

double JoinSelectivity(const AtomPlan &atom,
                       const std::unordered_set<std::string> &shared) {
  std::size_t max_distinct = 1;
  if (atom.stats != nullptr) {
    for (const std::string &var : shared) {
      auto it = atom.var_source_column.find(var);
      if (it != atom.var_source_column.end()) {
        max_distinct = std::max(max_distinct, atom.stats->distinct[it->second]);
      }
    }
  }
  return 1.0 / static_cast<double>(max_distinct);
}

std::unique_ptr<PhysicalOp> MakeJoin(std::unique_ptr<PhysicalOp> left,
                                     AtomPlan &right,
                                     const std::vector<Column> &left_schema,
                                     const std::unordered_set<std::string> &shared,
                                     double est) {
  auto join = std::make_unique<PhysicalOp>();
  join->kind = OpKind::kHashJoin;
  join->est_rows = est;
  for (const std::string &var : shared) {
    join->left_keys.push_back(FindColumn(left_schema, var));
    join->right_keys.push_back(FindColumn(right.schema, var));
  }
  join->output_schema = left_schema;
  for (std::size_t i = 0; i < right.schema.size(); ++i) {
    if (shared.count(right.schema[i].var) == 0) {
      join->output_schema.push_back(right.schema[i]);
    }
  }
  join->inputs.push_back(std::move(left));
  join->inputs.push_back(std::move(right.op));
  return join;
}

std::unique_ptr<PhysicalOp> MakeSimilarityJoin(std::unique_ptr<PhysicalOp> left,
                                               AtomPlan &right,
                                               const std::vector<Column> &left_schema,
                                               const SimAtom &sim, int left_key,
                                               int right_key, double est) {
  auto join = std::make_unique<PhysicalOp>();
  join->kind = OpKind::kSimilarityJoin;
  join->est_rows = est;
  join->measure = sim.spec->name;
  join->sim_threshold = sim.threshold;
  join->left_key = left_key;
  join->right_key = right_key;
  join->output_schema = left_schema;
  for (const Column &column : right.schema) {
    join->output_schema.push_back(column);
  }
  join->inputs.push_back(std::move(left));
  join->inputs.push_back(std::move(right.op));
  return join;
}

} // namespace

StatusOr<std::unique_ptr<PhysicalOp>> BuildRulePlan(
    const Rule &rule, const Catalog &catalog, const Statistics &stats,
    Dictionary &dictionary, int delta_relational_index) {
  std::unordered_map<std::string, DataType> var_types;
  std::vector<AtomPlan> atoms;
  std::vector<const Atom *> builtins;
  std::vector<SimAtom> sims;

  int relational_index = 0;
  for (const Atom &atom : rule.body) {
    if (BuiltinRegistry::Instance().IsBuiltin(atom.predicate)) {
      const BuiltinSpec *spec = BuiltinRegistry::Instance().Find(atom.predicate);
      if (spec->kind == BuiltinKind::kSimilarity) {
        if (atom.terms.size() != 3 || !atom.terms[0].is_variable ||
            !atom.terms[1].is_variable || atom.terms[2].is_variable) {
          return Status::Error("similarity atom needs two variables and a "
                               "constant threshold: " + atom.predicate);
        }
        const Constant &threshold = atom.terms[2].constant;
        if (threshold.type == DataType::kString) {
          return Status::Error("similarity threshold must be numeric");
        }
        SimAtom sim;
        sim.spec = spec;
        sim.var_x = atom.terms[0].variable;
        sim.var_y = atom.terms[1].variable;
        sim.threshold = threshold.type == DataType::kInt64
                            ? static_cast<double>(threshold.int_value)
                            : threshold.double_value;
        sims.push_back(std::move(sim));
      } else {
        builtins.push_back(&atom);
      }
      continue;
    }
    const bool delta = relational_index == delta_relational_index;
    auto compiled = CompileAtom(atom, catalog, stats, dictionary, delta, &var_types);
    if (!compiled.ok()) {
      return compiled.status();
    }
    atoms.push_back(std::move(compiled).value());
    ++relational_index;
  }

  std::unique_ptr<PhysicalOp> current;
  std::vector<Column> current_schema;
  std::unordered_set<std::string> bound;
  double current_est = 1;

  if (atoms.empty()) {
    current = MakeUnitScan();
  } else {
    // Greedy data-aware ordering: start small, then prefer connected atoms with
    // the smallest estimated join output.
    std::vector<bool> used(atoms.size(), false);
    std::size_t first = 0;
    for (std::size_t i = 1; i < atoms.size(); ++i) {
      if (atoms[i].est < atoms[first].est) {
        first = i;
      }
    }
    used[first] = true;
    current_est = atoms[first].est;
    current_schema = atoms[first].schema;
    bound = atoms[first].vars;
    current = std::move(atoms[first].op);

    // Connection rank encodes cost: equality join (0) is cheaper than a
    // similarity join (1), which is cheaper than a cross product (2). The
    // optimizer prefers the cheapest connection, then the smallest estimate.
    for (std::size_t step = 1; step < atoms.size(); ++step) {
      int best = -1;
      int best_rank = 3;
      double best_est = 0;
      int best_sim = -1;
      int best_left_key = -1;
      int best_right_key = -1;
      std::unordered_set<std::string> best_shared;

      for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (used[i]) {
          continue;
        }
        std::unordered_set<std::string> shared;
        for (const std::string &var : atoms[i].vars) {
          if (bound.count(var) != 0) {
            shared.insert(var);
          }
        }

        int rank = 2;
        int sim_index = -1;
        int left_key = -1;
        int right_key = -1;
        double est = current_est * atoms[i].est;
        if (!shared.empty()) {
          rank = 0;
          est = current_est * atoms[i].est * JoinSelectivity(atoms[i], shared);
        } else {
          for (std::size_t s = 0; s < sims.size(); ++s) {
            if (sims[s].consumed) {
              continue;
            }
            const SimAtom &sim = sims[s];
            if (bound.count(sim.var_x) != 0 && atoms[i].vars.count(sim.var_y) != 0) {
              rank = 1;
              sim_index = static_cast<int>(s);
              left_key = FindColumn(current_schema, sim.var_x);
              right_key = FindColumn(atoms[i].schema, sim.var_y);
            } else if (bound.count(sim.var_y) != 0 &&
                       atoms[i].vars.count(sim.var_x) != 0) {
              rank = 1;
              sim_index = static_cast<int>(s);
              left_key = FindColumn(current_schema, sim.var_y);
              right_key = FindColumn(atoms[i].schema, sim.var_x);
            } else {
              continue;
            }
            est = current_est * atoms[i].est * kSimilaritySelectivity;
            break;
          }
        }

        if (best == -1 || rank < best_rank ||
            (rank == best_rank && est < best_est)) {
          best = static_cast<int>(i);
          best_rank = rank;
          best_est = est;
          best_sim = sim_index;
          best_left_key = left_key;
          best_right_key = right_key;
          best_shared = std::move(shared);
        }
      }

      used[best] = true;
      if (best_rank == 1) {
        sims[best_sim].consumed = true;
        current = MakeSimilarityJoin(std::move(current), atoms[best],
                                     current_schema, sims[best_sim],
                                     best_left_key, best_right_key,
                                     FloorRows(best_est));
      } else {
        current = MakeJoin(std::move(current), atoms[best], current_schema,
                           best_shared, FloorRows(best_est));
      }
      current_schema = current->output_schema;
      current_est = FloorRows(best_est);
      for (const std::string &var : atoms[best].vars) {
        bound.insert(var);
      }
    }
  }

  // A similarity atom whose two variables ended up bound in the same working
  // relation (not used as a join edge) degenerates to a filter.
  for (const SimAtom &sim : sims) {
    if (sim.consumed) {
      continue;
    }
    const int col_x = FindColumn(current_schema, sim.var_x);
    const int col_y = FindColumn(current_schema, sim.var_y);
    if (col_x < 0 || col_y < 0) {
      return Status::Error("similarity variable not bound: " + sim.var_x);
    }
    const double sim_est = current_est * kSimilaritySelectivity;
    auto filter = std::make_unique<PhysicalOp>();
    filter->kind = OpKind::kFilter;
    filter->builtin = sim.spec->name;
    filter->output_schema = current_schema;
    filter->est_rows = sim_est;
    filter->filter_args.push_back(FilterArg{false, col_x, 0, var_types[sim.var_x]});
    filter->filter_args.push_back(FilterArg{false, col_y, 0, var_types[sim.var_y]});
    filter->filter_args.push_back(
        FilterArg{true, -1, DoubleToCell(sim.threshold), DataType::kDouble});
    filter->inputs.push_back(std::move(current));
    current = std::move(filter);
    current_est = sim_est;
  }

  // Builtin comparisons once their variables are bound.
  for (const Atom *builtin : builtins) {
    auto filter = std::make_unique<PhysicalOp>();
    filter->kind = OpKind::kFilter;
    filter->builtin = builtin->predicate;
    filter->output_schema = current_schema;
    filter->est_rows = current_est;
    for (const Term &term : builtin->terms) {
      if (term.is_variable) {
        const int col = FindColumn(current_schema, term.variable);
        if (col < 0) {
          return Status::Error("builtin variable not bound: " + term.variable);
        }
        filter->filter_args.push_back(
            FilterArg{false, col, 0, var_types[term.variable]});
      } else {
        filter->filter_args.push_back(
            FilterArg{true, -1, ConstantToCell(term.constant, dictionary),
                      term.constant.type});
      }
    }
    filter->inputs.push_back(std::move(current));
    current = std::move(filter);
  }

  // Project the head, injecting head constants as constant columns.
  auto project = std::make_unique<PhysicalOp>();
  project->kind = OpKind::kProject;
  project->est_rows = current_est;
  for (const Term &term : rule.head.terms) {
    ProjectColumn pc;
    if (term.is_variable) {
      pc.index = FindColumn(current_schema, term.variable);
      if (pc.index < 0) {
        return Status::Error("head variable not bound: " + term.variable);
      }
      pc.output = Column{term.variable, var_types[term.variable]};
    } else {
      pc.is_constant = true;
      pc.constant = ConstantToCell(term.constant, dictionary);
      pc.output = Column{"", term.constant.type};
    }
    project->project_columns.push_back(pc);
    project->output_schema.push_back(pc.output);
  }
  project->inputs.push_back(std::move(current));

  auto dedup = std::make_unique<PhysicalOp>();
  dedup->kind = OpKind::kDedup;
  dedup->output_schema = project->output_schema;
  dedup->est_rows = current_est;
  dedup->inputs.push_back(std::move(project));
  return dedup;
}

} // namespace hyperdeduce