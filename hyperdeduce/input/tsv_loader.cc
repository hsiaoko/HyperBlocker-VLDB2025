#include "hyperdeduce/input/tsv_loader.h"

#include "hyperdeduce/io/csv.h"

namespace hyperdeduce {

StatusOr<Relation *> TsvLoader::Load(const std::string &path,
                                     const std::string &name, Catalog *catalog,
                                     const LoadOptions &options) const {
  CsvOptions csv_options;
  csv_options.has_header = options.has_header;
  csv_options.separator = '\t';
  csv_options.column_types = options.column_types;
  return LoadCsv(path, name, catalog, csv_options);
}

} // namespace hyperdeduce