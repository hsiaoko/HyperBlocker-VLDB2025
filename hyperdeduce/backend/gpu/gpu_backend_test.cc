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

#ifdef HYPERDEDUCE_WITH_CUDA

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

TEST(GpuBackendTest, SimilarityJoinMatchesCpu) {
  const char rules[] =
      "docA(\"a1\", \"martha\").\n"
      "docA(\"a2\", \"dixon\").\n"
      "docA(\"a3\", \"algorithm\").\n"
      "docB(\"b1\", \"marhta\").\n"
      "docB(\"b2\", \"dicksonx\").\n"
      "docB(\"b3\", \"algorithm\").\n"
      "match(a, b) :- docA(a, t1), docB(b, t2), jaro_winkler(t1, t2, 0.85).";
  CpuBackend cpu;
  GpuBackend gpu1(1);
  GpuBackend gpu4(4);
  const auto expected = RunPairs(rules, &cpu, "match");
  EXPECT_EQ(expected, RunPairs(rules, &gpu1, "match"));
  EXPECT_EQ(expected, RunPairs(rules, &gpu4, "match"));
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