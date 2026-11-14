// The boot loading screen (ui/boot_overlay.h).
#include "ui/boot_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "common/lbp3_boot.h"  // shadPS4: the preload's progress
#include "ui/overlay_layers.h"

namespace ui::boot {
namespace {

using Clock = std::chrono::steady_clock;

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_BOOT_SCREEN");
        const bool enabled = !(v && std::strcmp(v, "0") == 0);
        if (!enabled) std::fprintf(stderr, "[boot] loading screen off (LBP3_BOOT_SCREEN=0)\n");
        return enabled;
    }();
    return on;
}

std::mutex g_artMutex;
std::filesystem::path g_bgPath, g_iconPath;

std::atomic<int> g_gpu{0};  // 0 = not started, 1 = starting, 2 = ready
std::atomic<bool> g_armed{false};  // SetArt was called: the game itself is starting (not a test program)

// Presenter thread.
bool g_gone = false;
bool g_started = false;
Clock::time_point g_shownAt;  // the window's first frame with the screen
bool g_haveFirstFrame = false;
Clock::time_point g_firstFrameAt;  // the game's first frame (black or not)
bool g_fading = false;
Clock::time_point g_fadeAt;
bool g_triedArt = false;
SDL_Texture* g_bg = nullptr;
SDL_Texture* g_icon = nullptr;
float g_bgW = 0, g_bgH = 0;

constexpr double kFadeSeconds = 0.35;
constexpr double kBlackCapSeconds = 60.0;  // after the game's first frame
constexpr double kSlowHintSeconds = 12.0;

double Since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

SDL_Texture* LoadPng(const std::filesystem::path& path, float* w, float* h) {
    SDL_Renderer* r = overlay::detail::Renderer();
    if (path.empty() || !r) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;
    const std::u8string u8 = path.u8string();
    const std::string p(u8.begin(), u8.end());
    SDL_Surface* surface = SDL_LoadPNG(p.c_str());
    if (!surface) {
        std::fprintf(stderr, "[boot] %s not loaded: %s\n", p.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surface);
    if (t) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        if (w) *w = static_cast<float>(surface->w);
        if (h) *h = static_cast<float>(surface->h);
    }
    SDL_DestroySurface(surface);
    return t;
}

void DropArt() {
    if (g_bg) SDL_DestroyTexture(g_bg);
    if (g_icon) SDL_DestroyTexture(g_icon);
    g_bg = g_icon = nullptr;
    g_triedArt = false;
}

void LoadArt() {
    if (g_triedArt) return;
    g_triedArt = true;
    std::filesystem::path bg, icon;
    {
        std::lock_guard lock(g_artMutex);
        bg = g_bgPath;
        icon = g_iconPath;
    }
    g_bg = LoadPng(bg, &g_bgW, &g_bgH);
    g_icon = LoadPng(icon, nullptr, nullptr);
    std::fprintf(stderr, "[boot] loading screen up (background %s, icon %s)\n", g_bg ? "from the game" : "plain",
                 g_icon ? "from the game" : "none");
}

ImTextureRef Ref(SDL_Texture* t) { return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t))); }

ImU32 White(float a) { return IM_COL32(255, 255, 255, static_cast<int>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f)); }
ImU32 Grey(int v, float a) { return IM_COL32(v, v, v, static_cast<int>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f)); }

std::string Thousands(unsigned v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return s;
}

// What the status line says, and the bar's fill (negative = no count: the bar sweeps).
struct Status {
    const char* text;
    std::string count;
    float fill;
};

