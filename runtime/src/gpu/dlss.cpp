// The DLSS upscaler (gpu/dlss.h). Compiled only in a DLSS build (LBP3_DLSS): it links NVIDIA's
// NGX library from the DLSS SDK, which never enters this repository (docs/adr/0002).
//
// Stage 1 of issue #24: DLAA (render size = output size) on the HUD-less image, with the game's
// depth and its 8-bit vectors, no jitter yet; the screen HUD is put back from the finished frame.
#include "gpu/dlss.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "common/lbp3_res.h"
#include "gpu/dlss_shaders/dlss_compose_comp.h"
#include "gpu/dlss_shaders/dlss_mvdecode_comp.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

#include <vk_mem_alloc.h>

#include <nvsdk_ngx_helpers_vk.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace gpu::internal {  // renderer.cpp
Vulkan::Instance* GetInstance();
Vulkan::Scheduler* GetScheduler();
Vulkan::Rasterizer* GetRasterizer();
AmdGpu::Liverpool* GetLiverpool();
}  // namespace gpu::internal

namespace gpu::dlss {
namespace {

constexpr uint32_t kVelocityClear = 0x007f7f00u;
// The port's own NGX project id (NVIDIA's guide: a custom engine names itself with any GUID).
constexpr char kProjectId[] = "4f6b1c2e-8a3d-4e57-9b10-c3d2a1e5f708";
constexpr char kEngineVersion[] = "lbp3port";
// The game's vectors, pixels per step at 1920x1080 (video/framegen_warp.h, perf/fg_inputs.md).
constexpr float kVecScaleX = 0.941f, kVecScaleY = 0.529f;
constexpr float kHudT = 8.0f;  // as frame generation's HUD put-back

bool g_wanted = false;  // LBP3_UPSCALER=dlss
bool g_ready = false;   // NGX started and DLSS available
std::wstring g_dataPath;
std::wstring g_exeDir;
const wchar_t* g_paths[1] = {nullptr};
NVSDK_NGX_FeatureCommonInfo g_common{};
NVSDK_NGX_Parameter* g_caps = nullptr;
NVSDK_NGX_Handle* g_feature = nullptr;
uint32_t g_featW = 0, g_featH = 0, g_featOutW = 0, g_featOutH = 0;
bool g_reset = true;
std::array<std::atomic<uint64_t>, 16> g_display{};

bool EnvOn(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '1';
}

void NVSDK_CONV NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    const size_t n = message ? std::strlen(message) : 0;
    std::fprintf(stderr, "[dlss] ngx: %s%s", message ? message : "", n && message[n - 1] == '\n' ? "" : "\n");
}

void InitCommon() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    g_exeDir.assign(buf, n);
    if (const size_t slash = g_exeDir.find_last_of(L"\\/"); slash != std::wstring::npos) g_exeDir.resize(slash);
    g_paths[0] = g_exeDir.c_str();
    // NGX's own files (its logs, when on) go to user\dlss under the game root (the working folder).
    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(L"user\\dlss", MAX_PATH, full, nullptr)) g_dataPath = full;
    else g_dataPath = g_exeDir;
    CreateDirectoryW(g_dataPath.c_str(), nullptr);
    g_common.PathListInfo.Path = g_paths;
    g_common.PathListInfo.Length = 1;
    g_common.LoggingInfo.LoggingCallback = NgxLog;
    g_common.LoggingInfo.MinimumLoggingLevel =
        EnvOn("LBP3_DLSS_LOG") ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_OFF;
    g_common.LoggingInfo.DisableOtherLoggingSinks = true;
}

NVSDK_NGX_FeatureDiscoveryInfo Discovery() {
    NVSDK_NGX_FeatureDiscoveryInfo d{};
    d.SDKVersion = NVSDK_NGX_Version_API;
    d.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    d.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d.Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    d.ApplicationDataPath = g_dataPath.c_str();
    d.FeatureInfo = &g_common;
    return d;
}

