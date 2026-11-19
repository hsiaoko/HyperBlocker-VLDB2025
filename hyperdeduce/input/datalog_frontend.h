#ifndef HYPERDEDUCE_INPUT_DATALOG_FRONTEND_H_
#define HYPERDEDUCE_INPUT_DATALOG_FRONTEND_H_

#include <string>
#include <vector>

#include "hyperdeduce/input/rule_frontend.h"

namespace hyperdeduce {

// The Datalog text frontend. A thin adapter over the recursive-descent parser;
// it needs no catalog context.
class DatalogFrontend : public RuleFrontend {
public:
  std::string name() const override { return "datalog"; }

  std::vector<std::string> extensions() const override {
    return {"dl", "datalog", "rules"};
  }

  StatusOr<std::vector<Rule>>
  Parse(const std::string &source, const RuleInputContext &context) const override;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_DATALOG_FRONTEND_H_