#include "video/presenter.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

namespace video {
namespace {

constexpr int kMaxBuffers = 16;
constexpr int kWindowWidth = 1280, kWindowHeight = 720;

std::mutex g_mutex;
DisplayBuffer g_buffers[kMaxBuffers];
std::atomic<int> g_shown{-1};
std::atomic<bool> g_started{false};

std::vector<uint8_t> g_frame;  // latest GPU-rendered frame, guarded by g_mutex
uint32_t g_frameW = 0, g_frameH = 0;
bool g_haveFrame = false;
uint64_t g_frameSeq = 0;  // bumps with every new frame, so unchanged ones aren't re-uploaded

// Real display refreshes. With LBP3_PACING=present (upstream) the present loop counts
// them, trusting SDL_RenderPresent to block on vsync. A driver can force vsync off
// (NVIDIA's "Vertical sync: Force off"), and then presents return thousands of times a
// second. The default (vblank) counts them on a separate thread from the DXGI output
// the window is on (IDXGIOutput::WaitForVBlank), which the driver setting doesn't affect.
std::mutex g_vblankMutex;
std::condition_variable g_vblankCv;
uint64_t g_vblanks = 0;
std::atomic<bool> g_vblankSourceOk{false};
std::atomic<double> g_refreshHz{0.0};
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<void (*)(int)> g_hotkeyHandler{nullptr};
std::atomic<uint64_t> g_presents{0};

void CountVBlank() {
    {
        std::lock_guard lock(g_vblankMutex);
        ++g_vblanks;
    }
    g_vblankCv.notify_all();
}

// Waits on the vblanks of whichever output the window is on, checked about every
// 60 refreshes so moving the window to another display follows it.
void VBlankThread() {
    IDXGIFactory1* factory = nullptr;
    IDXGIOutput* output = nullptr;
    HMONITOR monitor = nullptr;
    int sinceCheck = 0;
    for (;;) {
        if (!output || ++sinceCheck >= 60) {
            sinceCheck = 0;
            const HWND hwnd = g_hwnd.load();
            const HMONITOR m = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
            if (m && (m != monitor || !output || !factory || !factory->IsCurrent())) {
                if (output) output->Release();
                output = nullptr;
                if (!factory || !factory->IsCurrent()) {
                    if (factory) factory->Release();
                    factory = nullptr;
                    CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
                }
                IDXGIAdapter1* adapter = nullptr;
                for (UINT a = 0; factory && !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
                    IDXGIOutput* o = nullptr;
                    for (UINT i = 0; !output && adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
                        DXGI_OUTPUT_DESC desc{};
                        if (SUCCEEDED(o->GetDesc(&desc)) && desc.Monitor == m) {
                            output = o;
                            std::fprintf(stderr, "[video] vblank source: %ls\n", desc.DeviceName);
                        } else {
                            o->Release();
                        }
                    }
                    adapter->Release();
                }
                monitor = m;
                if (!output) std::fprintf(stderr, "[video] vblank source: no DXGI output for the window's monitor\n");
            }
        }
        if (!output || FAILED(output->WaitForVBlank())) {
            g_vblankSourceOk = false;
            if (output) output->Release();
            output = nullptr;
            Sleep(100);
            continue;
        }
        g_vblankSourceOk = true;
        CountVBlank();
    }
}

void PresenterThread() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[video] SDL video init failed: %s\n", SDL_GetError());
        return;
    }
    SDL_Window* window = SDL_CreateWindow("LittleBigPlanet 3 (native)", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_RESIZABLE);
    // LBP3_DISPLAY=<n>: open the window centred on display n (1 = the first SDL lists).
    if (const char* d = std::getenv("LBP3_DISPLAY"); d && window) {
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        const int n = std::atoi(d);
        if (ids && n >= 1 && n <= count) {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(ids[n - 1]),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(ids[n - 1]));
        }
        SDL_free(ids);
    }
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        std::fprintf(stderr, "[video] window creation failed: %s\n", SDL_GetError());
        return;
    }
    const bool vsyncOk = SDL_SetRenderVSync(renderer, 1);
    int vsync = 0;
    SDL_GetRenderVSync(renderer, &vsync);
    std::fprintf(stderr, "[video] window renderer: %s, vsync %s (%d)\n", SDL_GetRendererName(renderer),
                 vsyncOk ? "on" : "FAILED", vsync);
    auto readRefresh = [&] {
        if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))) {
            // The exact rational when SDL has it (239.999 Hz shows as 240.00 in refresh_rate).
            const double hz = mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0
                                  ? double(mode->refresh_rate_numerator) / double(mode->refresh_rate_denominator)
                                  : double(mode->refresh_rate);
            g_refreshHz = hz;
            std::fprintf(stderr, "[video] display refresh %.3f Hz\n", hz);
        }
    };
    readRefresh();
    const Pacing pacing = PacingMode();
    std::fprintf(stderr, "[video] pacing: %s\n", PacingName());
    if (pacing != Pacing::Present) {
        g_hwnd = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        std::thread(VBlankThread).detach();
    }
    uint64_t presentedAtVblank = 0;
    LARGE_INTEGER qpf, statsFrom;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&statsFrom);
    uint64_t statsPresents = 0, statsVblanks = 0;
    SDL_Texture* texture = nullptr;
    uint32_t texW = 0, texH = 0;
    uint64_t shownSeq = ~0ull;
    std::vector<uint8_t> frame;

    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (auto handler = g_hotkeyHandler.load()) handler(static_cast<int>(e.key.key));
            }
            if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) readRefresh();
            if (e.type == SDL_EVENT_QUIT) {
                std::fprintf(stderr, "[video] window closed\n");
                std::fflush(stderr);
                ExitProcess(0);
            }
        }

        // Present once per real vblank, so the window doesn't spin when vsync is forced off.
        if (pacing != Pacing::Present && g_vblankSourceOk.load()) {
            std::unique_lock lock(g_vblankMutex);
            g_vblankCv.wait_for(lock, std::chrono::milliseconds(50), [&] { return g_vblanks > presentedAtVblank; });
            presentedAtVblank = g_vblanks;
        }

        DisplayBuffer buf;
        bool fresh = true;
        {
            std::lock_guard lock(g_mutex);
            if (g_haveFrame) {
                fresh = g_frameSeq != shownSeq;
                if (fresh) {
                    frame.swap(g_frame);  // take it; our previous buffer goes back to the GPU thread
                    shownSeq = g_frameSeq;
                }
                buf = {frame.data(), g_frameW, g_frameH, g_frameW, 0, 0};
            } else {
                const int i = g_shown.load();
                if (i >= 0 && i < kMaxBuffers) buf = g_buffers[i];
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (buf.address && buf.width && buf.height) {
            if (!texture || texW != buf.width || texH != buf.height) {
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                            static_cast<int>(buf.width), static_cast<int>(buf.height));
                texW = buf.width;
                texH = buf.height;
            }
            // 32-bit BGRA rows. Only upload when a new frame arrived.
            if (fresh || !g_haveFrame)
                SDL_UpdateTexture(texture, nullptr, buf.address, static_cast<int>(buf.pitchPixels * 4));
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        }
        SDL_RenderPresent(renderer);  // paced by vsync, unless a driver forces it off
        ++g_presents;
        if (pacing == Pacing::Present) CountVBlank();

        // Presents and vblanks per second, every 5 s.
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double secs = double(now.QuadPart - statsFrom.QuadPart) / double(qpf.QuadPart);
        if (secs >= 5.0) {
            uint64_t vb;
            {
                std::lock_guard lock(g_vblankMutex);
                vb = g_vblanks;
            }
            const uint64_t pr = g_presents.load();
            std::fprintf(stderr, "[pace] %.1f presents/s, %.1f vblanks/s (%s), refresh %.2f Hz\n",
                         double(pr - statsPresents) / secs, double(vb - statsVblanks) / secs, PacingName(),
                         g_refreshHz.load());
            statsPresents = pr;
            statsVblanks = vb;
            statsFrom = now;
        }
    }
}

}  // namespace

