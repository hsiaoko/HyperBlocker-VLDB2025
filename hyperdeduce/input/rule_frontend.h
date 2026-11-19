#ifndef HYPERDEDUCE_INPUT_RULE_FRONTEND_H_
#define HYPERDEDUCE_INPUT_RULE_FRONTEND_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/catalog/catalog.h"
#include "hyperdeduce/parser/ast.h"

namespace hyperdeduce {

// Context a frontend may need beyond the raw source. Datalog ignores it; the
// YAML frontend reads relation arities from the catalog and maps table indices
// to relation names. Keeping it optional lets every frontend share one signature.
struct RuleInputContext {
  const Catalog *catalog = nullptr;
  std::vector<std::string> table_names;
  std::string head_name = "match";
};

// One input format is one special case of this interface. Adding a format means
// deriving a frontend and registering it; the engine and CLI never branch on the
// format themselves.
class RuleFrontend {
public:
  virtual ~RuleFrontend() = default;

  virtual std::string name() const = 0;

  // File extensions this frontend handles, without the leading dot.
  virtual std::vector<std::string> extensions() const = 0;

  virtual StatusOr<std::vector<Rule>>
  Parse(const std::string &source, const RuleInputContext &context) const = 0;
};

// Selects a frontend by name or file extension. Datalog and YAML register here
// at startup; a new format registers once and is dispatchable everywhere.
class FrontendRegistry {
public:
  static FrontendRegistry &Instance();

  void Register(std::unique_ptr<RuleFrontend> frontend);

  const RuleFrontend *ByName(const std::string &name) const;

  // extension without the leading dot; nullptr when unknown.
  const RuleFrontend *ByExtension(const std::string &extension) const;

  std::vector<std::string> Names() const;

private:
  FrontendRegistry();

  std::unordered_map<std::string, std::unique_ptr<RuleFrontend>> by_name_;
  std::unordered_map<std::string, RuleFrontend *> by_extension_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_RULE_FRONTEND_H_