#include "hyperdeduce/input/datalog_frontend.h"

#include "hyperdeduce/parser/parser.h"

namespace hyperdeduce {

StatusOr<std::vector<Rule>>
DatalogFrontend::Parse(const std::string &source,
                       const RuleInputContext &context) const {
  return ParseProgram(source);
}

} // namespace hyperdeduce