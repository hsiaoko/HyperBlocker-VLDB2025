#include "hyperdeduce/plan/physical_plan.h"

#include <sstream>

namespace hyperdeduce {

const char *OpKindName(OpKind kind) {
  switch (kind) {
  case OpKind::kScan:
    return "Scan";
  case OpKind::kFilter:
    return "Filter";
  case OpKind::kHashJoin:
    return "HashJoin";
  case OpKind::kProject:
    return "Project";
  case OpKind::kDedup:
    return "Dedup";
  }
  return "Unknown";
}

namespace {

void ExplainNode(const PhysicalOp &op, int depth, std::ostringstream *out) {
  for (int i = 0; i < depth; ++i) {
    *out << "  ";
  }
  *out << OpKindName(op.kind) << " [rows~" << op.est_rows << "] (";
  for (std::size_t i = 0; i < op.output_schema.size(); ++i) {
    if (i > 0) {
      *out << ", ";
    }
    *out << op.output_schema[i].var << ":" << DataTypeName(op.output_schema[i].type);
  }
  *out << ")";
  switch (op.kind) {
  case OpKind::kScan:
    *out << " " << op.source_relation << (op.delta ? " delta" : " full");
    break;
  case OpKind::kFilter:
    *out << " " << op.builtin;
    break;
  case OpKind::kHashJoin:
    *out << " keys=" << op.left_keys.size();
    break;
  default:
    break;
  }
  *out << "\n";
  for (const auto &input : op.inputs) {
    ExplainNode(*input, depth + 1, out);
  }
}

} // namespace

std::string Explain(const PhysicalOp &op) {
  std::ostringstream out;
  ExplainNode(op, 0, &out);
  return out.str();
}

} // namespace hyperdeduce