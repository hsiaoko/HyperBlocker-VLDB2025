#include "hyperdeduce/backend/gpu/gpu_backend.h"

#include <cuda_runtime.h>
#include <thrust/copy.h>
#include <thrust/device_vector.h>
#include <thrust/fill.h>
#include <thrust/reduce.h>
#include <thrust/scan.h>
#include <thrust/sequence.h>
#include <thrust/sort.h>

#include <algorithm>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hyperdeduce/builtin/builtin.h"

namespace hyperdeduce {
namespace {

#define CUDA_CHECK(expr)                                                  \
  do {                                                                    \
    cudaError_t error_ = (expr);                                          \
    if (error_ != cudaSuccess) {                                          \
      return Status::Error(std::string("cuda: ") + cudaGetErrorString(error_)); \
    }                                                                     \
  } while (0)

struct DeviceRelation {
  std::vector<Column> schema;
  std::vector<thrust::device_vector<Cell>> columns;
  std::size_t nrows = 0;
};

struct StringTable {
  const std::size_t *offsets;
  const char *chars;
};

struct FilterArgDevice {
  int column;
  Cell constant;
  int is_constant;
  int type;
};

__device__ int LexString(Cell a, Cell b, StringTable table) {
  const char *pa = table.chars + table.offsets[a];
  const char *pb = table.chars + table.offsets[b];
  const std::size_t na = table.offsets[a + 1] - table.offsets[a];
  const std::size_t nb = table.offsets[b + 1] - table.offsets[b];
  const std::size_t n = na < nb ? na : nb;
  for (std::size_t i = 0; i < n; ++i) {
    if (pa[i] != pb[i]) {
      return pa[i] < pb[i] ? -1 : 1;
    }
  }
  if (na == nb) {
    return 0;
  }
  return na < nb ? -1 : 1;
}

__device__ int CompareCells(Cell a, Cell b, int ta, int tb, StringTable table,
                            bool *comparable) {
  *comparable = true;
  if (ta == static_cast<int>(DataType::kString) &&
      tb == static_cast<int>(DataType::kString)) {
    if (a == b) {
      return 0;
    }
    return LexString(a, b, table);
  }
  const bool an = ta == static_cast<int>(DataType::kInt64) ||
                  ta == static_cast<int>(DataType::kDouble);
  const bool bn = tb == static_cast<int>(DataType::kInt64) ||
                  tb == static_cast<int>(DataType::kDouble);
  if (an && bn) {
    const double da = ta == static_cast<int>(DataType::kInt64)
                          ? static_cast<double>(static_cast<std::int64_t>(a))
                          : __longlong_as_double(a);
    const double db = tb == static_cast<int>(DataType::kInt64)
                          ? static_cast<double>(static_cast<std::int64_t>(b))
                          : __longlong_as_double(b);
    if (da == db) {
      return 0;
    }
    return da < db ? -1 : 1;
  }
  *comparable = false;
  return 0;
}

__device__ bool ApplyOp(int op, int ordering, bool comparable) {
  if (!comparable) {
    return op == 1; // !=
  }
  switch (op) {
  case 0:
    return ordering == 0;
  case 1:
    return ordering != 0;
  case 2:
    return ordering < 0;
  case 3:
    return ordering <= 0;
  case 4:
    return ordering > 0;
  case 5:
    return ordering >= 0;
  }
  return false;
}

__global__ void FilterKernel(std::size_t nrows, Cell *const *cols,
                             FilterArgDevice arg0, FilterArgDevice arg1, int op,
                             StringTable table, int *keep) {
  const std::size_t r = blockIdx.x * blockDim.x + threadIdx.x;
  if (r >= nrows) {
    return;
  }
  const Cell a = arg0.is_constant ? arg0.constant : cols[arg0.column][r];
  const Cell b = arg1.is_constant ? arg1.constant : cols[arg1.column][r];
  bool comparable = false;
  const int ordering = CompareCells(a, b, arg0.type, arg1.type, table, &comparable);
  keep[r] = ApplyOp(op, ordering, comparable) ? 1 : 0;
}

__global__ void ScatterKernel(std::size_t nrows, std::size_t ncols,
                              Cell *const *in, Cell *const *out, const int *keep,
                              const int *offsets) {
  const std::size_t r = blockIdx.x * blockDim.x + threadIdx.x;
  if (r >= nrows || !keep[r]) {
    return;
  }
  const std::size_t pos = offsets[r];
  for (std::size_t c = 0; c < ncols; ++c) {
    out[c][pos] = in[c][r];
  }
}

// Lexicographic raw-cell comparison of two key tuples. Join and dedup only need
// equality grouping, so a total order on raw cells suffices regardless of type.
__device__ int CompareKeys(Cell *const *cols_a, const int *keys_a, int row_a,
                           Cell *const *cols_b, const int *keys_b, int row_b,
                           int nkeys) {
  for (int k = 0; k < nkeys; ++k) {
    const Cell a = cols_a[keys_a[k]][row_a];
    const Cell b = cols_b[keys_b[k]][row_b];
    if (a != b) {
      return a < b ? -1 : 1;
    }
  }
  return 0;
}

struct RightKeyLess {
  Cell *const *cols;
  const int *keys;
  int nkeys;
  __device__ bool operator()(int a, int b) const {
    return CompareKeys(cols, keys, a, cols, keys, b, nkeys) < 0;
  }
};

__global__ void JoinBoundKernel(std::size_t nrows_left, std::size_t nrows_right,
                                Cell *const *left_cols, const int *left_keys,
                                Cell *const *right_cols, const int *right_keys,
                                int nkeys, const int *perm, int *lo, int *hi) {
  const std::size_t l = blockIdx.x * blockDim.x + threadIdx.x;
  if (l >= nrows_left) {
    return;
  }
  if (nkeys == 0) {
    lo[l] = 0;
    hi[l] = static_cast<int>(nrows_right);
    return;
  }
  int lower = 0;
  int upper = static_cast<int>(nrows_right);
  while (lower < upper) {
    const int mid = (lower + upper) / 2;
    if (CompareKeys(left_cols, left_keys, l, right_cols, right_keys, perm[mid],
                    nkeys) <= 0) {
      upper = mid;
    } else {
      lower = mid + 1;
    }
  }
  lo[l] = lower;
  int higher = lower;
  upper = static_cast<int>(nrows_right);
  while (higher < upper) {
    const int mid = (higher + upper) / 2;
    if (CompareKeys(left_cols, left_keys, l, right_cols, right_keys, perm[mid],
                    nkeys) < 0) {
      upper = mid;
    } else {
      higher = mid + 1;
    }
  }
  hi[l] = higher;
}

__global__ void JoinCountKernel(std::size_t nrows_left, const int *lo,
                                const int *hi, int *counts) {
  const std::size_t l = blockIdx.x * blockDim.x + threadIdx.x;
  if (l >= nrows_left) {
    return;
  }
  counts[l] = hi[l] - lo[l];
}

__global__ void JoinFillKernel(std::size_t nrows_left, std::size_t left_ncols,
                               std::size_t right_keep_count, Cell *const *left_cols,
                               Cell *const *right_cols, const int *right_keep,
                               const int *perm, const int *lo, const int *offsets,
                               Cell *const *out) {
  const std::size_t l = blockIdx.x * blockDim.x + threadIdx.x;
  if (l >= nrows_left) {
    return;
  }
  const int base = offsets[l];
  const int begin = lo[l];
  const int count = offsets[l + 1] - offsets[l];
  for (int j = begin; j < begin + count; ++j) {
    const int out_row = base + (j - begin);
    const int s = perm[j];
    for (std::size_t c = 0; c < left_ncols; ++c) {
      out[c][out_row] = left_cols[c][l];
    }
    for (std::size_t c = 0; c < right_keep_count; ++c) {
      out[left_ncols + c][out_row] = right_cols[right_keep[c]][s];
    }
  }
}

__global__ void DedupMarkKernel(std::size_t nrows, std::size_t ncols,
                                Cell *const *cols, const int *perm, int *keep) {
  const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= nrows) {
    return;
  }
  if (i == 0) {
    keep[perm[0]] = 1;
    return;
  }
  bool different = false;
  for (std::size_t c = 0; c < ncols; ++c) {
    if (cols[c][perm[i]] != cols[c][perm[i - 1]]) {
      different = true;
      break;
    }
  }
  keep[perm[i]] = different ? 1 : 0;
}

int BlockCount(std::size_t nrows) {
  return static_cast<int>((nrows + 255) / 256);
}

thrust::device_vector<Cell *> ColumnPointers(DeviceRelation *relation) {
  thrust::device_vector<Cell *> pointers(relation->columns.size());
  std::vector<Cell *> host(relation->columns.size());
  for (std::size_t c = 0; c < relation->columns.size(); ++c) {
    host[c] = thrust::raw_pointer_cast(relation->columns[c].data());
  }
  thrust::copy(host.begin(), host.end(), pointers.begin());
  return pointers;
}

StatusOr<DeviceRelation> UploadRelation(const Relation &relation) {
  DeviceRelation output;
  for (const ColumnSchema &column : relation.schema()) {
    output.schema.push_back(Column{column.name, column.type});
  }
  output.nrows = relation.num_rows();
  output.columns.resize(relation.num_columns());
  for (std::size_t c = 0; c < relation.num_columns(); ++c) {
    const std::vector<Cell> &host = relation.column_data(c);
    output.columns[c].assign(host.begin(), host.end());
  }
  return output;
}

StatusOr<Relation> DownloadRelation(const DeviceRelation &relation,
                                    const std::vector<Column> &schema) {
  std::vector<ColumnSchema> host_schema;
  for (const Column &column : schema) {
    host_schema.push_back(ColumnSchema{column.var, column.type});
  }
  Relation output("", host_schema);
  std::vector<std::vector<Cell>> columns(relation.columns.size());
  for (std::size_t c = 0; c < relation.columns.size(); ++c) {
    columns[c].resize(relation.nrows);
    thrust::copy(relation.columns[c].begin(), relation.columns[c].end(),
                 columns[c].begin());
  }
  Status appended = output.AppendColumns(columns, relation.nrows);
  if (!appended.ok()) {
    return appended;
  }
  return output;
}

int ComparisonOpCode(const std::string &name) {
  if (name == "=") return 0;
  if (name == "!=") return 1;
  if (name == "<") return 2;
  if (name == "<=") return 3;
  if (name == ">") return 4;
  if (name == ">=") return 5;
  return -1;
}

struct DeviceContext {
  std::unordered_map<std::string, DeviceRelation> full;
  std::unordered_map<std::string, DeviceRelation> delta;
  thrust::device_vector<std::size_t> offsets;
  thrust::device_vector<char> chars;
  StringTable table;
};

Status UploadDictionary(const Dictionary &dictionary, DeviceContext *ctx) {
  std::vector<std::size_t> offsets(dictionary.size() + 1);
  std::vector<char> chars;
  for (std::size_t id = 0; id < dictionary.size(); ++id) {
    offsets[id] = chars.size();
    const std::string &text = dictionary.Lookup(id);
    chars.insert(chars.end(), text.begin(), text.end());
  }
  offsets[dictionary.size()] = chars.size();
  ctx->offsets.assign(offsets.begin(), offsets.end());
  ctx->chars.assign(chars.begin(), chars.end());
  ctx->table.offsets = thrust::raw_pointer_cast(ctx->offsets.data());
  ctx->table.chars = thrust::raw_pointer_cast(ctx->chars.data());
  return Status::Ok();
}

StatusOr<DeviceRelation> EvalOp(const PhysicalOp &op, const ExecutionContext &ctx,
                                DeviceContext *device,
                                const std::unordered_map<const PhysicalOp *,
                                                         std::pair<std::size_t, std::size_t>>
                                    &shards);

StatusOr<DeviceRelation> EvalScan(const PhysicalOp &op, const ExecutionContext &ctx,
                                  DeviceContext *device,
                                  const std::unordered_map<const PhysicalOp *,
                                                           std::pair<std::size_t, std::size_t>>
                                      &shards) {
  DeviceRelation output;
  output.schema = op.output_schema;
  if (op.source_relation.empty()) {
    output.nrows = 1;
    return output;
  }
  const Relation *source = ctx.GetRelation(op.source_relation, op.delta);
  if (source == nullptr) {
    return Status::Error("scan source missing: " + op.source_relation);
  }
  auto &cache = op.delta ? device->delta : device->full;
  auto it = cache.find(op.source_relation);
  if (it == cache.end()) {
    auto uploaded = UploadRelation(*source);
    if (!uploaded.ok()) {
      return uploaded.status();
    }
    it = cache.emplace(op.source_relation, std::move(uploaded).value()).first;
  }
  const DeviceRelation &input = it->second;
  std::size_t begin = 0;
  std::size_t end = input.nrows;
  auto shard = shards.find(&op);
  if (shard != shards.end()) {
    begin = shard->second.first;
    end = shard->second.second;
  }
  output.nrows = end - begin;
  output.columns.resize(op.output_schema.size());
  for (std::size_t c = 0; c < op.output_schema.size(); ++c) {
    const auto &src = input.columns[op.source_columns[c]];
    output.columns[c].assign(src.begin() + begin, src.begin() + end);
  }
  return output;
}

StatusOr<DeviceRelation> Compact(DeviceRelation *input,
                                 thrust::device_vector<int> *keep) {
  const std::size_t nrows = input->nrows;
  const std::size_t ncols = input->columns.size();
  const std::size_t kept = thrust::reduce(keep->begin(), keep->end(), 0);
  thrust::device_vector<int> offsets(nrows + 1);
  thrust::exclusive_scan(keep->begin(), keep->end(), offsets.begin());
  offsets[nrows] = static_cast<int>(kept);
  DeviceRelation output;
  output.schema = input->schema;
  output.nrows = kept;
  output.columns.resize(ncols);
  for (std::size_t c = 0; c < ncols; ++c) {
    output.columns[c].resize(kept);
  }
  if (kept > 0 && nrows > 0) {
    auto in_ptrs = ColumnPointers(input);
    auto out_ptrs = ColumnPointers(&output);
    ScatterKernel<<<BlockCount(nrows), 256>>>(
        nrows, ncols, thrust::raw_pointer_cast(in_ptrs.data()),
        thrust::raw_pointer_cast(out_ptrs.data()),
        thrust::raw_pointer_cast(keep->data()),
        thrust::raw_pointer_cast(offsets.data()));
    CUDA_CHECK(cudaGetLastError());
  }
  return output;
}

StatusOr<DeviceRelation> EvalFilter(const PhysicalOp &op, DeviceRelation *input,
                                    DeviceContext *device) {
  const int code = ComparisonOpCode(op.builtin);
  if (code < 0 || op.filter_args.size() != 2) {
    return Status::Error("gpu backend supports comparison builtins only: " +
                         op.builtin);
  }
  const std::size_t nrows = input->nrows;
  FilterArgDevice args[2];
  for (int a = 0; a < 2; ++a) {
    const FilterArg &arg = op.filter_args[a];
    args[a].column = arg.column;
    args[a].constant = arg.constant;
    args[a].is_constant = arg.is_constant ? 1 : 0;
    args[a].type = static_cast<int>(arg.type);
  }
  thrust::device_vector<int> keep(nrows, 0);
  if (nrows > 0) {
    auto ptrs = ColumnPointers(input);
    FilterKernel<<<BlockCount(nrows), 256>>>(
        nrows, thrust::raw_pointer_cast(ptrs.data()), args[0], args[1], code,
        device->table, thrust::raw_pointer_cast(keep.data()));
    CUDA_CHECK(cudaGetLastError());
  }
  return Compact(input, &keep);
}

StatusOr<DeviceRelation> EvalJoin(const PhysicalOp &op, DeviceRelation *left,
                                  DeviceRelation *right) {
  const std::size_t nrows_left = left->nrows;
  const std::size_t nrows_right = right->nrows;
  const int nkeys = static_cast<int>(op.left_keys.size());

  thrust::device_vector<int> left_keys(op.left_keys.begin(), op.left_keys.end());
  thrust::device_vector<int> right_keys(op.right_keys.begin(), op.right_keys.end());

  thrust::device_vector<int> perm(nrows_right);
  thrust::sequence(perm.begin(), perm.end());
  auto right_ptrs = ColumnPointers(right);
  if (nrows_right > 1 && nkeys > 0) {
    RightKeyLess less{thrust::raw_pointer_cast(right_ptrs.data()),
                      thrust::raw_pointer_cast(right_keys.data()), nkeys};
    thrust::sort(perm.begin(), perm.end(), less);
  }

  thrust::device_vector<int> lo(nrows_left, 0);
  thrust::device_vector<int> hi(nrows_left, 0);
  auto left_ptrs = ColumnPointers(left);
  if (nrows_left > 0) {
    JoinBoundKernel<<<BlockCount(nrows_left), 256>>>(
        nrows_left, nrows_right, thrust::raw_pointer_cast(left_ptrs.data()),
        thrust::raw_pointer_cast(left_keys.data()),
        thrust::raw_pointer_cast(right_ptrs.data()),
        thrust::raw_pointer_cast(right_keys.data()), nkeys,
        thrust::raw_pointer_cast(perm.data()), thrust::raw_pointer_cast(lo.data()),
        thrust::raw_pointer_cast(hi.data()));
    CUDA_CHECK(cudaGetLastError());
  }

  thrust::device_vector<int> counts(nrows_left, 0);
  thrust::device_vector<int> offsets(nrows_left + 1, 0);
  std::size_t total = 0;
  if (nrows_left > 0) {
    JoinCountKernel<<<BlockCount(nrows_left), 256>>>(
        nrows_left, thrust::raw_pointer_cast(lo.data()),
        thrust::raw_pointer_cast(hi.data()), thrust::raw_pointer_cast(counts.data()));
    CUDA_CHECK(cudaGetLastError());
    thrust::exclusive_scan(counts.begin(), counts.end(), offsets.begin());
    total = static_cast<std::size_t>(offsets[nrows_left - 1]) +
            static_cast<std::size_t>(counts[nrows_left - 1]);
    offsets[nrows_left] = static_cast<int>(total);
  }

  std::vector<int> right_keep;
  for (std::size_t c = 0; c < right->columns.size(); ++c) {
    bool is_key = false;
    for (int key : op.right_keys) {
      if (static_cast<std::size_t>(key) == c) {
        is_key = true;
        break;
      }
    }
    if (!is_key) {
      right_keep.push_back(static_cast<int>(c));
    }
  }
  thrust::device_vector<int> d_right_keep(right_keep.begin(), right_keep.end());

  DeviceRelation output;
  output.schema = op.output_schema;
  output.nrows = total;
  output.columns.resize(op.output_schema.size());
  for (std::size_t c = 0; c < op.output_schema.size(); ++c) {
    output.columns[c].resize(total);
  }
  if (total > 0 && nrows_left > 0) {
    auto out_ptrs = ColumnPointers(&output);
    JoinFillKernel<<<BlockCount(nrows_left), 256>>>(
        nrows_left, left->columns.size(), right_keep.size(),
        thrust::raw_pointer_cast(left_ptrs.data()),
        thrust::raw_pointer_cast(right_ptrs.data()),
        thrust::raw_pointer_cast(d_right_keep.data()),
        thrust::raw_pointer_cast(perm.data()), thrust::raw_pointer_cast(lo.data()),
        thrust::raw_pointer_cast(offsets.data()),
        thrust::raw_pointer_cast(out_ptrs.data()));
    CUDA_CHECK(cudaGetLastError());
  }
  return output;
}

StatusOr<DeviceRelation> EvalProject(const PhysicalOp &op, DeviceRelation *input) {
  DeviceRelation output;
  output.schema = op.output_schema;
  output.nrows = input->nrows;
  output.columns.resize(op.project_columns.size());
  for (std::size_t c = 0; c < op.project_columns.size(); ++c) {
    const ProjectColumn &pc = op.project_columns[c];
    if (pc.is_constant) {
      output.columns[c].assign(input->nrows, pc.constant);
    } else {
      output.columns[c] = input->columns[pc.index];
    }
  }
  return output;
}

StatusOr<DeviceRelation> EvalDedup(const PhysicalOp &op, DeviceRelation *input) {
  const std::size_t nrows = input->nrows;
  const std::size_t ncols = input->columns.size();
  if (ncols == 0) {
    DeviceRelation output;
    output.schema = op.output_schema;
    output.nrows = nrows == 0 ? 0 : 1;
    return output;
  }
  thrust::device_vector<int> perm(nrows);
  thrust::sequence(perm.begin(), perm.end());
  thrust::device_vector<int> all_keys(ncols);
  for (std::size_t c = 0; c < ncols; ++c) {
    all_keys[c] = static_cast<int>(c);
  }
  auto ptrs = ColumnPointers(input);
  if (nrows > 1) {
    RightKeyLess less{thrust::raw_pointer_cast(ptrs.data()),
                      thrust::raw_pointer_cast(all_keys.data()),
                      static_cast<int>(ncols)};
    thrust::sort(perm.begin(), perm.end(), less);
  }
  thrust::device_vector<int> keep(nrows, 0);
  if (nrows > 0) {
    DedupMarkKernel<<<BlockCount(nrows), 256>>>(
        nrows, ncols, thrust::raw_pointer_cast(ptrs.data()),
        thrust::raw_pointer_cast(perm.data()), thrust::raw_pointer_cast(keep.data()));
    CUDA_CHECK(cudaGetLastError());
  }
  return Compact(input, &keep);
}

StatusOr<DeviceRelation> EvalOp(const PhysicalOp &op, const ExecutionContext &ctx,
                                DeviceContext *device,
                                const std::unordered_map<const PhysicalOp *,
                                                         std::pair<std::size_t, std::size_t>>
                                    &shards) {
  std::vector<DeviceRelation> inputs;
  for (const auto &child : op.inputs) {
    auto evaluated = EvalOp(*child, ctx, device, shards);
    if (!evaluated.ok()) {
      return evaluated.status();
    }
    inputs.push_back(std::move(evaluated).value());
  }
  switch (op.kind) {
  case OpKind::kScan:
    return EvalScan(op, ctx, device, shards);
  case OpKind::kFilter:
    return EvalFilter(op, &inputs.at(0), device);
  case OpKind::kHashJoin:
    return EvalJoin(op, &inputs.at(0), &inputs.at(1));
  case OpKind::kProject:
    return EvalProject(op, &inputs.at(0));
  case OpKind::kDedup:
    return EvalDedup(op, &inputs.at(0));
  }
  return Status::Error("unknown operator kind");
}

const PhysicalOp *DrivingScan(const PhysicalOp &op) {
  const PhysicalOp *node = &op;
  while (node->kind != OpKind::kScan) {
    if (node->inputs.empty()) {
      return nullptr;
    }
    node = node->inputs.at(0).get();
  }
  return node;
}

StatusOr<Relation> RunOnDevice(int device_id, const PhysicalOp &plan,
                               const ExecutionContext &ctx,
                               const std::unordered_map<const PhysicalOp *,
                                                        std::pair<std::size_t, std::size_t>>
                                   &shards) {
  CUDA_CHECK(cudaSetDevice(device_id));
  DeviceContext device;
  Status dictionary = UploadDictionary(ctx.dictionary(), &device);
  if (!dictionary.ok()) {
    return dictionary;
  }
  auto result = EvalOp(plan, ctx, &device, shards);
  if (!result.ok()) {
    return result.status();
  }
  CUDA_CHECK(cudaDeviceSynchronize());
  return DownloadRelation(result.value(), plan.output_schema);
}

} // namespace

