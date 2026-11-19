#include "hyperdeduce/plan/optimizer.h"

#include <string>

#include <gtest/gtest.h>

#include "hyperdeduce/parser/parser.h"

namespace hyperdeduce {
namespace {

Relation *MakeRelation(Catalog *catalog, const std::string &name,
                       std::size_t nrows, std::size_t distinct_y) {
  auto created = catalog->CreateRelation(
      name, {{"x", DataType::kInt64}, {"y", DataType::kInt64}});
  std::vector<Cell> xs, ys;
  for (std::size_t i = 0; i < nrows; ++i) {
    xs.push_back(Int64ToCell(static_cast<std::int64_t>(i)));
    ys.push_back(Int64ToCell(static_cast<std::int64_t>(i % distinct_y)));
  }
  created.value()->AppendColumns({xs, ys}, nrows);
  return created.value();
}

const PhysicalOp &LeftmostScan(const PhysicalOp &op) {
  const PhysicalOp *node = &op;
  while (node->kind != OpKind::kScan) {
    node = node->inputs.at(0).get();
  }
  return *node;
}

TEST(OptimizerTest, JoinOrderFollowsStatistics) {
  Catalog catalog;
  MakeRelation(&catalog, "big", 1000, 1000);
  MakeRelation(&catalog, "small", 10, 10);
  Statistics stats;
  stats.Compute(catalog);

  auto rules = ParseProgram("out(x, y) :- big(x, y), small(y, x).");
  ASSERT_TRUE(rules.ok());
  auto plan = BuildRulePlan(rules.value()[0], catalog, stats, catalog.dictionary());
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_EQ("small", LeftmostScan(*plan.value()).source_relation);

  // Reverse the sizes and the chosen first scan must flip.
  Catalog catalog2;
  MakeRelation(&catalog2, "big", 10, 10);
  MakeRelation(&catalog2, "small", 1000, 1000);
  Statistics stats2;
  stats2.Compute(catalog2);
  auto plan2 = BuildRulePlan(rules.value()[0], catalog2, stats2, catalog2.dictionary());
  ASSERT_TRUE(plan2.ok());
  EXPECT_EQ("big", LeftmostScan(*plan2.value()).source_relation);
}

TEST(OptimizerTest, HeadProjectionKeepsHeadOrder) {
  Catalog catalog;
  MakeRelation(&catalog, "r", 5, 5);
  Statistics stats;
  stats.Compute(catalog);
  auto rules = ParseProgram("out(y, x) :- r(x, y).");
  ASSERT_TRUE(rules.ok());
  auto plan = BuildRulePlan(rules.value()[0], catalog, stats, catalog.dictionary());
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  ASSERT_EQ(2u, plan.value()->output_schema.size());
  EXPECT_EQ("y", plan.value()->output_schema[0].var);
  EXPECT_EQ("x", plan.value()->output_schema[1].var);
}

TEST(OptimizerTest, ConstantBecomesScanSelection) {
  Catalog catalog;
  MakeRelation(&catalog, "r", 5, 5);
  Statistics stats;
  stats.Compute(catalog);
  auto rules = ParseProgram("out(x) :- r(x, 3).");
  ASSERT_TRUE(rules.ok());
  auto plan = BuildRulePlan(rules.value()[0], catalog, stats, catalog.dictionary());
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  const std::string text = Explain(*plan.value());
  EXPECT_NE(std::string::npos, text.find("Filter"));
  EXPECT_NE(std::string::npos, text.find("Scan"));
}

TEST(OptimizerTest, DeltaFlagMarksOneScan) {
  Catalog catalog;
  MakeRelation(&catalog, "r", 5, 5);
  Statistics stats;
  stats.Compute(catalog);
  auto rules = ParseProgram("out(x) :- r(x, y), r(y, x).");
  ASSERT_TRUE(rules.ok());
  auto plan = BuildRulePlan(rules.value()[0], catalog, stats, catalog.dictionary(), 1);
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  const std::string text = Explain(*plan.value());
  EXPECT_NE(std::string::npos, text.find("delta"));
}

TEST(OptimizerTest, FactPlansAsUnitProjection) {
  Catalog catalog;
  Statistics stats;
  stats.Compute(catalog);
  auto rules = ParseProgram("p(\"alice\", 1).");
  ASSERT_TRUE(rules.ok());
  auto plan = BuildRulePlan(rules.value()[0], catalog, stats, catalog.dictionary());
  ASSERT_TRUE(plan.ok()) << plan.status().message();
  EXPECT_EQ(2u, plan.value()->output_schema.size());
  EXPECT_EQ("", LeftmostScan(*plan.value()).source_relation);
}

} // namespace
} // namespace hyperdeduce