#include "hyperdeduce/plan/stats.h"

#include <unordered_set>

namespace hyperdeduce {

void Statistics::Compute(const Catalog &catalog) {
  stats_.clear();
  for (const std::string &name : catalog.RelationNames()) {
    const Relation *relation = catalog.GetRelation(name);
    RelationStats stats;
    stats.rows = relation->num_rows();
    stats.distinct.resize(relation->num_columns());
    for (std::size_t c = 0; c < relation->num_columns(); ++c) {
      std::unordered_set<Cell> values;
      for (Cell cell : relation->column_data(c)) {
        values.insert(cell);
      }
      stats.distinct[c] = values.size();
    }
    stats_.emplace(name, std::move(stats));
  }
}

const RelationStats *Statistics::Find(const std::string &name) const {
  auto it = stats_.find(name);
  return it == stats_.end() ? nullptr : &it->second;
}

} // namespace hyperdeduce