#include "hyperdeduce/builtin/builtin.h"

#include <set>

#include <gtest/gtest.h>

#include "hyperdeduce/backend/cpu/cpu_backend.h"
#include "hyperdeduce/engine/engine.h"

namespace hyperdeduce {
namespace {

// Adding a builtin touches one registration point. The parser then classifies
// the name as a builtin and the CPU backend evaluates it through the same spec.
TEST(BuiltinTest, CustomPredicateRegistersOnce) {
  BuiltinSpec spec;
  spec.name = "positive";
  spec.arity = 1;
  spec.eval = [](const std::vector<Cell> &args, const std::vector<DataType> &types,
                 const Dictionary &) {
    return types[0] == DataType::kInt64 && CellToInt64(args[0]) > 0;
  };
  BuiltinRegistry::Instance().Register(spec);
  EXPECT_TRUE(BuiltinRegistry::Instance().IsBuiltin("positive"));

  Engine engine;
  ASSERT_TRUE(engine.AddRules(
      "nums(-3).\n"
      "nums(0).\n"
      "nums(5).\n"
      "nums(9).\n"
      "pos(x) :- nums(x), positive(x).").ok());
  CpuBackend backend;
  ASSERT_TRUE(engine.Execute(&backend).ok());
  const Relation *pos = engine.GetRelation("pos");
  std::set<std::int64_t> values;
  for (std::size_t r = 0; r < pos->num_rows(); ++r) {
    values.insert(CellToInt64(pos->at(0, r)));
  }
  EXPECT_EQ(std::set<std::int64_t>({5, 9}), values);
}

} // namespace
} // namespace hyperdeduce