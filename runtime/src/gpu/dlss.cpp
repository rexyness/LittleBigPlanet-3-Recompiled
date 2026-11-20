// The DLSS upscaler (gpu/dlss.h). Compiled only in a DLSS build (LBP3_DLSS): it links NVIDIA's
// NGX library from the DLSS SDK, which never enters this repository (docs/adr/0002).
//
// Stage 1 of issue #24: DLAA (render size = output size) on the HUD-less image, with the game's
// depth and its 8-bit vectors, no jitter yet; the screen HUD is put back from the finished frame.
#include "gpu/dlss.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "common/lbp3_res.h"
#include "gpu/dlss_modes.h"
#include "gpu/dlss_shaders/dlss_compose_comp.h"
#include "gpu/dlss_shaders/dlss_mvdecode_comp.h"
#include "gpu/dlss_shaders/dlss_velscale_comp.h"
#include "gpu/fg_live.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

#include <vk_mem_alloc.h>

#include <nvsdk_ngx_helpers.h>
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

// Settings (environment, read with LBP3_UPSCALER). Each piece has its own switch so it can be
// judged on its own.
float g_mvSignX = 1.0f, g_mvSignY = 1.0f;     // LBP3_DLSS_MV: normal | zero | flipx | flipy | flipxy
bool g_jitterOn = true;                        // LBP3_DLSS_JITTER: on | off
float g_jitterSignX = 1.0f, g_jitterSignY = 1.0f;  // LBP3_DLSS_JITTER_SIGN: ++ | +- | -+ | -- (what DLSS is told)
bool g_skipSmaa = true;                        // LBP3_DLSS_SMAA: skip | keep
bool g_trace = false;                          // LBP3_DLSS_TRACE=1
float g_mipBias = 0.0f;                        // LBP3_DLSS_MIPBIAS overrides NVIDIA's (stage 3)
// The game's SMAA writes its result back into the scene buffer with one full-screen draw (PS
// 6d4d5238, perf/fps60_render_re.md #375-380; "copy back" 6d4d5238b13eadc4 in the blA traces).
// Leaving out that draw keeps the scene un-antialiased for DLSS; the passes before it only fill
// SMAA's own targets.
constexpr u64 kSmaaCopyBackPs = 0x6d4d5238b13eadc4ull;

// Jitter: one Halton(2,3) offset per rendered frame, in render pixels within [-0.5, 0.5]
// (NVIDIA's guide 3.7). The frame being drawn uses g_jx/g_jy; the flip evaluates DLSS with the
// offset its frame was drawn with and moves to the next phase.
uint32_t g_phase = 0, g_phases = 16;
float g_jx = 0.0f, g_jy = 0.0f;
bool g_frameJittered = false;   // the frame being drawn is jittered (set by the first jittered draw)
bool g_prevUpscaled = false;    // the last flip was upscaled: jitter and the SMAA skip may run
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

// ---- Modes and the start-up check -------------------------------------------------------------

constexpr NVSDK_NGX_PerfQuality_Value kModeValues[dlss_modes::kCount] = {
    NVSDK_NGX_PerfQuality_Value_DLAA, NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_Balanced,
    NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_UltraPerformance};
int g_mode = 0;                   // index into dlss_modes::kModes (DLAA unless Prepare chose another)
bool g_disabled = false;          // Prepare found DLSS can't run here: the upscaler stays off
uint32_t g_outW = 0, g_outH = 0;  // the output size; 0 = the render size (DLAA)

const char* ModeLabel() { return dlss_modes::kModes[g_mode].label; }

