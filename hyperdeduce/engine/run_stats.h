#ifndef HYPERDEDUCE_ENGINE_RUN_STATS_H_
#define HYPERDEDUCE_ENGINE_RUN_STATS_H_

#include <cstddef>
#include <string>
#include <vector>

#include "hyperdeduce/base/types.h"

namespace hyperdeduce {

// Per-relation load statistics.
struct RelationLoadStats {
  std::string name;
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<DataType> types;
  double load_ms = 0;
};

// Per-stratum evaluation statistics.
struct StratumStats {
  std::vector<std::string> predicates;
  bool recursive = false;
  int rounds = 0;
  std::size_t facts_derived = 0;
  double eval_ms = 0;
};

// GPU activity collected during evaluation.
struct GpuStats {
  int devices = 0;
  double kernel_ms = 0;     // summed similarity-join kernel time
  std::size_t pairs = 0;    // pairs evaluated by similarity joins
  double util_avg = 0;      // sampled GPU utilization, percent
  double util_max = 0;
};

// Everything worth knowing about one engine run. The engine fills it as it
// loads, plans, and evaluates; FormatReport renders it for the user.
struct RunStats {
  std::vector<RelationLoadStats> loads;
  int rule_count = 0;
  int stratum_count = 0;
  double load_ms = 0;
  double plan_ms = 0;
  double eval_ms = 0;
  double total_ms = 0;
  std::string backend;
  std::vector<StratumStats> strata;
  GpuStats gpu;
  std::vector<std::pair<std::string, std::size_t>> outputs;
};

std::string FormatReport(const RunStats &stats);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_ENGINE_RUN_STATS_H_