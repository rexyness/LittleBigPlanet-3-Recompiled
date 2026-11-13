// LBP3Launcher's Windows side (launcher_sys.h).
#include "launcher_sys.h"

#include "launcher_account.h"  // ParseAccountFile
#include "launcher_quitmenu.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace launcher {
namespace {

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string U(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string FirstLine(const std::string& text) {
    std::string line = text.substr(0, text.find('\n'));
    while (!line.empty() && static_cast<unsigned char>(line.back()) <= ' ') line.pop_back();
    return line;
}

std::int64_t WriteTime(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

std::string Stamp(std::int64_t t, const char* fmt) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

// The game folder the launcher would find with no setting.
fs::path AutoRoot(const fs::path& launcher_dir) { return FindRoot(launcher_dir, ""); }

bool IsGameRoot(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && (fs::exists(dir / "game_dir.txt", ec) || fs::exists(dir / "user" / "savedata", ec));
}

// Files below `dir`, and their total size.
void CountTree(const fs::path& dir, std::uintmax_t& files, std::uintmax_t& bytes) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            ++files;
            bytes += it->file_size(ec);
        }
    }
}

bool CopyTreeChecked(const fs::path& from, const fs::path& to, std::string* error) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        if (error) *error = "copying " + from.string() + " failed: " + ec.message();
        return false;
    }
    std::uintmax_t f1 = 0, b1 = 0, f2 = 0, b2 = 0;
    CountTree(from, f1, b1);
    CountTree(to, f2, b2);
    if (f1 != f2 || b1 != b2) {
        if (error) *error = "the copy of " + from.string() + " doesn't match (files or sizes differ)";
        return false;
    }
    return true;
}

std::string RegString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[512];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return U(buf);
    return {};
}

std::string SystemInfo() {
    std::ostringstream o;
    const wchar_t* nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    o << "Windows: " << RegString(HKEY_LOCAL_MACHINE, nt, L"ProductName") << " "
      << RegString(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion") << " build "
      << RegString(HKEY_LOCAL_MACHINE, nt, L"CurrentBuild") << "\n";
    o << "CPU: "
      << RegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString")
      << "\n";
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    o << "Logical processors: " << si.dwNumberOfProcessors << "\n";
    MEMORYSTATUSEX mem{sizeof mem};
    if (GlobalMemoryStatusEx(&mem)) o << "RAM: " << (mem.ullTotalPhys >> 20) << " MB\n";
    DISPLAY_DEVICEW dd{sizeof dd};
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) o << "Display adapter: " << U(dd.DeviceString) << "\n";
        dd = DISPLAY_DEVICEW{sizeof dd};
    }
    return o.str();
}

void WriteText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
}

}  // namespace

std::string NowStamp() {
    return Stamp(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
                 "%Y%m%d_%H%M%S");
}

std::string ReadFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

fs::path LauncherDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

fs::path MainCheckoutFromGitFile(const std::string& text) {
    std::string line = FirstLine(text);
    const std::string tag = "gitdir:";
    if (line.compare(0, tag.size(), tag) != 0) return {};
    std::string dir = line.substr(tag.size());
    while (!dir.empty() && dir.front() == ' ') dir.erase(dir.begin());
    std::replace(dir.begin(), dir.end(), '\\', '/');
    const size_t at = Lower(dir).rfind("/.git/worktrees/");
    if (at == std::string::npos) return {};
    return fs::path(dir.substr(0, at)).make_preferred();
}

bool Portable(const fs::path& launcher_dir) {
    std::error_code ec;
    return fs::exists(launcher_dir / "portable.txt", ec);
}