// Whether DLSS can run on the GPU the game will use (shadPS4's choice with no GPU set: a discrete
// GPU first, then the most device-local memory), from a throwaway Vulkan instance: NVIDIA's DLL
// next to lbp3.exe, then NGX's feature query. Needs InitCommon().
bool CanRun(std::string* why, std::string* gpu) {
    const std::wstring dll = g_exeDir + L"\\nvngx_dlss.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        *why = "NVIDIA's DLSS DLL (nvngx_dlss.dll) isn't next to lbp3.exe";
        return false;
    }
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) {
        *why = "no Vulkan driver (vulkan-1.dll)";
        return false;
    }
    const auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
    const auto create = gipa ? reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
    VkInstance instance{};
    const VkApplicationInfo app{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "lbp3 dlss check",
                                .applicationVersion = 1, .pEngineName = kEngineVersion, .engineVersion = 1,
                                .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) {
        *why = "a Vulkan instance couldn't be created";
        return false;
    }
    const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    const auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    const auto memory =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
    uint32_t n = 0;
    enumerate(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    if (n) enumerate(instance, &n, devices.data());
    VkPhysicalDevice best{};
    bool bestDiscrete = false;
    uint64_t bestMem = 0;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties p{};
        props(pd, &p);
        VkPhysicalDeviceMemoryProperties m{};
        memory(pd, &m);
        uint64_t local = 0;
        for (uint32_t i = 0; i < m.memoryHeapCount; ++i)
            if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += m.memoryHeaps[i].size;
        const bool discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (!best || (discrete && !bestDiscrete) || (discrete == bestDiscrete && local > bestMem)) {
            best = pd;
            bestDiscrete = discrete;
            bestMem = local;
            *gpu = p.deviceName;
        }
    }
    if (!best) {
        destroy(instance, nullptr);
        *why = "no GPU";
        return false;
    }
    const NVSDK_NGX_FeatureDiscoveryInfo d = Discovery();
    NVSDK_NGX_FeatureRequirement req{};
    const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, best, &d, &req);
    destroy(instance, nullptr);
    if (NVSDK_NGX_FAILED(r)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "NVIDIA's DLSS check failed on %s (0x%08x)", gpu->c_str(), unsigned(r));
        *why = buf;
        return false;
    }
    if (req.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
        const unsigned f = unsigned(req.FeatureSupported);
        *why = "DLSS isn't supported on " + *gpu + ":";
        if (f & NVSDK_NGX_FeatureSupportResult_AdapterUnsupported) *why += " not an NVIDIA RTX GPU;";
        if (f & NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported) *why += " the driver is too old;";
        if (f & NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported) *why += " Windows is too old;";
        if (f & (NVSDK_NGX_FeatureSupportResult_CheckNotPresent | NVSDK_NGX_FeatureSupportResult_NotImplemented))
            *why += " the driver can't check;";
        why->pop_back();
        return false;
    }
    return true;
}

// CanRun in a child process (lbp3.exe --dlss-probe): the check loads the Vulkan loader, NVIDIA's
// driver and NGX, which take thread-local-storage slots the runtime still needs at start-up ("no
// inline TEB TLS slot available"), so the game's own process loads nothing for it.
bool CanRunInChild(std::string* why, std::string* gpu) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SECURITY_ATTRIBUTES sa{.nLength = sizeof(sa), .bInheritHandle = TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        *why = "the DLSS check couldn't start";
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{.cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES};
    si.hStdOutput = wr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --dlss-probe";
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(exe, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!started) {
        CloseHandle(rd);
        *why = "the DLSS check couldn't start";
        return false;
    }
    // Wait first (with the time limit), then read: the child prints one short line, far less than
    // the pipe holds, so it never blocks on a full pipe.
    std::string out;
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        out = "dlss: not available: the DLSS check took too long";
    } else {
        char buf[512];
        DWORD got = 0;
        while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got) out.append(buf, got);
    }
    CloseHandle(rd);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    // The last "dlss: " line (the child may print other lines first).
    std::string line;
    for (size_t p = out.find("dlss: "); p != std::string::npos; p = out.find("dlss: ", p + 1)) {
        const size_t e = out.find_first_of("\r\n", p);
        line = out.substr(p + 6, e == std::string::npos ? std::string::npos : e - p - 6);
    }
    constexpr std::string_view kOn = "available on ", kOff = "not available: ";
    if (code == 0 && line.rfind(kOn, 0) == 0) {
        *gpu = line.substr(kOn.size());
        return true;
    }
    *why = line.rfind(kOff, 0) == 0 ? line.substr(kOff.size()) : "the DLSS check failed (exit " + std::to_string(code) + ")";
    return false;
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