Pacing PacingMode() {
    static const Pacing mode = [] {
        const char* v = std::getenv("LBP3_PACING");
        if (v && std::strcmp(v, "present") == 0) return Pacing::Present;
        if (v && std::strcmp(v, "timer") == 0) return Pacing::Timer;
        return Pacing::VBlank;
    }();
    return mode;
}

const char* PacingName() {
    switch (PacingMode()) {
    case Pacing::Present: return "present";
    case Pacing::Timer: return "timer";
    default: return "vblank";
    }
}

uint64_t VBlankCount() {
    std::lock_guard lock(g_vblankMutex);
    return g_vblanks;
}

bool WaitForFlipSlot(double gameHz, int64_t* slotNs) {
    if (PacingMode() == Pacing::Timer) return false;
    if (PacingMode() == Pacing::VBlank && !g_vblankSourceOk.load()) return false;
    const double refresh = g_refreshHz.load();
    if (refresh <= 0.0 || gameHz <= 0.0) return false;
    const double ratio = refresh / gameHz;
    const uint64_t perFlip = static_cast<uint64_t>(ratio + 0.5);
    if (perFlip == 0 || std::abs(ratio - static_cast<double>(perFlip)) > 0.05) return false;  // e.g. 30 fps on 144 Hz
    static uint64_t next = 0;
    std::unique_lock lock(g_vblankMutex);
    if (next == 0 || next > g_vblanks + perFlip) next = g_vblanks + perFlip;  // first flip / after a resync
    const bool ok = g_vblankCv.wait_for(lock, std::chrono::milliseconds(200), [&] { return g_vblanks >= next; });
    if (!ok) return false;  // window not presenting (minimized?): use the timer
    const uint64_t vblank = g_vblanks;
    // Running late keeps the cadence from the current vblank instead of trying to catch up.
    next = std::max(next, vblank) + perFlip;
    lock.unlock();
    // The flip's time for the game's frame clock: the vblank's ideal time (anchor + index *
    // refresh period), not when this thread woke up (~0.1 ms of jitter), so the game sees
    // the exact frame deltas a console would. This doesn't measurably cure the 0/2 sim
    // steps (camera snaps); those are game-side. Re-anchors when the real time strays
    // more than 2 ms (a late frame, a missed vblank, a refresh-rate change).
    if (slotNs) {
        static double anchorNs = 0, anchorPeriodNs = 0;
        static uint64_t anchorVblank = 0;
        const double nowNs = double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
        const double periodNs = 1e9 / refresh;
        double ideal = anchorNs + double(vblank - anchorVblank) * periodNs;
        if (anchorNs == 0 || periodNs != anchorPeriodNs || vblank < anchorVblank || std::abs(nowNs - ideal) > 2e6) {
            anchorNs = ideal = nowNs;
            anchorVblank = vblank;
            anchorPeriodNs = periodNs;
        }
        *slotNs = static_cast<int64_t>(ideal);
    }
    return true;
}

