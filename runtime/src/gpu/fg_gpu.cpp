#include "gpu/fg_gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include <vk_mem_alloc.h>

#include "gpu/fg_shaders/fg_warp_coarse_comp.h"
#include "gpu/fg_shaders/fg_warp_compose_comp.h"
#include "gpu/fg_shaders/fg_warp_decide_comp.h"
#include "gpu/fg_shaders/fg_warp_down_comp.h"
#include "gpu/fg_shaders/fg_warp_gamevec_comp.h"
#include "gpu/fg_shaders/fg_warp_hudshare_comp.h"
#include "gpu/fg_shaders/fg_warp_init_comp.h"
#include "gpu/fg_shaders/fg_warp_luma0_comp.h"
#include "gpu/fg_shaders/fg_warp_midinit_comp.h"
#include "gpu/fg_shaders/fg_warp_smooth_comp.h"

namespace gpu::fggpu {
namespace {

constexpr int kLevels = 3;
constexpr int kBlock = 8;
constexpr int kStats = 16;  // uints in the stats buffer (fg_warp.comp's S_ constants)
enum Pass { kHudShare, kDecide, kLuma0, kDown, kGameVec, kCoarse, kInit, kSmooth, kMidInit, kCompose, kPasses };
enum Binding { bFinalPrev, bFinalCur, bHudPrev, bHudCur, bVel, bLuma, bFields, bGVec, bStats, bOut, kBindings };

// fg_warp.comp's push constants, in order.
struct PC {
    int32_t W, H, lw, lh, bw, bh, plPrev, plCur, src0, dst0, src1, dst1, par0, par1, pbw, pbh, gOff, radius,
        lambdaZero, lambdaSmooth;
    int32_t a[12];
};
static_assert(sizeof(PC) == 128);

struct Buf {
    vk::Buffer buf{};
    VmaAllocation alloc{};
    void* mapped = nullptr;
    vk::DeviceSize size = 0;
};

int32_t FloatBits(float f) {
    int32_t i;
    std::memcpy(&i, &f, 4);
    return i;
}

uint32_t DivUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

}  // namespace

struct WarpGpu::Impl {
    vk::Device device;
    vk::PhysicalDevice phys;
    VmaAllocator allocator{};
    video::framegen::WarpParams p;
    double tsPeriodNs = 0;
    bool ok = false;

    vk::DescriptorSetLayout setLayout{};
    vk::PipelineLayout layout{};
    std::array<vk::Pipeline, kPasses> pipes{};
    vk::QueryPool queries{};
    // LBP3_FRAMEGEN_GPU_PROFILE=1 (offline): a timestamp after each pass, printed per frame.
    vk::QueryPool prof{};
    std::vector<const char*> profNames;
    bool profiling = false;

    uint32_t W = 0, H = 0;
    int lw[kLevels]{}, lh[kLevels]{}, bw[kLevels]{}, bh[kLevels]{};
    int lumaOff[2][kLevels]{};       // [frame: 0 = N-1, 1 = N][level]
    int fieldOff[3][kLevels][2]{};   // [0 back, 1 fwd, 2 mid (level 0 only)][level][ping-pong]
    int gvecOff[kLevels]{};          // levels 0 and 2
    Buf finals[2], huds[2], vel, luma, fields, gvec, stats, out, staging;
    Buf readOut[kReadSlots], readStats[kReadSlots];

    ~Impl() { Free(); DestroyPipelines(); }