Img g_hud, g_final, g_vel8, g_depth, g_mv, g_out, g_comp, g_velOut;
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
Pass g_mvPass, g_composePass, g_velPass;
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
           MakePass(g_composePass, std::span<const uint32_t>(DLSS_COMPOSE_COMP), 3) &&
           MakePass(g_velPass, std::span<const uint32_t>(DLSS_VELSCALE_COMP), 1);
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
    uint32_t draws = 0, displayDraws = 0, jittered = 0, skipped = 0, biased = 0;
} g_frame;
std::map<uint64_t, uint32_t> g_zCount;  // depth address -> depth-tested draws this frame
uint64_t g_velAddr = 0;
bool g_hooked = false;
void (*g_prevAfterDraw)(u64, u64, u32) = nullptr;
void (*g_prevOnFill)(u64, u64, u32, u32, bool) = nullptr;

struct Stats {
    uint32_t flips = 0, upscaled = 0, noHud = 0, noVel = 0, noDepth = 0, failed = 0;
    uint32_t jitteredFrames = 0, smaaSkipped = 0, stretched = 0;
    uint64_t biasedSamplers = 0;  // samplers a scene draw bound with the mip bias
    std::map<uint64_t, uint64_t> jitterTargets;  // rt0 -> jittered draws (LBP3_DLSS_TRACE)
} g_stats;
uint64_t g_flipCount = 0;

float Halton(uint32_t i, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) {
        f /= float(base);
        r += f * float(i % base);
        i /= base;
    }
    return r;
}

void NextJitter() {
    g_phase = (g_phase + 1) % g_phases;
    g_jx = Halton(g_phase + 1, 2) - 0.5f;
    g_jy = Halton(g_phase + 1, 3) - 0.5f;
}

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
    ++g_frame.draws;
    if (IsDisplay(rt0)) ++g_frame.displayDraws;
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

// The 3D scene's draws get the frame's sub-pixel offset: depth-tested, more than 12 indices (not a
// full-screen pass), a viewport the size of the render, and not into a display buffer. Only while
// the last flip was upscaled, so a frame DLSS doesn't take is never jittered twice in a row.
// A draw of the 3D scene: depth-tested, more than 12 indices (not a full-screen pass), a viewport the
// size of the render (w x h), not into a display buffer. rt0 = its colour target.
bool SceneDraw(float w, float h, uint64_t* rt0) {
    const auto& regs = internal::GetLiverpool()->regs;
    if (!regs.depth_control.depth_enable || regs.num_indices <= 12) return false;
    const Lbp3Res::Size rs = Lbp3Res::Current();
    if (std::abs(std::abs(w) - float(rs.w)) > 0.5f || std::abs(std::abs(h) - float(rs.h)) > 0.5f) return false;
    const auto& cb0 = regs.color_buffers[0];
    *rt0 = cb0 ? cb0.Address() : 0;
    return !IsDisplay(*rt0);
}

bool Jitter(float w, float h, float* dx, float* dy) {
    if (!g_jitterOn || !g_prevUpscaled) return false;
    uint64_t rt0 = 0;
    if (!SceneDraw(w, h, &rt0)) return false;
    *dx = g_jx;
    *dy = g_jy;
    g_frameJittered = true;
    ++g_frame.jittered;
    if (g_trace) ++g_stats.jitterTargets[rt0];
    return true;
}

// The 3D scene's samplers get NVIDIA's mip bias (guide 3.5: log2(render / output) - 1) while DLSS
// upscales, so textures keep the detail of the output size rather than the render size.
float SamplerBias() {
    if (g_mipBias == 0.0f || !g_prevUpscaled) return 0.0f;
    const auto& vp = internal::GetLiverpool()->regs.viewports[0];
    uint64_t rt0 = 0;
    if (!SceneDraw(2.0f * vp.xscale, 2.0f * vp.yscale, &rt0)) return 0.0f;
    ++g_frame.biased;
    return g_mipBias;
}

bool Skip(u64 vs, u64 ps, u32 n) {
    (void)vs;
    if (!g_skipSmaa || !g_prevUpscaled || ps != kSmaaCopyBackPs || n > 6) return false;
    ++g_frame.skipped;
    return true;
}

