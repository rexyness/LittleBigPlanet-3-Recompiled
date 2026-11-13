// Windows shell bits for the launcher: shortcuts (on the desktop and in the Start menu, only when
// the player asks), the taskbar identity, and images from the game dump (decoded at run time; the
// game's art is never bundled with the launcher).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

// The launcher's taskbar identity (the same one its shortcuts carry, so a pinned shortcut and
// the running window group together).
constexpr wchar_t kAppId[] = L"LBP3Port.Launcher";
void SetTaskbarIdentity();

enum class ShortcutPlace { Desktop, StartMenu };
// "LittleBigPlanet 3 Launcher.lnk" pointing at `target` (its icon, its folder as the working one).
// Returns the shortcut's path, or empty with *error set.
std::filesystem::path CreateShortcut(ShortcutPlace where, const std::filesystem::path& target, std::string* error);
std::filesystem::path ShortcutPath(ShortcutPlace where);
// The same, at an exact .lnk path (the tests use a temporary folder).
bool CreateShortcutAt(const std::filesystem::path& lnk, const std::filesystem::path& target, std::string* error);

// An image file (PNG, JPEG...) as RGBA8 pixels; empty on failure.
struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    bool ok() const { return w > 0 && h > 0; }
};
Image LoadImageFile(const std::filesystem::path& path);
// The same image scaled down to fit max_w x max_h (box filter; returns it unchanged if smaller).
Image Downscale(const Image& in, int max_w, int max_h);

}  // namespace launcher