    bool MakeBuf(vk::DeviceSize size, vk::BufferUsageFlags usage, bool host, Buf& b) {
        VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                                .usage = static_cast<VkBufferUsageFlags>(usage)};
        VmaAllocationCreateInfo ai{};
        if (host) {
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        } else {
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }
        VkBuffer vb{};
        VmaAllocationInfo out{};
        if (vmaCreateBuffer(allocator, &info, &ai, &vb, &b.alloc, &out) != VK_SUCCESS) return false;
        b.buf = vb;
        b.mapped = out.pMappedData;
        b.size = size;
        return true;
    }
    void FreeBuf(Buf& b) {
        if (b.buf) vmaDestroyBuffer(allocator, b.buf, b.alloc);
        b = Buf{};
    }
    void Free() {
        for (auto* b : {&finals[0], &finals[1], &huds[0], &huds[1], &vel, &luma, &fields, &gvec, &stats, &out, &staging})
            FreeBuf(*b);
        for (int i = 0; i < kReadSlots; ++i) FreeBuf(readOut[i]), FreeBuf(readStats[i]);
        W = H = 0;
    }
    void DestroyPipelines() {
        for (auto& pp : pipes)
            if (pp) device.destroyPipeline(pp), pp = vk::Pipeline{};
        if (layout) device.destroyPipelineLayout(layout), layout = vk::PipelineLayout{};
        if (setLayout) device.destroyDescriptorSetLayout(setLayout), setLayout = vk::DescriptorSetLayout{};
        if (queries) device.destroyQueryPool(queries), queries = vk::QueryPool{};
        if (prof) device.destroyQueryPool(prof), prof = vk::QueryPool{};
    }

