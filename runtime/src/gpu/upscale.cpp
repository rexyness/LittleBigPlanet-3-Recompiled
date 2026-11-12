#include "gpu/upscale.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#include "gpu/upscale_gpu.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

namespace gpu::internal {  // renderer.cpp
Vulkan::Instance* GetInstance();
Vulkan::Scheduler* GetScheduler();
}  // namespace gpu::internal

namespace gpu::upscale {
namespace {

using upgpu::UpscaleGpu;

std::atomic<bool> g_on{false};
float g_sharp = 0.25f;
std::atomic<uint32_t> g_winW{0}, g_winH{0};

std::mutex g_mutex;  // g_up's creation and resizing against Fetch
std::unique_ptr<UpscaleGpu> g_up;
std::atomic<uint64_t> g_readId[UpscaleGpu::kKinds][UpscaleGpu::kSlots];

// Command-processor thread state.
Debounce g_debounce;
Size g_frame;              // the last frame read back
Size g_target;             // what the upscaler is set up for ({} = not upscaling)
bool g_active = false;     // set up, and the window is bigger than the frame
bool g_inputCopied = false;  // this frame's display image went into the input buffer
uint64_t g_ids = 0;
uint64_t g_lastTick[UpscaleGpu::kKinds] = {0, 0};
int g_nextSlot[UpscaleGpu::kKinds] = {0, 0};
bool g_loggedTarget = false;
Size g_loggedWindow;
Size g_failedTarget;  // a set-up that failed: not retried until the target changes

struct Stats {
    uint64_t submitted[UpscaleGpu::kKinds]{}, fetched[UpscaleGpu::kKinds]{}, failed = 0;
    double gpuSum[UpscaleGpu::kKinds]{}, gpuMax[UpscaleGpu::kKinds]{};
    uint64_t gpuN[UpscaleGpu::kKinds]{};
    double waitSum = 0, waitMax = 0, copySum = 0, copyMax = 0;
    uint64_t copyN = 0;
};
std::mutex g_statsMutex;
Stats g_stats;

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

void WaitTick(uint64_t tick) {
    if (tick) internal::GetScheduler()->GetWorkSemaphore()->Wait(tick);
}

video::UpscaleTicket NewTicket(int kind) {
    video::UpscaleTicket t;
    t.id = ++g_ids;
    t.kind = kind;
    t.slot = g_nextSlot[kind];
    g_nextSlot[kind] = (g_nextSlot[kind] + 1) % UpscaleGpu::kSlots;
    t.width = g_up->OutW();
    t.height = g_up->OutH();
    g_readId[kind][t.slot].store(t.id, std::memory_order_release);
    return t;
}

}  // namespace

void ConfigureFromEnv(bool allowed) {
    const char* v = std::getenv("LBP3_UPSCALE");
    const bool want = v && std::strcmp(v, "fsr1") == 0;
    if (const char* s = std::getenv("LBP3_UPSCALE_SHARP"); s && *s) g_sharp = std::clamp(float(std::atof(s)), 0.0f, 4.0f);
    if (want && !allowed) {
        std::fprintf(stderr, "[upscale] off: frame generation's generator runs on the CPU (needs LBP3_FRAMEGEN_MODE=warp on the GPU)\n");
        return;
    }
    if (!want) {
        if (v && *v && std::strcmp(v, "off") != 0) std::fprintf(stderr, "[upscale] unknown LBP3_UPSCALE=%s; off\n", v);
        return;
    }
    g_on = true;
    std::fprintf(stderr, "[upscale] fsr1 on (RCAS sharpness %.2f stops): frames are upscaled to the window when it is bigger\n",
                 g_sharp);
}

bool Enabled() { return g_on.load(); }
float Sharpness() { return g_sharp; }

void SetWindowPixels(uint32_t width, uint32_t height) {
    g_winW.store(width, std::memory_order_relaxed);
    g_winH.store(height, std::memory_order_relaxed);
}

void OnReadbackCopy(void* cmdp, void* imagep) {
    if (!g_on) return;
    auto& image = *static_cast<VideoCore::Image*>(imagep);
    const uint32_t w = image.info.size.width, h = image.info.size.height;
    g_frame = {w, h};
    g_inputCopied = false;
    if (!g_active || !g_up || g_up->InW() != w || g_up->InH() != h) return;
    if (!FourByteColour(image.info.pixel_format)) return;
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    // The previous upscale (an earlier submission) reads the input buffer: copy after it.
    const vk::MemoryBarrier2 war{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader |
                                                 vk::PipelineStageFlagBits2::eTransfer,
                                 .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &war});
    const vk::BufferImageCopy region{.bufferOffset = 0,
                                     .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                     .imageExtent = {w, h, 1}};
    cmd.copyImageToBuffer(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                          vk::Buffer(static_cast<VkBuffer>(g_up->InputBuffer())), region);
    g_inputCopied = true;
}

