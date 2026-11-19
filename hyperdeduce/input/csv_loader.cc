#include "hyperdeduce/input/csv_loader.h"

#include "hyperdeduce/io/csv.h"

namespace hyperdeduce {

StatusOr<Relation *> CsvLoader::Load(const std::string &path,
                                     const std::string &name, Catalog *catalog,
                                     const LoadOptions &options) const {
  CsvOptions csv_options;
  csv_options.has_header = options.has_header;
  csv_options.separator = options.separator;
  csv_options.column_types = options.column_types;
  return LoadCsv(path, name, catalog, csv_options);
}

} // namespace hyperdeduce