    bool MakePipelines() {
        std::array<vk::DescriptorSetLayoutBinding, kBindings> b{};
        for (uint32_t i = 0; i < kBindings; ++i)
            b[i] = vk::DescriptorSetLayoutBinding{.binding = i, .descriptorType = vk::DescriptorType::eStorageBuffer,
                                                  .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute};
        const vk::DescriptorSetLayoutCreateInfo dl{.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                                                   .bindingCount = kBindings, .pBindings = b.data()};
        auto [r1, sl] = device.createDescriptorSetLayout(dl);
        if (r1 != vk::Result::eSuccess) return false;
        setLayout = sl;
        const vk::PushConstantRange range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0,
                                          .size = sizeof(PC)};
        const vk::PipelineLayoutCreateInfo li{.setLayoutCount = 1, .pSetLayouts = &setLayout,
                                              .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
        auto [r2, pl] = device.createPipelineLayout(li);
        if (r2 != vk::Result::eSuccess) return false;
        layout = pl;
        const std::array<std::span<const uint32_t>, kPasses> code{
            std::span<const uint32_t>(FG_WARP_HUDSHARE_COMP), std::span<const uint32_t>(FG_WARP_DECIDE_COMP),
            std::span<const uint32_t>(FG_WARP_LUMA0_COMP),    std::span<const uint32_t>(FG_WARP_DOWN_COMP),
            std::span<const uint32_t>(FG_WARP_GAMEVEC_COMP),  std::span<const uint32_t>(FG_WARP_COARSE_COMP),
            std::span<const uint32_t>(FG_WARP_INIT_COMP),     std::span<const uint32_t>(FG_WARP_SMOOTH_COMP),
            std::span<const uint32_t>(FG_WARP_MIDINIT_COMP),  std::span<const uint32_t>(FG_WARP_COMPOSE_COMP)};
        for (int i = 0; i < kPasses; ++i) {
            const vk::ShaderModule m = Vulkan::CompileSPV(code[i], device);
            if (!m) return false;
            const vk::ComputePipelineCreateInfo ci{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = m, .pName = "main"}, .layout = layout};
            auto [r3, pipe] = device.createComputePipeline({}, ci);
            device.destroyShaderModule(m);
            if (r3 != vk::Result::eSuccess) return false;
            pipes[i] = pipe;
        }
        const vk::QueryPoolCreateInfo qi{.queryType = vk::QueryType::eTimestamp, .queryCount = 2 * kReadSlots};
        auto [r4, qp] = device.createQueryPool(qi);
        if (r4 == vk::Result::eSuccess) queries = qp;
        if (const char* e = std::getenv("LBP3_FRAMEGEN_GPU_PROFILE"); e && *e == '1') {
            auto [r5, pq] = device.createQueryPool(vk::QueryPoolCreateInfo{.queryType = vk::QueryType::eTimestamp,
                                                                           .queryCount = 64});
            if (r5 == vk::Result::eSuccess) prof = pq, profiling = true;
        }
        const auto props = phys.getProperties();
        tsPeriodNs = props.limits.timestampPeriod;
        return true;
    }

    bool Resize(uint32_t w, uint32_t h) {
        if (w == W && h == H && finals[0].buf) return true;
        Free();
        if (w < 64 || h < 64) return false;
        W = w;
        H = h;
        lw[0] = int(w / 2);
        lh[0] = int(h / 2);
        for (int l = 1; l < kLevels; ++l) lw[l] = lw[l - 1] / 2, lh[l] = lh[l - 1] / 2;
        int lumaTotal = 0;
        for (int f = 0; f < 2; ++f)
            for (int l = 0; l < kLevels; ++l) lumaOff[f][l] = lumaTotal, lumaTotal += lw[l] * lh[l];
        int fieldTotal = 0;
        for (int l = 0; l < kLevels; ++l) {
            bw[l] = (lw[l] + kBlock - 1) / kBlock;
            bh[l] = (lh[l] + kBlock - 1) / kBlock;
            for (int k = 0; k < 3; ++k)
                for (int pp = 0; pp < 2; ++pp) {
                    if (k == 2 && l != 0) continue;
                    fieldOff[k][l][pp] = fieldTotal;
                    fieldTotal += bw[l] * bh[l];
                }
        }
        gvecOff[0] = 0;
        gvecOff[1] = -1;
        gvecOff[2] = bw[0] * bh[0];
        const int gvecTotal = bw[0] * bh[0] + bw[2] * bh[2];
        const vk::DeviceSize fb = vk::DeviceSize{w} * h * 4;
        using U = vk::BufferUsageFlagBits;
        const vk::BufferUsageFlags dev = U::eStorageBuffer | U::eTransferDst | U::eTransferSrc;
        bool good = true;
        for (int i = 0; i < 2; ++i) good = good && MakeBuf(fb, dev, false, finals[i]) && MakeBuf(fb, dev, false, huds[i]);
        good = good && MakeBuf(fb, dev, false, vel) && MakeBuf(fb, dev, false, out) &&
               MakeBuf(vk::DeviceSize(lumaTotal) * 4, dev, false, luma) &&
               MakeBuf(vk::DeviceSize(fieldTotal) * 8, dev, false, fields) &&
               MakeBuf(vk::DeviceSize(gvecTotal) * 8, dev, false, gvec) && MakeBuf(kStats * 4, dev, false, stats);
        for (int i = 0; i < kReadSlots; ++i)
            good = good && MakeBuf(fb, U::eTransferDst, true, readOut[i]) &&
                   MakeBuf(kStats * 4, U::eTransferDst, true, readStats[i]);
        if (!good) {
            Free();
            return false;
        }
        return true;
    }

    void Barrier(vk::CommandBuffer cmd, vk::PipelineStageFlags2 src, vk::AccessFlags2 srcA, vk::PipelineStageFlags2 dst,
                 vk::AccessFlags2 dstA) {
        const vk::MemoryBarrier2 mb{.srcStageMask = src, .srcAccessMask = srcA, .dstStageMask = dst, .dstAccessMask = dstA};
        cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &mb});
    }
    void Mark(vk::CommandBuffer cmd, const char* name) {
        if (!profiling || profNames.size() >= 64) return;
        cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, prof, uint32_t(profNames.size()));
        profNames.push_back(name);
    }
    void ComputeBarrier(vk::CommandBuffer cmd) {
        using S = vk::PipelineStageFlagBits2;
        using A = vk::AccessFlagBits2;
        Barrier(cmd, S::eComputeShader, A::eShaderStorageWrite | A::eShaderStorageRead, S::eComputeShader,
                A::eShaderStorageRead | A::eShaderStorageWrite);
    }

    void Bind(vk::CommandBuffer cmd, Pass pass, int cur) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipes[pass]);
        const int prev = cur ^ 1;
        const Buf* bufs[kBindings] = {&finals[prev], &finals[cur], &huds[prev], &huds[cur], &vel,
                                      &luma,         &fields,      &gvec,       &stats,      &out};
        std::array<vk::DescriptorBufferInfo, kBindings> infos{};
        std::array<vk::WriteDescriptorSet, kBindings> writes{};
        for (uint32_t i = 0; i < kBindings; ++i) {
            infos[i] = vk::DescriptorBufferInfo{.buffer = bufs[i]->buf, .offset = 0, .range = VK_WHOLE_SIZE};
            writes[i] = vk::WriteDescriptorSet{.dstBinding = i, .descriptorCount = 1,
                                               .descriptorType = vk::DescriptorType::eStorageBuffer,
                                               .pBufferInfo = &infos[i]};
        }
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
    }
    void Push(vk::CommandBuffer cmd, const PC& pc) {
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PC), &pc);
    }
    PC Base() const {
        PC pc{};
        pc.W = int32_t(W);
        pc.H = int32_t(H);
        pc.lambdaZero = p.lambdaZero;
        pc.lambdaSmooth = p.lambdaSmooth;
        pc.gOff = -1;
        return pc;
    }
    void SetLevel(PC& pc, int l) const {
        pc.lw = lw[l];
        pc.lh = lh[l];
        pc.bw = bw[l];
        pc.bh = bh[l];
        pc.plPrev = lumaOff[0][l];
        pc.plCur = lumaOff[1][l];
    }

    void Record(vk::CommandBuffer cmd, const FrameFlags& f, int slot) {
        using S = vk::PipelineStageFlagBits2;
        using A = vk::AccessFlagBits2;
        const int cur = f.cur & 1;
        // The input copies (transfer, earlier in this or earlier submissions) before any read,
        // and the previous generation's reads and copies before this one's writes.
        Barrier(cmd, S::eTransfer | S::eComputeShader | S::eHost, A::eTransferWrite | A::eShaderStorageWrite | A::eHostWrite,
                S::eComputeShader | S::eTransfer, A::eShaderStorageRead | A::eShaderStorageWrite | A::eTransferWrite);
        if (queries) {
            cmd.resetQueryPool(queries, uint32_t(slot) * 2, 2);
            cmd.writeTimestamp2(S::eTopOfPipe, queries, uint32_t(slot) * 2);
        }
        if (profiling) {
            profNames.clear();
            cmd.resetQueryPool(prof, 0, 64);
            Mark(cmd, "start");
        }
        cmd.fillBuffer(stats.buf, 0, kStats * 4, 0);
        Barrier(cmd, S::eTransfer, A::eTransferWrite, S::eComputeShader, A::eShaderStorageRead | A::eShaderStorageWrite);

        const bool hudAllowed = p.useHudless && f.hudPrev && f.hudCur;
        const bool useVec = p.useGameVectors && f.vel;
        // The HUD share (N against its HUD-less image) decides whether the HUD-less images are
        // interpolated (stats[S_USEHUD]).
        if (hudAllowed) {
            Bind(cmd, kHudShare, cur);
            PC pc = Base();
            pc.a[0] = p.hudT;
            pc.a[1] = int32_t(DivUp(W - 4, 16));
            pc.a[2] = int32_t(DivUp(H - 4, 16));
            Push(cmd, pc);
            cmd.dispatch(DivUp(uint32_t(pc.a[1]), 8), DivUp(uint32_t(pc.a[2]), 8), 1);
            ComputeBarrier(cmd);
        }
        {
            Bind(cmd, kDecide, cur);
            PC pc = Base();
            pc.a[0] = hudAllowed ? 1 : 0;
            Push(cmd, pc);
            cmd.dispatch(1, 1, 1);
            ComputeBarrier(cmd);
            Mark(cmd, "hud");
        }
        // Luma pyramids of both frames.
        {
            Bind(cmd, kLuma0, cur);
            PC pc = Base();
            SetLevel(pc, 0);
            pc.a[0] = hudAllowed ? 1 : 0;
            Push(cmd, pc);
            cmd.dispatch(DivUp(uint32_t(lw[0]), 8), DivUp(uint32_t(lh[0]), 8), 2);
            ComputeBarrier(cmd);
            for (int l = 1; l < kLevels; ++l) {
                Bind(cmd, kDown, cur);
                PC d = Base();
                SetLevel(d, l);
                d.a[0] = lumaOff[0][l - 1];
                d.a[1] = lumaOff[1][l - 1];
                d.a[2] = lw[l - 1];
                d.a[3] = lh[l - 1];
                Push(cmd, d);
                cmd.dispatch(DivUp(uint32_t(lw[l]), 8), DivUp(uint32_t(lh[l]), 8), 2);
                ComputeBarrier(cmd);
            }
            Mark(cmd, "luma");
        }
        // The game's vectors as candidates (levels 0 and 2), and the saturation counts.
        if (useVec) {
            for (int l : {0, kLevels - 1}) {
                Bind(cmd, kGameVec, cur);
                PC pc = Base();
                SetLevel(pc, l);
                pc.gOff = gvecOff[l];
                // Pixels per step at 1920x1080 (vecScaleX/Y), scaled to this render size.
                const float sx = p.vecScaleX * f.motionScale * (float(W) / 1920.0f),
                            sy = p.vecScaleY * f.motionScale * (float(H) / 1080.0f);
                const int scale = 2 << l;
                pc.a[0] = l;
                pc.a[1] = FloatBits(sx / float(scale));
                pc.a[2] = FloatBits(sy / float(scale));
                pc.a[3] = (p.vecChanCover & 15) | ((p.vecChanX & 15) << 4) | ((p.vecChanY & 15) << 8);
                pc.a[4] = FloatBits(p.vecBias);
                Push(cmd, pc);
                cmd.dispatch(DivUp(uint32_t(bw[l]), 8), DivUp(uint32_t(bh[l]), 8), 1);
            }
            ComputeBarrier(cmd);
            Mark(cmd, "gamevec");
        }
        // One-sided fields (z = 0: anchored on N, z = 1: on N-1), coarse to fine.
        int at[3][kLevels]{};  // which ping-pong copy holds each field
        auto smooth = [&](int l, bool mid) {
            for (int pass = 0; pass < p.smoothPasses; ++pass) {
                Bind(cmd, kSmooth, cur);
                PC pc = Base();
                SetLevel(pc, l);
                if (mid) {
                    pc.a[0] = 1;
                    pc.src0 = fieldOff[2][0][at[2][0]];
                    pc.dst0 = fieldOff[2][0][at[2][0] ^ 1];
                    at[2][0] ^= 1;
                } else {
                    pc.src0 = fieldOff[0][l][at[0][l]];
                    pc.dst0 = fieldOff[0][l][at[0][l] ^ 1];
                    pc.src1 = fieldOff[1][l][at[1][l]];
                    pc.dst1 = fieldOff[1][l][at[1][l] ^ 1];
                    at[0][l] ^= 1;
                    at[1][l] ^= 1;
                }
                Push(cmd, pc);
                cmd.dispatch(DivUp(uint32_t(bw[l]), 8), DivUp(uint32_t(bh[l]), 8), mid ? 1 : 2);
                ComputeBarrier(cmd);
                Mark(cmd, mid ? "smooth mid" : (l == 0 ? "smooth l0" : (l == 1 ? "smooth l1" : "smooth l2")));
            }
        };
        {
            const int l = kLevels - 1;
            Bind(cmd, kCoarse, cur);
            PC pc = Base();
            SetLevel(pc, l);
            pc.dst0 = fieldOff[0][l][0];
            pc.dst1 = fieldOff[1][l][0];
            pc.gOff = useVec ? gvecOff[l] : -1;
            pc.radius = p.coarseRadius * 2;
            Push(cmd, pc);
            cmd.dispatch(uint32_t(bw[l]), uint32_t(bh[l]), 2);
            ComputeBarrier(cmd);
            Mark(cmd, "coarse");
            smooth(l, false);
        }
        for (int l = kLevels - 2; l >= 0; --l) {
            Bind(cmd, kInit, cur);
            PC pc = Base();
            SetLevel(pc, l);
            pc.par0 = fieldOff[0][l + 1][at[0][l + 1]];
            pc.par1 = fieldOff[1][l + 1][at[1][l + 1]];
            pc.pbw = bw[l + 1];
            pc.pbh = bh[l + 1];
            at[0][l] = at[1][l] = 0;
            pc.dst0 = fieldOff[0][l][0];
            pc.dst1 = fieldOff[1][l][0];
            pc.gOff = (l == 0 && useVec) ? gvecOff[0] : -1;
            pc.radius = p.refineRadius;
            Push(cmd, pc);
            cmd.dispatch(DivUp(uint32_t(bw[l]), 8), DivUp(uint32_t(bh[l]), 8), 2);
            ComputeBarrier(cmd);
            Mark(cmd, l == 0 ? "init l0" : "init l1");
            smooth(l, false);
        }
        // The in-between field.
        {
            Bind(cmd, kMidInit, cur);
            PC pc = Base();
            SetLevel(pc, 0);
            pc.a[0] = fieldOff[0][0][at[0][0]];
            pc.a[1] = fieldOff[1][0][at[1][0]];
            at[2][0] = 0;
            pc.dst0 = fieldOff[2][0][0];
            pc.gOff = useVec ? gvecOff[0] : -1;
            Push(cmd, pc);
            cmd.dispatch(DivUp(uint32_t(bw[0]), 8), DivUp(uint32_t(bh[0]), 8), 1);
            ComputeBarrier(cmd);
            Mark(cmd, "midinit");
            smooth(0, true);
        }
        // Compose (and the HUD put-back).
        {
            Bind(cmd, kCompose, cur);
            PC pc = Base();
            pc.bw = bw[0];
            pc.bh = bh[0];
            pc.a[0] = fieldOff[2][0][at[2][0]];
            pc.a[1] = p.agreeT;
            pc.a[2] = p.altT;
            pc.a[3] = p.staticT;
            pc.a[4] = (p.perPixelAlt ? 1 : 0) | (p.staticCopy ? 2 : 0) | (hudAllowed ? 4 : 0) | (f.t >= 0.5f ? 8 : 0);
            pc.a[5] = p.fallback;
            const int tP = int(std::lround(f.t * 512.0f));
            pc.a[6] = tP;
            pc.a[7] = 512 - tP;
            pc.a[8] = int(std::lround(f.t * 256.0f));
            pc.a[9] = p.hudT;
            Push(cmd, pc);
            cmd.dispatch(DivUp(W, 8), DivUp(H, 8), 1);
            Mark(cmd, "compose");
        }
        Barrier(cmd, S::eComputeShader, A::eShaderStorageWrite, S::eTransfer, A::eTransferRead);
        cmd.copyBuffer(out.buf, readOut[slot].buf, vk::BufferCopy{.size = vk::DeviceSize{W} * H * 4});
        cmd.copyBuffer(stats.buf, readStats[slot].buf, vk::BufferCopy{.size = kStats * 4});
        if (queries) cmd.writeTimestamp2(S::eAllCommands, queries, uint32_t(slot) * 2 + 1);
        Barrier(cmd, S::eTransfer, A::eTransferWrite, S::eHost, A::eHostRead);
        lastUseVec[slot] = useVec;
    }

    bool lastUseVec[kReadSlots]{};
};

