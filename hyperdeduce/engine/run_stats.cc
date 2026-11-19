#include "hyperdeduce/engine/run_stats.h"

#include <iomanip>
#include <sstream>

namespace hyperdeduce {
namespace {

std::string Ms(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << value << " ms";
  return out.str();
}

std::string TypeList(const std::vector<DataType> &types) {
  std::string text;
  for (std::size_t i = 0; i < types.size(); ++i) {
    if (i > 0) {
      text += ",";
    }
    text += DataTypeName(types[i]);
  }
  return text;
}

} // namespace

std::string FormatReport(const RunStats &stats) {
  std::ostringstream out;
  out << "=== hyperdeduce run report ===\n";

  out << "input\n";
  for (const RelationLoadStats &load : stats.loads) {
    out << "  " << load.name << ": " << load.rows << " rows x " << load.cols
        << " cols [" << TypeList(load.types) << "], load " << Ms(load.load_ms)
        << "\n";
  }
  out << "  rules: " << stats.rule_count << "\n";

  out << "plan\n";
  out << "  strata: " << stats.stratum_count << ", plan time " << Ms(stats.plan_ms)
      << "\n";

  out << "execution\n";
  out << "  backend: " << stats.backend;
  if (stats.gpu.devices > 0) {
    out << " (" << stats.gpu.devices << " device(s))";
  }
  out << "\n";
  for (const StratumStats &stratum : stats.strata) {
    out << "  stratum [";
    for (std::size_t i = 0; i < stratum.predicates.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << stratum.predicates[i];
    }
    out << "]: " << (stratum.recursive ? "recursive" : "non-recursive") << ", "
        << stratum.rounds << " round(s), " << stratum.facts_derived
        << " facts derived, " << Ms(stratum.eval_ms) << "\n";
  }
  if (stats.gpu.devices > 0) {
    out << "  gpu kernel time " << Ms(stats.gpu.kernel_ms) << ", "
        << stats.gpu.pairs << " pairs evaluated\n";
    out << "  gpu utilization avg " << std::fixed << std::setprecision(1)
        << stats.gpu.util_avg << "%, max " << stats.gpu.util_max << "%\n";
  }

  out << "timing\n";
  out << "  load " << Ms(stats.load_ms) << ", plan " << Ms(stats.plan_ms)
      << ", eval " << Ms(stats.eval_ms) << ", total " << Ms(stats.total_ms)
      << "\n";

  out << "output\n";
  for (const auto &output : stats.outputs) {
    out << "  " << output.first << ": " << output.second << " rows\n";
  }
  return out.str();
}

} // namespace hyperdeduce