Status CurrentStatus() {
    const unsigned phase = Lbp3Boot::g_phase.load(std::memory_order_acquire);
    const unsigned done = Lbp3Boot::g_done.load(std::memory_order_relaxed);
    const unsigned total = Lbp3Boot::g_total.load(std::memory_order_relaxed);
    const auto counted = [&](const char* text) {
        if (total == 0) return Status{text, {}, -1.f};
        const unsigned d = std::min(done, total);
        const unsigned pct = static_cast<unsigned>(100.0 * d / total);
        return Status{text, Thousands(d) + " / " + Thousands(total) + "   " + std::to_string(pct) + "%",
                      static_cast<float>(d) / static_cast<float>(total)};
    };
    switch (phase) {
    case Lbp3Boot::kDriverCache: return {"Loading the graphics driver's cache", {}, -1.f};
    case Lbp3Boot::kReading: return counted("Loading shaders");
    case Lbp3Boot::kBuilding: return counted("Preparing shaders");
    case Lbp3Boot::kDone: return {"Starting LittleBigPlanet 3", {}, -1.f};
    default: break;
    }
    if (g_gpu.load() == 2) return {"Starting LittleBigPlanet 3", {}, -1.f};
    if (g_gpu.load() == 1) return {"Starting the graphics device", {}, -1.f};
    return {"Starting", {}, -1.f};
}

}  // namespace

void SetArt(const std::filesystem::path& background, const std::filesystem::path& icon) {
    std::lock_guard lock(g_artMutex);
    g_bgPath = background;
    g_iconPath = icon;
    g_armed = true;
}

void NoteGpuInit(bool finished) { g_gpu = finished ? 2 : 1; }

void NoteFrame(const void* pixels, unsigned width, unsigned height, unsigned pitchPixels) {
    if (!Enabled() || g_gone || g_fading || !g_started || !pixels || width < 2 || height < 2) return;
    if (!g_haveFirstFrame) {
        g_haveFirstFrame = true;
        g_firstFrameAt = Clock::now();
        std::fprintf(stderr, "[boot] the game's first frame after %.1f s\n", Since(g_shownAt));
    }
    // A picture, not a black frame: any of a 32x18 grid of samples above a dark grey.
    const auto* px = static_cast<const uint8_t*>(pixels);
    bool picture = false;
    for (unsigned gy = 0; gy < 18 && !picture; ++gy) {
        const unsigned y = (2 * gy + 1) * height / 36;
        for (unsigned gx = 0; gx < 32; ++gx) {
            const unsigned x = (2 * gx + 1) * width / 64;
            const uint8_t* p = px + (static_cast<size_t>(y) * pitchPixels + x) * 4;
            if (std::max({p[0], p[1], p[2]}) > 16) {
                picture = true;
                break;
            }
        }
    }
    const bool capped = Since(g_firstFrameAt) > kBlackCapSeconds;
    if (picture || capped) {
        g_fading = true;
        g_fadeAt = Clock::now();
        std::fprintf(stderr, "[boot] %s after %.1f s: loading screen off\n",
                     picture ? "the game's first picture" : "60 s of black frames", Since(g_shownAt));
    }
}

