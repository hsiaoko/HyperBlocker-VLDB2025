#include <set>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "hyperdeduce/backend/cpu/cpu_backend.h"
#include "hyperdeduce/engine/engine.h"

#ifdef HYPERDEDUCE_WITH_CUDA
#include "hyperdeduce/backend/gpu/gpu_backend.h"
#endif

namespace hyperdeduce {
namespace {

std::set<std::pair<std::string, std::string>> RunPairs(
    const std::string &rules, Backend *backend, const std::string &name) {
  Engine engine;
  EXPECT_TRUE(engine.AddRules(rules).ok());
  EXPECT_TRUE(engine.Execute(backend).ok());
  const Relation *relation = engine.GetRelation(name);
  const Dictionary &dictionary = engine.catalog().dictionary();
  std::set<std::pair<std::string, std::string>> rows;
  for (std::size_t r = 0; r < relation->num_rows(); ++r) {
    rows.emplace(dictionary.Lookup(relation->at(0, r)),
                 dictionary.Lookup(relation->at(1, r)));
  }
  return rows;
}

const char kClosureRules[] =
    "parent(\"a\", \"b\").\n"
    "parent(\"b\", \"c\").\n"
    "parent(\"c\", \"d\").\n"
    "parent(\"d\", \"e\").\n"
    "ancestor(x, y) :- parent(x, y).\n"
    "ancestor(x, z) :- parent(x, y), ancestor(y, z).";

#ifdef HYPERDEDUCE_WITH_CUDA

TEST(GpuBackendTest, MatchesCpuOnTransitiveClosure) {
  CpuBackend cpu;
  GpuBackend gpu(1);
  EXPECT_EQ(RunPairs(kClosureRules, &cpu, "ancestor"),
            RunPairs(kClosureRules, &gpu, "ancestor"));
}

TEST(GpuBackendTest, FourDevicesMatchCpu) {
  CpuBackend cpu;
  GpuBackend gpu(4);
  EXPECT_EQ(RunPairs(kClosureRules, &cpu, "ancestor"),
            RunPairs(kClosureRules, &gpu, "ancestor"));
}

TEST(GpuBackendTest, BuiltinFilterMatchesCpu) {
  const char rules[] =
      "person(\"alice\", 20).\n"
      "person(\"bob\", 15).\n"
      "person(\"carol\", 30).\n"
      "adult_pair(x, y) :- person(x, a), person(y, b), a >= 18, b >= 18, x != y.";
  CpuBackend cpu;
  GpuBackend gpu(1);
  EXPECT_EQ(RunPairs(rules, &cpu, "adult_pair"),
            RunPairs(rules, &gpu, "adult_pair"));
}

#else

TEST(GpuBackendTest, NotBuilt) {
  GTEST_SKIP() << "built without CUDA";
}

#endif

} // namespace
} // namespace hyperdeduce