// The game dump as the launcher sees it (launcher_dump.h).
#include "launcher_dump.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

namespace launcher {
namespace {

std::uint32_t Le32(const std::string& d, size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(d[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 3])) << 24;
}
std::uint16_t Le16(const std::string& d, size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(d[at]) | static_cast<unsigned char>(d[at + 1]) << 8);
}
std::uint32_t Be32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

std::string Sha1(const std::string& data) {
    unsigned char h[20] = {};
    BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
               static_cast<ULONG>(data.size()), h, sizeof h);
    return std::string(reinterpret_cast<char*>(h), 20);
}

std::string Mb(std::uintmax_t bytes) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(1);
    if (bytes >= (1ull << 30)) o << bytes / double(1ull << 30) << " GB";
    else o << bytes / double(1ull << 20) << " MB";
    return o.str();
}

}  // namespace

std::string Sha1Digest(const std::string& data) { return Sha1(data); }

Sfo ParseSfo(const std::string& d) {
    Sfo s;
    if (d.size() < 20 || d.compare(0, 4, "\0PSF", 4) != 0) return s;
    const std::uint32_t keys = Le32(d, 8), data = Le32(d, 12), count = Le32(d, 16);
    if (keys > d.size() || data > d.size() || 20ull + 16ull * count > d.size()) return s;
    for (std::uint32_t i = 0; i < count; ++i) {
        const size_t e = 20 + 16 * static_cast<size_t>(i);
        const std::uint16_t key_off = Le16(d, e), fmt = Le16(d, e + 2);
        const std::uint32_t len = Le32(d, e + 4), off = Le32(d, e + 12);
        const size_t k = keys + key_off;
        if (k >= d.size() || static_cast<size_t>(data) + off + len > d.size()) return s;
        const size_t kend = d.find('\0', k);
        if (kend == std::string::npos) return s;
        const std::string key = d.substr(k, kend - k);
        const std::string value = d.substr(data + off, len);
        if (fmt == 0x0404 && len >= 4) s.number[key] = Le32(value, 0);
        else s.text[key] = value.substr(0, value.find('\0'));
    }
    s.ok = true;
    return s;
}

Sfo ReadSfo(const fs::path& path) { return ParseSfo(Slurp(path)); }

DumpItem CheckFarc(const fs::path& farc, int samples) {
    DumpItem it;
    it.what = farc.filename().string();
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(farc, ec);
    if (ec) return {DumpItem::Bad, it.what, "missing"};
    std::ifstream f(farc, std::ios::binary);
    if (!f || size < 8) return {DumpItem::Bad, it.what, "unreadable or too small"};
    unsigned char tail[8];
    f.seekg(static_cast<std::streamoff>(size - 8));
    f.read(reinterpret_cast<char*>(tail), 8);
    if (!f) return {DumpItem::Bad, it.what, "couldn't read its end"};
    const std::uint32_t count = Be32(tail);
    if (std::memcmp(tail + 4, "FAR4", 4) == 0) return {DumpItem::Ok, it.what, Mb(size) + ", level archive"};
    if (std::memcmp(tail + 4, "FARC", 4) != 0) return {DumpItem::Bad, it.what, "not an archive (cut short or damaged)"};
    const std::uintmax_t table = static_cast<std::uintmax_t>(count) * 28;
    if (table + 8 > size) return {DumpItem::Bad, it.what, "its table doesn't fit (cut short)"};
    const std::uintmax_t table_at = size - 8 - table;
    std::string t(static_cast<size_t>(table), '\0');
    f.seekg(static_cast<std::streamoff>(table_at));
    f.read(t.data(), static_cast<std::streamsize>(table));
    if (!f) return {DumpItem::Bad, it.what, "couldn't read its table"};
    struct Entry {
        std::string sha;
        std::uint32_t off, len;
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto* p = reinterpret_cast<const unsigned char*>(t.data()) + 28ull * i;
        const Entry e{std::string(reinterpret_cast<const char*>(p), 20), Be32(p + 20), Be32(p + 24)};
        if (static_cast<std::uintmax_t>(e.off) + e.len > table_at)
            return {DumpItem::Bad, it.what, "an entry points past its data (damaged)"};
        entries.push_back(e);
    }
    // The smallest entries, read back and hashed: cheap, and a damaged copy rarely survives it.
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.len < b.len; });
    int checked = 0;
    for (const Entry& e : entries) {
        if (checked >= samples) break;
        if (e.len == 0) continue;
        std::string data(e.len, '\0');
        f.seekg(e.off);
        f.read(data.data(), e.len);
        if (!f || Sha1(data) != e.sha) return {DumpItem::Bad, it.what, "a file inside doesn't match its hash (damaged)"};
        ++checked;
    }
    return {DumpItem::Ok, it.what, Mb(size) + ", " + std::to_string(count) + " files, " + std::to_string(checked) + " checked"};
}

