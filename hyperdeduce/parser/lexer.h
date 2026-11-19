#ifndef HYPERDEDUCE_PARSER_LEXER_H_
#define HYPERDEDUCE_PARSER_LEXER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "hyperdeduce/base/status.h"

namespace hyperdeduce {

enum class TokenKind {
  kIdent,
  kInt,
  kDouble,
  kString,
  kLParen,
  kRParen,
  kComma,
  kDot,
  kImplies, // :-
  kEq,      // =
  kNeq,     // !=
  kLt,      // <
  kLe,      // <=
  kGt,      // >
  kGe,      // >=
  kEnd,
};

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string text;
  std::int64_t int_value = 0;
  double double_value = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

// Splits Datalog source into tokens. Line comments start with "//" or "%".
// Returns an error Status with a line and column on malformed input.
StatusOr<std::vector<Token>> Tokenize(const std::string &source);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PARSER_LEXER_H_