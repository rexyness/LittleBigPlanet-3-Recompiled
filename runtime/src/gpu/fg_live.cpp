#include "gpu/fg_live.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#include "gpu/fg_gpu.h"
#include "gpu/fg_offline.h"
#include "sdl_window.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

#include <vk_mem_alloc.h>

namespace gpu::internal {  // renderer.cpp
Vulkan::Instance* GetInstance();
Vulkan::Scheduler* GetScheduler();
Vulkan::Rasterizer* GetRasterizer();
AmdGpu::Liverpool* GetLiverpool();
}  // namespace gpu::internal

namespace gpu::fglive {
namespace {

constexpr uint32_t kVelocityClear = 0x007f7f00u;

std::atomic<bool> g_on{false};
video::framegen::WarpParams g_params;
std::array<std::atomic<uint64_t>, 16> g_display{};
std::mutex g_genMutex;  // creation/resizing against the worker's Fetch
std::unique_ptr<fggpu::WarpGpu> g_gen;

// Command-processor thread state.
int g_slot = 0;  // the slot frame N's copies go into
bool g_hudValid[2] = {false, false};
bool g_haveFinal[2] = {false, false};
struct Frame {
    bool composite = false, hud = false, vel = false, final = false;
} g_frame;
uint64_t g_kicks = 0;
std::atomic<uint64_t> g_readId[fggpu::WarpGpu::kReadSlots];
bool g_inputsHooked = false;

bool IsDisplay(uint64_t addr) {
    if (!addr) return false;
    for (const auto& a : g_display)
        if (a.load(std::memory_order_relaxed) == addr) return true;
    return false;
}

bool FourByteColour(vk::Format f) {
    switch (f) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
        return true;
    default:
        return false;
    }
}

// The newest render-target image at addr (several alias a pooled address), as lane A's capture.
VideoCore::Image* FindTarget(uint64_t addr) {
    auto* rast = internal::GetRasterizer();
    if (!addr || !rast) return nullptr;
    VideoCore::Image* best = nullptr;
    rast->GetTextureCache().ForEachImageWithAddress(addr, [&](VideoCore::ImageId, VideoCore::Image& image) {
        if (!image.usage.render_target) return;
        if (image.info.size.width < 640 || image.info.size.height < 360) return;
        if (!best || image.tick_accessed_last > best->tick_accessed_last) best = &image;
    });
    return best;
}

// Records a copy of the image into one of the generator's buffers, in the current game command
// buffer. Copies only: the image is read (transfer source), never written.
bool CopyInto(VideoCore::Image& image, void* dstBuffer) {
    if (!g_gen || !g_gen->Ready()) return false;
    if (image.info.size.width != g_gen->Width() || image.info.size.height != g_gen->Height()) return false;
    if (!FourByteColour(image.info.pixel_format)) return false;
    auto* sched = internal::GetScheduler();
    sched->EndRendering();
    const auto cmd = sched->CommandBuffer();
    VideoCore::Image::Barriers barriers;
    image.GetBarriers(barriers, vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead,
                      vk::PipelineStageFlagBits2::eTransfer);
    // The previous generation (an earlier submission) reads these buffers: copy after it.
    const vk::MemoryBarrier2 war{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader |
                                                 vk::PipelineStageFlagBits2::eTransfer,
                                 .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &war,
                                            .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                            .pImageMemoryBarriers = barriers.data()});
    const vk::BufferImageCopy region{.bufferOffset = 0,
                                     .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                     .imageExtent = {image.info.size.width, image.info.size.height, 1}};
    cmd.copyImageToBuffer(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                          vk::Buffer(static_cast<VkBuffer>(dstBuffer)), region);
    return true;
}

void AfterDraw(uint64_t vs, uint64_t ps, uint32_t n) {
    (void)vs;
    (void)n;
    if (g_frame.composite || ps == 0) return;
    const auto& regs = internal::GetLiverpool()->regs;
    const auto& cb0 = regs.color_buffers[0];
    const uint64_t rt0 = cb0 ? cb0.Address() : 0;
    if (!IsDisplay(rt0)) return;
    // The composite: the first draw with a pixel shader into a display buffer. The screen HUD
    // comes after it, so this is the HUD-less image.
    g_frame.composite = true;
    if (VideoCore::Image* image = FindTarget(rt0)) g_frame.hud = CopyInto(*image, g_gen ? g_gen->HudBuffer(g_slot) : nullptr);
}

