// The warp generator on the GPU (perf/fg_gpu.md): in game it takes the frame the command
// processor already submitted (gpu/fg_live.h); offline it runs the same passes on a headless
// device of its own (gpu/fg_offline.h).
#pragma once
#include <memory>

#include "gpu/fg_gpu.h"
#include "video/framegen.h"
#include "video/framegen_warp.h"

namespace video::framegen {

std::unique_ptr<Generator> MakeLiveGpuWarpGenerator();
std::unique_ptr<Generator> MakeOfflineGpuWarpGenerator(const WarpParams& p);
// The last result of a GPU warp generator (nullptr for other generators or before the first).
const gpu::fggpu::Result* LastGpuResult(const Generator* g);

}  // namespace video::framegen
