#include "hyperdeduce/catalog/relation.h"

#include <utility>

namespace hyperdeduce {

Relation::Relation(std::string name, std::vector<ColumnSchema> schema)
    : name_(std::move(name)), schema_(std::move(schema)) {
  columns_.resize(schema_.size());
}

Status Relation::AppendRow(const std::vector<Cell> &row) {
  if (row.size() != schema_.size()) {
    return Status::Error("row width does not match relation schema");
  }
  for (std::size_t i = 0; i < schema_.size(); ++i) {
    columns_[i].push_back(row[i]);
  }
  ++num_rows_;
  return Status::Ok();
}

Status Relation::AppendColumns(const std::vector<std::vector<Cell>> &columns,
                               std::size_t nrows) {
  if (columns.size() != schema_.size()) {
    return Status::Error("batch column count does not match relation schema");
  }
  for (std::size_t i = 0; i < schema_.size(); ++i) {
    if (columns[i].size() != nrows) {
      return Status::Error("batch column height does not match nrows");
    }
    columns_[i].insert(columns_[i].end(), columns[i].begin(), columns[i].end());
  }
  num_rows_ += nrows;
  return Status::Ok();
}

void Relation::Reserve(std::size_t nrows) {
  for (auto &column : columns_) {
    column.reserve(nrows);
  }
}

} // namespace hyperdeduce