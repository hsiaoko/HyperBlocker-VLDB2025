#ifndef HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_
#define HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_

#include <memory>
#include <string>
#include <vector>

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
  ~GpuBackend() override;

  std::string name() const override { return "gpu"; }

  StatusOr<Relation> Execute(const PhysicalOp &plan,
                             const ExecutionContext &context) override;

  std::vector<int> device_ids() const override { return device_ids_; }

  // Samples device utilization across a whole evaluation via NVML.
  void BeginMonitor() override;
  void EndMonitor() override;
  double gpu_util_avg() const override;
  double gpu_util_max() const override;

private:
  int num_devices_;
  std::vector<int> device_ids_;
  struct Monitor;
  std::unique_ptr<Monitor> monitor_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BACKEND_GPU_GPU_BACKEND_H_