#ifndef HYPERDEDUCE_INPUT_RELATION_LOADER_H_
#define HYPERDEDUCE_INPUT_RELATION_LOADER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/base/types.h"
#include "hyperdeduce/catalog/catalog.h"

namespace hyperdeduce {

// Tabular load options shared by file loaders. A loader uses the fields that
// apply to its format and ignores the rest.
struct LoadOptions {
  bool has_header = false;
  char separator = ',';
  std::vector<DataType> column_types;
};

// One data format is one special case of this interface, symmetric to
// RuleFrontend on the rule side. A loader reads a file into a new catalog
// relation. Adding a format means deriving a loader and registering it; the
// engine and CLI dispatch by extension and never hardcode a format.
class RelationLoader {
public:
  virtual ~RelationLoader() = default;

  virtual std::string name() const = 0;

  // File extensions this loader handles, without the leading dot.
  virtual std::vector<std::string> extensions() const = 0;

  virtual StatusOr<Relation *> Load(const std::string &path,
                                    const std::string &name, Catalog *catalog,
                                    const LoadOptions &options) const = 0;
};

// Selects a loader by name or file extension. CSV and TSV register here at
// startup; a new format registers once and is dispatchable everywhere.
class LoaderRegistry {
public:
  static LoaderRegistry &Instance();

  void Register(std::unique_ptr<RelationLoader> loader);

  const RelationLoader *ByName(const std::string &name) const;

  // extension without the leading dot; nullptr when unknown.
  const RelationLoader *ByExtension(const std::string &extension) const;

  std::vector<std::string> Names() const;

private:
  LoaderRegistry();

  std::unordered_map<std::string, std::unique_ptr<RelationLoader>> by_name_;
  std::unordered_map<std::string, RelationLoader *> by_extension_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_RELATION_LOADER_H_