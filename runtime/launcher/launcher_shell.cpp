// Windows shell bits for the launcher (launcher_shell.h).
#include "launcher_shell.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>

// stb_image (public domain / MIT, shadPS4's copy in externals/stb) decodes the dump's PNGs.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

namespace launcher {
namespace fs = std::filesystem;

void SetTaskbarIdentity() { SetCurrentProcessExplicitAppUserModelID(kAppId); }

fs::path ShortcutPath(ShortcutPlace where) {
    PWSTR dir = nullptr;
    const KNOWNFOLDERID& id = where == ShortcutPlace::Desktop ? FOLDERID_Desktop : FOLDERID_Programs;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &dir))) return {};
    fs::path p = fs::path(dir) / L"LittleBigPlanet 3 Launcher.lnk";
    CoTaskMemFree(dir);
    return p;
}

fs::path CreateShortcut(ShortcutPlace where, const fs::path& target, std::string* error) {
    const fs::path lnk = ShortcutPath(where);
    if (lnk.empty()) {
        if (error) *error = "couldn't find the folder";
        return {};
    }
    return CreateShortcutAt(lnk, target, error) ? lnk : fs::path();
}

bool CreateShortcutAt(const fs::path& lnk, const fs::path& target, std::string* error) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        if (error) *error = "the shell's shortcut maker isn't available";
        return false;
    }
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(target.parent_path().c_str());
    link->SetIconLocation(target.c_str(), 0);
    link->SetDescription(L"Start LittleBigPlanet 3 (native port) with its settings");
    // The same taskbar identity as the running launcher.
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&props)))) {
        PROPVARIANT v;
        if (SUCCEEDED(InitPropVariantFromString(kAppId, &v))) {
            props->SetValue(PKEY_AppUserModel_ID, v);
            props->Commit();
            PropVariantClear(&v);
        }
        props->Release();
    }
    IPersistFile* file = nullptr;
    HRESULT hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Save(lnk.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    if (FAILED(hr)) {
        if (error) *error = "couldn't save " + lnk.string();
        return false;
    }
    return true;
}

Image LoadImageFile(const fs::path& path) {
    Image img;
    int n = 0;
    const std::u8string name = path.u8string();  // stb_image takes UTF-8 names (STBI_WINDOWS_UTF8)
    stbi_uc* px = stbi_load(reinterpret_cast<const char*>(name.c_str()), &img.w, &img.h, &n, 4);
    if (!px) return Image{};
    img.rgba.assign(px, px + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(px);
    return img;
}

Image LoadImageMemory(const void* data, size_t size) {
    Image img;
    int n = 0;
    stbi_uc* px = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &img.w, &img.h, &n, 4);
    if (!px) return Image{};
    img.rgba.assign(px, px + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(px);
    return img;
}

Image Downscale(const Image& in, int max_w, int max_h) {
    if (!in.ok() || (in.w <= max_w && in.h <= max_h)) return in;
    const double s = std::min(double(max_w) / in.w, double(max_h) / in.h);
    Image out;
    out.w = std::max(1, int(in.w * s));
    out.h = std::max(1, int(in.h * s));
    out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
    for (int y = 0; y < out.h; ++y) {
        const int y0 = int(y / s), y1 = std::min(in.h, std::max(y0 + 1, int((y + 1) / s)));
        for (int x = 0; x < out.w; ++x) {
            const int x0 = int(x / s), x1 = std::min(in.w, std::max(x0 + 1, int((x + 1) / s)));
            unsigned sum[4] = {0, 0, 0, 0}, count = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const std::uint8_t* p = &in.rgba[(static_cast<size_t>(yy) * in.w + xx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                    ++count;
                }
            std::uint8_t* q = &out.rgba[(static_cast<size_t>(y) * out.w + x) * 4];
            for (int c = 0; c < 4; ++c) q[c] = static_cast<std::uint8_t>(sum[c] / std::max(1u, count));
        }
    }
    return out;
}

}  // namespace launcher
