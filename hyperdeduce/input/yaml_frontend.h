#ifndef HYPERDEDUCE_INPUT_YAML_FRONTEND_H_
#define HYPERDEDUCE_INPUT_YAML_FRONTEND_H_

#include <cstddef>
#include <string>
#include <vector>

#include "hyperdeduce/input/rule_frontend.h"

namespace hyperdeduce {

// A relation referenced by a legacy YAML rule, identified by its table index.
struct YamlTable {
  std::string name;
  std::size_t arity = 0;
};

// Decodes an original HyperBlocker YAML rule into the shared Rule AST. The
// encoding follows core/common/yaml_config.h at tag vldb2025-paper. Equality
// columns become a shared variable so the optimizer blocks on them first.
StatusOr<std::vector<Rule>> ParseYamlRules(
    const std::string &yaml_text, const std::vector<YamlTable> &tables,
    const std::string &head_name = "match",
    const std::string &measure = "jaro_winkler");

// The legacy YAML frontend. It resolves table arities from the catalog context
// and delegates to ParseYamlRules.
class YamlFrontend : public RuleFrontend {
public:
  std::string name() const override { return "yaml"; }

  std::vector<std::string> extensions() const override {
    return {"yaml", "yml"};
  }

  StatusOr<std::vector<Rule>>
  Parse(const std::string &source, const RuleInputContext &context) const override;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_YAML_FRONTEND_H_