#include "hyperdeduce/input/yaml_frontend.h"

#include <gtest/gtest.h>

#include "hyperdeduce/input/rule_frontend.h"

namespace hyperdeduce {
namespace {

const char kAcmYaml[] =
    "Preconditions:\n"
    "  Relations:\n"
    "    - [0, 0, 1, 2, 3, 4]\n"
    "    - [1, 0, 1, 2, 3, 4]\n"
    "  Equalities:\n"
    "    - [0, 1, 4, 4]\n"
    "  Sim:\n"
    "    - [0, 1, 1, 1, 2, 2]\n"
    "  Threshold:\n"
    "    - [0, 1, 0.81, 0.79]\n"
    "Conseq:\n"
    "  Option: Equality\n"
    "  Equality: [0, 1, 0, 0]\n";

std::vector<YamlTable> TwoTables() {
  return {{"tableA", 5}, {"tableB", 5}};
}

TEST(YamlFrontendTest, DecodesAcmRule) {
  auto rules = ParseYamlRules(kAcmYaml, TwoTables());
  ASSERT_TRUE(rules.ok()) << rules.status().message();
  ASSERT_EQ(1u, rules.value().size());
  const Rule &rule = rules.value()[0];
  EXPECT_EQ("match", rule.head.predicate);
  ASSERT_EQ(2u, rule.head.terms.size());
  EXPECT_EQ("a0", rule.head.terms[0].variable);
  EXPECT_EQ("b0", rule.head.terms[1].variable);

  ASSERT_EQ(4u, rule.body.size());
  EXPECT_EQ("tableA", rule.body[0].predicate);
  EXPECT_EQ("k0", rule.body[0].terms[4].variable);
  EXPECT_EQ("tableB", rule.body[1].predicate);
  EXPECT_EQ("k0", rule.body[1].terms[4].variable);

  EXPECT_EQ("jaro_winkler", rule.body[2].predicate);
  EXPECT_EQ("a1", rule.body[2].terms[0].variable);
  EXPECT_EQ("b1", rule.body[2].terms[1].variable);
  EXPECT_DOUBLE_EQ(0.81, rule.body[2].terms[2].constant.double_value);
  EXPECT_DOUBLE_EQ(0.79, rule.body[3].terms[2].constant.double_value);
}

TEST(YamlFrontendTest, NoEqualityHasNoSharedVariable) {
  const char yaml[] =
      "Preconditions:\n"
      "  Equalities:\n"
      "    - [0, 1]\n"
      "  Sim:\n"
      "    - [0, 1, 1, 1]\n"
      "  Threshold:\n"
      "    - [0, 1, 0.88]\n"
      "Conseq:\n"
      "  Equality: [0, 1, 0, 0]\n";
  auto rules = ParseYamlRules(yaml, TwoTables());
  ASSERT_TRUE(rules.ok()) << rules.status().message();
  const Rule &rule = rules.value()[0];
  EXPECT_EQ("a4", rule.body[0].terms[4].variable);
  EXPECT_EQ("b4", rule.body[1].terms[4].variable);
  ASSERT_EQ(3u, rule.body.size());
  EXPECT_DOUBLE_EQ(0.88, rule.body[2].terms[2].constant.double_value);
}

TEST(YamlFrontendTest, RejectsBadYaml) {
  EXPECT_FALSE(ParseYamlRules("not: [valid", TwoTables()).ok());
  EXPECT_FALSE(ParseYamlRules("Preconditions: {}\n", TwoTables()).ok());
}

TEST(FrontendRegistryTest, DispatchesByExtension) {
  const auto &registry = FrontendRegistry::Instance();
  ASSERT_NE(nullptr, registry.ByExtension("yaml"));
  EXPECT_EQ("yaml", registry.ByExtension("yaml")->name());
  EXPECT_EQ("yaml", registry.ByExtension("yml")->name());
  ASSERT_NE(nullptr, registry.ByExtension("dl"));
  EXPECT_EQ("datalog", registry.ByExtension("dl")->name());
  EXPECT_EQ(nullptr, registry.ByExtension("unknown"));
}

TEST(FrontendRegistryTest, DatalogFrontendParses) {
  const auto *frontend = FrontendRegistry::Instance().ByName("datalog");
  ASSERT_NE(nullptr, frontend);
  auto rules = frontend->Parse("p(x) :- q(x).", {});
  ASSERT_TRUE(rules.ok()) << rules.status().message();
  EXPECT_EQ(1u, rules.value().size());
}

} // namespace
} // namespace hyperdeduce