std::vector<std::string> Names(const char* what, NVSDK_NGX_Result r, uint32_t n, const VkExtensionProperties* props) {
    std::vector<std::string> out;
    if (NVSDK_NGX_FAILED(r)) {
        std::fprintf(stderr, "[dlss] NGX's %s extension query failed (0x%08x)\n", what, unsigned(r));
        return out;
    }
    std::string list;
    for (uint32_t i = 0; i < n; ++i) {
        out.emplace_back(props[i].extensionName);
        list += (i ? ", " : "") + out.back();
    }
    std::fprintf(stderr, "[dlss] NGX asks for %u %s extension(s)%s%s\n", n, what, n ? ": " : "", list.c_str());
    return out;
}

std::vector<std::string> InstanceExtensions() {
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery();
    uint32_t n = 0;
    VkExtensionProperties* props = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &n, &props);
    return Names("instance", r, n, props);
}

std::vector<std::string> DeviceExtensions(VkInstance instance, VkPhysicalDevice pd) {
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery();
    uint32_t n = 0;
    VkExtensionProperties* props = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, pd, &d, &n, &props);
    return Names("device", r, n, props);
}

// ---- Our own images ---------------------------------------------------------------------------

struct Img {
    vk::Image image{};
    VmaAllocation alloc{};
    vk::ImageView view{};
    vk::Format format = vk::Format::eUndefined;
    uint32_t w = 0, h = 0;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eTopOfPipe;
    vk::AccessFlags2 access{};
};

Img g_hud, g_final, g_vel8, g_depth, g_mv, g_out, g_comp;
vk::Buffer g_depthBuf{};
VmaAllocation g_depthBufAlloc{};
vk::DeviceSize g_depthBufSize = 0;

vk::Device Dev() { return internal::GetInstance()->GetDevice(); }
VmaAllocator Alloc() { return internal::GetInstance()->GetAllocator(); }

void Free(Img& i) {
    if (i.view) Dev().destroyImageView(i.view);
    if (i.image) vmaDestroyImage(Alloc(), static_cast<VkImage>(i.image), i.alloc);
    i = Img{};
}

