#include "hyperdeduce/engine/engine.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <utility>

#include "hyperdeduce/builtin/builtin.h"
#include "hyperdeduce/input/csv_loader.h"
#include "hyperdeduce/input/datalog_frontend.h"
#include "hyperdeduce/plan/optimizer.h"

namespace hyperdeduce {
namespace {

bool IsNumeric(DataType type) {
  return type == DataType::kInt64 || type == DataType::kDouble;
}

} // namespace

StatusOr<Relation *> Engine::LoadRelation(const std::string &path,
                                          const std::string &name,
                                          const LoadOptions &options) {
  std::string extension = std::filesystem::path(path).extension().string();
  if (!extension.empty()) {
    extension = extension.substr(1);
  }
  const RelationLoader *loader =
      LoaderRegistry::Instance().ByExtension(extension);
  if (loader == nullptr) {
    return Status::Error("no loader registered for extension: " + extension);
  }
  const auto start = std::chrono::steady_clock::now();
  auto loaded = loader->Load(path, name, &catalog_, options);
  if (!loaded.ok()) {
    return loaded.status();
  }
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
  Relation *relation = loaded.value();
  RelationLoadStats load_stats;
  load_stats.name = name;
  load_stats.rows = relation->num_rows();
  load_stats.cols = relation->num_columns();
  for (const ColumnSchema &column : relation->schema()) {
    load_stats.types.push_back(column.type);
  }
  load_stats.load_ms = ms;
  run_stats_.loads.push_back(std::move(load_stats));
  run_stats_.load_ms += ms;
  planned_ = false;
  return relation;
}

Status Engine::LoadCsv(const std::string &path, const std::string &name,
                       const CsvOptions &options) {
  LoadOptions load_options;
  load_options.has_header = options.has_header;
  load_options.separator = options.separator;
  load_options.column_types = options.column_types;
  const CsvLoader loader;
  auto loaded = loader.Load(path, name, &catalog_, load_options);
  if (!loaded.ok()) {
    return loaded.status();
  }
  planned_ = false;
  return Status::Ok();
}

Status Engine::AddRules(const std::string &datalog) {
  const DatalogFrontend frontend;
  return AddRules(datalog, frontend);
}

Status Engine::AddRules(const std::string &source, const RuleFrontend &frontend,
                        RuleInputContext context) {
  context.catalog = &catalog_;
  auto parsed = frontend.Parse(source, context);
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
  const auto start = std::chrono::steady_clock::now();
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
  run_stats_.plan_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count();
  run_stats_.rule_count = static_cast<int>(rules_.size());
  run_stats_.stratum_count = static_cast<int>(program_->strata().size());
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

  const auto stratum_start = std::chrono::steady_clock::now();
  int rounds = 0;
  std::size_t facts_derived = 0;
  bool first = true;
  while (true) {
    ++rounds;
    ExecutionContext context(&catalog_.dictionary());
    context.stats = &backend_stats_;
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
    facts_derived += total_new;

    if (!recursive) {
      break;
    }
    if (!first && total_new == 0) {
      break;
    }
    first = false;
  }

  StratumStats stratum_stats;
  stratum_stats.predicates = stratum;
  stratum_stats.recursive = recursive;
  stratum_stats.rounds = rounds;
  stratum_stats.facts_derived = facts_derived;
  stratum_stats.eval_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - stratum_start)
                              .count();
  run_stats_.strata.push_back(std::move(stratum_stats));
  return Status::Ok();
}

Status Engine::Execute(Backend *backend) {
  Status planned = EnsurePlanned();
  if (!planned.ok()) {
    return planned;
  }
  run_stats_.strata.clear();
  run_stats_.outputs.clear();
  run_stats_.gpu = GpuStats{};
  backend_stats_.Reset();
  run_stats_.backend = backend->name();
  run_stats_.gpu.devices = static_cast<int>(backend->device_ids().size());

  const auto start = std::chrono::steady_clock::now();
  backend->BeginMonitor();
  for (const std::vector<std::string> &stratum : program_->strata()) {
    Status evaluated = EvaluateStratum(backend, stratum);
    if (!evaluated.ok()) {
      backend->EndMonitor();
      return evaluated;
    }
  }
  backend->EndMonitor();
  run_stats_.eval_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count();
  run_stats_.gpu.kernel_ms = backend_stats_.kernel_ms;
  run_stats_.gpu.pairs = backend_stats_.pairs;
  run_stats_.gpu.util_avg = backend->gpu_util_avg();
  run_stats_.gpu.util_max = backend->gpu_util_max();
  run_stats_.total_ms = run_stats_.load_ms + run_stats_.plan_ms + run_stats_.eval_ms;

  for (const std::string &name : IdbNames()) {
    run_stats_.outputs.emplace_back(name, GetRelation(name)->num_rows());
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