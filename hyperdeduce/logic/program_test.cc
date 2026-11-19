#include "hyperdeduce/logic/program.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "hyperdeduce/parser/parser.h"

namespace hyperdeduce {
namespace {

StatusOr<Program> BuildFromText(const std::string &text) {
  auto rules = ParseProgram(text);
  if (!rules.ok()) {
    return rules.status();
  }
  return BuildProgram(rules.value());
}

int StratumIndex(const Program &program, const std::string &name) {
  const auto &strata = program.strata();
  for (std::size_t i = 0; i < strata.size(); ++i) {
    if (std::find(strata[i].begin(), strata[i].end(), name) != strata[i].end()) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

TEST(ProgramTest, TransitiveClosureIsRecursive) {
  auto program = BuildFromText(
      "ancestor(x, y) :- parent(x, y).\n"
      "ancestor(x, z) :- parent(x, y), ancestor(y, z).");
  ASSERT_TRUE(program.ok()) << program.status().message();
  const PredicateInfo *ancestor = program.value().FindPredicate("ancestor");
  ASSERT_NE(nullptr, ancestor);
  EXPECT_TRUE(ancestor->is_idb);
  EXPECT_TRUE(ancestor->recursive);
  const PredicateInfo *parent = program.value().FindPredicate("parent");
  EXPECT_FALSE(parent->is_idb);
  EXPECT_EQ(1u, program.value().strata().size());
}

TEST(ProgramTest, StrataOrderDependenciesFirst) {
  auto program = BuildFromText(
      "a(x) :- b(x).\n"
      "b(x) :- c(x).");
  ASSERT_TRUE(program.ok()) << program.status().message();
  EXPECT_LT(StratumIndex(program.value(), "b"), StratumIndex(program.value(), "a"));
  EXPECT_FALSE(program.value().FindPredicate("a")->recursive);
}

TEST(ProgramTest, MutualRecursionFormsOneStratum) {
  auto program = BuildFromText(
      "p(x) :- q(x).\n"
      "q(x) :- p(x).");
  ASSERT_TRUE(program.ok()) << program.status().message();
  ASSERT_EQ(1u, program.value().strata().size());
  EXPECT_EQ(2u, program.value().strata()[0].size());
  EXPECT_TRUE(program.value().FindPredicate("p")->recursive);
  EXPECT_TRUE(program.value().FindPredicate("q")->recursive);
}

TEST(ProgramTest, StrataNamesValidWhenEdbInterleaved) {
  // base (EDB) appears between two IDB heads in first-appearance order. The
  // IDB dependency graph must use a dense numbering, not predicates_ slots.
  auto program = BuildFromText(
      "a(x) :- base(x).\n"
      "b(x) :- a(x), base(x).");
  ASSERT_TRUE(program.ok()) << program.status().message();
  const auto &strata = program.value().strata();
  ASSERT_EQ(2u, strata.size());
  for (const auto &stratum : strata) {
    for (const std::string &name : stratum) {
      EXPECT_FALSE(name.empty());
    }
  }
  EXPECT_LT(StratumIndex(program.value(), "a"), StratumIndex(program.value(), "b"));
}

TEST(ProgramTest, RejectsUnsafeHeadVariable) {
  auto program = BuildFromText("p(x, y) :- q(x).");
  EXPECT_FALSE(program.ok());
}

TEST(ProgramTest, RejectsUnsafeBuiltinVariable) {
  auto program = BuildFromText("p(x) :- q(x), y > 3.");
  EXPECT_FALSE(program.ok());
}

TEST(ProgramTest, RejectsArityConflict) {
  auto program = BuildFromText("p(x) :- q(x).\n r(x) :- p(x, y).");
  EXPECT_FALSE(program.ok());
}

TEST(ProgramTest, RejectsBuiltinHead) {
  auto program = BuildFromText(">(x, y) :- q(x, y).");
  EXPECT_FALSE(program.ok());
}

TEST(ProgramTest, FactWithVariableIsUnsafe) {
  auto program = BuildFromText("p(x).");
  EXPECT_FALSE(program.ok());
}

} // namespace
} // namespace hyperdeduce