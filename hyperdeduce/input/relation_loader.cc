#include "hyperdeduce/input/relation_loader.h"

#include <utility>

#include "hyperdeduce/input/csv_loader.h"
#include "hyperdeduce/input/tsv_loader.h"

namespace hyperdeduce {

LoaderRegistry &LoaderRegistry::Instance() {
  static LoaderRegistry registry;
  return registry;
}

LoaderRegistry::LoaderRegistry() {
  Register(std::make_unique<CsvLoader>());
  Register(std::make_unique<TsvLoader>());
}

void LoaderRegistry::Register(std::unique_ptr<RelationLoader> loader) {
  for (const std::string &extension : loader->extensions()) {
    by_extension_[extension] = loader.get();
  }
  by_name_.emplace(loader->name(), std::move(loader));
}

const RelationLoader *LoaderRegistry::ByName(const std::string &name) const {
  auto it = by_name_.find(name);
  return it == by_name_.end() ? nullptr : it->second.get();
}

const RelationLoader *
LoaderRegistry::ByExtension(const std::string &extension) const {
  auto it = by_extension_.find(extension);
  return it == by_extension_.end() ? nullptr : it->second;
}

std::vector<std::string> LoaderRegistry::Names() const {
  std::vector<std::string> names;
  names.reserve(by_name_.size());
  for (const auto &entry : by_name_) {
    names.push_back(entry.first);
  }
  return names;
}

} // namespace hyperdeduce