bool Make(Img& i, uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage) {
    if (i.image && i.w == w && i.h == h && i.format == fmt) return true;
    if (i.image) {
        (void)Dev().waitIdle();  // a size change: the old image may still be in use
        Free(i);
    }
    const VkImageCreateInfo ci{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = static_cast<VkFormat>(fmt),
                               .extent = {w, h, 1},
                               .mipLevels = 1,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = static_cast<VkImageUsageFlags>(usage),
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    const VmaAllocationCreateInfo ai{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage img{};
    if (vmaCreateImage(Alloc(), &ci, &ai, &img, &i.alloc, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "[dlss] could not allocate a %ux%u %s image\n", w, h, vk::to_string(fmt).c_str());
        return false;
    }
    i.image = img;
    auto [r, view] = Dev().createImageView(vk::ImageViewCreateInfo{
        .image = i.image,
        .viewType = vk::ImageViewType::e2D,
        .format = fmt,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
    if (r != vk::Result::eSuccess) {
        vmaDestroyImage(Alloc(), img, i.alloc);
        i = Img{};
        return false;
    }
    i.view = view;
    i.format = fmt;
    i.w = w;
    i.h = h;
    return true;
}

void To(vk::CommandBuffer cmd, Img& i, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access) {
    const vk::ImageMemoryBarrier2 b{.srcStageMask = i.stage,
                                    .srcAccessMask = i.access,
                                    .dstStageMask = stage,
                                    .dstAccessMask = access,
                                    .oldLayout = i.layout,
                                    .newLayout = layout,
                                    .image = i.image,
                                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
    i.layout = layout;
    i.stage = stage;
    i.access = access;
}

void ToRead(vk::CommandBuffer cmd, Img& i) {
    To(cmd, i, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader,
       vk::AccessFlagBits2::eShaderSampledRead);
}

void ToWrite(vk::CommandBuffer cmd, Img& i) {
    To(cmd, i, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
       vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead);
}

bool FourByte(vk::Format f) {
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

bool Bgra(vk::Format f) { return f == vk::Format::eB8G8R8A8Unorm || f == vk::Format::eB8G8R8A8Srgb; }

// The same bytes read raw: the game's 8-bit images stay in their own encoding (sRGB stays
// perceptual, which is what DLSS expects of a non-HDR input).
vk::Format Unorm(vk::Format f) { return Bgra(f) ? vk::Format::eB8G8R8A8Unorm : vk::Format::eR8G8B8A8Unorm; }

// ---- Pipelines --------------------------------------------------------------------------------

struct Pass {
    vk::DescriptorSetLayout setLayout{};
    vk::PipelineLayout layout{};
    vk::Pipeline pipe{};
};
Pass g_mvPass, g_composePass;
vk::Sampler g_nearest{}, g_linear{};

struct PC {
    float a, b;
    uint32_t w, h;
};

bool MakePass(Pass& p, std::span<const uint32_t> code, uint32_t samplers) {
    std::array<vk::DescriptorSetLayoutBinding, 4> b{};
    for (uint32_t i = 0; i <= samplers; ++i)
        b[i] = vk::DescriptorSetLayoutBinding{.binding = i,
                                              .descriptorType = i < samplers ? vk::DescriptorType::eCombinedImageSampler
                                                                             : vk::DescriptorType::eStorageImage,
                                              .descriptorCount = 1,
                                              .stageFlags = vk::ShaderStageFlagBits::eCompute};
    auto [r1, sl] = Dev().createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR, .bindingCount = samplers + 1, .pBindings = b.data()});
    if (r1 != vk::Result::eSuccess) return false;
    p.setLayout = sl;
    const vk::PushConstantRange range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(PC)};
    auto [r2, pl] = Dev().createPipelineLayout(vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1, .pSetLayouts = &p.setLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range});
    if (r2 != vk::Result::eSuccess) return false;
    p.layout = pl;
    const vk::ShaderModule m = Vulkan::CompileSPV(code, Dev());
    if (!m) return false;
    auto [r3, pipe] = Dev().createComputePipeline(
        {}, vk::ComputePipelineCreateInfo{.stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = m, .pName = "main"},
                                          .layout = p.layout});
    Dev().destroyShaderModule(m);
    if (r3 != vk::Result::eSuccess) return false;
    p.pipe = pipe;
    return true;
}

bool MakePipelines() {
    auto [r1, nearest] = Dev().createSampler(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge, .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge});
    auto [r2, linear] = Dev().createSampler(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear, .minFilter = vk::Filter::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge, .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge});
    if (r1 != vk::Result::eSuccess || r2 != vk::Result::eSuccess) return false;
    g_nearest = nearest;
    g_linear = linear;
    return MakePass(g_mvPass, std::span<const uint32_t>(DLSS_MVDECODE_COMP), 1) &&
           MakePass(g_composePass, std::span<const uint32_t>(DLSS_COMPOSE_COMP), 3);
}

uint32_t DivUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Binds pass p, its images (samplers first, the storage image last) and constants, and dispatches
// one thread per pixel of w x h.
void Run(vk::CommandBuffer cmd, const Pass& p, std::initializer_list<std::pair<const Img*, vk::Sampler>> reads,
         const Img& write, const PC& pc) {
    std::array<vk::DescriptorImageInfo, 4> info{};
    std::array<vk::WriteDescriptorSet, 4> w{};
    uint32_t n = 0;
    for (const auto& [img, sampler] : reads) {
        info[n] = vk::DescriptorImageInfo{.sampler = sampler, .imageView = img->view,
                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
        w[n] = vk::WriteDescriptorSet{.dstBinding = n, .descriptorCount = 1,
                                      .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &info[n]};
        ++n;
    }
    info[n] = vk::DescriptorImageInfo{.imageView = write.view, .imageLayout = vk::ImageLayout::eGeneral};
    w[n] = vk::WriteDescriptorSet{.dstBinding = n, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
                                  .pImageInfo = &info[n]};
    ++n;
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, p.pipe);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, p.layout, 0,
                             vk::ArrayProxy<const vk::WriteDescriptorSet>(n, w.data()));
    cmd.pushConstants(p.layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PC), &pc);
    cmd.dispatch(DivUp(pc.w, 8), DivUp(pc.h, 8), 1);
}

// ---- Capturing the game's images as they go by (command-processor thread) --------------------