void BeginFrame() {
    if (!g_on) return;
    const Size window{g_winW.load(std::memory_order_relaxed), g_winH.load(std::memory_order_relaxed)};
    const Size target = g_debounce.Update(TargetFor(g_frame, window));
    if (target.Empty()) {
        if (g_active || !g_loggedTarget || window != g_loggedWindow) {
            std::fprintf(stderr, "[upscale] not upscaling: the window (%ux%u) isn't bigger than the frame (%ux%u)\n",
                         window.w, window.h, g_frame.w, g_frame.h);
            g_loggedTarget = true;
            g_loggedWindow = window;
        }
        g_active = false;
        g_target = {};
        return;
    }
    if (g_active && target == g_target && g_up && g_up->InW() == g_frame.w && g_up->InH() == g_frame.h) return;
    if (!g_active && target == g_failedTarget) return;
    // (Re)set up: wait for the upscaler's work in flight first, as its buffers are about to go.
    WaitTick(g_lastTick[0]);
    WaitTick(g_lastTick[1]);
    {
        std::lock_guard lk(g_mutex);
        for (auto& kind : g_readId)
            for (auto& id : kind) id.store(0, std::memory_order_release);
        if (!g_up) {
            auto* inst = internal::GetInstance();
            fggpu::Device d;
            d.device = static_cast<VkDevice>(inst->GetDevice());
            d.physicalDevice = static_cast<VkPhysicalDevice>(inst->GetPhysicalDevice());
            d.allocator = inst->GetAllocator();
            d.queueFamily = inst->GetGraphicsQueueFamilyIndex();
            g_up = std::make_unique<UpscaleGpu>(d);
        }
        g_active = g_up->Resize(g_frame.w, g_frame.h, target.w, target.h);
    }
    g_inputCopied = false;  // copied into the old buffers, if at all
    g_target = g_active ? target : Size{};
    g_failedTarget = g_active ? Size{} : target;
    g_loggedTarget = true;
    g_loggedWindow = window;
    if (g_active)
        std::fprintf(stderr, "[upscale] fsr1 %ux%u -> %ux%u (the window)\n", g_frame.w, g_frame.h, target.w, target.h);
    else
        std::fprintf(stderr, "[upscale] could not set up %ux%u -> %ux%u; not upscaling\n", g_frame.w, g_frame.h, target.w,
                     target.h);
}

video::UpscaleTicket RecordGen(void* cmd, void* src, uint32_t width, uint32_t height) {
    if (!g_on || !g_active || !g_up || g_up->InW() != width || g_up->InH() != height) return {};
    video::UpscaleTicket t = NewTicket(1);
    g_up->Record(cmd, src, 1, t.slot, g_sharp);
    std::lock_guard lk(g_statsMutex);
    ++g_stats.submitted[1];
    return t;
}

void SetGenTick(video::UpscaleTicket& t, uint64_t tick) {
    if (!t.id) return;
    t.tick = tick;
    g_lastTick[1] = tick;
}

video::UpscaleTicket KickReal() {
    if (!g_on || !g_active || !g_inputCopied || !g_up) {
        g_inputCopied = false;
        return {};
    }
    g_inputCopied = false;
    video::UpscaleTicket t = NewTicket(0);
    auto* sched = internal::GetScheduler();
    sched->EndRendering();
    // A command buffer of its own: the readback's Finish ended the frame's, and this one is
    // submitted at once, before the game records its next frame.
    g_up->Record(static_cast<VkCommandBuffer>(sched->CommandBuffer()), nullptr, 0, t.slot, g_sharp);
    t.tick = sched->CurrentTick();
    sched->Flush();
    g_lastTick[0] = t.tick;
    std::lock_guard lk(g_statsMutex);
    ++g_stats.submitted[0];
    return t;
}

bool Fetch(const video::UpscaleTicket& t, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
    if (!t.id || !g_up) return false;
    const auto t0 = std::chrono::steady_clock::now();
    WaitTick(t.tick);
    const auto t1 = std::chrono::steady_clock::now();
    bool ok = false;
    double gpuMs = -1;
    {
        std::lock_guard lk(g_mutex);
        auto& id = g_readId[t.kind][t.slot];
        if (id.load(std::memory_order_acquire) == t.id && g_up->OutW() == t.width && g_up->OutH() == t.height) {
            const uint8_t* src = g_up->Output(t.kind, t.slot);
            const size_t bytes = size_t(t.width) * t.height * 4;
            if (src) {
                out.resize(bytes);
                std::memcpy(out.data(), src, bytes);
                gpuMs = g_up->GpuMs(t.kind, t.slot);
                std::atomic_thread_fence(std::memory_order_acquire);
                ok = id.load(std::memory_order_acquire) == t.id;  // not reused while copying
            }
        }
    }
    const auto t2 = std::chrono::steady_clock::now();
    const double waitMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double copyMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::lock_guard lk(g_statsMutex);
    if (!ok) {
        ++g_stats.failed;
        return false;
    }
    ++g_stats.fetched[t.kind];
    if (gpuMs >= 0) {
        g_stats.gpuSum[t.kind] += gpuMs;
        g_stats.gpuMax[t.kind] = std::max(g_stats.gpuMax[t.kind], gpuMs);
        ++g_stats.gpuN[t.kind];
    }
    g_stats.waitSum += waitMs;
    g_stats.waitMax = std::max(g_stats.waitMax, waitMs);
    g_stats.copySum += copyMs;
    g_stats.copyMax = std::max(g_stats.copyMax, copyMs);
    ++g_stats.copyN;
    width = t.width;
    height = t.height;
    return true;
}

