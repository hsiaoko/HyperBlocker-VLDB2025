#include "hyperdeduce/builtin/builtin.h"

#include <utility>

namespace hyperdeduce {
namespace {

enum class CompareOp { kEq, kNeq, kLt, kLe, kGt, kGe };

// Orders two cells. Sets comparable to false when the types cannot be ordered
// (a string against a number). Equality on strings uses id equality, which is
// exactly string equality; ordering on strings uses lexical text.
int CompareCells(Cell a, Cell b, DataType ta, DataType tb,
                 const Dictionary &dictionary, bool *comparable) {
  *comparable = true;
  if (ta == DataType::kString && tb == DataType::kString) {
    if (a == b) {
      return 0;
    }
    const std::string &sa = dictionary.Lookup(a);
    const std::string &sb = dictionary.Lookup(b);
    return sa < sb ? -1 : 1;
  }
  const bool a_numeric = ta == DataType::kInt64 || ta == DataType::kDouble;
  const bool b_numeric = tb == DataType::kInt64 || tb == DataType::kDouble;
  if (a_numeric && b_numeric) {
    const double da = ta == DataType::kInt64 ? CellToInt64(a) : CellToDouble(a);
    const double db = tb == DataType::kInt64 ? CellToInt64(b) : CellToDouble(b);
    if (da == db) {
      return 0;
    }
    return da < db ? -1 : 1;
  }
  *comparable = false;
  return 0;
}

bool ApplyOp(CompareOp op, int ordering, bool comparable) {
  if (!comparable) {
    return op == CompareOp::kNeq;
  }
  switch (op) {
  case CompareOp::kEq:
    return ordering == 0;
  case CompareOp::kNeq:
    return ordering != 0;
  case CompareOp::kLt:
    return ordering < 0;
  case CompareOp::kLe:
    return ordering <= 0;
  case CompareOp::kGt:
    return ordering > 0;
  case CompareOp::kGe:
    return ordering >= 0;
  }
  return false;
}

BuiltinSpec MakeComparison(const std::string &name, CompareOp op) {
  BuiltinSpec spec;
  spec.name = name;
  spec.arity = 2;
  spec.eval = [op](const std::vector<Cell> &args,
                   const std::vector<DataType> &types,
                   const Dictionary &dictionary) {
    bool comparable = false;
    const int ordering =
        CompareCells(args[0], args[1], types[0], types[1], dictionary, &comparable);
    return ApplyOp(op, ordering, comparable);
  };
  return spec;
}

} // namespace

BuiltinRegistry &BuiltinRegistry::Instance() {
  static BuiltinRegistry registry;
  return registry;
}

BuiltinRegistry::BuiltinRegistry() {
  Register(MakeComparison("=", CompareOp::kEq));
  Register(MakeComparison("!=", CompareOp::kNeq));
  Register(MakeComparison("<", CompareOp::kLt));
  Register(MakeComparison("<=", CompareOp::kLe));
  Register(MakeComparison(">", CompareOp::kGt));
  Register(MakeComparison(">=", CompareOp::kGe));
}

void BuiltinRegistry::Register(BuiltinSpec spec) {
  const std::string name = spec.name;
  specs_.emplace(name, std::move(spec));
}

const BuiltinSpec *BuiltinRegistry::Find(const std::string &name) const {
  auto it = specs_.find(name);
  return it == specs_.end() ? nullptr : &it->second;
}

bool BuiltinRegistry::IsBuiltin(const std::string &name) const {
  return specs_.count(name) != 0;
}

} // namespace hyperdeduce