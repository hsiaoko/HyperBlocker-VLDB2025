#include "hyperdeduce/catalog/catalog.h"

#include <utility>

namespace hyperdeduce {

StatusOr<Relation *> Catalog::CreateRelation(const std::string &name,
                                             std::vector<ColumnSchema> schema) {
  if (relations_.count(name) != 0) {
    return Status::Error("relation already exists: " + name);
  }
  auto relation = std::make_unique<Relation>(name, std::move(schema));
  Relation *ptr = relation.get();
  relations_.emplace(name, std::move(relation));
  return ptr;
}

Relation *Catalog::GetRelation(const std::string &name) {
  auto it = relations_.find(name);
  return it == relations_.end() ? nullptr : it->second.get();
}

const Relation *Catalog::GetRelation(const std::string &name) const {
  auto it = relations_.find(name);
  return it == relations_.end() ? nullptr : it->second.get();
}

StatusOr<Relation *> Catalog::GetOrCreateRelation(
    const std::string &name, std::vector<ColumnSchema> schema) {
  if (Relation *existing = GetRelation(name)) {
    return existing;
  }
  return CreateRelation(name, std::move(schema));
}

std::vector<std::string> Catalog::RelationNames() const {
  std::vector<std::string> names;
  names.reserve(relations_.size());
  for (const auto &entry : relations_) {
    names.push_back(entry.first);
  }
  return names;
}

} // namespace hyperdeduce