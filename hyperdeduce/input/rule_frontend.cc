#include "hyperdeduce/input/rule_frontend.h"

#include <utility>

#include "hyperdeduce/input/datalog_frontend.h"
#include "hyperdeduce/input/yaml_frontend.h"

namespace hyperdeduce {

FrontendRegistry &FrontendRegistry::Instance() {
  static FrontendRegistry registry;
  return registry;
}

FrontendRegistry::FrontendRegistry() {
  Register(std::make_unique<DatalogFrontend>());
  Register(std::make_unique<YamlFrontend>());
}

void FrontendRegistry::Register(std::unique_ptr<RuleFrontend> frontend) {
  for (const std::string &extension : frontend->extensions()) {
    by_extension_[extension] = frontend.get();
  }
  by_name_.emplace(frontend->name(), std::move(frontend));
}

const RuleFrontend *FrontendRegistry::ByName(const std::string &name) const {
  auto it = by_name_.find(name);
  return it == by_name_.end() ? nullptr : it->second.get();
}

const RuleFrontend *
FrontendRegistry::ByExtension(const std::string &extension) const {
  auto it = by_extension_.find(extension);
  return it == by_extension_.end() ? nullptr : it->second;
}

std::vector<std::string> FrontendRegistry::Names() const {
  std::vector<std::string> names;
  names.reserve(by_name_.size());
  for (const auto &entry : by_name_) {
    names.push_back(entry.first);
  }
  return names;
}

} // namespace hyperdeduce