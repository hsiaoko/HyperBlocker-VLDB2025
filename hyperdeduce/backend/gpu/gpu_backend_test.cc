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

TEST(GpuBackendTest, SimilarityFilterAfterEqualityMatchesCpu) {
  // A shared year variable makes the equality a join edge; the similarity then
  // sits in filter position on the joined tuple (the equalities-first form).
  const char rules[] =
      "docA(\"a1\", \"martha\", 2020).\n"
      "docA(\"a2\", \"martha\", 2021).\n"
      "docB(\"b1\", \"marhta\", 2020).\n"
      "docB(\"b2\", \"marhta\", 2021).\n"
      "match(a, b) :- docA(a, t1, y), docB(b, t2, y), jaro_winkler(t1, t2, 0.9).";
  CpuBackend cpu;
  GpuBackend gpu1(1);
  GpuBackend gpu4(4);
  const auto expected = RunPairs(rules, &cpu, "match");
  EXPECT_EQ(expected, RunPairs(rules, &gpu1, "match"));
  EXPECT_EQ(expected, RunPairs(rules, &gpu4, "match"));
}

TEST(GpuBackendTest, LongStringsMatchCpu) {
  // Strings past the old 256-char device cap: the difference lies beyond 256,
  // so truncation would drop a pair that full-string similarity keeps.
  std::string a(256, 'A');
  std::string b(256, 'B');
  for (int i = 0; i < 40; ++i) {
    a += "common";
    b += "common";
  }
  const std::string rules =
      "docA(\"a1\", \"" + a + "\").\n" +
      "docB(\"b1\", \"" + b + "\").\n" +
      "match(x, y) :- docA(x, t1), docB(y, t2), jaro_winkler(t1, t2, 0.6).";
  CpuBackend cpu;
  GpuBackend gpu1(1);
  GpuBackend gpu4(4);
  const auto expected = RunPairs(rules, &cpu, "match");
  EXPECT_EQ(1u, expected.size());
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