WarpGpu::WarpGpu(const Device& dev, const video::framegen::WarpParams& p) : impl_(std::make_unique<Impl>()) {
    impl_->device = vk::Device(static_cast<VkDevice>(dev.device));
    impl_->phys = vk::PhysicalDevice(static_cast<VkPhysicalDevice>(dev.physicalDevice));
    impl_->allocator = static_cast<VmaAllocator>(dev.allocator);
    impl_->p = p;
    impl_->ok = impl_->MakePipelines();
    if (!impl_->ok) std::fprintf(stderr, "[framegen] GPU warp: pipeline creation failed\n");
}

WarpGpu::~WarpGpu() = default;

bool WarpGpu::Resize(uint32_t w, uint32_t h) { return impl_->ok && impl_->Resize(w, h); }
uint32_t WarpGpu::Width() const { return impl_->W; }
uint32_t WarpGpu::Height() const { return impl_->H; }
bool WarpGpu::Ready() const { return impl_->ok && impl_->finals[0].buf; }
void* WarpGpu::FinalBuffer(int slot) const { return static_cast<VkBuffer>(impl_->finals[slot & 1].buf); }
void* WarpGpu::HudBuffer(int slot) const { return static_cast<VkBuffer>(impl_->huds[slot & 1].buf); }
void* WarpGpu::VelBuffer() const { return static_cast<VkBuffer>(impl_->vel.buf); }
uint64_t WarpGpu::FrameBytes() const { return uint64_t(impl_->W) * impl_->H * 4; }