namespace layer {

bool Wanted() {
    if (!g_armed.load() || !Enabled() || g_gone) return false;
    if (!g_started) {
        g_started = true;
        g_shownAt = Clock::now();
    }
    if (g_fading && Since(g_fadeAt) >= kFadeSeconds) {
        g_gone = true;
        DropArt();
        return false;
    }
    return true;
}

void Reset() { DropArt(); }

void Frame(ImVec2 d) {
    if (d.x < 8.f || d.y < 8.f) return;
    LoadArt();
    const float a = g_fading ? static_cast<float>(1.0 - Since(g_fadeAt) / kFadeSeconds) : 1.f;
    if (a <= 0.f) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float u = d.y / 1080.f;  // layout unit: 1 px at 1080p

    // The background: the game's art, filling the window (cropped, not stretched).
    if (g_bg && g_bgW > 0 && g_bgH > 0) {
        dl->AddRectFilled({0, 0}, d, Grey(0, a));
        const float s = std::max(d.x / g_bgW, d.y / g_bgH);
        const ImVec2 size{g_bgW * s, g_bgH * s};
        const ImVec2 p0{(d.x - size.x) * 0.5f, (d.y - size.y) * 0.5f};
        dl->AddImage(Ref(g_bg), p0, {p0.x + size.x, p0.y + size.y}, {0, 0}, {1, 1}, White(a));
    } else {
        dl->AddRectFilledMultiColor({0, 0}, d, IM_COL32(28, 44, 78, int(255 * a)), IM_COL32(28, 44, 78, int(255 * a)),
                                    IM_COL32(8, 10, 18, int(255 * a)), IM_COL32(8, 10, 18, int(255 * a)));
    }
    // A dark band at the bottom for the text.
    const float bandTop = d.y * 0.5f;
    dl->AddRectFilledMultiColor({0, bandTop}, d, Grey(0, 0.f), Grey(0, 0.f), Grey(0, 0.92f * a), Grey(0, 0.92f * a));
    // Text with a soft shadow, so it reads over any part of the art.
    const auto text = [&](ImFont* f, float size, ImVec2 at, ImU32 col, const char* s) {
        dl->AddText(f, size, {at.x + 2.f * u, at.y + 2.f * u}, Grey(0, 0.55f * a), s);
        dl->AddText(f, size, at, col, s);
    };

    const float margin = 64.f * u;
    const float iconSize = 120.f * u;
    const float bottom = d.y - margin;
    float x = margin;
    if (g_icon) {
        const ImVec2 p0{margin, bottom - iconSize};
        dl->AddRectFilled({p0.x - 3 * u, p0.y - 3 * u}, {p0.x + iconSize + 3 * u, bottom + 3 * u}, Grey(0, 0.35f * a),
                          18.f * u);
        dl->AddImageRounded(Ref(g_icon), p0, {p0.x + iconSize, bottom}, {0, 0}, {1, 1}, White(a), 16.f * u);
        x += iconSize + 32.f * u;
    }
    const float right = d.x - margin;

    ImFont* title = overlay::detail::KeyFont();
    ImFont* body = overlay::detail::Font();
    const float titleSize = 44.f * u, bodySize = 27.f * u, hintSize = 21.f * u;
    const float top = g_icon ? bottom - iconSize : bottom - 118.f * u;
    text(title, titleSize, {x, top}, White(a), "LittleBigPlanet 3");

    const Status st = CurrentStatus();
    // "Starting", "Loading shaders"... with a dot animation while there's no count.
    std::string line = st.text;
    if (st.fill < 0.f) {
        const int dots = static_cast<int>(Since(g_shownAt) * 2.5) % 4;
        line.append(static_cast<size_t>(dots), '.');
    }
    const float statusY = top + titleSize + 6.f * u;
    text(body, bodySize, {x, statusY}, Grey(235, a), line.c_str());
    if (!st.count.empty()) {
        const ImVec2 sz = body->CalcTextSizeA(bodySize, FLT_MAX, 0.f, st.count.c_str());
        text(body, bodySize, {right - sz.x, statusY}, Grey(235, a), st.count.c_str());
    }

    // The bar: filled to the count, or a sweeping segment when there's none.
    const float barH = 8.f * u, barY = bottom - barH, round = barH * 0.5f;
    dl->AddRectFilled({x, barY}, {right, bottom}, White(0.18f * a), round);
    if (st.fill >= 0.f) {
        const float fx = x + (right - x) * std::clamp(st.fill, 0.f, 1.f);
        if (fx > x + 1.f) dl->AddRectFilled({x, barY}, {fx, bottom}, White(0.92f * a), round);
    } else {
        const float w = (right - x) * 0.22f;
        const double period = 1.6;
        const float t = static_cast<float>(std::fmod(Since(g_shownAt), period) / period);
        const float s0 = x - w + (right - x + w) * t;
        dl->PushClipRect({x, barY}, {right, bottom}, true);
        dl->AddRectFilled({s0, barY}, {s0 + w, bottom}, White(0.75f * a), round);
        dl->PopClipRect();
    }

    // Reading the shader cache after a PC restart is slow (Windows reads each file from disk).
    if (Lbp3Boot::g_phase.load(std::memory_order_relaxed) == Lbp3Boot::kReading && Since(g_shownAt) > kSlowHintSeconds) {
        const char* hint = "The first start after the PC restarts takes longer while Windows reads the shader cache.";
        text(body, hintSize, {x, bottom + 12.f * u}, Grey(205, 0.85f * a), hint);
    }
}

}  // namespace layer
}  // namespace ui::boot
