#include "hyperdeduce/input/yaml_frontend.h"

#include <map>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace hyperdeduce {
namespace {

Term VariableTerm(const std::string &name) { return Term::Variable(name); }

Term DoubleConstant(double value) {
  Constant constant;
  constant.type = DataType::kDouble;
  constant.double_value = value;
  return Term::Const(constant);
}

std::vector<int> IntSequence(const YAML::Node &node) {
  std::vector<int> values;
  for (const auto &element : node) {
    values.push_back(element.as<int>());
  }
  return values;
}

} // namespace

StatusOr<std::vector<Rule>> ParseYamlRules(const std::string &yaml_text,
                                           const std::vector<YamlTable> &tables,
                                           const std::string &head_name,
                                           const std::string &measure) {
  if (tables.size() < 2) {
    return Status::Error("yaml rule needs two tables");
  }
  try {
    const YAML::Node root = YAML::Load(yaml_text);
    const YAML::Node pre = root["Preconditions"];
    if (!pre || !pre["Sim"] || !pre["Threshold"]) {
      return Status::Error("yaml rule missing Preconditions/Sim/Threshold");
    }
    const std::vector<int> sim = IntSequence(pre["Sim"][0]);
    std::vector<double> threshold;
    for (const auto &element : pre["Threshold"][0]) {
      threshold.push_back(element.as<double>());
    }
    std::vector<int> eq;
    if (pre["Equalities"] && pre["Equalities"].size() > 0) {
      eq = IntSequence(pre["Equalities"][0]);
    }
    if (!root["Conseq"] || !root["Conseq"]["Equality"]) {
      return Status::Error("yaml rule missing Conseq/Equality");
    }
    const std::vector<int> con = IntSequence(root["Conseq"]["Equality"]);
    if (sim.size() < 4 || con.size() < 4) {
      return Status::Error("yaml rule Sim/Conseq too short");
    }

    std::map<std::pair<int, int>, std::string> vars;
    for (int t = 0; t < 2; ++t) {
      for (std::size_t c = 0; c < tables[t].arity; ++c) {
        vars[{t, static_cast<int>(c)}] =
            std::string(1, 'a' + t) + std::to_string(c);
      }
    }
    auto checked = [&](int table, int col) -> StatusOr<std::string> {
      if (table < 0 || table >= 2 || col < 0 ||
          static_cast<std::size_t>(col) >= tables[table].arity) {
        return Status::Error("yaml column index out of range");
      }
      return vars[{table, col}];
    };
    for (std::size_t k = 2; k + 1 < eq.size(); k += 2) {
      const std::string shared = "k" + std::to_string((k - 2) / 2);
      auto left = checked(eq[0], eq[k]);
      auto right = checked(eq[1], eq[k + 1]);
      if (!left.ok()) return left.status();
      if (!right.ok()) return right.status();
      vars[{eq[0], eq[k]}] = shared;
      vars[{eq[1], eq[k + 1]}] = shared;
    }

    Rule rule;
    rule.head.predicate = head_name;
    auto head_left = checked(con[0], con[2]);
    auto head_right = checked(con[1], con[3]);
    if (!head_left.ok()) return head_left.status();
    if (!head_right.ok()) return head_right.status();
    rule.head.terms.push_back(VariableTerm(head_left.value()));
    rule.head.terms.push_back(VariableTerm(head_right.value()));

    for (int t = 0; t < 2; ++t) {
      Atom atom;
      atom.predicate = tables[t].name;
      for (std::size_t c = 0; c < tables[t].arity; ++c) {
        atom.terms.push_back(VariableTerm(vars[{t, static_cast<int>(c)}]));
      }
      rule.body.push_back(std::move(atom));
    }

    const int sim_tl = sim[0];
    const int sim_tr = sim[1];
    for (std::size_t k = 2; k + 1 < sim.size(); k += 2) {
      const std::size_t pair = (k - 2) / 2;
      if (2 + pair >= threshold.size()) {
        return Status::Error("yaml threshold missing for similarity pair");
      }
      auto left = checked(sim_tl, sim[k]);
      auto right = checked(sim_tr, sim[k + 1]);
      if (!left.ok()) return left.status();
      if (!right.ok()) return right.status();
      Atom atom;
      atom.predicate = measure;
      atom.terms.push_back(VariableTerm(left.value()));
      atom.terms.push_back(VariableTerm(right.value()));
      atom.terms.push_back(DoubleConstant(threshold[2 + pair]));
      rule.body.push_back(std::move(atom));
    }

    return std::vector<Rule>{std::move(rule)};
  } catch (const YAML::Exception &error) {
    return Status::Error(std::string("yaml parse error: ") + error.what());
  }
}

StatusOr<std::vector<Rule>>
YamlFrontend::Parse(const std::string &source,
                    const RuleInputContext &context) const {
  if (context.catalog == nullptr) {
    return Status::Error("yaml frontend needs a catalog for relation arities");
  }
  std::vector<YamlTable> tables;
  for (const std::string &name : context.table_names) {
    const Relation *relation = context.catalog->GetRelation(name);
    if (relation == nullptr) {
      return Status::Error("unknown relation for yaml rule: " + name);
    }
    tables.push_back(YamlTable{name, relation->num_columns()});
  }
  return ParseYamlRules(source, tables, context.head_name);
}

} // namespace hyperdeduce