void WarpGpu::RecordUpload(void* cmdp, void* dst, const void* pixels, int index) {
    Impl& m = *impl_;
    const vk::DeviceSize fb = FrameBytes();
    if (!m.staging.buf && !m.MakeBuf(fb * 5, vk::BufferUsageFlagBits::eTransferSrc, true, m.staging)) return;
    std::memcpy(static_cast<uint8_t*>(m.staging.mapped) + fb * index, pixels, fb);
    vmaFlushAllocation(m.allocator, m.staging.alloc, fb * index, fb);
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    cmd.copyBuffer(m.staging.buf, vk::Buffer(static_cast<VkBuffer>(dst)),
                   vk::BufferCopy{.srcOffset = fb * index, .dstOffset = 0, .size = fb});
}

void WarpGpu::Record(void* cmd, const FrameFlags& f, int readSlot) {
    impl_->Record(vk::CommandBuffer(static_cast<VkCommandBuffer>(cmd)), f, readSlot % kReadSlots);
}

const uint8_t* WarpGpu::Output(int readSlot) {
    Buf& b = impl_->readOut[readSlot % kReadSlots];
    vmaInvalidateAllocation(impl_->allocator, b.alloc, 0, b.size);
    return static_cast<const uint8_t*>(b.mapped);
}

