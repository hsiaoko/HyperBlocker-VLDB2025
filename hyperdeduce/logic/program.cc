#include "hyperdeduce/logic/program.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_set>
#include <utility>

#include "hyperdeduce/builtin/builtin.h"

namespace hyperdeduce {
namespace {

void CollectVariables(const Atom &atom, std::unordered_set<std::string> *vars) {
  for (const Term &term : atom.terms) {
    if (term.is_variable) {
      vars->insert(term.variable);
    }
  }
}

bool IsRelational(const Atom &atom) {
  return !BuiltinRegistry::Instance().IsBuiltin(atom.predicate);
}

Status ValidateRules(const std::vector<Rule> &rules,
                     std::unordered_map<std::string, std::size_t> *arity) {
  for (const Rule &rule : rules) {
    if (!IsRelational(rule.head)) {
      return Status::Error("builtin predicate in rule head: " + rule.head.predicate);
    }
    auto record_arity = [&](const Atom &atom) -> Status {
      auto it = arity->find(atom.predicate);
      if (it == arity->end()) {
        arity->emplace(atom.predicate, atom.terms.size());
      } else if (it->second != atom.terms.size()) {
        return Status::Error("arity conflict for predicate " + atom.predicate);
      }
      return Status::Ok();
    };

    Status head_arity = record_arity(rule.head);
    if (!head_arity.ok()) {
      return head_arity;
    }

    std::unordered_set<std::string> bound;
    for (const Atom &atom : rule.body) {
      if (IsRelational(atom)) {
        Status body_arity = record_arity(atom);
        if (!body_arity.ok()) {
          return body_arity;
        }
        CollectVariables(atom, &bound);
      }
    }

    for (const Term &term : rule.head.terms) {
      if (term.is_variable && bound.count(term.variable) == 0) {
        return Status::Error("unsafe head variable " + term.variable +
                             " in rule for " + rule.head.predicate);
      }
    }

    for (const Atom &atom : rule.body) {
      if (IsRelational(atom)) {
        continue;
      }
      const BuiltinSpec *spec = BuiltinRegistry::Instance().Find(atom.predicate);
      if (spec->arity != atom.terms.size()) {
        return Status::Error("builtin " + atom.predicate + " expects " +
                             std::to_string(spec->arity) + " arguments");
      }
      for (const Term &term : atom.terms) {
        if (term.is_variable && bound.count(term.variable) == 0) {
          return Status::Error("unsafe builtin variable " + term.variable +
                               " in rule for " + rule.head.predicate);
        }
      }
    }
  }
  return Status::Ok();
}

// Tarjan's strongly connected components over the IDB dependency graph.
std::vector<int> StronglyConnectedComponents(
    const std::vector<std::vector<int>> &adj, int *num_components) {
  const int n = static_cast<int>(adj.size());
  std::vector<int> index(n, -1);
  std::vector<int> lowlink(n, 0);
  std::vector<int> component(n, -1);
  std::vector<bool> on_stack(n, false);
  std::vector<int> stack;
  int counter = 0;
  int count = 0;

  std::function<void(int)> strongconnect = [&](int v) {
    index[v] = lowlink[v] = counter++;
    stack.push_back(v);
    on_stack[v] = true;
    for (int w : adj[v]) {
      if (index[w] == -1) {
        strongconnect(w);
        lowlink[v] = std::min(lowlink[v], lowlink[w]);
      } else if (on_stack[w]) {
        lowlink[v] = std::min(lowlink[v], index[w]);
      }
    }
    if (lowlink[v] == index[v]) {
      while (true) {
        const int w = stack.back();
        stack.pop_back();
        on_stack[w] = false;
        component[w] = count;
        if (w == v) {
          break;
        }
      }
      ++count;
    }
  };

  for (int v = 0; v < n; ++v) {
    if (index[v] == -1) {
      strongconnect(v);
    }
  }
  *num_components = count;
  return component;
}

} // namespace

const PredicateInfo *Program::FindPredicate(const std::string &name) const {
  auto it = predicate_index_.find(name);
  return it == predicate_index_.end() ? nullptr : &predicates_[it->second];
}

std::vector<const Rule *> Program::RulesForHead(const std::string &name) const {
  std::vector<const Rule *> result;
  for (const Rule &rule : rules_) {
    if (rule.head.predicate == name) {
      result.push_back(&rule);
    }
  }
  return result;
}

StatusOr<Program> BuildProgram(std::vector<Rule> rules) {
  std::unordered_map<std::string, std::size_t> arity;
  Status valid = ValidateRules(rules, &arity);
  if (!valid.ok()) {
    return valid;
  }

  Program program;
  program.rules_ = std::move(rules);

  std::unordered_set<std::string> idb_names;
  for (const Rule &rule : program.rules_) {
    idb_names.insert(rule.head.predicate);
  }

  // Stable predicate ordering: first appearance in the rules.
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  auto add_name = [&](const std::string &name) {
    if (seen.insert(name).second) {
      names.push_back(name);
    }
  };
  for (const Rule &rule : program.rules_) {
    add_name(rule.head.predicate);
    for (const Atom &atom : rule.body) {
      if (IsRelational(atom)) {
        add_name(atom.predicate);
      }
    }
  }

  // IDB predicates get a dense 0..n-1 numbering for the dependency graph,
  // independent of their slot in predicates_ (which also holds EDB predicates).
  std::unordered_map<std::string, int> idb_index;
  int idb_count = 0;
  for (const std::string &name : names) {
    PredicateInfo info;
    info.name = name;
    info.arity = arity[name];
    info.is_idb = idb_names.count(name) != 0;
    if (info.is_idb) {
      idb_index[name] = idb_count++;
    }
    program.predicate_index_[name] = program.predicates_.size();
    program.predicates_.push_back(std::move(info));
  }

  // Build the IDB-only graph. Edge u -> v means u must be evaluated before v.
  const int n = static_cast<int>(idb_index.size());
  std::vector<std::vector<int>> adj(n);
  std::vector<bool> self_loop(n, false);
  std::vector<std::string> idb_names_ordered(n);
  for (const auto &entry : idb_index) {
    idb_names_ordered[entry.second] = entry.first;
  }
  for (const Rule &rule : program.rules_) {
    const int head = idb_index[rule.head.predicate];
    for (const Atom &atom : rule.body) {
      if (!IsRelational(atom)) {
        continue;
      }
      auto it = idb_index.find(atom.predicate);
      if (it == idb_index.end()) {
        continue; // base relation, always available
      }
      const int body = it->second;
      adj[body].push_back(head);
      if (body == head) {
        self_loop[head] = true;
      }
    }
  }

  int num_components = 0;
  const std::vector<int> component =
      StronglyConnectedComponents(adj, &num_components);

  std::vector<int> component_size(num_components, 0);
  for (int c : component) {
    ++component_size[c];
  }
  for (int v = 0; v < n; ++v) {
    const std::string &name = idb_names_ordered[v];
    const std::size_t index = program.predicate_index_[name];
    program.predicates_[index].recursive =
        component_size[component[v]] > 1 || self_loop[v];
  }

  // Condensation DAG, then Kahn topological order so dependencies come first.
  std::vector<std::unordered_set<int>> condensation(num_components);
  std::vector<int> indegree(num_components, 0);
  for (int u = 0; u < n; ++u) {
    for (int v : adj[u]) {
      const int cu = component[u];
      const int cv = component[v];
      if (cu != cv && condensation[cu].insert(cv).second) {
        ++indegree[cv];
      }
    }
  }
  std::queue<int> ready;
  for (int c = 0; c < num_components; ++c) {
    if (indegree[c] == 0) {
      ready.push(c);
    }
  }
  std::vector<std::vector<std::string>> strata;
  while (!ready.empty()) {
    const int c = ready.front();
    ready.pop();
    std::vector<std::string> stratum;
    for (int v = 0; v < n; ++v) {
      if (component[v] == c) {
        stratum.push_back(idb_names_ordered[v]);
      }
    }
    strata.push_back(std::move(stratum));
    for (int next : condensation[c]) {
      if (--indegree[next] == 0) {
        ready.push(next);
      }
    }
  }
  program.strata_ = std::move(strata);
  return program;
}

} // namespace hyperdeduce