// The best-of-both-worlds UX pieces (best/README.md), without the game:
//   - the virtual file edits behind Quit Game in the pause menu (hle/file_edits.h): parsing the
//     LBPEDIT1 description and reading the edited view (kept part, patches, appended bytes);
//   - the pause menu's Quit Game button: its text and the two-press confirmation (hle/quit.h);
//   - the in-game menu's model: moving, toggles, volume, the two-press quit (ui/menu_model.h);
//   - the system language codes (hle/language.h).
//   uxtest
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hle/file_edits.h"
#include "hle/language.h"
#include "hle/quit.h"
#include "ui/menu_model.h"

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

namespace {

template <typename T>
void Put(std::vector<uint8_t>& out, T v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// One LBPEDIT1 v2 description, like tools/quit_menu.py writes.
std::vector<uint8_t> Description(const std::string& name, uint64_t original, uint64_t cut, const std::string& add,
                                 const std::vector<std::pair<uint64_t, std::string>>& patches) {
    std::vector<uint8_t> d{'L', 'B', 'P', 'E', 'D', 'I', 'T', '1'};
    Put<uint32_t>(d, 2);
    Put<uint16_t>(d, static_cast<uint16_t>(name.size()));
    d.insert(d.end(), name.begin(), name.end());
    Put<uint64_t>(d, original);
    Put<uint64_t>(d, cut);
    Put<uint64_t>(d, add.size());
    d.insert(d.end(), add.begin(), add.end());
    Put<uint32_t>(d, static_cast<uint32_t>(patches.size()));
    for (const auto& [at, bytes] : patches) {
        Put<uint64_t>(d, at);
        Put<uint32_t>(d, static_cast<uint32_t>(bytes.size()));
        d.insert(d.end(), bytes.begin(), bytes.end());
    }
    return d;
}

std::string ReadView(const hle::file_edits::Edit& e, const std::string& host, uint64_t at, uint64_t len) {
    std::string out(len, '?');
    auto readHost = [&](uint8_t* dst, uint64_t pos, uint64_t n) -> int64_t {
        if (pos >= host.size()) return 0;
        const uint64_t k = std::min<uint64_t>(n, host.size() - pos);
        std::memcpy(dst, host.data() + pos, k);
        return static_cast<int64_t>(k);
    };
    const int64_t got = hle::file_edits::Read(e, reinterpret_cast<uint8_t*>(out.data()), at, len, readHost);
    out.resize(got < 0 ? 0 : static_cast<size_t>(got));
    return out;
}

void FileEdits() {
    using hle::file_edits::Edit;
    using hle::file_edits::Parse;
    // A 16-byte "file": keep 10 bytes (with a 3-byte patch at 2), then append "XYZW".
    const std::string host = "0123456789abcdef";
    std::vector<Edit> edits;
    std::string error;
    CHECK(Parse(Description("patch_001.farc", 16, 10, "XYZW", {{2, "PQR"}}), edits, error));
    CHECK(edits.size() == 1);
    if (edits.size() != 1) return;
    const Edit& e = edits[0];
    CHECK(e.guest == "/app0/patch_001.farc");
    CHECK(e.Size() == 14);
    CHECK(ReadView(e, host, 0, 14) == "01PQR56789XYZW");
    CHECK(ReadView(e, host, 0, 100) == "01PQR56789XYZW");  // short at the view's end
    CHECK(ReadView(e, host, 3, 4) == "QR56");              // inside the patch's tail
    CHECK(ReadView(e, host, 8, 4) == "89XY");              // across the cut
    CHECK(ReadView(e, host, 11, 3) == "YZW");              // appended bytes only
    CHECK(ReadView(e, host, 14, 5).empty());               // past the end
    CHECK(ReadView(e, host, 2, 1) == "P");

    // Two files, a sub-folder name, no appended part (the map's case: same size, a patch).
    std::vector<uint8_t> two = Description("patch_001.farc", 16, 10, "XYZW", {});
    std::vector<uint8_t> second = Description("output/orbisguids.map", 16, 16, "", {{4, "ab"}});
    two.insert(two.end(), second.begin() + 12, second.end());  // drop the second header
    CHECK(Parse(two, edits, error));
    CHECK(edits.size() == 2);
    if (edits.size() == 2) {
        CHECK(edits[1].guest == "/app0/output/orbisguids.map");
        CHECK(edits[1].Size() == 16);
        CHECK(ReadView(edits[1], host, 0, 16) == "0123ab6789abcdef");
    }

    // Malformed: wrong magic, wrong version, truncated, cut past the end, patch outside the kept part.
    std::vector<uint8_t> bad = Description("a", 16, 10, "", {});
    bad[0] = 'X';
    CHECK(!Parse(bad, edits, error));
    bad = Description("a", 16, 10, "", {});
    bad[8] = 3;
    CHECK(!Parse(bad, edits, error));
    bad = Description("a", 16, 10, "XYZW", {});
    bad.resize(bad.size() - 6);
    CHECK(!Parse(bad, edits, error));
    CHECK(!Parse(Description("a", 16, 20, "", {}), edits, error));
    CHECK(!Parse(Description("a", 16, 10, "", {{9, "xy"}}), edits, error));
}

std::u16string Text(const char16_t* t) { return t ? std::u16string(t) : std::u16string(u"<game>"); }

void PauseMenuQuit() {
    using namespace hle::quit;
    uint64_t armed = 0;
    bool quit = false;
    CHECK(Text(PauseMenuText(kQuitLabel, 1000, armed, quit)) == u"Quit Game");
    CHECK(!quit);
    CHECK(PauseMenuText(12345, 1000, armed, quit) == nullptr);  // any other key: the game's own text
    // First press arms it for 3 s; the label then asks for a second press.
    CHECK(Text(PauseMenuText(kQuitAction, 1000, armed, quit)).empty());
    CHECK(!quit && armed == 4000);
    CHECK(Text(PauseMenuText(kQuitLabel, 2000, armed, quit)) == u"Press again to quit");
    // Second press within 3 s quits.
    PauseMenuText(kQuitAction, 3999, armed, quit);
    CHECK(quit);
    // A press after it timed out only arms it again.
    armed = 4000;
    CHECK(Text(PauseMenuText(kQuitLabel, 4000, armed, quit)) == u"Quit Game");
    PauseMenuText(kQuitAction, 5000, armed, quit);
    CHECK(!quit && armed == 8000);
}

void Menu() {
    using namespace ui::menu;
    Model m;
    m.frameGenAvailable = false;
    CHECK(m.Selected() == Item::Stats);
    m.Move(1);  // frame generation is skipped when it wasn't started
    CHECK(m.Selected() == Item::Volume);
    m.Move(-1);
    CHECK(m.Selected() == Item::Stats);
    m.Move(-1);  // wraps
    CHECK(m.Selected() == Item::Close);
    m.frameGenAvailable = true;
    m.selected = static_cast<int>(Item::Stats);
    m.Move(1);
    CHECK(m.Selected() == Item::FrameGen);
    CHECK(m.Activate(0) == Action::ToggleFrameGen && m.frameGenOn);
    CHECK(m.Adjust(-1) == Action::ToggleFrameGen && !m.frameGenOn);
    m.selected = static_cast<int>(Item::Stats);
    CHECK(m.Activate(0) == Action::ToggleStats && m.stats);
    // Volume in steps of 10, clamped.
    m.selected = static_cast<int>(Item::Volume);
    m.volume = 100;
    CHECK(m.Adjust(1) == Action::None && m.volume == 100);
    CHECK(m.Adjust(-1) == Action::Volume && m.volume == 90);
    m.volume = 0;
    CHECK(m.Adjust(-1) == Action::None && m.volume == 0);
    CHECK(m.Activate(0) == Action::None);
    // Quit needs two presses within 3 s.
    m.selected = static_cast<int>(Item::Quit);
    CHECK(m.Activate(10000) == Action::QuitArmed);
    CHECK(m.QuitArmed(12000));
    CHECK(m.Activate(12999) == Action::Quit);
    Model q;
    q.selected = static_cast<int>(Item::Quit);
    CHECK(q.Activate(10000) == Action::QuitArmed);
    CHECK(!q.QuitArmed(13000));
    CHECK(q.Activate(13000) == Action::QuitArmed);  // timed out: arms again
    m.selected = static_cast<int>(Item::Close);
    CHECK(m.Activate(0) == Action::Close);
}

void Languages() {
    using hle::system::ParseLanguage;
    CHECK(ParseLanguage(nullptr) == 1);
    CHECK(ParseLanguage("") == 1);
    CHECK(ParseLanguage("en") == 1);
    CHECK(ParseLanguage("en-US") == 1);
    CHECK(ParseLanguage("de") == 4);
    CHECK(ParseLanguage("FR") == 2);
    CHECK(ParseLanguage("pt_br") == 17);
    CHECK(ParseLanguage("es-419") == 20);
    CHECK(ParseLanguage("en-gb") == 18);
    CHECK(ParseLanguage("0") == 0);
    CHECK(ParseLanguage("29") == 29);
    CHECK(ParseLanguage("30") == -1);
    CHECK(ParseLanguage("klingon") == -1);
    CHECK(std::string(hle::system::LanguageName(4)) == "German");
}

}  // namespace

int main() {
    FileEdits();
    PauseMenuQuit();
    Menu();
    Languages();
    std::printf("uxtest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
