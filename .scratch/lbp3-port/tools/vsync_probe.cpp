// Measures how fast SDL_RenderPresent returns with vsync on, the way LBP3's presenter uses it,
// plus independent vblank sources (DXGI WaitForVBlank, DwmFlush) for comparison.
//   probe.exe [renderer] [fullscreen]    renderer: direct3d11 (default), direct3d12, vulkan, opengl, gpu
#include <SDL3/SDL.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <psapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static double NowMs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return 1000.0 * double(c.QuadPart) / double(f.QuadPart);
}

static void Report(const char* what, std::vector<double>& d) {
    if (d.empty()) return;
    double sum = 0;
    for (double x : d) sum += x;
    std::vector<double> s = d;
    std::sort(s.begin(), s.end());
    std::printf("%-34s n=%zu avg %.3f ms (%.1f /s)  min %.3f  p50 %.3f  p95 %.3f  max %.3f\n", what, d.size(),
                sum / d.size(), 1000.0 * d.size() / sum, s.front(), s[s.size() / 2], s[s.size() * 95 / 100], s.back());
}

int main(int argc, char** argv) {
    const char* rname = argc > 1 ? argv[1] : "direct3d11";
    const bool fullscreen = argc > 2 && std::strcmp(argv[2], "fullscreen") == 0;
    timeBeginPeriod(1);
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SDL init: %s\n", SDL_GetError()); return 1; }

    int nd = 0;
    SDL_DisplayID* ds = SDL_GetDisplays(&nd);
    for (int i = 0; i < nd; ++i) {
        const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(ds[i]);
        SDL_Rect r{};
        SDL_GetDisplayBounds(ds[i], &r);
        std::printf("display %u '%s' %dx%d @ %.3f Hz (num/den %d/%d) at %d,%d%s\n", ds[i], SDL_GetDisplayName(ds[i]),
                    m ? m->w : 0, m ? m->h : 0, m ? m->refresh_rate : 0.f, m ? m->refresh_rate_numerator : 0,
                    m ? m->refresh_rate_denominator : 0, r.x, r.y, ds[i] == SDL_GetPrimaryDisplay() ? " [primary]" : "");
    }

    SDL_Window* w = SDL_CreateWindow("vsync probe", 1280, 720, SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    SDL_Renderer* r = SDL_CreateRenderer(w, rname);
    if (!r) { std::printf("renderer %s: %s\n", rname, SDL_GetError()); return 1; }
    const bool ok = SDL_SetRenderVSync(r, 1);
    int vs = -9;
    SDL_GetRenderVSync(r, &vs);
    const SDL_DisplayMode* wm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(w));
    std::printf("renderer %s, set vsync %s, get %d, window display %u @ %.3f Hz, fullscreen %d\n", SDL_GetRendererName(r),
                ok ? "ok" : "FAILED", vs, SDL_GetDisplayForWindow(w), wm ? wm->refresh_rate : 0.f, fullscreen);

    // A streaming texture the size of LBP3's frame, uploaded every 8th present like the game at 30 fps on 240 Hz.
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1920, 1080);
    std::vector<uint8_t> px(1920 * 1080 * 4, 0x40);

    auto pump = [] { SDL_Event e; while (SDL_PollEvent(&e)) {} };
    // Warm up.
    for (int i = 0; i < 60; ++i) { pump(); SDL_RenderClear(r); SDL_RenderPresent(r); }

    std::vector<double> present;
    double t0 = NowMs(), last = t0;
    const double start = t0;
    int i = 0;
    while (NowMs() - start < 3000.0) {
        pump();
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        if (i % 8 == 0) SDL_UpdateTexture(tex, nullptr, px.data(), 1920 * 4);
        SDL_RenderTexture(r, tex, nullptr, nullptr);
        SDL_RenderPresent(r);
        const double now = NowMs();
        present.push_back(now - last);
        last = now;
        ++i;
    }
    Report("SDL_RenderPresent interval", present);

    // DwmFlush: waits for the next DWM composition pass.
    std::vector<double> dwm;
    last = NowMs();
    for (int k = 0; k < 240; ++k) {
        DwmFlush();
        const double now = NowMs();
        dwm.push_back(now - last);
        last = now;
    }
    Report("DwmFlush interval", dwm);

    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)))
        std::printf("DWM rateRefresh %u/%u, qpcRefreshPeriod %.3f ms\n", ti.rateRefresh.uiNumerator,
                    ti.rateRefresh.uiDenominator, double(ti.qpcRefreshPeriod) * 1000.0 / [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return double(x.QuadPart); }());

    // DXGI WaitForVBlank on each output.
    IDXGIFactory1* fac = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac))) {
        IDXGIAdapter1* ad = nullptr;
        for (UINT a = 0; fac->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; ++a) {
            DXGI_ADAPTER_DESC1 add{};
            ad->GetDesc1(&add);
            IDXGIOutput* out = nullptr;
            for (UINT o = 0; ad->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC od{};
                out->GetDesc(&od);
                std::vector<double> vb;
                last = NowMs();
                for (int k = 0; k < 120; ++k) {
                    if (FAILED(out->WaitForVBlank())) break;
                    const double now = NowMs();
                    vb.push_back(now - last);
                    last = now;
                }
                char label[160];
                std::snprintf(label, sizeof(label), "WaitForVBlank %ls/%ls", add.Description, od.DeviceName);
                Report(label, vb);
                out->Release();
            }
            ad->Release();
        }
        fac->Release();
    }
    {
        HMODULE mods[1024];
        DWORD need = 0;
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
            for (DWORD k = 0; k < need / sizeof(HMODULE); ++k) {
                wchar_t path[MAX_PATH];
                GetModuleFileNameW(mods[k], path, MAX_PATH);
                if (!StrStrIW(path, L"\\Windows\\System32\\"))
                    std::printf("module %ls\n", path);
            }
    }
    SDL_Quit();
    return 0;
}
