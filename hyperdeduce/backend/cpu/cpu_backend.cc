#include "hyperdeduce/backend/cpu/cpu_backend.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "hyperdeduce/builtin/builtin.h"
#include "hyperdeduce/catalog/row_key.h"

namespace hyperdeduce {
namespace {

std::vector<ColumnSchema> ToSchema(const std::vector<Column> &columns) {
  std::vector<ColumnSchema> schema;
  schema.reserve(columns.size());
  for (const Column &column : columns) {
    schema.push_back(ColumnSchema{column.var, column.type});
  }
  return schema;
}

Relation GatherRows(const Relation &input, const std::vector<std::uint8_t> &keep,
                    std::size_t kept) {
  Relation output("", input.schema());
  std::vector<std::vector<Cell>> columns(input.num_columns());
  for (auto &column : columns) {
    column.reserve(kept);
  }
  for (std::size_t r = 0; r < input.num_rows(); ++r) {
    if (!keep[r]) {
      continue;
    }
    for (std::size_t c = 0; c < input.num_columns(); ++c) {
      columns[c].push_back(input.at(c, r));
    }
  }
  output.AppendColumns(columns, kept);
  return output;
}

StatusOr<Relation> EvalScan(const PhysicalOp &op, const ExecutionContext &ctx) {
  Relation output("", ToSchema(op.output_schema));
  if (op.source_relation.empty()) {
    output.AppendColumns({}, 1); // unit producer: one empty row
    return output;
  }
  const Relation *source = ctx.GetRelation(op.source_relation, op.delta);
  if (source == nullptr) {
    return Status::Error("scan source missing: " + op.source_relation);
  }
  const std::size_t nrows = source->num_rows();
  const std::size_t ncols = op.output_schema.size();
  std::vector<std::vector<Cell>> columns(ncols);
  tbb::parallel_for(std::size_t(0), ncols, [&](std::size_t i) {
    columns[i] = source->column_data(op.source_columns[i]);
  });
  output.AppendColumns(columns, nrows);
  return output;
}

StatusOr<Relation> EvalFilter(const PhysicalOp &op, const Relation &input,
                              const ExecutionContext &ctx) {
  const BuiltinSpec *spec = BuiltinRegistry::Instance().Find(op.builtin);
  if (spec == nullptr) {
    return Status::Error("unknown builtin: " + op.builtin);
  }
  const std::size_t nrows = input.num_rows();
  std::vector<DataType> types;
  types.reserve(op.filter_args.size());
  for (const FilterArg &arg : op.filter_args) {
    types.push_back(arg.type);
  }
  std::vector<std::uint8_t> keep(nrows, 0);
  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, nrows),
                    [&](const tbb::blocked_range<std::size_t> &range) {
    std::vector<Cell> args(op.filter_args.size());
    for (std::size_t r = range.begin(); r != range.end(); ++r) {
      for (std::size_t a = 0; a < op.filter_args.size(); ++a) {
        const FilterArg &arg = op.filter_args[a];
        args[a] = arg.is_constant ? arg.constant : input.at(arg.column, r);
      }
      keep[r] = spec->eval(args, types, ctx.dictionary()) ? 1 : 0;
    }
  });
  std::size_t kept = 0;
  for (std::uint8_t k : keep) {
    kept += k;
  }
  return GatherRows(input, keep, kept);
}

StatusOr<Relation> EvalJoin(const PhysicalOp &op, const Relation &left,
                            const Relation &right) {
  const std::size_t left_cols = left.num_columns();
  std::vector<std::size_t> right_keep;
  for (std::size_t c = 0; c < right.num_columns(); ++c) {
    bool is_key = false;
    for (int key : op.right_keys) {
      if (static_cast<std::size_t>(key) == c) {
        is_key = true;
        break;
      }
    }
    if (!is_key) {
      right_keep.push_back(c);
    }
  }
  const std::size_t out_cols = op.output_schema.size();
  const bool cross = op.left_keys.empty();

  std::unordered_map<RowKey, std::vector<std::size_t>, KeyHash> index;
  if (!cross) {
    for (std::size_t s = 0; s < right.num_rows(); ++s) {
      RowKey key(op.right_keys.size());
      for (std::size_t k = 0; k < op.right_keys.size(); ++k) {
        key[k] = right.at(op.right_keys[k], s);
      }
      index[std::move(key)].push_back(s);
    }
  }

  tbb::enumerable_thread_specific<std::vector<std::vector<Cell>>> locals;
  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, left.num_rows()),
                    [&](const tbb::blocked_range<std::size_t> &range) {
    auto &buffer = locals.local();
    if (buffer.empty()) {
      buffer.resize(out_cols);
    }
    for (std::size_t r = range.begin(); r != range.end(); ++r) {
      const std::vector<std::size_t> *matches = nullptr;
      RowKey key;
      if (!cross) {
        key.resize(op.left_keys.size());
        for (std::size_t k = 0; k < op.left_keys.size(); ++k) {
          key[k] = left.at(op.left_keys[k], r);
        }
        auto it = index.find(key);
        if (it == index.end()) {
          continue;
        }
        matches = &it->second;
      }
      const std::size_t count = cross ? right.num_rows() : matches->size();
      for (std::size_t m = 0; m < count; ++m) {
        const std::size_t s = cross ? m : (*matches)[m];
        for (std::size_t c = 0; c < left_cols; ++c) {
          buffer[c].push_back(left.at(c, r));
        }
        for (std::size_t c = 0; c < right_keep.size(); ++c) {
          buffer[left_cols + c].push_back(right.at(right_keep[c], s));
        }
      }
    }
  });

  std::size_t nrows = 0;
  for (const auto &buffer : locals) {
    if (!buffer.empty()) {
      nrows += buffer[0].size();
    }
  }
  std::vector<std::vector<Cell>> columns(out_cols);
  for (auto &buffer : locals) {
    for (std::size_t c = 0; c < out_cols; ++c) {
      columns[c].insert(columns[c].end(), buffer[c].begin(), buffer[c].end());
    }
  }
  Relation output("", ToSchema(op.output_schema));
  output.AppendColumns(columns, nrows);
  return output;
}

