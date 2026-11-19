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

// Maps each variable to a canonical label by first appearance in the relational
// body. Two bodies that differ only in variable names get the same labels, so
// structurally equivalent queries share a base regardless of naming.
std::map<std::string, std::string> CanonicalMap(const Rule &rule) {
  std::map<std::string, std::string> mapping;
  for (const Atom &atom : rule.body) {
    if (!IsRelational(atom)) {
      continue;
    }
    for (const Term &term : atom.terms) {
      if (term.is_variable && mapping.count(term.variable) == 0) {
        mapping[term.variable] = "c" + std::to_string(mapping.size());
      }
    }
  }
  return mapping;
}

std::string TermKey(const Term &term,
                    const std::map<std::string, std::string> &mapping) {
  if (term.is_variable) {
    return "v:" + mapping.at(term.variable);
  }
  return "c:" + std::to_string(static_cast<int>(term.constant.type)) + ":" +
         term.constant.text;
}

// A canonical key for the relational body: predicates, term order, variable
// equality pattern, and constants, with variables normalized by first appearance.
std::string BodySignature(const Rule &rule) {
  const auto mapping = CanonicalMap(rule);
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
      signature += TermKey(atom.terms[i], mapping);
    }
    signature += ");";
  }
  return signature;
}

Term RenameTerm(const Term &term,
                const std::map<std::string, std::string> &mapping) {
  if (!term.is_variable) {
    return term;
  }
  return Term::Variable(mapping.at(term.variable));
}

Atom RenameAtom(const Atom &atom,
                const std::map<std::string, std::string> &mapping) {
  Atom renamed;
  renamed.predicate = atom.predicate;
  for (const Term &term : atom.terms) {
    renamed.terms.push_back(RenameTerm(term, mapping));
  }
  return renamed;
}

Atom CanonicalAtom(const std::string &name, std::size_t arity) {
  Atom atom;
  atom.predicate = name;
  for (std::size_t i = 0; i < arity; ++i) {
    atom.terms.push_back(Term::Variable("c" + std::to_string(i)));
  }
  return atom;
}

} // namespace

void ApplyMqo(std::vector<Rule> &rules, const Catalog &catalog) {
  std::unordered_set<std::string> idb_heads;
  for (const Rule &rule : rules) {
    idb_heads.insert(rule.head.predicate);
  }

  // Group rules by canonical relational-body signature, keeping only all-EDB
  // bodies so the shared base is non-recursive and materializes once.
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
    const auto rep_mapping = CanonicalMap(representative);
    const std::size_t arity = rep_mapping.size();
    const std::string shared_name = "__shared_" + std::to_string(shared_id++);

    Rule base;
    base.head = CanonicalAtom(shared_name, arity);
    for (const Atom &atom : representative.body) {
      if (IsRelational(atom)) {
        base.body.push_back(RenameAtom(atom, rep_mapping));
      }
    }
    added.push_back(std::move(base));

    for (std::size_t i : group.second) {
      const auto mapping = CanonicalMap(rules[i]);
      Rule rewritten_rule;
      rewritten_rule.head = RenameAtom(rules[i].head, mapping);
      rewritten_rule.body.push_back(CanonicalAtom(shared_name, arity));
      for (const Atom &atom : rules[i].body) {
        if (!IsRelational(atom)) {
          rewritten_rule.body.push_back(RenameAtom(atom, mapping));
        }
      }
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