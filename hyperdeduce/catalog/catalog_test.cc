#include "hyperdeduce/catalog/catalog.h"

#include <gtest/gtest.h>

namespace hyperdeduce {
namespace {

TEST(DictionaryTest, InternsStableIds) {
  Dictionary dictionary;
  const SymbolId a = dictionary.Intern("alice");
  const SymbolId b = dictionary.Intern("bob");
  EXPECT_EQ(a, dictionary.Intern("alice"));
  EXPECT_NE(a, b);
  EXPECT_EQ("alice", dictionary.Lookup(a));
  EXPECT_EQ("bob", dictionary.Lookup(b));
  EXPECT_EQ(2u, dictionary.size());
}

TEST(RelationTest, AppendsRowsAndColumns) {
  Relation relation("parent", {{"x", DataType::kString}, {"y", DataType::kString}});
  ASSERT_TRUE(relation.AppendRow({1, 2}).ok());
  ASSERT_TRUE(relation.AppendColumns({{3, 5}, {4, 6}}, 2).ok());
  EXPECT_EQ(3u, relation.num_rows());
  EXPECT_EQ(2u, relation.num_columns());
  EXPECT_EQ(5u, relation.at(0, 2));
  EXPECT_EQ(6u, relation.at(1, 2));
}

TEST(RelationTest, RejectsWrongWidth) {
  Relation relation("r", {{"a", DataType::kInt64}});
  EXPECT_FALSE(relation.AppendRow({1, 2}).ok());
  EXPECT_FALSE(relation.AppendColumns({{1}, {2}}, 1).ok());
}

TEST(CatalogTest, CreateGetAndDuplicate) {
  Catalog catalog;
  auto created = catalog.CreateRelation("r", {{"a", DataType::kInt64}});
  ASSERT_TRUE(created.ok());
  EXPECT_EQ(created.value(), catalog.GetRelation("r"));
  EXPECT_EQ(nullptr, catalog.GetRelation("missing"));
  EXPECT_FALSE(catalog.CreateRelation("r", {{"a", DataType::kInt64}}).ok());

  auto again = catalog.GetOrCreateRelation("r", {{"a", DataType::kInt64}});
  ASSERT_TRUE(again.ok());
  EXPECT_EQ(created.value(), again.value());
}

} // namespace
} // namespace hyperdeduce