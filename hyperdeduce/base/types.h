#ifndef HYPERDEDUCE_BASE_TYPES_H_
#define HYPERDEDUCE_BASE_TYPES_H_

#include <cstdint>
#include <cstring>
#include <string>

namespace hyperdeduce {

// Every column stores 64-bit cells. The column's DataType tells how to read a
// cell: native integer bits, native double bits, or a global dictionary id for a
// string. Uniform 64-bit cells make every equi-join an integer join, which is
// what lets the same plan run on CPUs and GPUs.
enum class DataType { kInt64, kDouble, kString };

using Cell = std::uint64_t;
using SymbolId = std::uint64_t;
using RowId = std::uint64_t;

inline Cell Int64ToCell(std::int64_t value) {
  return static_cast<Cell>(value);
}

inline std::int64_t CellToInt64(Cell cell) {
  return static_cast<std::int64_t>(cell);
}

// C++17 has no std::bit_cast, so copy the object representation through memcpy.
inline Cell DoubleToCell(double value) {
  Cell cell = 0;
  std::memcpy(&cell, &value, sizeof(cell));
  return cell;
}

inline double CellToDouble(Cell cell) {
  double value = 0;
  std::memcpy(&value, &cell, sizeof(value));
  return value;
}

inline const char *DataTypeName(DataType type) {
  switch (type) {
  case DataType::kInt64:
    return "int64";
  case DataType::kDouble:
    return "double";
  case DataType::kString:
    return "string";
  }
  return "unknown";
}

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BASE_TYPES_H_