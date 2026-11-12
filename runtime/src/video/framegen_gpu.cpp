#include "video/framegen_gpu.h"

#include <cstdio>

#include "gpu/fg_live.h"
#include "gpu/fg_offline.h"

namespace video::framegen {
namespace {

class GpuWarpBase : public Generator {
public:
    const gpu::fggpu::Result* Last() const { return have_ ? &last_ : nullptr; }

protected:
    gpu::fggpu::Result last_;
    bool have_ = false;
};

// In game: the command processor submitted this frame's generation right after its readback;
// wait for it and take the result.
class LiveGpuWarp final : public GpuWarpBase {
public:
    const char* Name() const override { return "warp-gpu"; }
    double MarginMs() const override { return 2.0; }
    bool Generate(const GenInputs& in, std::vector<uint8_t>& out) override {
        if (!in.gpu.id) {
            NoteGpu(false, -1, 0, false, false, false);
            return false;
        }
        const gpu::fglive::Fetched f = gpu::fglive::Fetch(in.gpu, out);
        NoteGpu(f.ok, f.gpuMs, f.waitMs, f.declined, f.usedHudless, f.usedVectors);
        if (!f.ok) return false;
        last_ = gpu::fggpu::Result{};
        last_.gpuMs = f.gpuMs;
        last_.declined = f.declined;
        last_.usedVectors = f.usedVectors;
        last_.usedHudless = f.usedHudless;
        last_.covered = f.covered;
        last_.saturated = f.saturated;
        last_.total = f.total;
        have_ = true;
        return !f.declined;
    }
};

// Offline: upload the host frames and run the passes synchronously.
class OfflineGpuWarp final : public GpuWarpBase {
public:
    explicit OfflineGpuWarp(const WarpParams& p) : runner_(p) {}
    const char* Name() const override { return "warp-gpu"; }
    bool Ok() const { return runner_.Ok(); }
    bool Generate(const GenInputs& in, std::vector<uint8_t>& out) override {
        gpu::fggpu::OfflineInputs oi;
        oi.prev = in.prev.pixels;
        oi.cur = in.cur.pixels;
        oi.hudPrev = in.hudlessPrev;
        oi.hudCur = in.hudless;
        oi.vel = in.motionVectors;
        oi.width = in.cur.width;
        oi.height = in.cur.height;
        oi.t = in.t;
        oi.motionScale = in.motionScale;
        if (in.prev.width != in.cur.width || in.prev.height != in.cur.height) return false;
        if (!runner_.Run(oi, out, last_)) return false;
        have_ = true;
        return !last_.declined;
    }

private:
    gpu::fggpu::OfflineRunner runner_;
};

}  // namespace

std::unique_ptr<Generator> MakeLiveGpuWarpGenerator() { return std::make_unique<LiveGpuWarp>(); }

std::unique_ptr<Generator> MakeOfflineGpuWarpGenerator(const WarpParams& p) {
    auto g = std::make_unique<OfflineGpuWarp>(p);
    if (!g->Ok()) {
        std::fprintf(stderr, "[framegen] warp-gpu: no device\n");
        return nullptr;
    }
    return g;
}

const gpu::fggpu::Result* LastGpuResult(const Generator* g) {
    const auto* b = dynamic_cast<const GpuWarpBase*>(g);
    return b ? b->Last() : nullptr;
}

}  // namespace video::framegen