void Hook() {
    // Chained after whatever holds the hooks already (frame generation, the capture tool).
    g_prevAfterDraw = Vulkan::g_lbp3FgAfterDraw;
    g_prevOnFill = Vulkan::g_lbp3FgOnFill;
    Vulkan::g_lbp3FgAfterDraw = AfterDraw;
    Vulkan::g_lbp3FgOnFill = OnFill;
    Vulkan::g_lbp3ViewportJitter = Jitter;
    Vulkan::g_lbp3SkipDraw = Skip;
    Vulkan::g_lbp3SamplerBias = SamplerBias;
    g_hooked = true;
}

void Log() {
    if (++g_stats.flips < 300) return;
    std::fprintf(stderr,
                 "[dlss] %s %ux%u -> %ux%u: %u of %u flips upscaled; not: %u without the HUD-less image, %u "
                 "without vectors, %u without depth, %u failed (%u of those stretched); %u frames jittered, SMAA "
                 "left out in %u, %.0f samplers a frame with mip bias %.2f\n",
                 ModeLabel(), g_featW, g_featH, g_featOutW, g_featOutH, g_stats.upscaled, g_stats.flips, g_stats.noHud, g_stats.noVel,
                 g_stats.noDepth, g_stats.failed, g_stats.stretched, g_stats.jitteredFrames, g_stats.smaaSkipped,
                 double(g_stats.biasedSamplers) / std::max<uint32_t>(1, g_stats.flips), g_mipBias);
    if (g_trace && !g_stats.jitterTargets.empty()) {
        std::string list;
        for (const auto& [rt0, n] : g_stats.jitterTargets) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), " %llx:%.1f", static_cast<unsigned long long>(rt0),
                          double(n) / std::max<uint32_t>(1, g_stats.jitteredFrames));
            list += buf;
        }
        std::fprintf(stderr, "[dlss] jittered draws per jittered frame, by colour target:%s\n", list.c_str());
    }
    g_stats = Stats{};
}

const char* MvName() {
    if (g_mvSignX == 0.0f) return "zero";
    if (g_mvSignX < 0.0f) return g_mvSignY < 0.0f ? "flipxy" : "flipx";
    return g_mvSignY < 0.0f ? "flipy" : "normal";
}

void LogSettings(const char* why) {
    std::fprintf(stderr,
                 "[dlss] %s: vectors %s, jitter %s (%u phases, DLSS told %c%c), SMAA %s while upscaling, mip bias "
                 "%.2f%s\n",
                 why, MvName(), g_jitterOn ? "on" : "off", g_phases, g_jitterSignX > 0 ? '+' : '-',
                 g_jitterSignY > 0 ? '+' : '-', g_skipSmaa ? "left out" : "kept", g_mipBias, g_trace ? ", tracing" : "");
}

// One setting (the environment's or the remote command's). False = unknown.
bool Apply(const std::string& key, const std::string& v) {
    if (key == "mv") {
        g_mvSignX = g_mvSignY = 1.0f;
        if (v == "zero") g_mvSignX = g_mvSignY = 0.0f;
        else if (v == "flipx") g_mvSignX = -1.0f;
        else if (v == "flipy") g_mvSignY = -1.0f;
        else if (v == "flipxy") g_mvSignX = g_mvSignY = -1.0f;
        else if (v != "normal" && !v.empty()) return false;
    } else if (key == "jitter") {
        if (v == "off") g_jitterOn = false;
        else if (v == "on" || v.empty()) g_jitterOn = true;
        else if (v.size() == 2 && (v[0] == '+' || v[0] == '-') && (v[1] == '+' || v[1] == '-')) {
            g_jitterOn = true;
            g_jitterSignX = v[0] == '-' ? -1.0f : 1.0f;
            g_jitterSignY = v[1] == '-' ? -1.0f : 1.0f;
        } else return false;
    } else if (key == "smaa") {
        if (v != "skip" && v != "keep") return false;
        g_skipSmaa = v == "skip";
    } else if (key == "mipbias") {  // a number; "nvidia" = the guide's log2(render / output) - 1
        if (v == "nvidia") g_mipBias = float(std::log2(1.0 / dlss_modes::kModes[g_mode].ratio) - 1.0);
        else if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-' || v[0] == '.'))
            g_mipBias = float(std::atof(v.c_str()));
        else return false;
    } else return false;
    return true;
}

