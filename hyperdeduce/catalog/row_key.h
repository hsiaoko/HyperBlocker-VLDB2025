#ifndef HYPERDEDUCE_CATALOG_ROW_KEY_H_
#define HYPERDEDUCE_CATALOG_ROW_KEY_H_

#include <cstddef>
#include <vector>

#include "hyperdeduce/base/types.h"
#include "hyperdeduce/catalog/relation.h"

namespace hyperdeduce {

using RowKey = std::vector<Cell>;

// Hashes a whole row so relations can be deduplicated with a hash set.
struct KeyHash {
  std::size_t operator()(const RowKey &key) const {
    std::size_t hash = key.size();
    for (Cell cell : key) {
      hash ^= cell + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

inline RowKey RowOf(const Relation &relation, std::size_t row) {
  RowKey key(relation.num_columns());
  for (std::size_t c = 0; c < relation.num_columns(); ++c) {
    key[c] = relation.at(c, row);
  }
  return key;
}

} // namespace hyperdeduce

#endif // HYPERDEDUCE_CATALOG_ROW_KEY_H_