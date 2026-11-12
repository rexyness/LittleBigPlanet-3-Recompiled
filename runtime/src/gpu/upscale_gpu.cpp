#include "gpu/upscale_gpu.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <span>

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include <vk_mem_alloc.h>

#include "gpu/fg_shaders/upscale_easu_comp.h"
#include "gpu/fg_shaders/upscale_rcas_comp.h"

// FSR 1's constant setup on the CPU (AMD, MIT; shadPS4's in-tree copy, as its fsr_pass.cpp uses it).
#define A_CPU
#include "video_core/host_shaders/fsr/ffx_a.h"
#include "video_core/host_shaders/fsr/ffx_fsr1.h"

namespace gpu::upgpu {
namespace {

enum Pass { kEasu, kRcas, kPasses };
enum Binding { bIn, bMid, bOut, kBindings };

// upscale.comp's push constants.
struct PC {
    uint32_t con0[4], con1[4], con2[4], con3[4];
    uint32_t sizes[4];
};
static_assert(sizeof(PC) == 80);

struct Buf {
    vk::Buffer buf{};
    VmaAllocation alloc{};
    void* mapped = nullptr;
    vk::DeviceSize size = 0;
};

uint32_t DivUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

}  // namespace

struct UpscaleGpu::Impl {
    vk::Device device;
    vk::PhysicalDevice phys;
    VmaAllocator allocator{};
    double tsPeriodNs = 0;
    bool ok = false;

    vk::DescriptorSetLayout setLayout{};
    vk::PipelineLayout layout{};
    std::array<vk::Pipeline, kPasses> pipes{};
    vk::QueryPool queries{};  // two timestamps per (kind, slot)

    uint32_t inW = 0, inH = 0, outW = 0, outH = 0;
    Buf input, mid, out, staging;
    Buf readOut[kKinds][kSlots];

