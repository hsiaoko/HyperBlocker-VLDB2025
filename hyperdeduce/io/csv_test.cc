#include "hyperdeduce/io/csv.h"

#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace hyperdeduce {
namespace {

std::string WriteTempFile(const std::string &name, const std::string &content) {
  const std::string path = ::testing::TempDir() + "/" + name;
  std::ofstream out(path);
  out << content;
  out.close();
  return path;
}

TEST(CsvTest, InfersIntAndStringColumns) {
  const std::string path =
      WriteTempFile("infer.csv", "alice,1\nbob,2\ncarol,3\n");
  Catalog catalog;
  auto loaded = LoadCsv(path, "parent", &catalog);
  ASSERT_TRUE(loaded.ok()) << loaded.status().message();
  const Relation *relation = loaded.value();
  EXPECT_EQ(3u, relation->num_rows());
  EXPECT_EQ(DataType::kString, relation->column(0).type);
  EXPECT_EQ(DataType::kInt64, relation->column(1).type);
  EXPECT_EQ(2, CellToInt64(relation->at(1, 1)));
  EXPECT_EQ("bob", catalog.dictionary().Lookup(relation->at(0, 1)));
}

TEST(CsvTest, InfersDoubleColumn) {
  const std::string path = WriteTempFile("double.csv", "1.5\n2.25\n");
  Catalog catalog;
  auto loaded = LoadCsv(path, "d", &catalog);
  ASSERT_TRUE(loaded.ok()) << loaded.status().message();
  EXPECT_EQ(DataType::kDouble, loaded.value()->column(0).type);
  EXPECT_DOUBLE_EQ(2.25, CellToDouble(loaded.value()->at(0, 1)));
}

TEST(CsvTest, HeaderSuppliesNames) {
  const std::string path = WriteTempFile("header.csv", "src,dst\nalice,bob\n");
  Catalog catalog;
  CsvOptions options;
  options.has_header = true;
  auto loaded = LoadCsv(path, "edge", &catalog, options);
  ASSERT_TRUE(loaded.ok()) << loaded.status().message();
  EXPECT_EQ(1u, loaded.value()->num_rows());
  EXPECT_EQ("src", loaded.value()->column(0).name);
  EXPECT_EQ("dst", loaded.value()->column(1).name);
}

TEST(CsvTest, WriteReadRoundTrip) {
  const std::string path = WriteTempFile("round.csv", "alice,1\nbob,2\n");
  Catalog catalog;
  auto loaded = LoadCsv(path, "r", &catalog);
  ASSERT_TRUE(loaded.ok());
  const std::string out_path = ::testing::TempDir() + "/round_out.csv";
  ASSERT_TRUE(WriteCsv(*loaded.value(), catalog.dictionary(), out_path).ok());

  Catalog catalog2;
  auto reloaded = LoadCsv(out_path, "r", &catalog2);
  ASSERT_TRUE(reloaded.ok());
  EXPECT_EQ(loaded.value()->num_rows(), reloaded.value()->num_rows());
  for (std::size_t r = 0; r < loaded.value()->num_rows(); ++r) {
    EXPECT_EQ(CellToInt64(loaded.value()->at(1, r)),
              CellToInt64(reloaded.value()->at(1, r)));
    EXPECT_EQ(catalog.dictionary().Lookup(loaded.value()->at(0, r)),
              catalog2.dictionary().Lookup(reloaded.value()->at(0, r)));
  }
}

} // namespace
} // namespace hyperdeduce