#include "hyperdeduce/builtin/similarity.h"

#include <gtest/gtest.h>

namespace hyperdeduce {
namespace {

TEST(SimilarityTest, JaroCanonicalValues) {
  EXPECT_NEAR(0.944444, Jaro("MARTHA", "MARHTA"), 1e-6);
  EXPECT_NEAR(0.766667, Jaro("DIXON", "DICKSONX"), 1e-6);
  EXPECT_DOUBLE_EQ(1.0, Jaro("", ""));
  EXPECT_DOUBLE_EQ(0.0, Jaro("a", ""));
  EXPECT_DOUBLE_EQ(1.0, Jaro("abc", "abc"));
}

TEST(SimilarityTest, JaroWinklerPrefixBonus) {
  EXPECT_NEAR(0.961111, JaroWinkler("MARTHA", "MARHTA"), 1e-6);
  EXPECT_NEAR(0.813333, JaroWinkler("DIXON", "DICKSONX"), 1e-6);
  EXPECT_GE(JaroWinkler("MARTHA", "MARHTA"), Jaro("MARTHA", "MARHTA"));
}

TEST(SimilarityTest, JaccardIsIntersectionOverUnion) {
  EXPECT_DOUBLE_EQ(0.5, Jaccard("abc", "abd"));
  EXPECT_DOUBLE_EQ(1.0, Jaccard("ABC", "abc"));
  EXPECT_DOUBLE_EQ(1.0, Jaccard("", ""));
  EXPECT_DOUBLE_EQ(0.0, Jaccard("abc", "xyz"));
}

TEST(SimilarityTest, LevenshteinDistanceAndRatio) {
  EXPECT_EQ(3, LevenshteinDistance("kitten", "sitting"));
  EXPECT_EQ(0, LevenshteinDistance("abc", "abc"));
  EXPECT_EQ(3, LevenshteinDistance("", "abc"));
  EXPECT_NEAR(1.0 - 3.0 / 7.0, LevenshteinRatio("kitten", "sitting"), 1e-6);
  EXPECT_DOUBLE_EQ(1.0, LevenshteinRatio("", ""));
}

} // namespace
} // namespace hyperdeduce