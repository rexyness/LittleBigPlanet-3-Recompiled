// SPDX-License-Identifier: GPL-2.0-or-later
// Quit Game in the pause menu (launcher_quitmenu.h): a C++ port of LBPPC's scripts/quit_menu.py
// (emurphy04, https://github.com/emurphy04/LBPPC at aad6a91, GPL-2.0-or-later), step for step.
//
// The pause menu is a script, ModalOverlayStateMainMenu (patch_001.farc). Its Good Grief! button
// (DoGriefReportButton) takes its label from LAMS.Translate and, pressed, opens the grief report
// warning. Here the button asks Translate for two keys of LBPPC's own instead, which the runtime
// answers: QUIT_LABEL is the button's text, QUIT_ACTION a press. The button is also enabled
// offline, where the game greys Good Grief! out. Script format: ennuo's cwlib (RScript), revision
// 0x3f9 with compressed integers.
#include "launcher_quitmenu.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "launcher_dump.h"  // Sha1Digest
#include "miniz.h"

namespace launcher {
namespace fs = std::filesystem;
namespace {

using Bytes = std::string;

const char kScriptPath[] = "gamedata/scripts/modaloverlaystatemainmenu.ff";
const unsigned char kScriptSha1[20] = {0x50, 0x50, 0x06, 0x23, 0xd5, 0xa5, 0x03, 0x37, 0xd1, 0x72,
                                       0xc5, 0xc8, 0xf2, 0x1f, 0x15, 0x40, 0x7b, 0x89, 0x4e, 0x1c};  // the 1.28 patch's
const char kFarc[] = "patch_001.farc";
const char kMap[] = "output/orbisguids.map";
const char kFunction[] = "DoGriefReportButton__Q5Gooeyi";
constexpr std::int32_t kGriefLabel = -912847811;                           // Good Grief!'s LAMS key
constexpr std::uint32_t kQuitLabel = 0x4C425051, kQuitAction = 0x4C425058;  // 'LBPQ', 'LBPX'
constexpr int LCi = 0x03, CALL = 0xa4, B = 0xa6, NEW_OBJECT = 0xb5, RETURN = 0xa5;

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::uint32_t Be32(const Bytes& b, size_t at) {
    if (at + 4 > b.size()) throw Fail("cut short");
    return std::uint32_t(std::uint8_t(b[at])) << 24 | std::uint32_t(std::uint8_t(b[at + 1])) << 16 |
           std::uint32_t(std::uint8_t(b[at + 2])) << 8 | std::uint8_t(b[at + 3]);
}
std::uint16_t Be16(const Bytes& b, size_t at) {
    if (at + 2 > b.size()) throw Fail("cut short");
    return std::uint16_t(std::uint8_t(b[at]) << 8 | std::uint8_t(b[at + 1]));
}
Bytes PutBe32(std::uint32_t v) { return {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; }
Bytes PutBe16(std::uint16_t v) { return {char(v >> 8), char(v)}; }
template <class T>
Bytes PutLe(T v) {
    Bytes b(sizeof(T), '\0');
    for (size_t i = 0; i < sizeof(T); ++i) b[i] = char((std::uint64_t(v) >> (8 * i)) & 0xff);
    return b;
}

// Compressed-integer reads (uleb128; strings' lengths zigzag), big-endian fixed sizes.
struct Reader {
    const Bytes& d;
    size_t at = 0;
    std::uint8_t u8() {
        if (at >= d.size()) throw Fail("the script is cut short");
        return std::uint8_t(d[at++]);
    }
    void i16() { at += 2; }
    std::uint64_t uleb() {
        std::uint64_t value = 0;
        for (int shift = 0;; shift += 7) {
            const std::uint8_t byte = u8();
            if (shift < 64) value |= std::uint64_t(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return value;
        }
    }
    void string() {
        std::uint64_t n = uleb();
        const std::int64_t len = std::int64_t(n >> 1) ^ -std::int64_t(n & 1);
        at += size_t(len);
    }
    void resource() {
        uleb();
        const std::uint8_t kind = u8();
        if (kind & 2) uleb();
        if (kind & 1) at += 20;
    }
    template <class F>
    void rows(F read) {
        const std::uint64_t n = uleb();
        for (std::uint64_t i = 0; i < n; ++i) read();
    }
    std::vector<std::uint64_t> vector() {
        const std::uint64_t n = uleb();
        if (!n) return {};
        const std::uint8_t width = u8();
        std::vector<std::uint64_t> values(n, 0);
        for (int byte = 0; byte < width; ++byte)
            for (std::uint64_t i = 0; i < n; ++i) values[i] |= std::uint64_t(u8()) << (8 * byte);
        return values;
    }
};

Bytes Uleb(std::uint64_t value) {
    Bytes out;
    do {
        std::uint8_t byte = value & 0x7f;
        value >>= 7;
        out.push_back(char(byte | (value ? 0x80 : 0)));
    } while (value);
    return out;
}

struct Function {
    std::string name;
    size_t begin, end;
};
struct Code {
    size_t start = 0, end = 0;
    std::vector<std::uint64_t> code;
    std::vector<Function> functions;
};

// The bytecode (where its count starts, its end, the instructions) and each function's name and range.
Code CodeOf(const Bytes& body) {
    Reader r{body};
    r.string();
    r.resource();
    r.i16();
    r.rows([&] { r.u8(), r.u8(), r.u8(), r.u8(), r.resource(), r.uleb(); });  // type references
    r.rows([&] { r.uleb(), r.uleb(); });                                    // field references
    r.rows([&] { r.uleb(), r.uleb(); });                                    // function references
    r.rows([&] { r.i16(), r.uleb(), r.uleb(); });                           // fields
    r.rows([&] { r.i16(), r.uleb(), r.uleb(), r.uleb(), r.uleb(); });       // properties
    struct Raw {
        std::uint64_t name;
        size_t begin, end;
    };
    std::vector<Raw> raw;
    r.rows([&] {
        r.i16();
        r.uleb();
        const std::uint64_t name = r.uleb();
        const size_t b = Be16(body, r.at + 4), n = Be16(body, r.at + 6);  // ranges[1]
        r.at += 16;
        r.uleb();
        raw.push_back({name, b, b + n});
    });
    r.rows([&] { r.uleb(), r.uleb(); });  // arguments
    Code c;
    c.start = r.at;
    const std::uint64_t count = r.uleb();
    for (std::uint64_t i = 0; i < count; ++i) c.code.push_back(r.uleb());
    c.end = r.at;
    r.rows([&] { r.u8(), r.u8(); });                          // line numbers (i16)
    r.rows([&] { r.i16(), r.uleb(), r.uleb(), r.uleb(); });   // locals
    const std::vector<std::uint64_t> offsets = r.vector();
    const std::uint64_t size = r.uleb();
    if (r.at + size > body.size()) throw Fail("the script's name pool is cut short");
    const Bytes pool = body.substr(r.at, size_t(size));
    for (const Raw& f : raw) {
        if (f.name >= offsets.size()) throw Fail("a function name is missing");
        const size_t a = size_t(offsets[f.name]), z = pool.find('\0', a);
        if (a >= pool.size() || z == Bytes::npos) throw Fail("a function name is missing");
        c.functions.push_back({pool.substr(a, z - a), f.begin, f.end});
    }
    return c;
}

std::uint64_t High(std::uint64_t bits, std::uint32_t value) { return (bits & 0xffffffffull) | (std::uint64_t(value) << 32); }
std::int32_t S32(std::uint64_t bits) { return std::int32_t(std::uint32_t(bits >> 32)); }

// DoGriefReportButton, checked instruction by instruction against the 1.28 script first.
bool Edit(std::vector<std::uint64_t>& code, size_t begin, size_t end) {
    if (end > code.size() || end - begin != 45) return false;
    std::uint64_t* c = code.data() + begin;
    struct Expect {
        int at, op;
        bool check;
        std::int32_t value;
    };
    const Expect expected[] = {{0, LCi, true, kGriefLabel}, {2, CALL, false, 0}, {6, B, true, 2},
                               {7, LCi, true, 2},           {23, NEW_OBJECT, false, 0}, {44, RETURN, false, 0}};
    for (const Expect& e : expected)
        if (int(c[e.at] & 0xff) != e.op || (e.check && S32(c[e.at]) != e.value)) return false;
    c[0] = High(c[0], kQuitLabel);    // the label: Translate(QUIT_LABEL)
    c[7] = High(c[7], 0);             // enabled offline too (2 greys it out)
    c[23] = High(c[0], kQuitAction);  // pressed: Translate(QUIT_ACTION), with the label's ARG and CALL
    c[24] = c[1];
    c[25] = c[2];
    c[26] = High(c[6], 44 - 26);      // then to the RETURN
    return true;
}

// The resource's zlib chunks (32 KiB of script each) and their sizes.
Bytes Chunks(const Bytes& body) {
    Bytes sizes, packed;
    std::uint16_t parts = 0;
    for (size_t i = 0; i < body.size(); i += 0x8000, ++parts) {
        const size_t n = std::min<size_t>(0x8000, body.size() - i);
        mz_ulong len = mz_compressBound(mz_ulong(n));
        Bytes z(len, '\0');
        if (mz_compress2(reinterpret_cast<unsigned char*>(z.data()), &len, reinterpret_cast<const unsigned char*>(body.data() + i),
                         mz_ulong(n), 9) != MZ_OK)
            throw Fail("compressing the script failed");
        z.resize(len);
        sizes += PutBe16(std::uint16_t(z.size())) + PutBe16(std::uint16_t(n));
        packed += z;
    }
    return PutBe16(1) + PutBe16(parts) + sizes + packed;
}

Bytes ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Fail("can't read " + p.filename().string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

fs::path QuitMenuFolder(const fs::path& game_dir) {
    std::error_code ec;
    for (const char* sub : {"CUSA00473-patch", "CUSA00473"}) {
        const fs::path f = game_dir / sub;
        if (fs::exists(f / kFarc, ec) && fs::exists(f / "output" / "orbisguids.map", ec)) return f;
    }
    return {};
}

QuitMenuResult WriteQuitMenuEdits(const fs::path& game, const fs::path& out) {
    std::error_code ec;
    fs::remove(out, ec);
    auto no = [](const std::string& why) { return QuitMenuResult{false, "Quit Game: not added (" + why + ")"}; };
    try {
        const Bytes guidMap = ReadAll(game / "output" / "orbisguids.map");
        const Bytes key = PutBe16(std::uint16_t(sizeof kScriptPath - 1)) + kScriptPath;
        const size_t at = guidMap.find(key);
        const size_t fields = at + 2 + (sizeof kScriptPath - 1) + 4;  // the date, then the size and hash
        if (at == Bytes::npos || fields + 24 > guidMap.size() || std::memcmp(guidMap.data() + fields + 4, kScriptSha1, 20) != 0)
            return no("the pause menu script is not the 1.28 one");

        // The archive's table.
        std::ifstream farc(game / kFarc, std::ios::binary);
        if (!farc) return no(std::string("can't read ") + kFarc);
        farc.seekg(0, std::ios::end);
        const std::uint64_t farcSize = std::uint64_t(farc.tellg());
        if (farcSize < 8) return no(std::string(kFarc) + " is cut short");
        Bytes tail8(8, '\0');
        farc.seekg(std::streamoff(farcSize - 8));
        farc.read(tail8.data(), 8);
        std::vector<Bytes> entries;
        std::uint64_t tableStart = farcSize;
        if (tail8.compare(4, 4, "FARC") == 0) {
            const std::uint32_t count = Be32(tail8, 0);
            if (std::uint64_t(count) * 28 + 8 > farcSize) return no(std::string(kFarc) + "'s table doesn't fit");
            tableStart = farcSize - 8 - std::uint64_t(count) * 28;
            Bytes table(size_t(count) * 28, '\0');
            farc.seekg(std::streamoff(tableStart));
            farc.read(table.data(), std::streamsize(table.size()));
            for (std::uint32_t i = 0; i < count; ++i) entries.push_back(table.substr(size_t(i) * 28, 28));
        }
        const Bytes sha(reinterpret_cast<const char*>(kScriptSha1), 20);
        auto found = std::find_if(entries.begin(), entries.end(), [&](const Bytes& e) { return e.compare(0, 20, sha) == 0; });
        if (found == entries.end()) return no(std::string("no pause menu script in ") + kFarc);
        const std::uint32_t offset = Be32(*found, 20), size = Be32(*found, 24);
        Bytes resource(size, '\0');
        farc.seekg(offset);
        farc.read(resource.data(), size);
        if (!farc || Sha1Digest(resource) != sha) return no(std::string("the pause menu script in ") + kFarc + " does not match its hash");

        // Header: FSHb, revision, dependency table offset, branch (4), compression flags, compressed, chunks.
        const std::uint32_t dependencies = Be32(resource, 8);
        const Bytes flags = resource.substr(12, 6);
        const std::uint16_t count = Be16(resource, 20);
        size_t data = 22 + 4 * size_t(count);
        Bytes body;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint16_t packed = Be16(resource, 22 + 4 * size_t(i)), unpacked = Be16(resource, 24 + 4 * size_t(i));
            if (data + packed > resource.size()) return no("the script is cut short");
            Bytes chunk(unpacked ? unpacked : 0x10000, '\0');
            mz_ulong len = mz_ulong(chunk.size());
            if (mz_uncompress(reinterpret_cast<unsigned char*>(chunk.data()), &len,
                              reinterpret_cast<const unsigned char*>(resource.data() + data), packed) != MZ_OK)
                return no("the script doesn't unpack");
            chunk.resize(len);
            body += chunk;
            data += packed;
        }
        Code code = CodeOf(body);
        size_t begin = 0, stop = 0;
        for (const Function& f : code.functions)
            if (f.name == kFunction) {
                begin = f.begin;
                stop = f.end;
                break;
            }
        if (!Edit(code.code, begin, stop)) return no(std::string(kFunction) + " is not the one this was made for");
        Bytes newCode = Uleb(code.code.size());
        for (std::uint64_t c : code.code) newCode += Uleb(c);
        body = body.substr(0, code.start) + newCode + body.substr(code.end);
        const Bytes packed = Chunks(body);
        if (dependencies > resource.size()) return no("the script's dependency table is out of place");
        const Bytes neu = resource.substr(0, 8) + PutBe32(std::uint32_t(12 + flags.size() + packed.size())) + flags + packed +
                          resource.substr(dependencies);
        const Bytes sha1 = Sha1Digest(neu);

        // The archive as the game sees it: its data, the new script, the table with it added (in hash order).
        const Bytes entry = sha1 + PutBe32(std::uint32_t(tableStart)) + PutBe32(std::uint32_t(neu.size()));
        auto pos = std::lower_bound(entries.begin(), entries.end(), sha1,
                                    [](const Bytes& e, const Bytes& k) { return e.compare(0, 20, k) < 0; });
        entries.insert(pos, entry);
        Bytes tail = neu;
        for (const Bytes& e : entries) tail += e;
        tail += PutBe32(std::uint32_t(entries.size())) + "FARC";

        fs::create_directories(out.parent_path(), ec);
        Bytes file = Bytes("LBPEDIT1", 8) + PutLe<std::uint32_t>(2);
        auto section = [&](const std::string& path, std::uint64_t original, std::uint64_t cut, const Bytes& add,
                           const std::vector<std::pair<std::uint64_t, Bytes>>& patches) {
            file += PutLe<std::uint16_t>(std::uint16_t(path.size())) + path;
            file += PutLe<std::uint64_t>(original) + PutLe<std::uint64_t>(cut) + PutLe<std::uint64_t>(add.size()) + add;
            file += PutLe<std::uint32_t>(std::uint32_t(patches.size()));
            for (const auto& [where, d] : patches) file += PutLe<std::uint64_t>(where) + PutLe<std::uint32_t>(std::uint32_t(d.size())) + d;
        };
        section(kFarc, farcSize, tableStart, tail, {});
        section(kMap, guidMap.size(), guidMap.size(), Bytes(), {{fields, PutBe32(std::uint32_t(neu.size())) + sha1}});
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        f.write(file.data(), std::streamsize(file.size()));
        if (!f) {
            f.close();
            fs::remove(out, ec);
            return no("couldn't write " + out.string());
        }
        static const char* hex = "0123456789abcdef";
        std::string id;
        for (int i = 0; i < 6; ++i) id += std::string{hex[std::uint8_t(sha1[i]) >> 4], hex[std::uint8_t(sha1[i]) & 15]};
        return {true, "Quit Game: added to the pause menu (script " + id + ", " + std::to_string(neu.size()) + " bytes)"};
    } catch (const std::exception& e) {
        fs::remove(out, ec);
        return no(e.what());
    }
}

}  // namespace launcher