struct Frame {
    bool composite = false, hud = false, vel = false, depth = false;
} g_frame;
std::map<uint64_t, uint32_t> g_zCount;  // depth address -> depth-tested draws this frame
uint64_t g_velAddr = 0;
bool g_hooked = false;
void (*g_prevAfterDraw)(u64, u64, u32) = nullptr;
void (*g_prevOnFill)(u64, u64, u32, u32, bool) = nullptr;

struct Stats {
    uint32_t flips = 0, upscaled = 0, noHud = 0, noVel = 0, noDepth = 0, failed = 0;
} g_stats;

bool IsDisplay(uint64_t addr) {
    if (!addr) return false;
    for (const auto& a : g_display)
        if (a.load(std::memory_order_relaxed) == addr) return true;
    return false;
}

// The newest image at addr with the wanted usage (several alias a pooled address).
VideoCore::Image* FindTarget(uint64_t addr, bool depth) {
    auto* rast = internal::GetRasterizer();
    if (!addr || !rast) return nullptr;
    VideoCore::Image* best = nullptr;
    rast->GetTextureCache().ForEachImageWithAddress(addr, [&](VideoCore::ImageId, VideoCore::Image& image) {
        if (depth ? !image.usage.depth_target : !image.usage.render_target) return;
        if (image.info.size.width < 640 || image.info.size.height < 360) return;
        if (!best || image.tick_accessed_last > best->tick_accessed_last) best = &image;
    });
    return best;
}

uint64_t BestDepth() {
    uint64_t best = 0;
    uint32_t n = 0;
    for (const auto& [addr, count] : g_zCount)
        if (count > n) n = count, best = addr;
    return best;
}

vk::CommandBuffer GameCmd(VideoCore::Image& src) {
    auto* sched = internal::GetScheduler();
    sched->EndRendering();
    const auto cmd = sched->CommandBuffer();
    VideoCore::Image::Barriers barriers;
    src.GetBarriers(barriers, vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eTransfer);
    if (!barriers.empty())
        cmd.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
                                                .pImageMemoryBarriers = barriers.data()});
    return cmd;
}

// Copies a game colour image into ours. The game's image is only read (a transfer source).
bool Capture(VideoCore::Image& src, Img& dst) {
    const vk::Format fmt = src.info.pixel_format;
    if (!FourByte(fmt)) return false;
    const uint32_t w = src.info.size.width, h = src.info.size.height;
    if (!Make(dst, w, h, Unorm(fmt), vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)) return false;
    const auto cmd = GameCmd(src);
    To(cmd, dst, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferWrite);
    cmd.copyImage(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.image, vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .extent = {w, h, 1}});
    return true;
}

// Copies the scene depth's depth aspect into our R32F image, through a buffer (a depth image
// can't be copied into a colour image directly).
bool CaptureDepth(VideoCore::Image& src) {
    const vk::Format fmt = src.info.pixel_format;
    if (fmt != vk::Format::eD32Sfloat && fmt != vk::Format::eD32SfloatS8Uint) return false;
    const uint32_t w = src.info.size.width, h = src.info.size.height;
    const vk::DeviceSize bytes = vk::DeviceSize{w} * h * 4;
    if (g_depthBufSize < bytes) {
        if (g_depthBuf) {
            (void)Dev().waitIdle();
            vmaDestroyBuffer(Alloc(), static_cast<VkBuffer>(g_depthBuf), g_depthBufAlloc);
            g_depthBuf = vk::Buffer{};
            g_depthBufSize = 0;
        }
        const VkBufferCreateInfo bi{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
                                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        const VmaAllocationCreateInfo ai{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VkBuffer buf{};
        if (vmaCreateBuffer(Alloc(), &bi, &ai, &buf, &g_depthBufAlloc, nullptr) != VK_SUCCESS) return false;
        g_depthBuf = buf;
        g_depthBufSize = bytes;
    }
    if (!Make(g_depth, w, h, vk::Format::eR32Sfloat, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled))
        return false;
    const auto cmd = GameCmd(src);
    // The previous frame's copy out of the buffer (an earlier submission) must be done first.
    const vk::MemoryBarrier2 war{.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
                                 .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &war});
    cmd.copyImageToBuffer(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, g_depthBuf,
                          vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
                                              .imageExtent = {w, h, 1}});
    const vk::MemoryBarrier2 raw{.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                 .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                 .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &raw});
    To(cmd, g_depth, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferWrite);
    cmd.copyBufferToImage(g_depthBuf, g_depth.image, vk::ImageLayout::eTransferDstOptimal,
                          vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                              .imageExtent = {w, h, 1}});
    return true;
}