void StartPresenter() {
    if (g_started.exchange(true)) return;
    std::thread(PresenterThread).detach();
}

void SetDisplayBuffer(int index, const DisplayBuffer& buffer) {
    if (index < 0 || index >= kMaxBuffers) return;
    std::lock_guard lock(g_mutex);
    g_buffers[index] = buffer;
}

void SetHotkeyHandler(void (*handler)(int key)) { g_hotkeyHandler = handler; }

void TriggerHotkey(int key) {
    if (auto handler = g_hotkeyHandler.load()) handler(key);
}

void PresentBuffer(int index) { g_shown = index; }

// LBP3_DUMP_FRAMES=skip:count writes `count` frames after the first `skip` (quarter size, 24-bit BMP) to
// analysis/frames/ for looking at animation and flicker frame by frame.
static std::atomic<int> g_frameDumpRequest{0};

void RequestFrameDump(int count) { g_frameDumpRequest = count; }

static void MaybeDumpFrame(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
    static int skip = 0, limit = 0;
    static const bool parsed = [] {
        if (const char* v = std::getenv("LBP3_DUMP_FRAMES")) std::sscanf(v, "%d:%d", &skip, &limit);
        return true;
    }();
    (void)parsed;
    static int seen = 0, n = 0;
    if (const int more = g_frameDumpRequest.exchange(0); more > 0) {  // RequestFrameDump
        skip = seen;
        limit = n + more;
    }
    if (seen++ < skip || n >= limit || w < 4 || h < 4) return;
    if (FILE* t = std::fopen("analysis\\frames\\times.txt", n == 0 ? "w" : "a")) {  // hand-over times
        std::fprintf(t, "%d %.3f\n", n,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
                         .count());
        std::fclose(t);
    }
    const uint32_t ow = w / 4, oh = h / 4, row = (ow * 3 + 3) & ~3u;
    std::vector<uint8_t> out(54 + row * oh);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
    out[0] = 'B'; out[1] = 'M';
    put32(2, static_cast<uint32_t>(out.size())); put32(10, 54); put32(14, 40);
    put32(18, ow); put32(22, oh); out[26] = 1; out[28] = 24; put32(34, row * oh);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x) {
            const uint8_t* s = &px[((h - 1 - y * 4) * w + x * 4) * 4];  // BMP rows go bottom-up
            uint8_t* d = &out[54 + y * row + x * 3];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    CreateDirectoryA("analysis\\frames", nullptr);
    char name[64];
    std::snprintf(name, sizeof(name), "analysis\\frames\\f%04d.bmp", n++);
    if (FILE* f = std::fopen(name, "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
}

void PresentPixels(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
    MaybeDumpFrame(pixels, width, height);
    {
        // Frame delivery stats: uneven gaps between frames look like judder/tearing.
        using Clock = std::chrono::steady_clock;
        static Clock::time_point last = Clock::now();
        static double minMs = 1e9, maxMs = 0, sumMs = 0;
        static int n = 0;
        const auto now = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
        sumMs += ms;
        if (++n == 300) {
            std::fprintf(stderr, "[video] frame gaps over 300 frames: min %.1f ms, avg %.1f ms, max %.1f ms\n", minMs,
                         sumMs / n, maxMs);
            minMs = 1e9, maxMs = 0, sumMs = 0, n = 0;
        }
    }
    std::lock_guard lock(g_mutex);
    g_frame.swap(pixels);  // the caller gets an older buffer back to refill: no allocation per frame
    g_frameW = width;
    g_frameH = height;
    g_haveFrame = true;
    ++g_frameSeq;
}

}  // namespace video
