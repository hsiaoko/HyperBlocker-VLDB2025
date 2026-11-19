#ifndef HYPERDEDUCE_PARSER_AST_H_
#define HYPERDEDUCE_PARSER_AST_H_

#include <cstdint>
#include <string>
#include <vector>

#include "hyperdeduce/base/types.h"

namespace hyperdeduce {

// A constant term. The parser records the literal text and, for numbers, the
// parsed value. String constants stay as text until planning interns them in the
// catalog dictionary.
struct Constant {
  DataType type = DataType::kString;
  std::string text;
  std::int64_t int_value = 0;
  double double_value = 0;
};

// A term is either a variable (a bare identifier) or a constant.
struct Term {
  bool is_variable = false;
  std::string variable;
  Constant constant;

  static Term Variable(std::string name) {
    Term term;
    term.is_variable = true;
    term.variable = std::move(name);
    return term;
  }

  static Term Const(Constant value) {
    Term term;
    term.is_variable = false;
    term.constant = std::move(value);
    return term;
  }
};

// An atom is a predicate applied to terms. Relational atoms name a relation;
// builtin atoms name a registered comparison such as "<".
struct Atom {
  std::string predicate;
  std::vector<Term> terms;
};

// A Horn clause. An empty body makes the rule a fact.
struct Rule {
  Atom head;
  std::vector<Atom> body;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PARSER_AST_H_