Result WarpGpu::ReadResult(int readSlot) {
    Impl& m = *impl_;
    const int slot = readSlot % kReadSlots;
    Buf& b = m.readStats[slot];
    vmaInvalidateAllocation(m.allocator, b.alloc, 0, b.size);
    uint32_t s[kStats];
    std::memcpy(s, b.mapped, sizeof s);
    Result r;
    r.total = s[0];
    r.covered = s[1];
    r.saturated = s[2];
    r.hudHit = s[3];
    r.hudN = s[4];
    r.usedHudless = s[5] != 0;
    r.pixAvg = s[6];
    r.pixAlt = s[7];
    r.pixFall = s[8];
    r.pixStatic = s[9];
    r.pixHud = s[10];
    r.usedVectors = m.lastUseVec[slot];
    // As the CPU version: decline when nearly all covered vectors are saturated (a cut).
    r.declined = r.usedVectors && r.covered && double(r.saturated) > double(m.p.maxSaturated) * double(r.covered);
    if (m.queries) {
        uint64_t ts[2]{};
        const vk::Result qr = m.device.getQueryPoolResults(m.queries, uint32_t(slot) * 2, 2, sizeof ts, ts, sizeof(uint64_t),
                                                           vk::QueryResultFlagBits::e64);
        if (qr == vk::Result::eSuccess && ts[1] >= ts[0]) r.gpuMs = double(ts[1] - ts[0]) * m.tsPeriodNs / 1e6;
    }
    if (m.profiling && m.profNames.size() > 1) {
        std::vector<uint64_t> ts(m.profNames.size());
        if (m.device.getQueryPoolResults(m.prof, 0, uint32_t(ts.size()), ts.size() * 8, ts.data(), 8,
                                         vk::QueryResultFlagBits::e64) == vk::Result::eSuccess) {
            std::fprintf(stderr, "[fgprof]");
            for (size_t i = 1; i < ts.size(); ++i)
                std::fprintf(stderr, " %s %.3f", m.profNames[i], double(ts[i] - ts[i - 1]) * m.tsPeriodNs / 1e6);
            std::fprintf(stderr, "\n");
        }
    }
    return r;
}

}  // namespace gpu::fggpu
