#ifndef HYPERDEDUCE_CATALOG_CATALOG_H_
#define HYPERDEDUCE_CATALOG_CATALOG_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/catalog/dictionary.h"
#include "hyperdeduce/catalog/relation.h"

namespace hyperdeduce {

// Owns the global dictionary and every relation, base (EDB) and derived (IDB).
// The catalog is the single source of truth for schema and data; plans and
// backends refer to relations by name and read them through the catalog.
class Catalog {
public:
  Dictionary &dictionary() { return dictionary_; }
  const Dictionary &dictionary() const { return dictionary_; }

  // Creates a relation. Fails if the name already exists.
  StatusOr<Relation *> CreateRelation(const std::string &name,
                                      std::vector<ColumnSchema> schema);

  // Returns the relation, or nullptr when absent.
  Relation *GetRelation(const std::string &name);
  const Relation *GetRelation(const std::string &name) const;

  // Returns the existing relation, or creates it with the given schema.
  StatusOr<Relation *> GetOrCreateRelation(const std::string &name,
                                           std::vector<ColumnSchema> schema);

  std::vector<std::string> RelationNames() const;

private:
  Dictionary dictionary_;
  std::unordered_map<std::string, std::unique_ptr<Relation>> relations_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_CATALOG_CATALOG_H_