#ifndef HYPERDEDUCE_CATALOG_RELATION_H_
#define HYPERDEDUCE_CATALOG_RELATION_H_

#include <cstddef>
#include <string>
#include <vector>

#include "hyperdeduce/base/status.h"
#include "hyperdeduce/base/types.h"

namespace hyperdeduce {

struct ColumnSchema {
  std::string name;
  DataType type = DataType::kString;
};

// A named, columnar relation. Every column is a vector of 64-bit cells whose
// meaning comes from the column's DataType.
//
// Ownership invariant: the engine is the single writer. A backend reads a
// relation through a const reference only while the engine is blocked inside one
// synchronous evaluation step, so reads never race with appends. This keeps
// snapshots free and appends amortized O(1) instead of copy-on-write.
class Relation {
public:
  Relation(std::string name, std::vector<ColumnSchema> schema);

  const std::string &name() const { return name_; }

  std::size_t num_rows() const { return num_rows_; }

  std::size_t num_columns() const { return schema_.size(); }

  const std::vector<ColumnSchema> &schema() const { return schema_; }

  const ColumnSchema &column(std::size_t index) const { return schema_[index]; }

  const std::vector<Cell> &column_data(std::size_t index) const {
    return columns_[index];
  }

  Cell at(std::size_t col, std::size_t row) const {
    return columns_[col][row];
  }

  // Appends one row. The row width must equal the schema width.
  Status AppendRow(const std::vector<Cell> &row);

  // Appends a columnar batch. Each column must carry nrows cells.
  Status AppendColumns(const std::vector<std::vector<Cell>> &columns,
                       std::size_t nrows);

  void Reserve(std::size_t nrows);

private:
  std::string name_;
  std::vector<ColumnSchema> schema_;
  std::vector<std::vector<Cell>> columns_;
  std::size_t num_rows_ = 0;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_CATALOG_RELATION_H_