void PrintStatus() {
    if (!g_on) return;
    Stats s;
    {
        std::lock_guard lk(g_statsMutex);
        s = g_stats;
        g_stats = Stats{};
    }
    if (!s.submitted[0] && !s.submitted[1] && !s.fetched[0] && !s.fetched[1] && !s.failed) return;
    auto avg = [](double sum, uint64_t n) { return n ? sum / double(n) : 0.0; };
    std::fprintf(stderr,
                 "[upscale] fsr1 -> %ux%u: %llu real + %llu generated submitted, %llu + %llu shown upscaled, %llu not; "
                 "gpu %.2f/%.2f ms real, %.2f/%.2f generated (avg/max); fetch wait %.2f/%.2f, copy %.2f/%.2f ms\n",
                 g_target.w, g_target.h, static_cast<unsigned long long>(s.submitted[0]),
                 static_cast<unsigned long long>(s.submitted[1]), static_cast<unsigned long long>(s.fetched[0]),
                 static_cast<unsigned long long>(s.fetched[1]), static_cast<unsigned long long>(s.failed),
                 avg(s.gpuSum[0], s.gpuN[0]), s.gpuMax[0], avg(s.gpuSum[1], s.gpuN[1]), s.gpuMax[1],
                 avg(s.waitSum, s.copyN), s.waitMax, avg(s.copySum, s.copyN), s.copyMax);
}

}  // namespace gpu::upscale

// ---- Offline (upscaletest, upeval): its own headless device -------------------------------------
#include "gpu/upscale_offline.h"
#include "sdl_window.h"

namespace gpu::upgpu {

struct OfflineUpscaler::Impl {
    Frontend::WindowSDL window;
    std::unique_ptr<Vulkan::Instance> instance;
    vk::CommandPool pool{};
    vk::CommandBuffer cmd{};
    vk::Fence fence{};
    std::unique_ptr<UpscaleGpu> up;
    int slot = 0;
};

OfflineUpscaler::OfflineUpscaler() : impl_(std::make_unique<Impl>()) {
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
    fggpu::Device d;
    d.device = static_cast<VkDevice>(dev);
    d.physicalDevice = static_cast<VkPhysicalDevice>(m.instance->GetPhysicalDevice());
    d.allocator = m.instance->GetAllocator();
    d.queueFamily = m.instance->GetGraphicsQueueFamilyIndex();
    m.up = std::make_unique<UpscaleGpu>(d);
    if (!m.up->Ok()) m.up.reset();
}

OfflineUpscaler::~OfflineUpscaler() {
    Impl& m = *impl_;
    if (!m.instance) return;
    const vk::Device dev = m.instance->GetDevice();
    (void)dev.waitIdle();
    m.up.reset();
    if (m.fence) dev.destroyFence(m.fence);
    if (m.pool) dev.destroyCommandPool(m.pool);
}

bool OfflineUpscaler::Ok() const { return impl_->up != nullptr; }

bool OfflineUpscaler::Run(const void* in, uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, float sharpness,
                          std::vector<uint8_t>& out, double& gpuMs) {
    Impl& m = *impl_;
    gpuMs = -1;
    if (!m.up || !in) return false;
    if (!m.up->Resize(inW, inH, outW, outH)) return false;
    const vk::Device dev = m.instance->GetDevice();
    (void)m.cmd.reset();
    (void)m.cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    m.up->RecordUpload(static_cast<VkCommandBuffer>(m.cmd), in);
    m.up->Record(static_cast<VkCommandBuffer>(m.cmd), nullptr, 0, m.slot, sharpness);
    (void)m.cmd.end();
    (void)dev.resetFences(m.fence);
    const vk::SubmitInfo si{.commandBufferCount = 1, .pCommandBuffers = &m.cmd};
    if (m.instance->GetGraphicsQueue().submit(si, m.fence) != vk::Result::eSuccess) return false;
    if (dev.waitForFences(m.fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) return false;
    const uint8_t* src = m.up->Output(0, m.slot);
    if (!src) return false;
    out.assign(src, src + m.up->OutBytes());
    gpuMs = m.up->GpuMs(0, m.slot);
    m.slot = (m.slot + 1) % UpscaleGpu::kSlots;
    return true;
}

}  // namespace gpu::upgpu