void AfterDraw(u64 vs, u64 ps, u32 n) {
    if (g_prevAfterDraw) g_prevAfterDraw(vs, ps, n);
    const auto& regs = internal::GetLiverpool()->regs;
    const auto& cb0 = regs.color_buffers[0];
    const uint64_t rt0 = cb0 ? cb0.Address() : 0;
    const auto& db = regs.depth_buffer;
    const Lbp3Res::Size rs = Lbp3Res::Current();
    if (rt0 && rt0 != g_velAddr && db.DepthValid() && regs.depth_control.depth_enable && db.Pitch() >= rs.w &&
        db.Height() >= rs.h)
        ++g_zCount[db.DepthAddress()];
    if (g_frame.composite || ps == 0 || !IsDisplay(rt0)) return;
    // The final composite: the first draw with a pixel shader into a display buffer. The screen
    // HUD comes after it, so the display image now is the HUD-less image.
    g_frame.composite = true;
    if (VideoCore::Image* image = FindTarget(rt0, false)) g_frame.hud = Capture(*image, g_hud);
}

void OnFill(u64 dst, u64 bytes, u32 value, u32 mask, bool full) {
    if (g_prevOnFill) g_prevOnFill(dst, bytes, value, mask, full);
    const Lbp3Res::Size rs = Lbp3Res::Current();
    if (value != kVelocityClear || g_frame.vel || bytes < uint64_t{rs.w} * rs.h * 4) return;
    // The game clears its velocity target after its motion blur used it, before the composite:
    // the vectors and the scene depth are copied just before the clear.
    g_velAddr = dst;
    if (VideoCore::Image* image = FindTarget(dst, false)) g_frame.vel = Capture(*image, g_vel8);
    if (VideoCore::Image* depth = FindTarget(BestDepth(), true)) g_frame.depth = CaptureDepth(*depth);
}

void Hook() {
    // Chained after whatever holds the hooks already (frame generation, the capture tool).
    g_prevAfterDraw = Vulkan::g_lbp3FgAfterDraw;
    g_prevOnFill = Vulkan::g_lbp3FgOnFill;
    Vulkan::g_lbp3FgAfterDraw = AfterDraw;
    Vulkan::g_lbp3FgOnFill = OnFill;
    g_hooked = true;
}

void Log() {
    if (++g_stats.flips < 300) return;
    std::fprintf(stderr,
                 "[dlss] DLAA %ux%u: %u of %u flips upscaled; not: %u without the HUD-less image, %u without "
                 "vectors, %u without depth, %u failed\n",
                 g_featOutW, g_featOutH, g_stats.upscaled, g_stats.flips, g_stats.noHud, g_stats.noVel,
                 g_stats.noDepth, g_stats.failed);
    g_stats = Stats{};
}

}  // namespace

void RegisterVulkanExtensions() {
    const char* v = std::getenv("LBP3_UPSCALER");
    g_wanted = v && _stricmp(v, "dlss") == 0;
    if (!g_wanted) return;
    InitCommon();
    Vulkan::g_lbp3ExtraInstanceExtensions = InstanceExtensions;
    Vulkan::g_lbp3ExtraDeviceExtensions = DeviceExtensions;
}

