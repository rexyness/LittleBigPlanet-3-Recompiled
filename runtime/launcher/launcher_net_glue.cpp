// The few shadPS4 functions the shadNet client (runtime/src/net/shadnet) calls outside itself, for
// the launcher: its log goes to analysis\launcher_shadnet_log.txt beside the game's logs (warnings
// and errors only; the client never logs the password), and its thread names to Windows.
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "common/thread.h"

namespace launcher {
// Set by the launcher once it knows the game folder (empty: the log is dropped).
std::string g_shadnetLogPath;
}  // namespace launcher

namespace Common::Log {

std::array<Level, NUM_LOG_CLASSES> g_class_levels = [] {
    std::array<Level, NUM_LOG_CLASSES> a{};
    a.fill(Level::Warning);
    return a;
}();

void VLog(Class, Level level, const char* file, int line, const char* func, fmt::string_view format, fmt::format_args args) {
    static const char* const kNames[] = {"trace", "debug", "info", "warning", "error", "critical", "off"};
    if (launcher::g_shadnetLogPath.empty()) return;
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (std::FILE* f = std::fopen(launcher::g_shadnetLogPath.c_str(), "a")) {
        const char* slash = std::strrchr(file, '\\');
        if (!slash) slash = std::strrchr(file, '/');
        std::fprintf(f, "[shadnet %s] %s:%d %s: %s\n", kNames[static_cast<int>(level)], slash ? slash + 1 : file, line, func,
                     fmt::vformat(format, args).c_str());
        std::fclose(f);
    }
}

}  // namespace Common::Log

namespace Common {

void SetCurrentThreadName(const char* name) {
    wchar_t w[64];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64);
    SetThreadDescription(GetCurrentThread(), w);
}

}  // namespace Common