fs::path FindRoot(const fs::path& launcher_dir, const std::string& setting) {
    if (!setting.empty()) return fs::path(setting);
    std::error_code pec;
    // The portable package (tools/make_package.ps1): everything sits in the launcher's own folder, marked by
    // portable.txt; user\, analysis\ and game_dir.txt are made there on first use.
    if (fs::exists(launcher_dir / "portable.txt", pec)) return launcher_dir;
    const fs::path parent = launcher_dir.parent_path();
    if (IsGameRoot(parent)) return parent;
    std::error_code ec;
    if (fs::is_regular_file(parent / ".git", ec)) {
        const fs::path main = MainCheckoutFromGitFile(ReadFile(parent / ".git"));
        if (IsGameRoot(main)) return main;
    }
    return {};
}

Paths ResolvePaths(const Settings& s) {
    Paths p;
    p.launcher_dir = LauncherDir();
    p.root = FindRoot(p.launcher_dir, s.game_root);
    const fs::path port_exe = p.root.empty() ? fs::path() : p.root / "build" / "lbp3.exe";
    const fs::path best_exe = p.root.empty() ? fs::path() : p.root.parent_path() / "lbp3-wt" / "best" / "build" / "lbp3.exe";
    switch (s.build) {
    case Build::Best: p.exe = best_exe; break;
    case Build::Port: p.exe = port_exe; break;
    case Build::Custom: p.exe = fs::path(s.exe_path); break;
    default: p.exe = p.launcher_dir / "lbp3.exe"; break;
    }
    p.worktree = p.exe.parent_path().parent_path();
    // the dump: LBP3_GAME_DIR, else game_dir.txt's first line, else ..\LBP3 Game Files (as lbp3.exe)
    if (const char* v = std::getenv("LBP3_GAME_DIR"); v && *v) p.game_dir = v;
    else if (!p.root.empty()) {
        const std::string line = FirstLine(ReadFile(p.root / "game_dir.txt"));
        p.game_dir = line.empty() ? p.root.parent_path() / "LBP3 Game Files" : fs::path(line);
    }
    if (!p.game_dir.empty()) p.dlc_dir = p.game_dir / "CUSA00473-DLC";
    if (!s.dlc_dir.empty()) p.dlc_dir = fs::path(s.dlc_dir);
    std::error_code ec;
    const bool is_port = !port_exe.empty() && fs::equivalent(p.exe, port_exe, ec);
    const fs::path own = p.root / "user" / "shader_cache_best";
    if (!p.root.empty()) {
        if (s.cache == CacheChoice::Own || (s.cache == CacheChoice::Auto && !is_port && fs::exists(own / "CUSA00473", ec)))
            p.cache_dir = own;
        p.saves = p.root / "user" / "savedata" / "CUSA00473";
        p.backups = p.root / "analysis" / "savedata_backups";
    }
    const std::string exe_l = Lower(p.exe.string());
    if (is_port) p.log_base = "play";
    else if (exe_l.find("\\lbp3-wt\\best\\build\\") != std::string::npos) p.log_base = "play_best";
    else p.log_base = "play_launcher";
    return p;
}

std::vector<Check> RunChecks(const Settings& s, const Paths& p) {
    std::vector<Check> c;
    std::error_code ec;
    c.push_back({!p.root.empty() && fs::exists(p.root, ec), "Game folder (saves, logs)",
                 p.root.empty() ? "not found: set it on the Help page" : p.root.string()});
    c.push_back({fs::exists(p.exe, ec), "lbp3.exe", p.exe.empty() ? "no build chosen" : p.exe.string()});
    const bool dump = !p.game_dir.empty() && fs::exists(p.game_dir / "CUSA00473" / "eboot.bin", ec);
    c.push_back({dump, "Game files (the dump)",
                 dump ? p.game_dir.string()
                      : (p.game_dir.empty() ? "unknown (no game_dir.txt)" : p.game_dir.string() + " has no CUSA00473\\eboot.bin")});
    const bool dlc = !p.dlc_dir.empty() && fs::exists(p.dlc_dir, ec);
    c.push_back({dlc, "DLC folder", dlc ? p.dlc_dir.string() : "none (the game runs without DLC)"});
    if (!p.cache_dir.empty())
        c.push_back({fs::exists(p.cache_dir / "CUSA00473", ec), "This build's shader cache", p.cache_dir.string()});
    if (s.quit_menu) {
        const bool files = !QuitMenuFolder(p.game_dir).empty();
        c.push_back({files, "Quit Game setup",
                     files ? "built in (written before Play)" : "the game files have no patch_001.farc (the in-game menu can still quit)"});
    }
    if (s.online) {
        const Settings e = EffectiveOnline(s, p);  // the active account file decides
        const bool ok = !NormalizeServer(e.online_server).empty() && ValidOnlineName(e.online_name);
        c.push_back({ok, "Online", ok ? NormalizeServer(e.online_server) + " as " + e.online_name
                                      : "needs an online account (Online page), or a server address and a name"});
    }
    return c;
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{sizeof e};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, L"lbp3.exe") == 0) {
            found = true;
            break;
        }
    CloseHandle(snap);
    return found;
}