void OnFill(uint64_t dst, uint64_t bytes, uint32_t value, uint32_t mask, bool full) {
    (void)mask;
    (void)full;
    if (value != kVelocityClear || g_frame.vel || bytes < uint64_t{1920} * 1080 * 4) return;
    // The game clears its velocity target after the motion blur used it: copy it just before.
    if (VideoCore::Image* image = FindTarget(dst)) g_frame.vel = CopyInto(*image, g_gen ? g_gen->VelBuffer() : nullptr);
}

}  // namespace

void Configure(const video::framegen::WarpParams& p) {
    g_params = p;
    g_on = true;
    // The capture tool (LBP3_FG_DUMP) owns the same two hooks: with it, only the final frames.
    if (!Vulkan::g_lbp3FgAfterDraw && !Vulkan::g_lbp3FgOnFill) {
        Vulkan::g_lbp3FgAfterDraw = AfterDraw;
        Vulkan::g_lbp3FgOnFill = OnFill;
        g_inputsHooked = true;
    }
    std::fprintf(stderr, "[framegen] GPU warp on%s\n",
                 g_inputsHooked ? " (HUD-less colour and the game's vectors from their hooks)"
                                : "; the capture tool holds the input hooks, so final frames only");
}

bool Active() { return g_on.load(); }

void SetDisplayBuffer(int index, uint64_t address) {
    if (index >= 0 && index < 16) g_display[index] = address;
}

void OnReadbackCopy(void* cmdp, void* imagep) {
    if (!g_on) return;
    auto& image = *static_cast<VideoCore::Image*>(imagep);
    const uint32_t w = image.info.size.width, h = image.info.size.height;
    if (!g_gen || g_gen->Width() != w || g_gen->Height() != h) {
        std::lock_guard lk(g_genMutex);
        if (!g_gen) {
            auto* inst = internal::GetInstance();
            fggpu::Device d;
            d.device = static_cast<VkDevice>(inst->GetDevice());
            d.physicalDevice = static_cast<VkPhysicalDevice>(inst->GetPhysicalDevice());
            d.allocator = inst->GetAllocator();
            d.queueFamily = inst->GetGraphicsQueueFamilyIndex();
            g_gen = std::make_unique<fggpu::WarpGpu>(d, g_params);
        }
        if (!g_gen->Resize(w, h)) {
            std::fprintf(stderr, "[framegen] GPU warp: could not allocate for %ux%u\n", w, h);
            return;
        }
        g_haveFinal[0] = g_haveFinal[1] = false;
        g_hudValid[0] = g_hudValid[1] = false;
        g_frame.hud = g_frame.vel = false;  // copied before the buffers existed, or at another size
    }
    if (!FourByteColour(image.info.pixel_format)) return;
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    const vk::MemoryBarrier2 war{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader |
                                                 vk::PipelineStageFlagBits2::eTransfer,
                                 .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &war});
    const vk::BufferImageCopy region{.bufferOffset = 0,
                                     .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                     .imageExtent = {w, h, 1}};
    cmd.copyImageToBuffer(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                          vk::Buffer(static_cast<VkBuffer>(g_gen->FinalBuffer(g_slot))), region);
    g_frame.final = true;
}

Ticket Kick() {
    Ticket t;
    const Frame f = g_frame;
    g_frame = Frame{};
    if (!g_on || !g_gen || !g_gen->Ready() || !f.final) return t;
    const int cur = g_slot;
    g_haveFinal[cur] = true;
    g_hudValid[cur] = f.hud;
    if (g_haveFinal[cur ^ 1]) {
        fggpu::FrameFlags flags;
        flags.cur = cur;
        flags.hudPrev = g_hudValid[cur ^ 1];
        flags.hudCur = f.hud;
        flags.vel = f.vel;
        t.id = ++g_kicks;
        t.slot = int(t.id % fggpu::WarpGpu::kReadSlots);
        g_readId[t.slot].store(t.id, std::memory_order_release);
        auto* sched = internal::GetScheduler();
        sched->EndRendering();
        // A command buffer of its own: the readback's Finish ended the frame's, and this one is
        // submitted at once, before the game records its next frame.
        g_gen->Record(static_cast<VkCommandBuffer>(sched->CommandBuffer()), flags, t.slot);
        t.tick = sched->CurrentTick();
        sched->Flush();
    }
    g_slot = cur ^ 1;
    return t;
}