GpuBackend::GpuBackend(int num_devices) : num_devices_(num_devices) {}

StatusOr<Relation> GpuBackend::Execute(const PhysicalOp &plan,
                                       const ExecutionContext &context) {
  int available = 0;
  CUDA_CHECK(cudaGetDeviceCount(&available));
  if (available == 0) {
    return Status::Error("no CUDA device available");
  }
  int devices = num_devices_ > 0 ? std::min(num_devices_, available) : available;

  const PhysicalOp *driving = DrivingScan(plan);
  std::size_t driving_rows = 0;
  if (driving != nullptr && !driving->source_relation.empty()) {
    const Relation *source =
        context.GetRelation(driving->source_relation, driving->delta);
    driving_rows = source != nullptr ? source->num_rows() : 0;
  }

  if (devices == 1 || driving_rows == 0) {
    return RunOnDevice(0, plan, context, {});
  }

  // Shard the driving scan across devices. Each output row of a left-deep plan
  // derives from exactly one driving row, so the union of shards is exact.
  std::vector<std::thread> threads;
  std::vector<StatusOr<Relation>> results(devices, Status::Error("not run"));
  for (int d = 0; d < devices; ++d) {
    const std::size_t begin = driving_rows * d / devices;
    const std::size_t end = driving_rows * (d + 1) / devices;
    std::unordered_map<const PhysicalOp *, std::pair<std::size_t, std::size_t>> shards;
    shards[driving] = {begin, end};
    threads.emplace_back([&, d, shards]() {
      results[d] = RunOnDevice(d, plan, context, shards);
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  std::vector<ColumnSchema> schema;
  for (const Column &column : plan.output_schema) {
    schema.push_back(ColumnSchema{column.var, column.type});
  }
  Relation combined("", schema);
  for (auto &result : results) {
    if (!result.ok()) {
      return result.status();
    }
    const Relation &part = result.value();
    std::vector<std::vector<Cell>> columns(part.num_columns());
    for (std::size_t c = 0; c < part.num_columns(); ++c) {
      columns[c] = part.column_data(c);
    }
    Status appended = combined.AppendColumns(columns, part.num_rows());
    if (!appended.ok()) {
      return appended;
    }
  }
  return combined;
}

} // namespace hyperdeduce