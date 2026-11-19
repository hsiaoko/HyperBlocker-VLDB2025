#ifndef HYPERDEDUCE_INPUT_TSV_LOADER_H_
#define HYPERDEDUCE_INPUT_TSV_LOADER_H_

#include <string>
#include <vector>

#include "hyperdeduce/input/relation_loader.h"

namespace hyperdeduce {

// Loads tab-separated files. Reuses the CSV reader with a tab separator.
class TsvLoader : public RelationLoader {
public:
  std::string name() const override { return "tsv"; }

  std::vector<std::string> extensions() const override { return {"tsv"}; }

  StatusOr<Relation *> Load(const std::string &path, const std::string &name,
                            Catalog *catalog,
                            const LoadOptions &options) const override;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_TSV_LOADER_H_