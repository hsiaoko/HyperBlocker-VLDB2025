#ifndef HYPERDEDUCE_BUILTIN_BUILTIN_H_
#define HYPERDEDUCE_BUILTIN_BUILTIN_H_

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyperdeduce/base/types.h"
#include "hyperdeduce/catalog/dictionary.h"

namespace hyperdeduce {

// Evaluates a builtin on bound argument cells. Types accompany the cells so the
// builtin can compare integers, doubles, and strings correctly; the dictionary
// resolves string ids for lexical ordering.
using BuiltinEval = std::function<bool(const std::vector<Cell> &args,
                                       const std::vector<DataType> &types,
                                       const Dictionary &dictionary)>;

// A similarity measure maps two strings to a score in [0, 1]. It is the inner
// loop of a similarity join, not a row filter.
using SimilarityMeasure =
    std::function<double(const std::string &, const std::string &)>;

enum class BuiltinKind { kComparison, kSimilarity };

struct BuiltinSpec {
  std::string name;
  std::size_t arity = 0;
  BuiltinKind kind = BuiltinKind::kComparison;
  BuiltinEval eval;
  // Set for kSimilarity: the binary measure used by a similarity join.
  SimilarityMeasure measure;
};

// The single registration point for builtin predicates. Comparisons register
// here at startup. Adding a builtin means calling Register once; the parser
// classifies atoms by name and the backends evaluate through the same spec.
class BuiltinRegistry {
public:
  static BuiltinRegistry &Instance();

  void Register(BuiltinSpec spec);

  const BuiltinSpec *Find(const std::string &name) const;

  bool IsBuiltin(const std::string &name) const;

private:
  BuiltinRegistry();

  std::unordered_map<std::string, BuiltinSpec> specs_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BUILTIN_BUILTIN_H_