Fetched Fetch(const Ticket& t, std::vector<uint8_t>& out) {
    Fetched r;
    if (!t.id || !g_gen) return r;
    const auto t0 = std::chrono::steady_clock::now();
    internal::GetScheduler()->GetWorkSemaphore()->Wait(t.tick);
    r.waitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::lock_guard lk(g_genMutex);
    if (g_readId[t.slot].load(std::memory_order_acquire) != t.id) return r;
    const uint8_t* src = g_gen->Output(t.slot);
    const size_t bytes = size_t(g_gen->FrameBytes());
    out.resize(bytes);
    std::memcpy(out.data(), src, bytes);
    const fggpu::Result res = g_gen->ReadResult(t.slot);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (g_readId[t.slot].load(std::memory_order_acquire) != t.id) return r;  // reused while copying
    r.ok = true;
    r.declined = res.declined;
    r.usedVectors = res.usedVectors;
    r.usedHudless = res.usedHudless;
    r.gpuMs = res.gpuMs;
    r.covered = res.covered;
    r.saturated = res.saturated;
    r.total = res.total;
    r.pixText = res.pixText;
    return r;
}

}  // namespace gpu::fglive

// ---- Offline runner (fgeval, framegentest): its own headless device ----------------------------
namespace gpu::fggpu {

struct OfflineRunner::Impl {
    Frontend::WindowSDL window;
    std::unique_ptr<Vulkan::Instance> instance;
    vk::CommandPool pool{};
    vk::CommandBuffer cmd{};
    vk::Fence fence{};
    std::unique_ptr<WarpGpu> gen;
    int slot = 0;
};

OfflineRunner::OfflineRunner(const video::framegen::WarpParams& p) : impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    m.instance = std::make_unique<Vulkan::Instance>(m.window, -1, false, false);
    const vk::Device dev = m.instance->GetDevice();
    auto [r1, pool] = dev.createCommandPool(vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m.instance->GetGraphicsQueueFamilyIndex()});
    if (r1 != vk::Result::eSuccess) return;
    m.pool = pool;
    auto [r2, cmds] = dev.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{.commandPool = m.pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    if (r2 != vk::Result::eSuccess) return;
    m.cmd = cmds[0];
    auto [r3, fence] = dev.createFence(vk::FenceCreateInfo{});
    if (r3 != vk::Result::eSuccess) return;
    m.fence = fence;
    Device d;
    d.device = static_cast<VkDevice>(dev);
    d.physicalDevice = static_cast<VkPhysicalDevice>(m.instance->GetPhysicalDevice());
    d.allocator = m.instance->GetAllocator();
    d.queueFamily = m.instance->GetGraphicsQueueFamilyIndex();
    m.gen = std::make_unique<WarpGpu>(d, p);
}

OfflineRunner::~OfflineRunner() {
    Impl& m = *impl_;
    if (!m.instance) return;
    const vk::Device dev = m.instance->GetDevice();
    (void)dev.waitIdle();
    m.gen.reset();
    if (m.fence) dev.destroyFence(m.fence);
    if (m.pool) dev.destroyCommandPool(m.pool);
}

bool OfflineRunner::Ok() const { return impl_->gen != nullptr; }

bool OfflineRunner::Run(const OfflineInputs& in, std::vector<uint8_t>& out, Result& res) {
    Impl& m = *impl_;
    if (!m.gen || !in.prev || !in.cur) return false;
    if (!m.gen->Resize(in.width, in.height)) return false;
    const vk::Device dev = m.instance->GetDevice();
    (void)m.cmd.reset();
    (void)m.cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    const int cur = 1;
    m.gen->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), m.gen->FinalBuffer(cur ^ 1), in.prev, 0);
    m.gen->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), m.gen->FinalBuffer(cur), in.cur, 1);
    const bool hud = in.hudPrev && in.hudCur;
    if (hud) {
        m.gen->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), m.gen->HudBuffer(cur ^ 1), in.hudPrev, 2);
        m.gen->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), m.gen->HudBuffer(cur), in.hudCur, 3);
    }
    if (in.vel) m.gen->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), m.gen->VelBuffer(), in.vel, 4);
    FrameFlags f;
    f.cur = cur;
    f.hudPrev = f.hudCur = hud;
    f.vel = in.vel != nullptr;
    f.t = in.t;
    f.motionScale = in.motionScale;
    m.gen->Record(static_cast<VkCommandBuffer>(m.cmd), f, m.slot);
    (void)m.cmd.end();
    (void)dev.resetFences(m.fence);
    const vk::SubmitInfo si{.commandBufferCount = 1, .pCommandBuffers = &m.cmd};
    if (m.instance->GetGraphicsQueue().submit(si, m.fence) != vk::Result::eSuccess) return false;
    if (dev.waitForFences(m.fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) return false;
    const uint8_t* src = m.gen->Output(m.slot);
    out.assign(src, src + m.gen->FrameBytes());
    res = m.gen->ReadResult(m.slot);
    m.slot = (m.slot + 1) % WarpGpu::kReadSlots;
    return true;
}

}  // namespace gpu::fggpu
