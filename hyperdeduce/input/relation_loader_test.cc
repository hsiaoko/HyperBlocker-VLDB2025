#include "hyperdeduce/input/relation_loader.h"

#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "hyperdeduce/engine/engine.h"

namespace hyperdeduce {
namespace {

std::string WriteTemp(const std::string &name, const std::string &content) {
  const std::string path = ::testing::TempDir() + "/" + name;
  std::ofstream out(path);
  out << content;
  return path;
}

TEST(LoaderRegistryTest, DispatchesByExtension) {
  const auto &registry = LoaderRegistry::Instance();
  ASSERT_NE(nullptr, registry.ByExtension("csv"));
  EXPECT_EQ("csv", registry.ByExtension("csv")->name());
  ASSERT_NE(nullptr, registry.ByExtension("tsv"));
  EXPECT_EQ("tsv", registry.ByExtension("tsv")->name());
  EXPECT_EQ(nullptr, registry.ByExtension("parquet"));
}

TEST(RelationLoaderTest, EngineLoadsCsvAndTsvByExtension) {
  const std::string csv = WriteTemp("loader_a.csv", "alice,20\nbob,15\n");
  const std::string tsv = WriteTemp("loader_b.tsv", "carol\t30\ndave\t17\n");
  Engine engine;
  auto a = engine.LoadRelation(csv, "people_csv");
  ASSERT_TRUE(a.ok()) << a.status().message();
  auto b = engine.LoadRelation(tsv, "people_tsv");
  ASSERT_TRUE(b.ok()) << b.status().message();
  EXPECT_EQ(2u, a.value()->num_rows());
  EXPECT_EQ(2u, b.value()->num_rows());
  EXPECT_EQ(DataType::kInt64, b.value()->column(1).type);
  EXPECT_EQ(30, CellToInt64(b.value()->at(1, 0)));
  EXPECT_EQ("carol", engine.catalog().dictionary().Lookup(b.value()->at(0, 0)));
}

TEST(RelationLoaderTest, UnknownExtensionErrors) {
  const std::string path = WriteTemp("loader_x.parquet", "not really parquet");
  Engine engine;
  auto loaded = engine.LoadRelation(path, "x");
  EXPECT_FALSE(loaded.ok());
}

} // namespace
} // namespace hyperdeduce