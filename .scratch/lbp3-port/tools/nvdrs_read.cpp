// Read-only: prints a few NVIDIA driver (DRS) settings from the global profile.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

using NvU32 = uint32_t;
using NvU16 = uint16_t;
using QI = void*(__cdecl*)(NvU32);

struct Setting {  // NVDRS_SETTING_V1
    NvU32 version;
    NvU16 name[2048];
    NvU32 id, type, location, isCurrentPredefined, isPredefinedValid;
    union { NvU32 u32; struct { NvU32 len; uint8_t data[4096]; } bin; NvU16 wsz[2048]; } predefined;
    union { NvU32 u32; struct { NvU32 len; uint8_t data[4096]; } bin; NvU16 wsz[2048]; } current;
};
struct ProfileInfo {  // NVDRS_PROFILE_V1
    NvU32 version;
    NvU16 name[2048];
    NvU32 gpuSupport, isPredefined, numOfApps, numOfSettings;
};

int main() {
    HMODULE h = LoadLibraryA("nvapi64.dll");
    if (!h) { std::printf("no nvapi64.dll\n"); return 1; }
    auto qi = reinterpret_cast<QI>(GetProcAddress(h, "nvapi_QueryInterface"));
    auto init = reinterpret_cast<int (*)()>(qi(0x0150E828));
    auto create = reinterpret_cast<int (*)(void**)>(qi(0x0694D52E));
    auto load = reinterpret_cast<int (*)(void*)>(qi(0x375DBD6B));
    auto global = reinterpret_cast<int (*)(void*, void**)>(qi(0x617BFF9F));
    auto get = reinterpret_cast<int (*)(void*, void*, NvU32, Setting*)>(qi(0x73BF8338));
    auto info = reinterpret_cast<int (*)(void*, void*, ProfileInfo*)>(qi(0x61CD6FD6));
    if (!init || !create || !load || !global || !get) { std::printf("missing nvapi entry points\n"); return 1; }
    std::printf("init %d\n", init());
    void* s = nullptr;
    std::printf("create %d\n", create(&s));
    std::printf("load %d\n", load(s));
    void* prof = nullptr;
    std::printf("global profile %d\n", global(s, &prof));
    if (info) {
        static ProfileInfo pi{};
        pi.version = sizeof(ProfileInfo) | (1u << 16);
        if (info(s, prof, &pi) == 0) std::printf("profile name: %ls, %u settings\n", reinterpret_cast<wchar_t*>(pi.name), pi.numOfSettings);
    }
    struct { NvU32 id; const char* what; } ids[] = {
        {0x00A879CF, "VSYNCMODE (passive 60925292, forceoff 08416747, forceon 47814940, fast 18888888)"},
        {0x005A375C, "VSYNCTEARCONTROL (adaptive 99941284, disable 96861077)"},
        {0x10835002, "FRL_FPS (max frame rate)"},
        {0x1194F158, "VRR_MODE (0 off, 1 fullscreen, 2 fullscreen+windowed)"},
        {0x10A879CF, "VRR_APP_OVERRIDE (G-SYNC on/off)"},
        {0x1094F1F7, "VRR_REQUEST_STATE"},
        {0x0005F543, "PRERENDERLIMIT"},
        {0x10835000, "LOW_LATENCY / PRERENDER (ultra)"},
        {0x20D690F8, "OGL_CPL_PREFER_DXPRESENT (Vulkan/OpenGL present method)"},
    };
    for (auto& e : ids) {
        static Setting st;
        std::memset(&st, 0, sizeof(st));
        st.version = sizeof(Setting) | (1u << 16);
        const int r = get(s, prof, e.id, &st);
        if (r == 0)
            std::printf("%08X %-80s current %08X (predefined %08X valid %u, isCurrentPredefined %u, location %u) '%ls'\n", e.id,
                        e.what, st.current.u32, st.predefined.u32, st.isPredefinedValid, st.isCurrentPredefined, st.location,
                        reinterpret_cast<wchar_t*>(st.name));
        else
            std::printf("%08X %-80s not set in profile (status %d)\n", e.id, e.what, r);
    }
    return 0;
}