std::vector<DumpItem> CheckDump(const fs::path& game_dir, const std::function<void(const std::string&)>& progress,
                                const std::atomic<bool>* cancel) {
    std::vector<DumpItem> out;
    auto say = [&](const std::string& s) {
        if (progress) progress(s);
    };
    auto stop = [&] { return cancel && cancel->load(); };
    std::error_code ec;
    const fs::path base = game_dir / "CUSA00473", patch = game_dir / "CUSA00473-patch";
    if (!fs::exists(base, ec)) {
        out.push_back({DumpItem::Bad, "Game folder", (base.string() + " not found")});
        return out;
    }
    out.push_back({DumpItem::Ok, "Game folder", base.string()});
    // a file from the patch folder if the dump keeps base and patch apart, else from the base
    auto find = [&](const fs::path& rel) {
        if (fs::exists(patch / rel, ec)) return patch / rel;
        return base / rel;
    };
    say("eboot.bin");
    const std::uintmax_t eboot = fs::file_size(base / "eboot.bin", ec);
    if (ec) out.push_back({DumpItem::Bad, "eboot.bin", "missing"});
    else if (eboot < 4u << 20) out.push_back({DumpItem::Bad, "eboot.bin", Mb(eboot) + ": too small (damaged?)"});
    else out.push_back({DumpItem::Ok, "eboot.bin", Mb(eboot)});

    say("param.sfo");
    const Sfo sfo = ReadSfo(find("sce_sys/param.sfo"));
    if (!sfo.ok) out.push_back({DumpItem::Bad, "param.sfo", "missing or unreadable"});
    else {
        const std::string id = sfo.Get("TITLE_ID"), ver = sfo.Get("APP_VER");
        const bool right = id == "CUSA00473";
        out.push_back({right ? (ver == "01.28" ? DumpItem::Ok : DumpItem::Warn) : DumpItem::Bad, "Game and version",
                       id + " " + ver + (right ? (ver == "01.28" ? "" : " (the port is made for 01.28)") : " (the port needs CUSA00473, LittleBigPlanet 3 US)")});
    }
    for (const char* f : {"icon0.png", "pic1.png"})
        if (!fs::exists(find(fs::path("sce_sys") / f), ec)) out.push_back({DumpItem::Warn, std::string("sce_sys\\") + f, "missing (only the launcher's art needs it)"});
    say("sce_module");
    int modules = 0;
    for (const auto& e : fs::directory_iterator(base / "sce_module", ec))
        if (e.path().extension() == ".prx") ++modules;
    out.push_back({modules > 0 ? DumpItem::Ok : DumpItem::Bad, "sce_module", modules > 0 ? std::to_string(modules) + " modules" : "missing"});
    const fs::path map = find("output/orbisguids.map");
    const std::uintmax_t map_size = fs::file_size(map, ec);
    out.push_back({ec ? DumpItem::Bad : DumpItem::Ok, "output\\orbisguids.map", ec ? "missing" : Mb(map_size)});

    for (const char* name : {"base_001.farc", "chunk1_001.farc", "craftworld_001.farc", "intro_001.farc", "patch_001.farc"}) {
        if (stop()) return out;
        say(name);
        DumpItem it = CheckFarc(find(name), 6);
        out.push_back(it);
    }
    // a sample of the level archives
    say("level archives");
    std::vector<fs::path> levels;
    const fs::path lvl = base / "gamedata" / "lbp3";
    for (auto e = fs::recursive_directory_iterator(lvl, ec); !ec && e != fs::recursive_directory_iterator(); e.increment(ec))
        if (e->path().extension() == ".farc") levels.push_back(e->path());
    if (levels.empty()) out.push_back({DumpItem::Bad, "Level archives", "none under gamedata\\lbp3"});
    else {
        std::mt19937 rng(12345);
        std::shuffle(levels.begin(), levels.end(), rng);
        int bad = 0, n = 0;
        std::string first_bad;
        for (const auto& l : levels) {
            if (stop() || n >= 40) break;
            ++n;
            const DumpItem it = CheckFarc(l, 2);
            if (it.level == DumpItem::Bad) {
                ++bad;
                if (first_bad.empty()) first_bad = l.filename().string() + ": " + it.detail;
            }
        }
        out.push_back({bad ? DumpItem::Bad : DumpItem::Ok, "Level archives",
                       std::to_string(levels.size()) + " found, " + std::to_string(n) + " checked" +
                           (bad ? ", " + std::to_string(bad) + " damaged (" + first_bad + ")" : "")});
    }
    say("done");
    return out;
}

std::vector<DlcPack> ListDlc(const fs::path& dlc_dir) {
    std::vector<DlcPack> out;
    std::error_code ec;
    for (const auto& title : fs::directory_iterator(dlc_dir, ec)) {
        if (!title.is_directory(ec)) continue;
        for (const auto& pack : fs::directory_iterator(title.path(), ec)) {
            if (!pack.is_directory(ec)) continue;
            DlcPack d;
            d.label = pack.path().filename().string();
            d.title_id = title.path().filename().string();
            const Sfo sfo = ReadSfo(pack.path() / "sce_sys" / "param.sfo");
            if (!sfo.ok) continue;  // not a pack (no param.sfo)
            d.title = sfo.Get("TITLE");
            d.content_id = sfo.Get("CONTENT_ID");
            if (d.title.empty()) d.title = d.label;
            for (const auto& f : fs::directory_iterator(pack.path(), ec))
                if (f.path().filename() != "sce_sys") d.license_only = false;
            out.push_back(std::move(d));
        }
    }
    // Placeholder titles: shown by the pack's label instead.
    std::map<std::string, int> uses;
    for (const auto& d : out) ++uses[d.title];
    for (auto& d : out)
        if (d.title.size() < 2 || uses[d.title] > 1) d.title = d.label, d.placeholder = true;
    std::sort(out.begin(), out.end(), [](const DlcPack& a, const DlcPack& b) {
        return a.placeholder != b.placeholder ? !a.placeholder : a.title < b.title;  // real names first
    });
    return out;
}

}  // namespace launcher