bool TestMarkerSet(const Paths& p) {
    std::error_code ec;
    return !p.root.empty() && fs::exists(p.root / "analysis" / "savedata_live_is_test", ec);
}

std::vector<Backup> ListBackups(const Paths& p) {
    std::vector<Backup> list;
    std::error_code ec;
    if (p.backups.empty() || !fs::exists(p.backups, ec)) return list;
    for (const auto& e : fs::directory_iterator(p.backups, ec))
        if (e.is_directory(ec)) list.push_back({e.path().filename().string(), e.path(), WriteTime(e.path())});
    std::sort(list.begin(), list.end(), [](const Backup& a, const Backup& b) { return a.time > b.time; });
    return list;
}

std::string BackupSaves(const Paths& p, const std::string& prefix, std::string* error) {
    std::error_code ec;
    if (p.saves.empty() || !fs::exists(p.saves, ec)) {
        if (error) *error = "no saves to back up (" + p.saves.string() + ")";
        return {};
    }
    const std::string name = prefix + "_" + NowStamp();
    if (!CopyTreeChecked(p.saves, p.backups / name, error)) return {};
    // The folder's time is its creation, which ListBackups sorts by.
    return name;
}

bool RestoreBackup(const Paths& p, const Backup& b, std::string* error, std::string* safety_backup) {
    std::error_code ec;
    if (!fs::exists(b.path, ec)) {
        if (error) *error = "the backup " + b.path.string() + " is gone";
        return false;
    }
    if (fs::exists(p.saves, ec)) {
        const std::string safety = BackupSaves(p, "before_restore", error);
        if (safety.empty()) return false;  // never replace saves that couldn't be backed up
        if (safety_backup) *safety_backup = safety;
    }
    const fs::path staged = p.saves.parent_path() / ("CUSA00473.restoring_" + NowStamp());
    if (!CopyTreeChecked(b.path, staged, error)) {
        fs::remove_all(staged, ec);
        return false;
    }
    const fs::path old = p.saves.parent_path() / ("CUSA00473.replaced_" + NowStamp());
    if (fs::exists(p.saves, ec)) {
        fs::rename(p.saves, old, ec);
        if (ec) {
            if (error) *error = "couldn't move the current saves aside: " + ec.message();
            fs::remove_all(staged, ec);
            return false;
        }
    }
    fs::rename(staged, p.saves, ec);
    if (ec) {
        if (error) *error = "couldn't put the backup in place: " + ec.message();
        std::error_code ec2;
        fs::rename(old, p.saves, ec2);  // the current saves back
        return false;
    }
    // The replaced saves are already in the safety backup.
    fs::remove_all(old, ec);
    return true;
}