bool Start() {
    if (!g_wanted || g_ready) return g_ready;
    auto* inst = internal::GetInstance();
    const VkInstance vi = static_cast<VkInstance>(inst->GetInstance());
    const VkPhysicalDevice pd = static_cast<VkPhysicalDevice>(inst->GetPhysicalDevice());
    const VkDevice dev = static_cast<VkDevice>(inst->GetDevice());
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, g_dataPath.c_str(), vi, pd, dev,
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr, VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr, &g_common);
    if (NVSDK_NGX_FAILED(r)) {
        std::fprintf(stderr, "[dlss] NGX did not start (0x%08x): no upscaler this run\n", unsigned(r));
        return false;
    }
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&g_caps);
    int available = 0;
    if (NVSDK_NGX_SUCCEED(r)) NVSDK_NGX_Parameter_GetI(g_caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available) {
        int needsDriver = 0, initResult = 0;
        unsigned major = 0, minor = 0;
        if (g_caps) {
            NVSDK_NGX_Parameter_GetI(g_caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
            NVSDK_NGX_Parameter_GetUI(g_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
            NVSDK_NGX_Parameter_GetUI(g_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
            NVSDK_NGX_Parameter_GetI(g_caps, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
        }
        std::fprintf(stderr, "[dlss] DLSS is not available (caps 0x%08x, init result 0x%08x)%s: no upscaler this run\n",
                     unsigned(r), unsigned(initResult),
                     needsDriver ? (" - it needs a newer driver, " + std::to_string(major) + "." + std::to_string(minor) +
                                    " or later")
                                       .c_str()
                                 : "");
        return false;
    }
    if (!MakePipelines()) {
        std::fprintf(stderr, "[dlss] could not build the upscaler's own passes: no upscaler this run\n");
        return false;
    }
    g_ready = true;
    std::fprintf(stderr, "[dlss] DLSS ready on %s (DLAA); NVIDIA's DLL from %ls\n",
                 std::string(inst->GetModelName()).c_str(), g_exeDir.c_str());
    return true;
}

bool Active() { return g_ready; }

void SetDisplayBuffer(int index, uint64_t address) {
    if (index >= 0 && index < 16) g_display[index] = address;
}

Output Process(void* cmdp, void* imagep) {
    Output out;
    const Frame f = g_frame;
    g_frame = Frame{};
    g_zCount.clear();
    if (!g_ready) return out;
    if (!g_hooked) {
        Hook();  // from the next frame on
        return out;
    }
    Log();
    auto& display = *static_cast<VideoCore::Image*>(imagep);
    const uint32_t W = display.info.size.width, H = display.info.size.height;
    const bool sized = g_hud.w == W && g_hud.h == H && g_vel8.w == W && g_vel8.h == H && g_depth.w == W && g_depth.h == H;
    if (!f.hud || !f.vel || !f.depth || !sized || !FourByte(display.info.pixel_format)) {
        g_stats.noHud += !f.hud;
        g_stats.noVel += f.hud && !f.vel;
        g_stats.noDepth += f.hud && f.vel && !f.depth;
        g_reset = true;  // the history no longer follows
        return out;
    }
    const uint32_t OW = W, OH = H;  // DLAA
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    const vk::Format fmt = Unorm(display.info.pixel_format);
    if (!Make(g_final, W, H, fmt, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_mv, W, H, vk::Format::eR16G16Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_out, OW, OH, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_comp, OW, OH, vk::Format::eR8G8B8A8Unorm,
              vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)) {
        ++g_stats.failed;
        return out;
    }

    if (!g_feature || g_featW != W || g_featH != H || g_featOutW != OW || g_featOutH != OH) {
        if (g_feature) {
            (void)Dev().waitIdle();
            NVSDK_NGX_VULKAN_ReleaseFeature(g_feature);
            g_feature = nullptr;
        }
        NVSDK_NGX_DLSS_Create_Params cp{};
        cp.Feature.InWidth = W;
        cp.Feature.InHeight = H;
        cp.Feature.InTargetWidth = OW;
        cp.Feature.InTargetHeight = OH;
        cp.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
        // 8-bit, perceptually encoded colour (not HDR); vectors at the render size and without
        // jitter; conventional depth (perf/fg_inputs.md); DLSS works out the exposure itself.
        cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        const NVSDK_NGX_Result r =
            NGX_VULKAN_CREATE_DLSS_EXT1(static_cast<VkDevice>(Dev()), static_cast<VkCommandBuffer>(cmd), 1, 1, &g_feature,
                                        g_caps, &cp);
        if (NVSDK_NGX_FAILED(r) || !g_feature) {
            std::fprintf(stderr, "[dlss] could not create DLSS for %ux%u -> %ux%u (0x%08x): no upscaler this run\n", W, H,
                         OW, OH, unsigned(r));
            g_feature = nullptr;
            g_ready = false;
            return out;
        }
        g_featW = W, g_featH = H, g_featOutW = OW, g_featOutH = OH;
        g_reset = true;
        std::fprintf(stderr, "[dlss] DLSS created: %ux%u -> %ux%u (DLAA), display format %s\n", W, H, OW, OH,
                     vk::to_string(display.info.pixel_format).c_str());
        if (Bgra(display.info.pixel_format))
            std::fprintf(stderr, "[dlss] the display buffer is BGRA: the HUD put-back writes RGBA, colours swap\n");
    }

    // The finished frame (the readback already made the display image a transfer source).
    To(cmd, g_final, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferWrite);
    cmd.copyImage(display.GetImage(), vk::ImageLayout::eTransferSrcOptimal, g_final.image,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .extent = {W, H, 1}});

    // The game's vectors -> DLSS's.
    ToRead(cmd, g_vel8);
    ToWrite(cmd, g_mv);
    Run(cmd, g_mvPass, {{&g_vel8, g_nearest}}, g_mv,
        PC{-kVecScaleX * float(W) / 1920.0f, -kVecScaleY * float(H) / 1080.0f, W, H});

    // DLSS.
    ToRead(cmd, g_mv);
    ToRead(cmd, g_hud);
    ToRead(cmd, g_depth);
    ToWrite(cmd, g_out);
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    NVSDK_NGX_Resource_VK color = NVSDK_NGX_Create_ImageView_Resource_VK(
        g_hud.view, g_hud.image, range, static_cast<VkFormat>(g_hud.format), W, H, false);
    NVSDK_NGX_Resource_VK depth = NVSDK_NGX_Create_ImageView_Resource_VK(
        g_depth.view, g_depth.image, range, static_cast<VkFormat>(g_depth.format), W, H, false);
    NVSDK_NGX_Resource_VK mv = NVSDK_NGX_Create_ImageView_Resource_VK(
        g_mv.view, g_mv.image, range, static_cast<VkFormat>(g_mv.format), W, H, false);
    NVSDK_NGX_Resource_VK output = NVSDK_NGX_Create_ImageView_Resource_VK(
        g_out.view, g_out.image, range, static_cast<VkFormat>(g_out.format), OW, OH, true);
    NVSDK_NGX_VK_DLSS_Eval_Params ep{};
    ep.Feature.pInColor = &color;
    ep.Feature.pInOutput = &output;
    ep.pInDepth = &depth;
    ep.pInMotionVectors = &mv;
    ep.InJitterOffsetX = 0.0f;
    ep.InJitterOffsetY = 0.0f;
    ep.InRenderSubrectDimensions = {W, H};
    ep.InReset = g_reset ? 1 : 0;
    ep.InMVScaleX = 1.0f;
    ep.InMVScaleY = 1.0f;
    ep.InFrameTimeDeltaInMsec = 1000.0f / 30.0f;
    const NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(static_cast<VkCommandBuffer>(cmd), g_feature, g_caps, &ep);
    if (NVSDK_NGX_FAILED(r)) {
        if (g_stats.failed++ == 0) std::fprintf(stderr, "[dlss] DLSS evaluation failed (0x%08x)\n", unsigned(r));
        g_reset = true;
        return out;
    }
    g_reset = false;

    // The screen HUD back on top, from the finished frame.
    ToRead(cmd, g_out);
    ToRead(cmd, g_final);
    ToWrite(cmd, g_comp);
    Run(cmd, g_composePass, {{&g_final, g_linear}, {&g_hud, g_linear}, {&g_out, g_nearest}}, g_comp,
        PC{kHudT, 0.0f, OW, OH});
    To(cmd, g_comp, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferRead);
    ++g_stats.upscaled;
    out.image = static_cast<VkImage>(g_comp.image);
    out.width = OW;
    out.height = OH;
    return out;
}

}  // namespace gpu::dlss