    ~Impl() {
        Free();
        for (auto& p : pipes)
            if (p) device.destroyPipeline(p);
        if (layout) device.destroyPipelineLayout(layout);
        if (setLayout) device.destroyDescriptorSetLayout(setLayout);
        if (queries) device.destroyQueryPool(queries);
    }

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
        VmaAllocationInfo info2{};
        if (vmaCreateBuffer(allocator, &info, &ai, &vb, &b.alloc, &info2) != VK_SUCCESS) return false;
        b.buf = vb;
        b.mapped = info2.pMappedData;
        b.size = size;
        return true;
    }
    void FreeBuf(Buf& b) {
        if (b.buf) vmaDestroyBuffer(allocator, b.buf, b.alloc);
        b = Buf{};
    }
    void Free() {
        for (auto* b : {&input, &mid, &out, &staging}) FreeBuf(*b);
        for (auto& kind : readOut)
            for (auto& b : kind) FreeBuf(b);
        inW = inH = outW = outH = 0;
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
        auto [r2, pl] = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{
            .setLayoutCount = 1, .pSetLayouts = &setLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range});
        if (r2 != vk::Result::eSuccess) return false;
        layout = pl;
        const std::array<std::span<const uint32_t>, kPasses> code{std::span<const uint32_t>(UPSCALE_EASU_COMP),
                                                                  std::span<const uint32_t>(UPSCALE_RCAS_COMP)};
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
        auto [r4, qp] = device.createQueryPool(
            vk::QueryPoolCreateInfo{.queryType = vk::QueryType::eTimestamp, .queryCount = 2 * kKinds * kSlots});
        if (r4 == vk::Result::eSuccess) queries = qp;
        tsPeriodNs = phys.getProperties().limits.timestampPeriod;
        return true;
    }

    bool Resize(uint32_t iw, uint32_t ih, uint32_t ow, uint32_t oh) {
        if (iw == inW && ih == inH && ow == outW && oh == outH && input.buf) return true;
        Free();
        if (iw < 16 || ih < 16 || ow < 16 || oh < 16) return false;
        using U = vk::BufferUsageFlagBits;
        const vk::BufferUsageFlags dev = U::eStorageBuffer | U::eTransferDst | U::eTransferSrc;
        const vk::DeviceSize inBytes = vk::DeviceSize{iw} * ih * 4, outBytes = vk::DeviceSize{ow} * oh * 4;
        bool good = MakeBuf(inBytes, dev, false, input) && MakeBuf(outBytes * 2, dev, false, mid) &&
                    MakeBuf(outBytes, dev, false, out);
        for (auto& kind : readOut)
            for (auto& b : kind) good = good && MakeBuf(outBytes, U::eTransferDst, true, b);
        if (!good) {
            Free();
            return false;
        }
        inW = iw;
        inH = ih;
        outW = ow;
        outH = oh;
        return true;
    }

    void Barrier(vk::CommandBuffer cmd, vk::PipelineStageFlags2 src, vk::AccessFlags2 srcA, vk::PipelineStageFlags2 dst,
                 vk::AccessFlags2 dstA) {
        const vk::MemoryBarrier2 mb{.srcStageMask = src, .srcAccessMask = srcA, .dstStageMask = dst, .dstAccessMask = dstA};
        cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &mb});
    }

    void Bind(vk::CommandBuffer cmd, Pass pass, vk::Buffer src) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipes[pass]);
        const vk::Buffer bufs[kBindings] = {src, mid.buf, out.buf};
        std::array<vk::DescriptorBufferInfo, kBindings> infos{};
        std::array<vk::WriteDescriptorSet, kBindings> writes{};
        for (uint32_t i = 0; i < kBindings; ++i) {
            infos[i] = vk::DescriptorBufferInfo{.buffer = bufs[i], .offset = 0, .range = VK_WHOLE_SIZE};
            writes[i] = vk::WriteDescriptorSet{.dstBinding = i, .descriptorCount = 1,
                                               .descriptorType = vk::DescriptorType::eStorageBuffer,
                                               .pBufferInfo = &infos[i]};
        }
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout, 0, writes);
    }

    void Record(vk::CommandBuffer cmd, vk::Buffer src, int kind, int slot, float sharpness) {
        using S = vk::PipelineStageFlagBits2;
        using A = vk::AccessFlagBits2;
        const int q = (kind * kSlots + slot) * 2;
        if (queries) {
            cmd.resetQueryPool(queries, uint32_t(q), 2);
            cmd.writeTimestamp2(S::eTopOfPipe, queries, uint32_t(q));
        }
        // The source was written by a transfer (the display copy) or a compute pass (the in-between
        // frame); mid and out were used by an earlier submission's passes and copy.
        Barrier(cmd, S::eTransfer | S::eComputeShader, A::eTransferWrite | A::eShaderStorageWrite | A::eTransferRead,
                S::eComputeShader, A::eShaderStorageRead | A::eShaderStorageWrite);
        PC pc{};
        FsrEasuCon(pc.con0, pc.con1, pc.con2, pc.con3, AF1(inW), AF1(inH), AF1(inW), AF1(inH), AF1(outW), AF1(outH));
        pc.sizes[0] = inW;
        pc.sizes[1] = inH;
        pc.sizes[2] = outW;
        pc.sizes[3] = outH;
        Bind(cmd, kEasu, src);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PC), &pc);
        cmd.dispatch(DivUp(outW, 8), DivUp(outH, 8), 1);
        Barrier(cmd, S::eComputeShader, A::eShaderStorageWrite, S::eComputeShader, A::eShaderStorageRead);
        FsrRcasCon(pc.con0, AF1(sharpness));
        Bind(cmd, kRcas, src);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PC), &pc);
        cmd.dispatch(DivUp(outW, 8), DivUp(outH, 8), 1);
        Barrier(cmd, S::eComputeShader, A::eShaderStorageWrite, S::eTransfer, A::eTransferRead);
        Buf& dst = readOut[kind][slot];
        cmd.copyBuffer(out.buf, dst.buf, vk::BufferCopy{.size = vk::DeviceSize{outW} * outH * 4});
        if (queries) cmd.writeTimestamp2(S::eAllCommands, queries, uint32_t(q) + 1);
        Barrier(cmd, S::eTransfer, A::eTransferWrite, S::eHost, A::eHostRead);
    }
};

