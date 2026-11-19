#ifndef HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_
#define HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_

#include <string>

#include "hyperdeduce/backend/backend.h"

namespace hyperdeduce {

// Interprets a physical plan on NVIDIA GPUs with CUDA and thrust. Scan, filter,
// sort-merge join, projection, and deduplication run as device kernels. With
// more than one device, the driving scan is sharded across devices and the
// partial results are unioned, which is exact for a left-deep plan.
//
// Comparison builtins run natively on the device. A custom builtin without a
// device evaluator is a documented extension point; the backend reports it
// rather than silently falling back.
class GpuBackend : public Backend {
public:
  // num_devices <= 0 means use every visible device.
  explicit GpuBackend(int num_devices = 0);

  std::string name() const override { return "gpu"; }

  StatusOr<Relation> Execute(const PhysicalOp &plan,
                             const ExecutionContext &context) override;

private:
  int num_devices_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_