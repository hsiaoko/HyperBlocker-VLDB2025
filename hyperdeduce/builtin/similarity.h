#ifndef HYPERDEDUCE_BUILTIN_SIMILARITY_H_
#define HYPERDEDUCE_BUILTIN_SIMILARITY_H_

#include <string>

namespace hyperdeduce {

// String similarity measures ported from the HyperBlocker paper artifact
// (tag vldb2025-paper, core/gpu/device_func.cuh). The original device kernels
// had defects: Jaro ignored transpositions, Jaccard returned a size ratio over
// uninitialized bitmaps, and Levenshtein was a stub returning 0. These are
// correct standard implementations of the same four measures.

// Jaro similarity in [0, 1]. Two empty strings are identical (1).
double Jaro(const std::string &a, const std::string &b);

// Jaro-Winkler similarity. prefix_weight scales the common-prefix bonus
// (prefix capped at 4 characters, as in the standard measure).
double JaroWinkler(const std::string &a, const std::string &b,
                   double prefix_weight = 0.1);

// Jaccard similarity over case-folded character sets: |A intersect B| / |A union B|.
double Jaccard(const std::string &a, const std::string &b);

// Levenshtein edit distance (insertions, deletions, substitutions).
int LevenshteinDistance(const std::string &a, const std::string &b);

// Normalized Levenshtein similarity in [0, 1]: 1 - distance / max(length).
double LevenshteinRatio(const std::string &a, const std::string &b);

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BUILTIN_SIMILARITY_H_