std::mutex g_remoteMutex;
std::vector<std::pair<std::string, std::string>> g_remote;  // queued by the input thread, applied at a flip

void ReadSettings() {
    const auto env = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return v ? v : "";
    };
    if (!env("LBP3_DLSS_MV").empty()) Apply("mv", env("LBP3_DLSS_MV"));
    if (env("LBP3_DLSS_JITTER") == "off") Apply("jitter", "off");
    if (!env("LBP3_DLSS_JITTER_SIGN").empty()) Apply("jitter", env("LBP3_DLSS_JITTER_SIGN"));
    if (!env("LBP3_DLSS_SMAA").empty()) Apply("smaa", env("LBP3_DLSS_SMAA"));
    g_trace = EnvOn("LBP3_DLSS_TRACE");
    // NVIDIA's guide 3.7.1: 8 x (output / render)^2 phases (Quality 18, Balanced 24, Performance
    // 32, Ultra Performance 72), at least 16 here (DLAA).
    const double ratio = dlss_modes::kModes[g_mode].ratio;
    g_phases = std::max<uint32_t>(16, uint32_t(std::ceil(8.0 * ratio * ratio)));
    // NVIDIA's guide 3.5: log2(render / output) - 1 (DLAA -1, Quality -1.58, Performance -2).
    g_mipBias = float(std::log2(1.0 / ratio) - 1.0);
    if (const char* b = std::getenv("LBP3_DLSS_MIPBIAS"); b && *b) g_mipBias = float(std::atof(b));
    NextJitter();
    LogSettings("settings");
}

}  // namespace

void Prepare() {
    const char* v = std::getenv("LBP3_UPSCALER");
    if (!v || _stricmp(v, "dlss") != 0) return;
    InitCommon();
    Lbp3Res::Size out = Lbp3Res::kNative;
    Lbp3Res::Parse(std::getenv("LBP3_RES"), &out);  // as Lbp3Res::Current() would read it
    const char* m = std::getenv("LBP3_UPSCALER_MODE");
    int mode = m && *m ? dlss_modes::Find(m) : dlss_modes::kDefault;
    if (mode < 0) {
        std::fprintf(stderr, "[dlss] LBP3_UPSCALER_MODE=%s isn't a mode: Quality\n", m);
        mode = dlss_modes::kDefault;
    }
    uint32_t rw = 0, rh = 0;
    if (!dlss_modes::RenderSize(out.w, out.h, dlss_modes::kModes[mode].ratio, &rw, &rh)) {
        std::fprintf(stderr, "[dlss] %s would render under 1280x720 at %ux%u: DLAA instead\n",
                     dlss_modes::kModes[mode].label, out.w, out.h);
        mode = 0;
        rw = out.w, rh = out.h;
    }
    std::string why, gpu;
    if (!CanRunInChild(&why, &gpu)) {
        std::fprintf(stderr, "[dlss] no upscaler this run: %s. The game renders at %ux%u.\n", why.c_str(), out.w, out.h);
        g_disabled = true;
        return;
    }
    g_mode = mode;
    g_outW = out.w;
    g_outH = out.h;
    if (rw != out.w || rh != out.h) {
        char size[32];
        std::snprintf(size, sizeof(size), "%ux%u", rw, rh);
        _putenv_s("LBP3_RES", size);  // the render size, before anything reads it
    }
    std::fprintf(stderr, "[dlss] %s on %s: the game renders %ux%u, DLSS makes %ux%u\n", ModeLabel(), gpu.c_str(), rw,
                 rh, out.w, out.h);
}

int Probe() {
    InitCommon();
    std::string why, gpu;
    if (CanRun(&why, &gpu)) {
        std::printf("dlss: available on %s\n", gpu.c_str());
        return 0;
    }
    std::printf("dlss: not available: %s\n", why.c_str());
    return 1;
}

