#include "hyperdeduce/logic/mqo.h"

#include <map>
#include <set>
#include <string>
#include <unordered_set>

#include "hyperdeduce/builtin/builtin.h"

namespace hyperdeduce {
namespace {

bool IsRelational(const Atom &atom) {
  return !BuiltinRegistry::Instance().IsBuiltin(atom.predicate);
}

std::string TermKey(const Term &term) {
  if (term.is_variable) {
    return "v:" + term.variable;
  }
  return "c:" + std::to_string(static_cast<int>(term.constant.type)) + ":" +
         term.constant.text;
}

// A canonical key for the relational body: predicates, term order, and variable
// names. Rules with the same key generate the same candidate tuples.
std::string BodySignature(const Rule &rule) {
  std::string signature;
  for (const Atom &atom : rule.body) {
    if (!IsRelational(atom)) {
      continue;
    }
    signature += atom.predicate + "(";
    for (std::size_t i = 0; i < atom.terms.size(); ++i) {
      if (i > 0) {
        signature += ",";
      }
      signature += TermKey(atom.terms[i]);
    }
    signature += ");";
  }
  return signature;
}

std::vector<std::string> BoundVariables(const Rule &rule) {
  std::vector<std::string> vars;
  std::unordered_set<std::string> seen;
  for (const Atom &atom : rule.body) {
    if (!IsRelational(atom)) {
      continue;
    }
    for (const Term &term : atom.terms) {
      if (term.is_variable && seen.insert(term.variable).second) {
        vars.push_back(term.variable);
      }
    }
  }
  return vars;
}

Atom SharedAtom(const std::string &name, const std::vector<std::string> &vars) {
  Atom atom;
  atom.predicate = name;
  for (const std::string &var : vars) {
    atom.terms.push_back(Term::Variable(var));
  }
  return atom;
}

} // namespace

void ApplyMqo(std::vector<Rule> &rules, const Catalog &catalog) {
  std::unordered_set<std::string> idb_heads;
  for (const Rule &rule : rules) {
    idb_heads.insert(rule.head.predicate);
  }

  // Group rules by relational-body signature, keeping only all-EDB bodies so the
  // shared base is non-recursive and materializes once.
  std::map<std::string, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < rules.size(); ++i) {
    bool has_relational = false;
    bool all_edb = true;
    for (const Atom &atom : rules[i].body) {
      if (!IsRelational(atom)) {
        continue;
      }
      has_relational = true;
      if (idb_heads.count(atom.predicate) != 0 ||
          catalog.GetRelation(atom.predicate) == nullptr) {
        all_edb = false;
        break;
      }
    }
    if (has_relational && all_edb) {
      groups[BodySignature(rules[i])].push_back(i);
    }
  }

  std::vector<Rule> added;
  std::unordered_set<std::size_t> rewritten;
  int shared_id = 0;
  for (const auto &group : groups) {
    if (group.second.size() < 2) {
      continue;
    }
    const Rule &representative = rules[group.second[0]];
    const std::vector<std::string> vars = BoundVariables(representative);
    const std::string shared_name = "__shared_" + std::to_string(shared_id++);

    Rule base;
    base.head = SharedAtom(shared_name, vars);
    for (const Atom &atom : representative.body) {
      if (IsRelational(atom)) {
        base.body.push_back(atom);
      }
    }
    added.push_back(std::move(base));

    for (std::size_t i : group.second) {
      Rule rewritten_rule = rules[i];
      std::vector<Atom> new_body;
      new_body.push_back(SharedAtom(shared_name, vars));
      for (const Atom &atom : rules[i].body) {
        if (!IsRelational(atom)) {
          new_body.push_back(atom);
        }
      }
      rewritten_rule.body = std::move(new_body);
      added.push_back(std::move(rewritten_rule));
      rewritten.insert(i);
    }
  }

  if (rewritten.empty()) {
    return;
  }
  std::vector<Rule> result;
  for (std::size_t i = 0; i < rules.size(); ++i) {
    if (rewritten.count(i) == 0) {
      result.push_back(std::move(rules[i]));
    }
  }
  for (Rule &rule : added) {
    result.push_back(std::move(rule));
  }
  rules = std::move(result);
}

} // namespace hyperdeduce