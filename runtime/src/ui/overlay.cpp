// The overlay host (ui/overlay.h): the ImGui context, the fonts and the frame, shared by the
// keyboard and dialog layers (ui/overlay_layers.h).
#include "ui/overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

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
    uint64_t osk = 0, dialog = 0;
    bool Any() const { return osk || dialog; }
    bool OskTop() const { return osk > dialog; }
};

Up SyncLayers() { return {osk::layer::Sync(), dialog::layer::Sync()}; }

bool Wanted() { return osk::layer::Wanted() || dialog::layer::Wanted(); }

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
        ShutdownImGui();
    }
    g_window = window;
    g_renderer = renderer;
    g_attached = window && renderer;
}

bool HandleEvent(const SDL_Event& e) {
    Up up;
    if (Wanted()) up = SyncLayers();
    if (!up.Any()) {
        // The release of a click that closed the last layer: the SDL3 backend still counts the
        // button as down until it sees it.
        if (g_imgui && e.type == SDL_EVENT_MOUSE_BUTTON_UP) ImGui_ImplSDL3_ProcessEvent(&e);
        return false;
    }
    if (g_imgui) ImGui_ImplSDL3_ProcessEvent(&e);  // the mouse
    return up.OskTop() ? osk::layer::Event(e) : dialog::layer::Event(e);
}

void Draw() {
    if (!Wanted()) return;
    Up up = SyncLayers();
    if (!up.Any()) return;
    // The pad: the top layer acts on it; the other only notes that it isn't on top.
    const bool oskTop = up.OskTop();
    if (up.osk) osk::layer::Poll(oskTop);
    if (up.dialog) dialog::layer::Poll(!oskTop);
    up = SyncLayers();  // a layer may have answered
    if (!up.Any() || !g_imgui) return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetForegroundDrawList()->AddRectFilled({0.f, 0.f}, display, detail::kDim);
    // Oldest first, so the newest is drawn on top; the one underneath is dimmed once more.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (up.OskTop()) {
        if (up.dialog) {
            dialog::layer::Frame(false, display);
            dl->AddRectFilled({0.f, 0.f}, display, detail::kDim);
        }
        osk::layer::Frame(true, display);
    } else {
        if (up.osk) {
            osk::layer::Frame(false, display);
            dl->AddRectFilled({0.f, 0.f}, display, detail::kDim);
        }
        dialog::layer::Frame(true, display);
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

}  // namespace ui::overlay