void RegisterVulkanExtensions() {
    const char* v = std::getenv("LBP3_UPSCALER");
    g_wanted = v && _stricmp(v, "dlss") == 0 && !g_disabled;
    if (!g_wanted) return;
    InitCommon();
    ReadSettings();
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
    if (g_outW) {  // NVIDIA's own render size for the mode, against the one Prepare chose
        unsigned optW = 0, optH = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
        float sharpness = 0.0f;
        NGX_DLSS_GET_OPTIMAL_SETTINGS(g_caps, g_outW, g_outH, kModeValues[g_mode], &optW, &optH, &maxW, &maxH, &minW,
                                      &minH, &sharpness);
        const Lbp3Res::Size rs = Lbp3Res::Current();
        const bool inside = rs.w >= minW && rs.w <= maxW && rs.h >= minH && rs.h <= maxH;
        std::fprintf(stderr, "[dlss] NVIDIA's %s for %ux%u: %ux%u (allowed %ux%u to %ux%u); the game renders %ux%u%s\n",
                     ModeLabel(), g_outW, g_outH, optW, optH, minW, minH, maxW, maxH, rs.w, rs.h,
                     inside ? "" : ", OUTSIDE that range");
    }
    g_ready = true;
    std::fprintf(stderr, "[dlss] DLSS ready on %s (%s); NVIDIA's DLL from %ls\n",
                 std::string(inst->GetModelName()).c_str(), ModeLabel(), g_exeDir.c_str());
    return true;
}

bool Active() { return g_ready; }

void OutputSize(uint32_t* w, uint32_t* h) {
    *w = g_outW;
    *h = g_outH;
}

void Remote(const std::string& key, const std::string& value) {
    if (!g_ready) {
        std::fprintf(stderr, "[dlss] remote '%s %s' ignored: the upscaler is off\n", key.c_str(), value.c_str());
        return;
    }
    std::lock_guard lk(g_remoteMutex);
    g_remote.emplace_back(key, value);
}

void SetDisplayBuffer(int index, uint64_t address) {
    if (index >= 0 && index < 16) g_display[index] = address;
}

namespace {

// One flip's DLSS; jx, jy = the offset its frame was drawn with (0 when it wasn't jittered).
Output Evaluate(void* cmdp, void* imagep, const Frame& f, float jx, float jy) {
    Output out;
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
    // The output size Prepare kept (the launcher's Resolution); the render size is the game's.
    const uint32_t OW = g_outW ? g_outW : W, OH = g_outH ? g_outH : H;
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    const vk::Format fmt = Unorm(display.info.pixel_format);
    if (!Make(g_final, W, H, fmt, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_mv, W, H, vk::Format::eR16G16Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_out, OW, OH, vk::Format::eR8G8B8A8Unorm,
              vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc) ||
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
        cp.Feature.InPerfQualityValue = OW == W && OH == H ? NVSDK_NGX_PerfQuality_Value_DLAA : kModeValues[g_mode];
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
        std::fprintf(stderr, "[dlss] DLSS created: %ux%u -> %ux%u (%s), display format %s\n", W, H, OW, OH,
                     ModeLabel(), vk::to_string(display.info.pixel_format).c_str());
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
        PC{-g_mvSignX * kVecScaleX * float(W) / 1920.0f, -g_mvSignY * kVecScaleY * float(H) / 1080.0f, W, H});

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
    // The viewport moved the scene by (jx, jy) render pixels; LBP3_DLSS_JITTER_SIGN says how DLSS
    // is told (settled at the basket: NVIDIA's guide leaves the sign to be checked).
    ep.InJitterOffsetX = g_jitterSignX * jx;
    ep.InJitterOffsetY = g_jitterSignY * jy;
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
    // Frame generation works on this picture at the output size (fg_live.h OnUpscaled): DLSS's
    // picture before the HUD went back is its HUD-less image, and the game's vectors are resampled.
    if (fglive::Active()) {
        To(cmd, g_out, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eTransfer,
           vk::AccessFlagBits2::eTransferRead);
        out.hudless = static_cast<VkImage>(g_out.image);
        if (Make(g_velOut, OW, OH, vk::Format::eR8G8B8A8Unorm,
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)) {
            ToWrite(cmd, g_velOut);
            Run(cmd, g_velPass, {{&g_vel8, g_nearest}}, g_velOut, PC{0.0f, 0.0f, OW, OH});
            To(cmd, g_velOut, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eTransfer,
               vk::AccessFlagBits2::eTransferRead);
            out.velocity = static_cast<VkImage>(g_velOut.image);
        }
    }
    ++g_stats.upscaled;
    out.image = static_cast<VkImage>(g_comp.image);
    out.width = OW;
    out.height = OH;
    return out;
}

// A flip DLSS can't take while upscaling (a loading screen, a menu with no 3D scene): the game's
// picture stretched (bilinear) to the output size, so the window and frame generation keep one size.
Output Stretch(void* cmdp, void* imagep) {
    Output out;
    auto& display = *static_cast<VideoCore::Image*>(imagep);
    if (!FourByte(display.info.pixel_format)) return out;
    const uint32_t W = display.info.size.width, H = display.info.size.height, OW = g_outW, OH = g_outH;
    vk::CommandBuffer cmd(static_cast<VkCommandBuffer>(cmdp));
    if (!Make(g_final, W, H, Unorm(display.info.pixel_format), vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled) ||
        !Make(g_comp, OW, OH, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc))
        return out;
    To(cmd, g_final, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferWrite);
    cmd.copyImage(display.GetImage(), vk::ImageLayout::eTransferSrcOptimal, g_final.image,
                  vk::ImageLayout::eTransferDstOptimal,
                  vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                .extent = {W, H, 1}});
    ToRead(cmd, g_final);
    ToWrite(cmd, g_comp);
    // hudT -1: every pixel comes from the finished frame, sampled at the output pixel's centre.
    Run(cmd, g_composePass, {{&g_final, g_linear}, {&g_final, g_linear}, {&g_final, g_nearest}}, g_comp,
        PC{-1.0f, 0.0f, OW, OH});
    To(cmd, g_comp, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eTransfer,
       vk::AccessFlagBits2::eTransferRead);
    ++g_stats.stretched;
    out.image = static_cast<VkImage>(g_comp.image);
    out.width = OW;
    out.height = OH;
    return out;
}

}  // namespace

