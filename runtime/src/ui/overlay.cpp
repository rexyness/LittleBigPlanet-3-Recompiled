// The overlay host (ui/overlay.h): the ImGui context, the fonts and the frame, shared by the
// keyboard and dialog layers (ui/overlay_layers.h).
#include "ui/overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <iterator>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ui/boot_overlay.h"
#include "ui/overlay_layers.h"

namespace ui::overlay {
namespace {

std::atomic<bool> g_attached{false};
std::atomic<uint64_t> g_stamp{0};

// Presenter thread.
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
bool g_imgui = false;
ImFont* g_font = nullptr;
ImFont* g_fontKeys = nullptr;
std::vector<SDL_Scancode> g_dialogKeys;  // see HandleEvent

bool FileExists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

ImFont* LoadFont(const std::string& fonts, const char* file) {
    const std::string main = fonts + file, symbols = fonts + "seguisym.ttf";
    if (!FileExists(main)) return nullptr;
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(main.c_str(), 0.0f);
    if (font && FileExists(symbols)) {  // currency signs and the like
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(symbols.c_str(), 0.0f, &merge);
    }
    return font;
}

void ShutdownImGui() {
    if (!g_imgui) return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_imgui = false;
    g_font = g_fontKeys = nullptr;
}

// The newest layer that is up takes the input; returns false if none is.
struct Up {
    uint64_t osk = 0, dialog = 0, menu = 0;
    bool Any() const { return osk || dialog || menu; }
    bool OskTop() const { return osk > dialog && osk > menu; }
    bool MenuTop() const { return menu > dialog && menu > osk; }
    bool OthersUp() const { return osk || dialog; }
};

Up SyncLayers() { return {osk::layer::Sync(), dialog::layer::Sync(), menu::layer::Sync()}; }

bool Wanted() { return osk::layer::Wanted() || dialog::layer::Wanted() || menu::layer::Wanted(); }

}  // namespace

namespace detail {

uint64_t NextStamp() { return ++g_stamp; }

void EnsureImGui() {
    if (g_imgui || !g_window || !g_renderer) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);
    // The pad belongs to input::Read; ImGui must not open or navigate with it.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    // Text input is started and stopped by the keyboard layer, not by ImGui widgets.
    ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;

    const char* windir = std::getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    g_font = LoadFont(fonts, "segoeui.ttf");
    g_fontKeys = LoadFont(fonts, "seguisb.ttf");
    if (!g_font) {
        std::fprintf(stderr, "[overlay] Segoe UI not found; using ImGui's built-in font\n");
        g_font = io.Fonts->AddFontDefault();
    }
    if (!g_fontKeys) g_fontKeys = g_font;
    g_imgui = true;
    std::fprintf(stderr, "[overlay] ImGui %s ready on the %s renderer\n", IMGUI_VERSION, SDL_GetRendererName(g_renderer));
}

bool ImGuiReady() { return g_imgui; }
SDL_Window* Window() { return g_window; }
SDL_Renderer* Renderer() { return g_renderer; }
ImFont* Font() { return g_font; }
ImFont* KeyFont() { return g_fontKeys; }

void ReleaseMouse() {
    if (g_imgui) ImGui::GetIO().ClearInputMouse();
    SDL_CaptureMouse(false);
}

}  // namespace detail

bool Available() { return g_attached.load(); }

void Attach(SDL_Window* window, SDL_Renderer* renderer) {
    if (window != g_window || renderer != g_renderer) {
        // A different window (the test programs make several): ImGui's backends and any
        // textures belong to the old one.
        osk::layer::Reset();
        dialog::layer::Reset();
        menu::layer::Reset();
        boot::layer::Reset();
        ShutdownImGui();
    }
    g_window = window;
    g_renderer = renderer;
    g_attached = window && renderer;
}

bool HandleEvent(const SDL_Event& e) {
    // Keys pressed while a dialog had the input, until released. Their text events come after
    // the key-down, when a dialog the key answered (Space) may be gone and the keyboard under
    // it on top again: that text must not be typed into it.
    if (e.type == SDL_EVENT_KEY_UP) std::erase(g_dialogKeys, e.key.scancode);
    Up up;
    if (Wanted()) up = SyncLayers();
    if (menu::layer::HotkeyEvent(e, up.OthersUp())) return true;  // Insert opens the in-game menu
    if (Wanted()) up = SyncLayers();
    if (!up.Any()) {
        // The release of a click that closed the last layer: the SDL3 backend still counts the
        // button as down until it sees it.
        if (g_imgui && e.type == SDL_EVENT_MOUSE_BUTTON_UP) ImGui_ImplSDL3_ProcessEvent(&e);
        return false;
    }
    if (e.type == SDL_EVENT_TEXT_INPUT && !g_dialogKeys.empty()) return true;
    if (g_imgui) ImGui_ImplSDL3_ProcessEvent(&e);  // the mouse
    if (up.MenuTop()) return menu::layer::Event(e);
    if (up.OskTop()) return osk::layer::Event(e);
    const bool used = dialog::layer::Event(e);
    if (used && e.type == SDL_EVENT_KEY_DOWN &&
        std::find(g_dialogKeys.begin(), g_dialogKeys.end(), e.key.scancode) == g_dialogKeys.end())
        g_dialogKeys.push_back(e.key.scancode);
    return used;
}

void Draw() {
    // The in-game menu's pad hotkey (L3+R3) is looked for even with nothing up.
    if (g_attached.load()) {
        Up before;
        if (Wanted()) before = SyncLayers();
        menu::layer::PollHotkey(before.OthersUp());
    }
    const bool hud = menu::layer::Hud();
    const bool bootScreen = boot::layer::Wanted();  // the loading screen until the game's first picture
    if (!Wanted() && !hud && !bootScreen) return;
    if (hud || bootScreen) detail::EnsureImGui();
    Up up = SyncLayers();
    if (!up.Any() && !hud && !bootScreen) return;
    // The pad: the top layer acts on it; the others only note that they aren't on top.
    const bool oskTop = up.OskTop(), menuTop = up.MenuTop();
    if (up.osk) osk::layer::Poll(oskTop);
    if (up.dialog) dialog::layer::Poll(!oskTop && !menuTop);
    if (up.menu) menu::layer::Poll(menuTop);
    up = SyncLayers();  // a layer may have answered
    if ((!up.Any() && !hud && !bootScreen) || !g_imgui) return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (bootScreen) boot::layer::Frame(display);  // ImGui's background list: under everything else
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (up.Any()) {
        dl->AddRectFilled({0.f, 0.f}, display, detail::kDim);
        // Oldest first, so the newest is drawn on top; each one underneath is dimmed once more.
        struct L { uint64_t stamp; int which; };
        L order[3] = {{up.osk, 0}, {up.dialog, 1}, {up.menu, 2}};
        std::sort(std::begin(order), std::end(order), [](const L& a, const L& b) { return a.stamp < b.stamp; });
        int last = -1;
        for (int i = 0; i < 3; ++i)
            if (order[i].stamp) last = i;
        for (int i = 0; i < 3; ++i) {
            if (!order[i].stamp) continue;
            const bool top = i == last;
            switch (order[i].which) {
            case 0: osk::layer::Frame(top, display); break;
            case 1: dialog::layer::Frame(top, display); break;
            default: menu::layer::Frame(top, display); break;
            }
            if (!top) dl->AddRectFilled({0.f, 0.f}, display, detail::kDim);
        }
    }
    menu::layer::DrawHudLayer(display);  // the corner stats, when on and the menu is closed
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

}  // namespace ui::overlay
