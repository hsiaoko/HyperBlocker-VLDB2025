#include "hyperdeduce/engine/engine.h"

#include <set>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "hyperdeduce/backend/cpu/cpu_backend.h"

namespace hyperdeduce {
namespace {

std::set<std::pair<std::string, std::string>> StringPairs(
    const Engine &engine, const std::string &name) {
  const Relation *relation = engine.GetRelation(name);
  const Dictionary &dictionary = engine.catalog().dictionary();
  std::set<std::pair<std::string, std::string>> rows;
  for (std::size_t r = 0; r < relation->num_rows(); ++r) {
    rows.emplace(dictionary.Lookup(relation->at(0, r)),
                 dictionary.Lookup(relation->at(1, r)));
  }
  return rows;
}

TEST(EngineTest, TransitiveClosureFixpoint) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "parent(\"a\", \"b\").\n"
      "parent(\"b\", \"c\").\n"
      "parent(\"c\", \"d\").\n"
      "ancestor(x, y) :- parent(x, y).\n"
      "ancestor(x, z) :- parent(x, y), ancestor(y, z).").ok());
  auto plan = engine.BuildPlan();
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());

  const auto rows = StringPairs(engine, "ancestor");
  const std::set<std::pair<std::string, std::string>> expected = {
      {"a", "b"}, {"a", "c"}, {"a", "d"},
      {"b", "c"}, {"b", "d"}, {"c", "d"}};
  EXPECT_EQ(expected, rows);
}

TEST(EngineTest, BuiltinComparisonFilter) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "person(\"alice\", 20).\n"
      "person(\"bob\", 15).\n"
      "person(\"carol\", 30).\n"
      "adult(x) :- person(x, a), a >= 18.").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const Relation *adult = engine.GetRelation("adult");
  ASSERT_EQ(2u, adult->num_rows());
  std::set<std::string> names;
  for (std::size_t r = 0; r < adult->num_rows(); ++r) {
    names.insert(engine.catalog().dictionary().Lookup(adult->at(0, r)));
  }
  EXPECT_EQ(std::set<std::string>({"alice", "carol"}), names);
}

TEST(EngineTest, JoinAcrossRelations) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "edge(\"a\", \"b\").\n"
      "edge(\"b\", \"c\").\n"
      "label(\"b\", \"mid\").\n"
      "label(\"c\", \"end\").\n"
      "reaches(x, l) :- edge(x, y), label(y, l).").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const auto rows = StringPairs(engine, "reaches");
  const std::set<std::pair<std::string, std::string>> expected = {
      {"a", "mid"}, {"b", "end"}};
  EXPECT_EQ(expected, rows);
}

TEST(EngineTest, RecursiveNumericGraph) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "edge(1, 2).\n"
      "edge(2, 3).\n"
      "edge(3, 4).\n"
      "path(x, y) :- edge(x, y).\n"
      "path(x, z) :- edge(x, y), path(y, z).").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const Relation *path = engine.GetRelation("path");
  EXPECT_EQ(6u, path->num_rows());
  std::set<std::pair<std::int64_t, std::int64_t>> rows;
  for (std::size_t r = 0; r < path->num_rows(); ++r) {
    rows.emplace(CellToInt64(path->at(0, r)), CellToInt64(path->at(1, r)));
  }
  EXPECT_TRUE(rows.count({1, 4}));
  EXPECT_TRUE(rows.count({2, 4}));
}

TEST(EngineTest, MutualRecursionFixpoint) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "edge(\"a\", \"b\").\n"
      "edge(\"b\", \"c\").\n"
      "even(x, y) :- edge(x, y).\n"
      "even(x, z) :- edge(x, y), odd(y, z).\n"
      "odd(x, z) :- edge(x, y), even(y, z).").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  EXPECT_EQ(2u, engine.GetRelation("even")->num_rows());
  EXPECT_EQ(1u, engine.GetRelation("odd")->num_rows());
}

TEST(EngineTest, SimilarityJoinIsABinaryOperator) {
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "docA(\"a1\", \"martha\").\n"
      "docA(\"a2\", \"dixon\").\n"
      "docB(\"b1\", \"marhta\").\n"
      "docB(\"b2\", \"dicksonx\").\n"
      "match(a, b) :- docA(a, t1), docB(b, t2), jaro_winkler(t1, t2, 0.9).").ok());
  auto plan = engine.BuildPlan();
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_NE(std::string::npos, plan.value().find("SimilarityJoin"));

  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const auto rows = StringPairs(engine, "match");
  const std::set<std::pair<std::string, std::string>> expected = {{"a1", "b1"}};
  EXPECT_EQ(expected, rows);
}

TEST(EngineTest, SimilarityJoinWithEqualityFirst) {
  // An equality join on year narrows candidates before the similarity join.
  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "docA(\"a1\", \"martha\", 2020).\n"
      "docA(\"a2\", \"martha\", 2021).\n"
      "docB(\"b1\", \"marhta\", 2020).\n"
      "docB(\"b2\", \"marhta\", 2021).\n"
      "match(a, b) :- docA(a, t1, y), docB(b, t2, y), jaro_winkler(t1, t2, 0.9).").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const auto rows = StringPairs(engine, "match");
  const std::set<std::pair<std::string, std::string>> expected = {
      {"a1", "b1"}, {"a2", "b2"}};
  EXPECT_EQ(expected, rows);
}

} // namespace
} // namespace hyperdeduce