Output Process(void* cmdp, void* imagep) {
    const Frame f = g_frame;
    g_frame = Frame{};
    g_zCount.clear();
    const bool jittered = g_frameJittered;
    const float jx = jittered ? g_jx : 0.0f, jy = jittered ? g_jy : 0.0f;
    g_frameJittered = false;
    ++g_flipCount;
    if (!g_ready) return {};
    if (!g_hooked) {
        Hook();  // from the next frame on
        return {};
    }
    {
        std::lock_guard lk(g_remoteMutex);
        for (const auto& [key, value] : g_remote) {
            if (Apply(key, value)) {
                g_reset = true;  // a new setting starts DLSS's history afresh
                LogSettings(("remote " + key + " " + value).c_str());
            } else {
                std::fprintf(stderr, "[dlss] remote: unknown setting '%s %s'\n", key.c_str(), value.c_str());
            }
        }
        g_remote.clear();
    }
    Log();
    Output out = Evaluate(cmdp, imagep, f, jx, jy);
    g_prevUpscaled = out.image != nullptr;  // the gate for the next frame's jitter and SMAA skip
    if (!out.image && g_outW) {
        const auto& d = static_cast<VideoCore::Image*>(imagep)->info.size;
        if (d.width != g_outW || d.height != g_outH) out = Stretch(cmdp, imagep);
    }
    g_stats.jitteredFrames += jittered;
    g_stats.smaaSkipped += f.skipped > 0;
    g_stats.biasedSamplers += f.biased;
    if (g_trace && !g_prevUpscaled)
        std::fprintf(stderr,
                     "[dlss] flip %llu not upscaled: composite %d, HUD-less %d, vectors %d, depth %d; %u draws, %u "
                     "into a display buffer, %u jittered, SMAA %s\n",
                     static_cast<unsigned long long>(g_flipCount), f.composite, f.hud, f.vel, f.depth, f.draws,
                     f.displayDraws, f.jittered, f.skipped ? "left out" : "drawn");
    NextJitter();
    return out;
}

}  // namespace gpu::dlss
