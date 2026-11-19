#ifndef HYPERDEDUCE_IO_CSV_H_
#define HYPERDEDUCE_IO_CSV_H_

#include <string>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/base/types.h"
#include "hyperdeduce/catalog/catalog.h"

namespace hyperdeduce {

struct CsvOptions {
  // When true, the first row supplies column names and is not data.
  bool has_header = false;
  char separator = ',';
  // Optional per-column type override. Empty means infer from the data.
  std::vector<DataType> column_types;
};

// Reads a CSV file into a new catalog relation. String cells are interned in the
// catalog dictionary; numeric cells store native bits. Column types are inferred
// unless CsvOptions::column_types is set.
StatusOr<Relation *> LoadCsv(const std::string &path, const std::string &name,
                             Catalog *catalog, const CsvOptions &options = {});

// Writes a relation to CSV, decoding string ids through the dictionary.
Status WriteCsv(const Relation &relation, const Dictionary &dictionary,
                const std::string &path, char separator = ',');

} // namespace hyperdeduce

#endif // HYPERDEDUCE_IO_CSV_H_