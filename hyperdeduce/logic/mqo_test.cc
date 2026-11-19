#include "hyperdeduce/logic/mqo.h"

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "hyperdeduce/backend/cpu/cpu_backend.h"
#include "hyperdeduce/engine/engine.h"

namespace hyperdeduce {
namespace {

std::string WriteTemp(const std::string &name, const std::string &content) {
  const std::string path = ::testing::TempDir() + "/" + name;
  std::ofstream out(path);
  out << content;
  return path;
}

const char kRules[] =
    "match_jw(a, b) :- docA(a, t1), docB(b, t2), jaro_winkler(t1, t2, 0.85).\n"
    "match_jaccard(a, b) :- docA(a, t1), docB(b, t2), jaccard(t1, t2, 0.5).\n"
    "match_lev(a, b) :- docA(a, t1), docB(b, t2), levenshtein(t1, t2, 0.6).";

std::set<std::vector<Cell>> Rows(const Engine &engine, const std::string &name) {
  const Relation *relation = engine.GetRelation(name);
  std::set<std::vector<Cell>> rows;
  for (std::size_t r = 0; r < relation->num_rows(); ++r) {
    rows.insert(RowOf(*relation, r));
  }
  return rows;
}

bool HasSharedRelation(const Engine &engine) {
  for (const std::string &name : engine.catalog().RelationNames()) {
    if (name.rfind("__shared_", 0) == 0) {
      return true;
    }
  }
  return false;
}

TEST(MqoTest, SharingPreservesResults) {
  const std::string a = WriteTemp("mqo_docA.csv", "a1,martha\na2,dixon\na3,algorithm\n");
  const std::string b =
      WriteTemp("mqo_docB.csv", "b1,marhta\nb2,dicksonx\nb3,algorithm\n");

  Engine with_mqo;
  ASSERT_TRUE(with_mqo.LoadCsv(a, "docA").ok());
  ASSERT_TRUE(with_mqo.LoadCsv(b, "docB").ok());
  ASSERT_TRUE(with_mqo.AddRules(kRules).ok());
  with_mqo.set_mqo(true);
  ASSERT_TRUE(with_mqo.BuildPlan().ok());
  EXPECT_TRUE(HasSharedRelation(with_mqo));
  CpuBackend backend;
  ASSERT_TRUE(with_mqo.Execute(&backend).ok());

  Engine without_mqo;
  ASSERT_TRUE(without_mqo.LoadCsv(a, "docA").ok());
  ASSERT_TRUE(without_mqo.LoadCsv(b, "docB").ok());
  ASSERT_TRUE(without_mqo.AddRules(kRules).ok());
  without_mqo.set_mqo(false);
  ASSERT_TRUE(without_mqo.BuildPlan().ok());
  EXPECT_FALSE(HasSharedRelation(without_mqo));
  ASSERT_TRUE(without_mqo.Execute(&backend).ok());

  for (const std::string &name :
       {std::string("match_jw"), std::string("match_jaccard"),
        std::string("match_lev")}) {
    EXPECT_EQ(Rows(without_mqo, name), Rows(with_mqo, name)) << name;
    EXPECT_GT(Rows(with_mqo, name).size(), 0u) << name;
  }
}

TEST(MqoTest, NormalizedVariableNamesShareOneBase) {
  const std::string a = WriteTemp("mqon_docA.csv", "a1,martha\na2,dixon\na3,algorithm\n");
  const std::string b =
      WriteTemp("mqon_docB.csv", "b1,marhta\nb2,dicksonx\nb3,algorithm\n");
  // Same structure, different variable names: normalization must group them.
  const char rules[] =
      "match_jw(a, b) :- docA(a, t1), docB(b, t2), jaro_winkler(t1, t2, 0.85).\n"
      "match_renamed(x, y) :- docA(x, u), docB(y, v), jaro_winkler(u, v, 0.85).";

  Engine engine;
  ASSERT_TRUE(engine.LoadCsv(a, "docA").ok());
  ASSERT_TRUE(engine.LoadCsv(b, "docB").ok());
  ASSERT_TRUE(engine.AddRules(rules).ok());
  engine.set_mqo(true);
  ASSERT_TRUE(engine.BuildPlan().ok());

  int shared_count = 0;
  for (const std::string &name : engine.catalog().RelationNames()) {
    if (name.rfind("__shared_", 0) == 0) {
      ++shared_count;
    }
  }
  EXPECT_EQ(1, shared_count);

  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  EXPECT_EQ(Rows(engine, "match_jw"), Rows(engine, "match_renamed"));
  EXPECT_GT(Rows(engine, "match_jw").size(), 0u);
}

TEST(MqoTest, HeterogeneousTableSchemasShareCorrectly) {
  // A is [int,string], B is [string,int]: different schemas. The shared base must
  // keep per-column types, route the similarity to the string columns, and emit
  // the int columns, identically for both differently-named rules.
  const std::string a = WriteTemp("mqoh_A.csv", "1,martha\n2,dixon\n3,algorithm\n");
  const std::string b = WriteTemp("mqoh_B.csv", "marhta,10\ndicksonx,20\nalgorithm,30\n");
  const char rules[] =
      "m1(x, y) :- A(x, t1), B(t2, y), jaro_winkler(t1, t2, 0.8).\n"
      "m2(u, v) :- A(u, s1), B(s2, v), jaro_winkler(s1, s2, 0.8).";

  Engine engine;
  ASSERT_TRUE(engine.LoadCsv(a, "A").ok());
  ASSERT_TRUE(engine.LoadCsv(b, "B").ok());
  ASSERT_TRUE(engine.AddRules(rules).ok());
  engine.set_mqo(true);
  ASSERT_TRUE(engine.BuildPlan().ok());

  const Relation *shared = engine.GetRelation("__shared_0");
  ASSERT_NE(nullptr, shared);
  ASSERT_EQ(4u, shared->num_columns());
  EXPECT_EQ(DataType::kInt64, shared->column(0).type);   // A col0
  EXPECT_EQ(DataType::kString, shared->column(1).type);  // A col1
  EXPECT_EQ(DataType::kString, shared->column(2).type);  // B col0
  EXPECT_EQ(DataType::kInt64, shared->column(3).type);   // B col1

  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  EXPECT_EQ(Rows(engine, "m1"), Rows(engine, "m2"));
  EXPECT_GT(Rows(engine, "m1").size(), 0u);
  EXPECT_EQ(DataType::kInt64, engine.GetRelation("m1")->column(0).type);

  Engine without;
  ASSERT_TRUE(without.LoadCsv(a, "A").ok());
  ASSERT_TRUE(without.LoadCsv(b, "B").ok());
  ASSERT_TRUE(without.AddRules(rules).ok());
  without.set_mqo(false);
  ASSERT_TRUE(without.BuildPlan().ok());
  ASSERT_TRUE(without.Execute(&backend).ok());
  EXPECT_EQ(Rows(without, "m1"), Rows(engine, "m1"));
  EXPECT_EQ(Rows(without, "m2"), Rows(engine, "m2"));
}

} // namespace
} // namespace hyperdeduce