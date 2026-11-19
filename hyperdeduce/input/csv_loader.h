#ifndef HYPERDEDUCE_INPUT_CSV_LOADER_H_
#define HYPERDEDUCE_INPUT_CSV_LOADER_H_

#include <string>
#include <vector>

#include "hyperdeduce/input/relation_loader.h"

namespace hyperdeduce {

// Loads comma-separated files. A thin adapter over io/csv.
class CsvLoader : public RelationLoader {
public:
  std::string name() const override { return "csv"; }

  std::vector<std::string> extensions() const override { return {"csv"}; }

  StatusOr<Relation *> Load(const std::string &path, const std::string &name,
                            Catalog *catalog,
                            const LoadOptions &options) const override;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_INPUT_CSV_LOADER_H_