void RotateLogs(const Paths& p) {
    std::error_code ec;
    const fs::path a = p.root / "analysis";
    fs::create_directories(a, ec);
    const fs::path log = a / (p.log_base + "_log.txt");
    if (fs::exists(log, ec)) {
        const fs::path keep = a / "play_logs" / (p.log_base + "_" + Stamp(WriteTime(log), "%Y%m%d_%H%M") + ".txt");
        fs::create_directories(keep.parent_path(), ec);
        if (!fs::exists(keep, ec)) fs::copy_file(log, keep, ec);
    }
    const fs::path l1 = a / (p.log_base + "_log.1.txt"), l2 = a / (p.log_base + "_log.2.txt");
    if (fs::exists(l1, ec)) fs::rename(l1, l2, ec);
    if (fs::exists(log, ec)) fs::rename(log, l1, ec);
}

fs::path LatestLog(const Paths& p) {
    std::error_code ec;
    const fs::path log = p.root / "analysis" / (p.log_base + "_log.txt");
    if (fs::exists(log, ec)) return log;
    const fs::path l1 = p.root / "analysis" / (p.log_base + "_log.1.txt");
    return fs::exists(l1, ec) ? l1 : fs::path();
}

bool PythonFound() {
    wchar_t buf[MAX_PATH];
    if (!SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr)) return false;
    // The Microsoft Store's stand-in (WindowsApps) only opens the Store.
    return Lower(U(buf)).find("\\windowsapps\\") == std::string::npos;
}

fs::path PythonPath() {
    wchar_t buf[MAX_PATH];
    if (!PythonFound() || !SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr)) return {};
    return buf;
}

EnvList QuitMenuSetup(const Paths& p, std::string* note) {
    // Native (launcher_quitmenu, a port of LBPPC's quit_menu.py): no Python needed.
    const fs::path folder = QuitMenuFolder(p.game_dir);
    if (folder.empty()) {
        if (note) *note = "Quit Game: not added (no patch_001.farc with output\\orbisguids.map in the game files)";
        return {};
    }
    const fs::path edits = p.root / "user" / "file_edits.bin";
    const QuitMenuResult r = WriteQuitMenuEdits(folder, edits);
    if (note) *note = r.message;
    if (!r.ok) return {};
    return {{"LBP3_FILE_EDITS", edits.string()}, {"LBP3_QUIT_MENU", "1"}};
}

fs::path AccountFilePath(const Paths& p) { return p.root.empty() ? fs::path() : p.root / "user" / "config" / "online_account.txt"; }

Settings EffectiveOnline(const Settings& s, const Paths& p) {
    Settings e = s;
    const fs::path f = AccountFilePath(p);
    std::error_code ec;
    if (f.empty() || !fs::exists(f, ec)) return e;  // no account file: the typed name and server
    // The active account file decides: its name and server (the game reads its password from it,
    // so a typed name must never be paired with another account's password).
    const AccountFile a = ParseAccountFile(ReadFile(f));
    if (!a.name.empty()) e.online_name = a.name;
    if (!a.server.empty()) e.online_server = a.server;
    return e;
}

EnvList GameEnvironment(const Settings& s, const Paths& p, const EnvList& extra) {
    EnvList env = MapSettings(EffectiveOnline(s, p)).env;
    // ResolvePaths already took LBP3_GAME_DIR from the launcher's own environment if it was set.
    if (!p.game_dir.empty()) env.emplace_back("LBP3_GAME_DIR", p.game_dir.string());
    if (!p.cache_dir.empty()) env.emplace_back("LBP3_SHADER_CACHE_DIR", p.cache_dir.string());
    // A DLC folder of the player's choosing (the runtime looks in <dump>\CUSA00473-DLC otherwise).
    if (!s.dlc_dir.empty()) env.emplace_back("LBP3_ADDCONT_DIR", s.dlc_dir);
    env.insert(env.end(), extra.begin(), extra.end());
    return env;
}

