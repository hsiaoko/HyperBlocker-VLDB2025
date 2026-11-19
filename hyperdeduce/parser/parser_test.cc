#include "hyperdeduce/parser/parser.h"

#include <gtest/gtest.h>

namespace hyperdeduce {
namespace {

TEST(ParserTest, ParsesFactWithConstants) {
  auto program = ParseProgram("parent(\"alice\", \"bob\").");
  ASSERT_TRUE(program.ok()) << program.status().message();
  ASSERT_EQ(1u, program.value().size());
  const Rule &rule = program.value()[0];
  EXPECT_EQ("parent", rule.head.predicate);
  EXPECT_TRUE(rule.body.empty());
  ASSERT_EQ(2u, rule.head.terms.size());
  EXPECT_FALSE(rule.head.terms[0].is_variable);
  EXPECT_EQ("alice", rule.head.terms[0].constant.text);
}

TEST(ParserTest, ParsesRecursiveRule) {
  auto program = ParseProgram(
      "ancestor(x, z) :- parent(x, y), ancestor(y, z).");
  ASSERT_TRUE(program.ok()) << program.status().message();
  const Rule &rule = program.value()[0];
  EXPECT_EQ("ancestor", rule.head.predicate);
  ASSERT_EQ(2u, rule.body.size());
  EXPECT_EQ("parent", rule.body[0].predicate);
  EXPECT_TRUE(rule.body[0].terms[0].is_variable);
  EXPECT_EQ("x", rule.body[0].terms[0].variable);
}

TEST(ParserTest, ParsesInfixComparison) {
  auto program = ParseProgram("adult(x) :- person(x, a), a >= 18.");
  ASSERT_TRUE(program.ok()) << program.status().message();
  const Rule &rule = program.value()[0];
  ASSERT_EQ(2u, rule.body.size());
  EXPECT_EQ(">=", rule.body[1].predicate);
  ASSERT_EQ(2u, rule.body[1].terms.size());
  EXPECT_EQ("a", rule.body[1].terms[0].variable);
  EXPECT_EQ(DataType::kInt64, rule.body[1].terms[1].constant.type);
  EXPECT_EQ(18, rule.body[1].terms[1].constant.int_value);
}

TEST(ParserTest, ParsesNumericAndZeroAry) {
  auto program = ParseProgram("p(x, 1.5) :- q(x), flag.");
  ASSERT_TRUE(program.ok()) << program.status().message();
  const Rule &rule = program.value()[0];
  EXPECT_EQ(DataType::kDouble, rule.head.terms[1].constant.type);
  EXPECT_DOUBLE_EQ(1.5, rule.head.terms[1].constant.double_value);
  EXPECT_TRUE(rule.body[1].terms.empty());
  EXPECT_EQ("flag", rule.body[1].predicate);
}

TEST(ParserTest, SkipsComments) {
  auto program = ParseProgram(
      "// a comment\np(x) :- q(x). % another\n");
  ASSERT_TRUE(program.ok()) << program.status().message();
  EXPECT_EQ(1u, program.value().size());
}

TEST(ParserTest, ReportsErrors) {
  EXPECT_FALSE(ParseProgram("p(x) :- q(x)").ok());          // missing dot
  EXPECT_FALSE(ParseProgram("p(\"unterminated).").ok());    // bad string
  EXPECT_FALSE(ParseProgram("p(x) :- .").ok());             // empty body
  EXPECT_FALSE(ParseProgram("p(x) :- q(x), .").ok());       // trailing comma
}

} // namespace
} // namespace hyperdeduce