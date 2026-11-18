// The English voice fallback (core/vfs.h, from LBPPC 0.1.5): a localised voice bank or dialogue
// file the copy doesn't have is served from the English recording; a file the copy has, a
// language that has its own, and every other path are left alone.
//   vfstest [game folder]   (with a game folder: the real gamedata/audio tree is checked too)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/vfs.h"

namespace fs = std::filesystem;
using core::vfs::EnglishVoiceCandidates;
using core::vfs::Resolve;
using core::vfs::Status;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static void Touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

int main(int argc, char** argv) {
    using V = std::vector<std::string>;
    // The three rules.
    CHECK(EnglishVoiceCandidates("gamedata/audio/german/lbp3_vo_ger.fsb") ==
          (V{"gamedata/audio/lbp3_vo_eng.fsb", "gamedata/audio/lbp3_vo_ger.fsb"}));
    CHECK(EnglishVoiceCandidates("gamedata/audio/latam_spanish/voiceover.fsb") == (V{"gamedata/audio/voiceover.fsb"}));
    CHECK(EnglishVoiceCandidates("gamedata/audio/braz_portuguese/move_voiceover.fsb") ==
          (V{"gamedata/audio/move_voiceover.fsb"}));
    CHECK(EnglishVoiceCandidates("gamedata/audio/dialogue/german/narrator/a/b.fsb") ==
          (V{"gamedata/audio/dialogue/narrator/a/b.fsb"}));
    // Not a language folder, not audio, a folder inside a language's banks, the folder itself.
    CHECK(EnglishVoiceCandidates("gamedata/audio/lbp3/lbp3_sfx.fsb").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/lbp3_vo_eng.fsb").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/germanic/x.fsb").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/german").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/german/").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/german/sub/x.fsb").empty());
    CHECK(EnglishVoiceCandidates("gamedata/videos/german/x_ger.fsb").empty());
    CHECK(EnglishVoiceCandidates("gamedata/audio/dialogue/narrator/a.fsb").empty());
    CHECK(EnglishVoiceCandidates("").empty());

    // Resolve over two layers (an update above a base), as the game has them.
    const fs::path root = fs::temp_directory_path() / "lbp3_vfstest";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path patch = root / "patch", base = root / "base";
    Touch(base / "gamedata/audio/lbp3_vo_eng.fsb");
    Touch(base / "gamedata/audio/voiceover.fsb");
    Touch(base / "gamedata/audio/french/lbp3_vo_fre.fsb");
    Touch(base / "gamedata/audio/german/lbp3_vo_new_ui_ger.fsb");
    Touch(patch / "gamedata/audio/dialogue/narrator/line.fsb");
    Touch(base / "gamedata/audio/dialogue/french/narrator/line.fsb");
    core::vfs::Init({patch, base}, root / "user");

    auto r = Resolve("/app0/gamedata/audio/german/lbp3_vo_ger.fsb");
    CHECK(r.status == Status::Ok && r.host == base / "gamedata/audio/lbp3_vo_eng.fsb");
    CHECK(r.guest == "/app0/gamedata/audio/german/lbp3_vo_ger.fsb" && !r.writable);
    r = Resolve("gamedata/audio/spanish/voiceover.fsb");  // relative paths are /app0's
    CHECK(r.status == Status::Ok && r.host == base / "gamedata/audio/voiceover.fsb");
    r = Resolve("/app0/gamedata/audio/dialogue/german/narrator/line.fsb");
    CHECK(r.status == Status::Ok && r.host == patch / "gamedata/audio/dialogue/narrator/line.fsb");
    // What the copy has is served as it is.
    r = Resolve("/app0/gamedata/audio/french/lbp3_vo_fre.fsb");
    CHECK(r.status == Status::Ok && r.host == base / "gamedata/audio/french/lbp3_vo_fre.fsb");
    r = Resolve("/app0/gamedata/audio/german/lbp3_vo_new_ui_ger.fsb");
    CHECK(r.status == Status::Ok && r.host == base / "gamedata/audio/german/lbp3_vo_new_ui_ger.fsb");
    r = Resolve("/app0/gamedata/audio/dialogue/french/narrator/line.fsb");
    CHECK(r.status == Status::Ok && r.host == base / "gamedata/audio/dialogue/french/narrator/line.fsb");
    // No English recording either: still not found.
    r = Resolve("/app0/gamedata/audio/german/lbp3_emotivox_loc_ger.fsb");
    CHECK(r.status == Status::NotFound);
    r = Resolve("/app0/gamedata/audio/dialogue/german/narrator/other.fsb");
    CHECK(r.status == Status::NotFound);
    r = Resolve("/app0/gamedata/other/german/lbp3_vo_ger.fsb");
    CHECK(r.status == Status::NotFound);
    fs::remove_all(root, ec);

    // A real copy: every bank French has (the one fully voiced language beside English) resolves
    // for German too, to a file outside the german folder unless German has its own.
    if (argc > 1) {
        const fs::path game = argv[1];
        core::vfs::Init({game}, fs::temp_directory_path() / "lbp3_vfstest_user");
        int banks = 0;
        for (const auto& e : fs::directory_iterator(game / "gamedata/audio/french", ec)) {
            std::string name = e.path().filename().string();
            if (const auto at = name.rfind("_fre.fsb"); at != std::string::npos) name.replace(at, 8, "_ger.fsb");
            r = Resolve("/app0/gamedata/audio/german/" + name);
            CHECK(r.status == Status::Ok);
            if (r.status != Status::Ok) std::printf("  no English file for german/%s\n", name.c_str());
            ++banks;
        }
        CHECK(banks > 0);
        std::printf("real copy: %d French banks checked for German\n", banks);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