std::uintptr_t StartGame(const Paths& p, const EnvList& env, const std::vector<std::string>& args, std::string* error) {
    std::error_code ec;
    if (!fs::exists(p.exe, ec)) {
        if (error) *error = p.exe.string() + " not found";
        return 0;
    }
    const fs::path a = p.root / "analysis";
    fs::create_directories(a, ec);
    // The environment block: the launcher's own minus any LBP3_* (so nothing stale leaks in),
    // plus the game's, sorted by name as Windows prefers.
    std::vector<std::wstring> entries;
    {
        LPWCH block = GetEnvironmentStringsW();
        for (LPWCH e = block; *e; e += wcslen(e) + 1)
            if (_wcsnicmp(e, L"LBP3_", 5) != 0) entries.emplace_back(e);
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [k, v] : env) entries.push_back(W(k + "=" + v));
    std::stable_sort(entries.begin(), entries.end(), [](const std::wstring& x, const std::wstring& y) {
        const std::wstring kx = x.substr(0, x.find(L'=', 1)), ky = y.substr(0, y.find(L'=', 1));
        return _wcsicmp(kx.c_str(), ky.c_str()) < 0;
    });
    std::wstring block;
    for (const auto& e : entries) {
        block += e;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    const fs::path out = a / (p.log_base + "_stdout.txt"), err = a / (p.log_base + "_log.txt");
    HANDLE hout = CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE herr = CreateFileW(err.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE hin = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    std::wstring cmd = L"\"" + p.exe.wstring() + L"\"";
    for (const auto& arg : args) cmd += L" " + W(arg);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hin;
    si.hStdOutput = hout;
    si.hStdError = herr;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                                   block.data(), p.root.c_str(), &si, &pi);
    const DWORD last = GetLastError();
    for (HANDLE h : {hout, herr, hin})
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) {
        if (error) *error = "couldn't start " + p.exe.string() + " (Windows error " + std::to_string(last) + ")";
        return 0;
    }
    CloseHandle(pi.hThread);
    return reinterpret_cast<std::uintptr_t>(pi.hProcess);
}

long GameExitCode(std::uintptr_t process) {
    if (!process) return -1;
    DWORD code = 0;
    if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(process), &code) || code == STILL_ACTIVE) return -1;
    return static_cast<long>(code);
}

void CloseGameHandle(std::uintptr_t process) {
    if (process) CloseHandle(reinterpret_cast<HANDLE>(process));
}

