#include "hyperdeduce/engine/engine.h"

#include <sstream>
#include <utility>

#include "hyperdeduce/builtin/builtin.h"
#include "hyperdeduce/parser/parser.h"
#include "hyperdeduce/plan/optimizer.h"

namespace hyperdeduce {
namespace {

bool IsNumeric(DataType type) {
  return type == DataType::kInt64 || type == DataType::kDouble;
}

} // namespace

Status Engine::LoadCsv(const std::string &path, const std::string &name,
                       const CsvOptions &options) {
  auto loaded = hyperdeduce::LoadCsv(path, name, &catalog_, options);
  if (!loaded.ok()) {
    return loaded.status();
  }
  planned_ = false;
  return Status::Ok();
}

Status Engine::AddRules(const std::string &datalog) {
  auto parsed = ParseProgram(datalog);
  if (!parsed.ok()) {
    return parsed.status();
  }
  for (Rule &rule : std::move(parsed).value()) {
    rules_.push_back(std::move(rule));
  }
  planned_ = false;
  return Status::Ok();
}

Status Engine::InferIdbSchemas() {
  std::unordered_map<std::string, std::vector<DataType>> types;
  std::unordered_map<std::string, std::vector<bool>> known;

  for (const PredicateInfo &info : program_->predicates()) {
    const Relation *existing = catalog_.GetRelation(info.name);
    if (existing != nullptr) {
      std::vector<DataType> column_types;
      for (const ColumnSchema &column : existing->schema()) {
        column_types.push_back(column.type);
      }
      types[info.name] = column_types;
      known[info.name].assign(column_types.size(), true);
    } else if (info.is_idb) {
      types[info.name].assign(info.arity, DataType::kString);
      known[info.name].assign(info.arity, false);
    }
  }

  bool changed = true;
  int guard = 0;
  while (changed && guard < 100) {
    changed = false;
    ++guard;
    for (const Rule &rule : rules_) {
      std::unordered_map<std::string, DataType> var_type;
      for (const Atom &atom : rule.body) {
        if (BuiltinRegistry::Instance().IsBuiltin(atom.predicate)) {
          continue;
        }
        auto it = types.find(atom.predicate);
        if (it == types.end()) {
          continue;
        }
        for (std::size_t j = 0; j < atom.terms.size(); ++j) {
          if (atom.terms[j].is_variable && known[atom.predicate][j]) {
            var_type[atom.terms[j].variable] = it->second[j];
          }
        }
      }
      const std::string &head = rule.head.predicate;
      for (std::size_t j = 0; j < rule.head.terms.size(); ++j) {
        const Term &term = rule.head.terms[j];
        DataType resolved = DataType::kString;
        bool has = false;
        if (!term.is_variable) {
          resolved = term.constant.type;
          has = true;
        } else {
          auto vt = var_type.find(term.variable);
          if (vt != var_type.end()) {
            resolved = vt->second;
            has = true;
          }
        }
        if (!has) {
          continue;
        }
        if (!known[head][j]) {
          types[head][j] = resolved;
          known[head][j] = true;
          changed = true;
        } else if (types[head][j] != resolved) {
          if (IsNumeric(types[head][j]) && IsNumeric(resolved)) {
            types[head][j] = DataType::kDouble;
            changed = true;
          } else {
            return Status::Error("type conflict for predicate " + head);
          }
        }
      }
    }
  }

  for (const PredicateInfo &info : program_->predicates()) {
    if (!info.is_idb) {
      continue;
    }
    std::vector<ColumnSchema> schema;
    for (std::size_t j = 0; j < info.arity; ++j) {
      schema.push_back(ColumnSchema{"c" + std::to_string(j), types[info.name][j]});
    }
    auto relation = catalog_.GetOrCreateRelation(info.name, std::move(schema));
    if (!relation.ok()) {
      return relation.status();
    }
    // Seed the dedup index with any facts the relation already holds.
    auto &index = idb_index_[info.name];
    const Relation *current = relation.value();
    for (std::size_t r = 0; r < current->num_rows(); ++r) {
      index.insert(RowOf(*current, r));
    }
  }
  return Status::Ok();
}

StatusOr<std::string> Engine::BuildPlan() {
  auto program = BuildProgram(rules_);
  if (!program.ok()) {
    return program.status();
  }
  program_ = std::make_unique<Program>(std::move(program).value());

  Status schemas = InferIdbSchemas();
  if (!schemas.ok()) {
    return schemas;
  }
  stats_.Compute(catalog_);

  full_plans_.clear();
  delta_plans_.clear();
  std::ostringstream explain;
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    auto plan = BuildRulePlan(rules_[i], catalog_, stats_, catalog_.dictionary());
    if (!plan.ok()) {
      return plan.status();
    }
    explain << "rule " << i << " (head " << rules_[i].head.predicate << "):\n";
    explain << Explain(*plan.value());
    full_plans_.push_back(std::move(plan).value());
  }
  planned_ = true;
  return explain.str();
}

