#include "hyperdeduce/io/csv.h"

#include <charconv>
#include <fstream>
#include <system_error>
#include <utility>

#include <rapidcsv.h>

namespace hyperdeduce {
namespace {

std::string Trim(const std::string &text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

bool TryParseInt64(const std::string &text, std::int64_t *out) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) {
    return false;
  }
  const auto result =
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), *out);
  return result.ec == std::errc{} &&
         result.ptr == trimmed.data() + trimmed.size();
}

bool TryParseDouble(const std::string &text, double *out) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) {
    return false;
  }
  const auto result =
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), *out);
  return result.ec == std::errc{} &&
         result.ptr == trimmed.data() + trimmed.size();
}

DataType InferColumnType(const std::vector<std::string> &column) {
  bool all_int = true;
  bool all_double = true;
  std::int64_t int_value = 0;
  double double_value = 0;
  for (const std::string &cell : column) {
    if (all_int && !TryParseInt64(cell, &int_value)) {
      all_int = false;
    }
    if (all_double && !TryParseDouble(cell, &double_value)) {
      all_double = false;
    }
    if (!all_int && !all_double) {
      return DataType::kString;
    }
  }
  if (all_int) {
    return DataType::kInt64;
  }
  if (all_double) {
    return DataType::kDouble;
  }
  return DataType::kString;
}

std::string QuoteCsvField(const std::string &field, char separator) {
  const bool needs_quote = field.find(separator) != std::string::npos ||
                           field.find('"') != std::string::npos ||
                           field.find('\n') != std::string::npos ||
                           field.find('\r') != std::string::npos;
  if (!needs_quote) {
    return field;
  }
  std::string quoted = "\"";
  for (char c : field) {
    if (c == '"') {
      quoted += '"';
    }
    quoted += c;
  }
  quoted += '"';
  return quoted;
}

std::string CellToText(Cell cell, DataType type, const Dictionary &dictionary) {
  switch (type) {
  case DataType::kInt64:
    return std::to_string(CellToInt64(cell));
  case DataType::kDouble:
    return std::to_string(CellToDouble(cell));
  case DataType::kString:
    return dictionary.Lookup(cell);
  }
  return "";
}

} // namespace

StatusOr<Relation *> LoadCsv(const std::string &path, const std::string &name,
                             Catalog *catalog, const CsvOptions &options) {
  rapidcsv::Document document(path, rapidcsv::LabelParams(-1, -1),
                              rapidcsv::SeparatorParams(options.separator));
  const std::size_t ncols = document.GetColumnCount();
  const std::size_t nrows = document.GetRowCount();
  if (ncols == 0) {
    return Status::Error("csv has no columns: " + path);
  }

  std::vector<std::vector<std::string>> raw(ncols);
  for (std::size_t c = 0; c < ncols; ++c) {
    raw[c] = document.GetColumn<std::string>(c);
  }

  std::size_t data_begin = 0;
  std::vector<ColumnSchema> schema(ncols);
  for (std::size_t c = 0; c < ncols; ++c) {
    schema[c].name =
        options.has_header && nrows > 0 ? raw[c][0] : "c" + std::to_string(c);
  }
  if (options.has_header) {
    data_begin = nrows > 0 ? 1 : 0;
  }

  if (!options.column_types.empty() && options.column_types.size() != ncols) {
    return Status::Error("column_types width does not match csv column count");
  }

  std::vector<std::vector<Cell>> columns(ncols);
  const std::size_t data_rows = nrows - data_begin;
  for (std::size_t c = 0; c < ncols; ++c) {
    DataType type = options.column_types.empty()
                        ? InferColumnType(std::vector<std::string>(
                              raw[c].begin() + data_begin, raw[c].end()))
                        : options.column_types[c];
    schema[c].type = type;
    columns[c].reserve(data_rows);
    for (std::size_t r = data_begin; r < nrows; ++r) {
      const std::string &text = raw[c][r];
      switch (type) {
      case DataType::kInt64: {
        std::int64_t value = 0;
        if (!TryParseInt64(text, &value)) {
          return Status::Error("cell is not int64 at column " +
                               std::to_string(c) + " row " + std::to_string(r));
        }
        columns[c].push_back(Int64ToCell(value));
        break;
      }
      case DataType::kDouble: {
        double value = 0;
        if (!TryParseDouble(text, &value)) {
          return Status::Error("cell is not double at column " +
                               std::to_string(c) + " row " + std::to_string(r));
        }
        columns[c].push_back(DoubleToCell(value));
        break;
      }
      case DataType::kString:
        columns[c].push_back(catalog->dictionary().Intern(text));
        break;
      }
    }
  }

  auto created = catalog->CreateRelation(name, std::move(schema));
  if (!created.ok()) {
    return created.status();
  }
  Relation *relation = created.value();
  Status appended = relation->AppendColumns(columns, data_rows);
  if (!appended.ok()) {
    return appended;
  }
  return relation;
}

Status WriteCsv(const Relation &relation, const Dictionary &dictionary,
                const std::string &path, char separator) {
  std::ofstream out(path);
  if (!out) {
    return Status::Error("cannot open output csv: " + path);
  }
  const std::size_t ncols = relation.num_columns();
  for (std::size_t r = 0; r < relation.num_rows(); ++r) {
    for (std::size_t c = 0; c < ncols; ++c) {
      if (c > 0) {
        out << separator;
      }
      out << QuoteCsvField(
          CellToText(relation.at(c, r), relation.column(c).type, dictionary),
          separator);
    }
    out << '\n';
  }
  return out.good() ? Status::Ok() : Status::Error("failed writing csv: " + path);
}

} // namespace hyperdeduce