void OpenPath(const fs::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::uint32_t Crc32(const void* data, size_t size) {
    static std::uint32_t table[256];
    static const bool ready = [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    std::uint32_t c = 0xFFFFFFFFu;
    const auto* b = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) c = table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool WriteZip(const fs::path& zip, const std::vector<std::pair<std::string, std::string>>& files, std::string* error) {
    std::string out, central;
    auto u16 = [](std::string& s, std::uint16_t v) {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>(v >> 8));
    };
    auto u32 = [&](std::string& s, std::uint32_t v) {
        u16(s, static_cast<std::uint16_t>(v & 0xFFFF));
        u16(s, static_cast<std::uint16_t>(v >> 16));
    };
    // DOS time of now
    SYSTEMTIME st;
    GetLocalTime(&st);
    const std::uint16_t dtime = static_cast<std::uint16_t>((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    const std::uint16_t ddate = static_cast<std::uint16_t>(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = Crc32(data.data(), data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        u32(out, 0x04034b50);
        u16(out, 20);      // version needed
        u16(out, 0x0800);  // UTF-8 names
        u16(out, 0);       // stored
        u16(out, dtime);
        u16(out, ddate);
        u32(out, crc);
        u32(out, static_cast<std::uint32_t>(data.size()));
        u32(out, static_cast<std::uint32_t>(data.size()));
        u16(out, static_cast<std::uint16_t>(name.size()));
        u16(out, 0);
        out += name;
        out += data;
        u32(central, 0x02014b50);
        u16(central, 20);  // made by
        u16(central, 20);
        u16(central, 0x0800);
        u16(central, 0);
        u16(central, dtime);
        u16(central, ddate);
        u32(central, crc);
        u32(central, static_cast<std::uint32_t>(data.size()));
        u32(central, static_cast<std::uint32_t>(data.size()));
        u16(central, static_cast<std::uint16_t>(name.size()));
        u16(central, 0);  // extra
        u16(central, 0);  // comment
        u16(central, 0);  // disk
        u16(central, 0);  // internal attributes
        u32(central, 0);  // external attributes
        u32(central, offset);
        central += name;
    }
    const std::uint32_t cd_offset = static_cast<std::uint32_t>(out.size());
    out += central;
    u32(out, 0x06054b50);
    u16(out, 0);
    u16(out, 0);
    u16(out, static_cast<std::uint16_t>(files.size()));
    u16(out, static_cast<std::uint16_t>(files.size()));
    u32(out, static_cast<std::uint32_t>(central.size()));
    u32(out, cd_offset);
    u16(out, 0);
    std::error_code ec;
    fs::create_directories(zip.parent_path(), ec);
    std::ofstream f(zip, std::ios::binary);
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!f) {
        if (error) *error = "couldn't write " + zip.string();
        return false;
    }
    return true;
}

fs::path MakeReport(const Settings& s, const Paths& p, std::string* error) {
    if (p.root.empty()) {
        if (error) *error = "no game folder";
        return {};
    }
    std::vector<std::pair<std::string, std::string>> files;
    std::error_code ec;
    const fs::path a = p.root / "analysis";
    for (const char* base : {"play_best", "play", "play_launcher"}) {
        for (const char* suffix : {"_log.txt", "_log.1.txt", "_stdout.txt"}) {
            const fs::path f = a / (std::string(base) + suffix);
            if (fs::exists(f, ec)) files.emplace_back(f.filename().string(), ReadFile(f));
        }
    }
    if (fs::exists(a / "launcher_log.txt", ec)) files.emplace_back("launcher_log.txt", ReadFile(a / "launcher_log.txt"));
    files.emplace_back("launcher.ini", ToIni(s));
    std::ostringstream info;
    info << "LBP3Launcher report, " << NowStamp() << "\n\n" << SystemInfo() << "\n";
    info << "Game folder: " << p.root.string() << "\nlbp3.exe: " << p.exe.string() << "\nGame files: "
         << p.game_dir.string() << "\nShader cache: " << (p.cache_dir.empty() ? "user\\shader_cache" : p.cache_dir.string())
         << "\n\nChecks:\n";
    for (const auto& c : RunChecks(s, p)) info << (c.ok ? "  ok   " : "  FAIL ") << c.what << ": " << c.detail << "\n";
    info << "\nThe saves and the online password are not in this report.\n";
    files.emplace_back("system.txt", info.str());
    const fs::path zip = a / "reports" / ("report_" + NowStamp() + ".zip");
    if (!WriteZip(zip, files, error)) return {};
    return zip;
}

fs::path SettingsFile(const fs::path& root) {
    if (root.empty()) return LauncherDir() / "launcher.ini";
    return root / "user" / "config" / "launcher.ini";
}

Settings LoadSettings() {
    const fs::path file = SettingsFile(AutoRoot(LauncherDir()));
    std::error_code ec;
    if (!fs::exists(file, ec)) return Settings{};
    return FromIni(ReadFile(file));
}

bool SaveSettings(const Settings& s) {
    const fs::path file = SettingsFile(AutoRoot(LauncherDir()));
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << ToIni(s);
        if (!f) return false;
    }
    fs::rename(tmp, file, ec);
    return !ec;
}

void AppendLauncherLog(const Paths& p, const std::string& line) {
    if (p.root.empty()) return;
    std::error_code ec;
    fs::create_directories(p.root / "analysis", ec);
    std::ofstream(p.root / "analysis" / "launcher_log.txt", std::ios::app) << NowStamp() << "  " << line << "\n";
}

}  // namespace launcher