UpscaleGpu::UpscaleGpu(const fggpu::Device& dev) : impl_(std::make_unique<Impl>()) {
    impl_->device = vk::Device(static_cast<VkDevice>(dev.device));
    impl_->phys = vk::PhysicalDevice(static_cast<VkPhysicalDevice>(dev.physicalDevice));
    impl_->allocator = static_cast<VmaAllocator>(dev.allocator);
    impl_->ok = impl_->MakePipelines();
    if (!impl_->ok) std::fprintf(stderr, "[upscale] pipeline creation failed\n");
}

UpscaleGpu::~UpscaleGpu() = default;

bool UpscaleGpu::Ok() const { return impl_->ok; }
bool UpscaleGpu::Resize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
    return impl_->ok && impl_->Resize(inW, inH, outW, outH);
}
bool UpscaleGpu::Ready() const { return impl_->ok && impl_->input.buf; }
uint32_t UpscaleGpu::InW() const { return impl_->inW; }
uint32_t UpscaleGpu::InH() const { return impl_->inH; }
uint32_t UpscaleGpu::OutW() const { return impl_->outW; }
uint32_t UpscaleGpu::OutH() const { return impl_->outH; }
uint64_t UpscaleGpu::OutBytes() const { return uint64_t(impl_->outW) * impl_->outH * 4; }
void* UpscaleGpu::InputBuffer() const { return static_cast<VkBuffer>(impl_->input.buf); }

void UpscaleGpu::RecordUpload(void* cmdp, const void* pixels) {
    Impl& m = *impl_;
    const vk::DeviceSize bytes = vk::DeviceSize{m.inW} * m.inH * 4;
    if (!bytes) return;
    if (m.staging.size < bytes) {
        m.FreeBuf(m.staging);
        if (!m.MakeBuf(bytes, vk::BufferUsageFlagBits::eTransferSrc, true, m.staging)) return;
    }
    std::memcpy(m.staging.mapped, pixels, bytes);
    vmaFlushAllocation(m.allocator, m.staging.alloc, 0, bytes);
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    m.Barrier(cmd, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead,
              vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
    cmd.copyBuffer(m.staging.buf, m.input.buf, vk::BufferCopy{.size = bytes});
}

void UpscaleGpu::Record(void* cmd, void* src, int kind, int slot, float sharpness) {
    Impl& m = *impl_;
    if (!Ready()) return;
    const vk::Buffer s = src ? vk::Buffer(static_cast<VkBuffer>(src)) : m.input.buf;
    m.Record(vk::CommandBuffer(static_cast<VkCommandBuffer>(cmd)), s, kind % kKinds, slot % kSlots, sharpness);
}

const uint8_t* UpscaleGpu::Output(int kind, int slot) {
    Buf& b = impl_->readOut[kind % kKinds][slot % kSlots];
    if (!b.buf) return nullptr;
    vmaInvalidateAllocation(impl_->allocator, b.alloc, 0, b.size);
    return static_cast<const uint8_t*>(b.mapped);
}

double UpscaleGpu::GpuMs(int kind, int slot) {
    Impl& m = *impl_;
    if (!m.queries) return -1;
    const int q = ((kind % kKinds) * kSlots + slot % kSlots) * 2;
    uint64_t ts[2]{};
    const vk::Result r = m.device.getQueryPoolResults(m.queries, uint32_t(q), 2, sizeof ts, ts, sizeof(uint64_t),
                                                      vk::QueryResultFlagBits::e64);
    if (r != vk::Result::eSuccess || ts[1] < ts[0]) return -1;
    return double(ts[1] - ts[0]) * m.tsPeriodNs / 1e6;
}

}  // namespace gpu::upgpu
