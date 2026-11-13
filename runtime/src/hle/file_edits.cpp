// Virtual edits to game files. See file_edits.h.
#include "hle/file_edits.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

namespace hle::file_edits {
namespace {

std::once_flag g_once;
std::vector<std::shared_ptr<const Edit>> g_edits;

struct Cursor {
    const std::vector<uint8_t>& d;
    size_t at = 0;
    bool ok = true;
    template <typename T>
    T Get() {
        T v{};
        if (!ok || at + sizeof(T) > d.size()) {
            ok = false;
            return v;
        }
        std::memcpy(&v, d.data() + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
    bool Bytes(std::vector<uint8_t>& out, uint64_t n) {
        if (!ok || n > d.size() - at) return ok = false;
        out.assign(d.begin() + static_cast<std::ptrdiff_t>(at), d.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += static_cast<size_t>(n);
        return true;
    }
};

}  // namespace

bool Parse(const std::vector<uint8_t>& data, std::vector<Edit>& out, std::string& error) {
    out.clear();
    if (data.size() < 12 || std::memcmp(data.data(), "LBPEDIT1", 8) != 0) {
        error = "not an LBPEDIT1 file";
        return false;
    }
    Cursor c{data, 8};
    const uint32_t version = c.Get<uint32_t>();
    if (version != 2) {
        error = "version " + std::to_string(version) + " (expected 2)";
        return false;
    }
    while (c.ok && c.at < data.size()) {
        Edit e;
        const uint16_t nameLen = c.Get<uint16_t>();
        std::vector<uint8_t> name;
        if (!c.Bytes(name, nameLen)) break;
        std::string rel(name.begin(), name.end());
        while (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) rel.erase(0, 1);
        for (char& ch : rel)
            if (ch == '\\') ch = '/';
        e.guest = "/app0/" + rel;
        e.original = c.Get<uint64_t>();
        e.cut = c.Get<uint64_t>();
        const uint64_t addLen = c.Get<uint64_t>();
        if (!c.Bytes(e.add, addLen)) break;
        const uint32_t patches = c.Get<uint32_t>();
        for (uint32_t i = 0; c.ok && i < patches; ++i) {
            Patch p;
            p.offset = c.Get<uint64_t>();
            const uint32_t len = c.Get<uint32_t>();
            c.Bytes(p.bytes, len);
            if (c.ok) e.patches.push_back(std::move(p));
        }
        if (!c.ok) break;
        if (e.cut > e.original) {
            error = e.guest + ": cut past the file's end";
            return false;
        }
        for (const Patch& p : e.patches)
            if (p.offset + p.bytes.size() > e.cut) {
                error = e.guest + ": a patch outside the kept part";
                return false;
            }
        out.push_back(std::move(e));
    }
    if (!c.ok) {
        error = "truncated";
        out.clear();
        return false;
    }
    return true;
}

void Load() {
    std::call_once(g_once, [] {
        const char* path = std::getenv("LBP3_FILE_EDITS");
        if (!path || !*path) return;
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "[edits] LBP3_FILE_EDITS=%s: can't read it; no game file is edited\n", path);
            return;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<Edit> edits;
        std::string error;
        if (!Parse(data, edits, error)) {
            std::fprintf(stderr, "[edits] %s: %s; no game file is edited\n", path, error.c_str());
            return;
        }
        for (Edit& e : edits) {
            std::fprintf(stderr, "[edits] %s: shown with %llu bytes kept, %zu added, %zu patch(es) (when it is %llu bytes)\n",
                         e.guest.c_str(), static_cast<unsigned long long>(e.cut), e.add.size(), e.patches.size(),
                         static_cast<unsigned long long>(e.original));
            g_edits.push_back(std::make_shared<const Edit>(std::move(e)));
        }
    });
}

std::shared_ptr<const Edit> Find(std::string_view guest, uint64_t hostSize) {
    Load();
    for (const auto& e : g_edits) {
        if (e->guest.size() != guest.size()) continue;
        bool same = true;  // guest paths are case-insensitive on the PS4's file system as used here
        for (size_t i = 0; i < guest.size() && same; ++i) {
            char a = e->guest[i], b = guest[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            same = a == b;
        }
        if (!same) continue;
        if (e->original != hostSize) {
            static std::once_flag warned;
            std::call_once(warned, [&] {
                std::fprintf(stderr, "[edits] %s is %llu bytes, not %llu: left unedited (another dump?)\n",
                             e->guest.c_str(), static_cast<unsigned long long>(hostSize),
                             static_cast<unsigned long long>(e->original));
            });
            return nullptr;
        }
        return e;
    }
    return nullptr;
}

}  // namespace hle::file_edits
