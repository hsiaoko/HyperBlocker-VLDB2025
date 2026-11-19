#ifndef HYPERDEDUCE_PARSER_PARSER_H_
#define HYPERDEDUCE_PARSER_PARSER_H_

#include <string>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/parser/ast.h"

namespace hyperdeduce {

// Parses Datalog text into rules. A rule is "head :- body." or a fact "head.".
// The body mixes relational atoms and infix comparisons such as "x < 5". A bare
// identifier in a term position is a variable; constants are numbers or quoted
// strings.
StatusOr<std::vector<Rule>> ParseProgram(const std::string &source);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_PARSER_PARSER_H_