StatusOr<Relation> EvalSimilarityJoin(const PhysicalOp &op, const Relation &left,
                                      const Relation &right,
                                      const ExecutionContext &ctx) {
  const BuiltinSpec *spec = BuiltinRegistry::Instance().Find(op.measure);
  if (spec == nullptr || !spec->measure) {
    return Status::Error("unknown similarity measure: " + op.measure);
  }
  const SimilarityMeasure &measure = spec->measure;
  const Dictionary &dictionary = ctx.dictionary();
  const std::size_t left_cols = left.num_columns();
  const std::size_t out_cols = op.output_schema.size();

  // Resolve the right key text once per row instead of once per pair.
  std::vector<std::string> right_text(right.num_rows());
  for (std::size_t s = 0; s < right.num_rows(); ++s) {
    right_text[s] = dictionary.Lookup(right.at(op.right_key, s));
  }

  tbb::enumerable_thread_specific<std::vector<std::vector<Cell>>> locals;
  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, left.num_rows()),
                    [&](const tbb::blocked_range<std::size_t> &range) {
    auto &buffer = locals.local();
    if (buffer.empty()) {
      buffer.resize(out_cols);
    }
    for (std::size_t r = range.begin(); r != range.end(); ++r) {
      const std::string left_text = dictionary.Lookup(left.at(op.left_key, r));
      for (std::size_t s = 0; s < right.num_rows(); ++s) {
        if (measure(left_text, right_text[s]) < op.sim_threshold) {
          continue;
        }
        for (std::size_t c = 0; c < left_cols; ++c) {
          buffer[c].push_back(left.at(c, r));
        }
        for (std::size_t c = 0; c < right.num_columns(); ++c) {
          buffer[left_cols + c].push_back(right.at(c, s));
        }
      }
    }
  });

  std::size_t nrows = 0;
  for (const auto &buffer : locals) {
    if (!buffer.empty()) {
      nrows += buffer[0].size();
    }
  }
  std::vector<std::vector<Cell>> columns(out_cols);
  for (auto &buffer : locals) {
    for (std::size_t c = 0; c < out_cols; ++c) {
      columns[c].insert(columns[c].end(), buffer[c].begin(), buffer[c].end());
    }
  }
  Relation output("", ToSchema(op.output_schema));
  output.AppendColumns(columns, nrows);
  return output;
}

StatusOr<Relation> EvalProject(const PhysicalOp &op, const Relation &input) {
  const std::size_t nrows = input.num_rows();
  const std::size_t ncols = op.project_columns.size();
  std::vector<std::vector<Cell>> columns(ncols);
  tbb::parallel_for(std::size_t(0), ncols, [&](std::size_t i) {
    const ProjectColumn &pc = op.project_columns[i];
    if (pc.is_constant) {
      columns[i].assign(nrows, pc.constant);
    } else {
      columns[i] = input.column_data(pc.index);
    }
  });
  Relation output("", ToSchema(op.output_schema));
  output.AppendColumns(columns, nrows);
  return output;
}

StatusOr<Relation> EvalDedup(const PhysicalOp &op, const Relation &input) {
  const std::size_t nrows = input.num_rows();
  if (input.num_columns() == 0) {
    Relation output("", ToSchema(op.output_schema));
    output.AppendColumns({}, nrows == 0 ? 0 : 1);
    return output;
  }
  std::unordered_set<RowKey, KeyHash> seen;
  std::vector<std::uint8_t> keep(nrows, 0);
  std::size_t kept = 0;
  for (std::size_t r = 0; r < nrows; ++r) {
    if (seen.insert(RowOf(input, r)).second) {
      keep[r] = 1;
      ++kept;
    }
  }
  return GatherRows(input, keep, kept);
}

StatusOr<Relation> EvalOp(const PhysicalOp &op, const ExecutionContext &ctx) {
  std::vector<Relation> inputs;
  inputs.reserve(op.inputs.size());
  for (const auto &child : op.inputs) {
    auto evaluated = EvalOp(*child, ctx);
    if (!evaluated.ok()) {
      return evaluated.status();
    }
    inputs.push_back(std::move(evaluated).value());
  }
  switch (op.kind) {
  case OpKind::kScan:
    return EvalScan(op, ctx);
  case OpKind::kFilter:
    return EvalFilter(op, inputs.at(0), ctx);
  case OpKind::kHashJoin:
    return EvalJoin(op, inputs.at(0), inputs.at(1));
  case OpKind::kSimilarityJoin:
    return EvalSimilarityJoin(op, inputs.at(0), inputs.at(1), ctx);
  case OpKind::kProject:
    return EvalProject(op, inputs.at(0));
  case OpKind::kDedup:
    return EvalDedup(op, inputs.at(0));
  }
  return Status::Error("unknown operator kind");
}

} // namespace

StatusOr<Relation> CpuBackend::Execute(const PhysicalOp &plan,
                                       const ExecutionContext &context) {
  return EvalOp(plan, context);
}

} // namespace hyperdeduce