Status Engine::EnsurePlanned() {
  if (planned_) {
    return Status::Ok();
  }
  auto plan = BuildPlan();
  return plan.status();
}

const PhysicalOp *Engine::DeltaPlan(int rule_index, int relational_ordinal) {
  const auto key = std::make_pair(rule_index, relational_ordinal);
  auto it = delta_plans_.find(key);
  if (it != delta_plans_.end()) {
    return it->second.get();
  }
  auto plan = BuildRulePlan(rules_[rule_index], catalog_, stats_,
                            catalog_.dictionary(), relational_ordinal);
  if (!plan.ok()) {
    return nullptr;
  }
  const PhysicalOp *ptr = plan.value().get();
  delta_plans_.emplace(key, std::move(plan).value());
  return ptr;
}

Status Engine::EvaluateStratum(Backend *backend,
                               const std::vector<std::string> &stratum) {
  std::unordered_set<std::string> members(stratum.begin(), stratum.end());
  bool recursive = false;
  for (const std::string &name : stratum) {
    if (program_->FindPredicate(name)->recursive) {
      recursive = true;
    }
  }

  std::vector<int> rule_indices;
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    if (members.count(rules_[i].head.predicate) != 0) {
      rule_indices.push_back(static_cast<int>(i));
    }
  }

  std::unordered_map<std::string, Relation> delta;
  for (const std::string &name : stratum) {
    const Relation *relation = catalog_.GetRelation(name);
    if (relation == nullptr) {
      return Status::Error("stratum relation missing: " + name);
    }
    delta.emplace(name, Relation(name, relation->schema()));
  }

  bool first = true;
  while (true) {
    ExecutionContext context(&catalog_.dictionary());
    for (const std::string &name : catalog_.RelationNames()) {
      const Relation *full = catalog_.GetRelation(name);
      const Relation *d = members.count(name) != 0 ? &delta.at(name) : nullptr;
      context.AddRelation(name, full, d);
    }

    std::unordered_map<std::string, std::vector<Relation>> candidates;
    for (int i : rule_indices) {
      const Rule &rule = rules_[i];
      if (first) {
        auto out = backend->Execute(*full_plans_[i], context);
        if (!out.ok()) {
          return out.status();
        }
        candidates[rule.head.predicate].push_back(std::move(out).value());
        continue;
      }
      int ordinal = 0;
      for (const Atom &atom : rule.body) {
        if (BuiltinRegistry::Instance().IsBuiltin(atom.predicate)) {
          continue;
        }
        if (members.count(atom.predicate) != 0) {
          const PhysicalOp *plan = DeltaPlan(i, ordinal);
          if (plan == nullptr) {
            return Status::Error("failed to build delta plan");
          }
          auto out = backend->Execute(*plan, context);
          if (!out.ok()) {
            return out.status();
          }
          candidates[rule.head.predicate].push_back(std::move(out).value());
        }
        ++ordinal;
      }
    }

    std::unordered_map<std::string, Relation> new_delta;
    for (const std::string &name : stratum) {
      new_delta.emplace(name, Relation(name, catalog_.GetRelation(name)->schema()));
    }
    std::size_t total_new = 0;
    for (auto &entry : candidates) {
      const std::string &head = entry.first;
      Relation *target = catalog_.GetRelation(head);
      auto &index = idb_index_[head];
      for (const Relation &out : entry.second) {
        std::vector<std::vector<Cell>> columns(target->num_columns());
        std::size_t nnew = 0;
        for (std::size_t r = 0; r < out.num_rows(); ++r) {
          if (index.insert(RowOf(out, r)).second) {
            for (std::size_t c = 0; c < target->num_columns(); ++c) {
              columns[c].push_back(out.at(c, r));
            }
            ++nnew;
          }
        }
        if (nnew > 0) {
          Status appended = target->AppendColumns(columns, nnew);
          if (!appended.ok()) {
            return appended;
          }
          new_delta.at(head).AppendColumns(columns, nnew);
          total_new += nnew;
        }
      }
    }
    delta = std::move(new_delta);

    if (!recursive) {
      break;
    }
    if (!first && total_new == 0) {
      break;
    }
    first = false;
  }
  return Status::Ok();
}

Status Engine::Execute(Backend *backend) {
  Status planned = EnsurePlanned();
  if (!planned.ok()) {
    return planned;
  }
  for (const std::vector<std::string> &stratum : program_->strata()) {
    Status evaluated = EvaluateStratum(backend, stratum);
    if (!evaluated.ok()) {
      return evaluated;
    }
  }
  return Status::Ok();
}

const Relation *Engine::GetRelation(const std::string &name) const {
  return catalog_.GetRelation(name);
}

std::vector<std::string> Engine::IdbNames() const {
  std::vector<std::string> names;
  if (program_ == nullptr) {
    return names;
  }
  for (const PredicateInfo &info : program_->predicates()) {
    if (info.is_idb) {
      names.push_back(info.name);
    }
  }
  return names;
}

} // namespace hyperdeduce