// Add-on content offline (ticket 18): param.sfo parsing, the pack scan (license-only vs with
// data, other title folders, bad packs), and the list / info / key calls the way LBP3 makes
// them (count, then list, then a key per entry; status 0 required), plus LBP3_ADDCONT=off.
//   addconttest            synthetic packs in a temp folder
//   addconttest <root>     also scans a real add-on root (e.g. the user's CUSA00473-DLC) and
//                          prints a summary (no pass/fail on its contents)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "hle/app_content.h"

namespace fs = std::filesystem;
using namespace hle::appcontent;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

// A minimal param.sfo with string entries (fmt 0x0204) and one int (0x0404).
static std::vector<uint8_t> MakeSfo(const std::vector<std::pair<std::string, std::string>>& strs) {
    std::vector<uint8_t> keys, data, idx;
    auto put16 = [](std::vector<uint8_t>& v, uint32_t x) { v.push_back(x & 0xff); v.push_back((x >> 8) & 0xff); };
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); };
    auto entry = [&](const std::string& k, uint32_t fmt, const std::vector<uint8_t>& val, uint32_t maxLen) {
        put16(idx, static_cast<uint32_t>(keys.size()));
        put16(idx, fmt);
        put32(idx, static_cast<uint32_t>(val.size()));
        put32(idx, maxLen);
        put32(idx, static_cast<uint32_t>(data.size()));
        keys.insert(keys.end(), k.begin(), k.end());
        keys.push_back(0);
        data.insert(data.end(), val.begin(), val.end());
        data.resize(data.size() + (maxLen - val.size()), 0);
    };
    for (const auto& [k, v] : strs) {
        std::vector<uint8_t> val(v.begin(), v.end());
        val.push_back(0);
        entry(k, 0x0204, val, static_cast<uint32_t>((val.size() + 3) / 4 * 4));
    }
    entry("ATTRIBUTE", 0x0404, {0, 0, 0, 0}, 4);
    const uint32_t count = static_cast<uint32_t>(strs.size() + 1);
    while (keys.size() % 4) keys.push_back(0);
    const uint32_t keyTable = 20 + 16 * count, dataTable = keyTable + static_cast<uint32_t>(keys.size());
    std::vector<uint8_t> out = {0, 'P', 'S', 'F', 1, 1, 0, 0};
    put32(out, keyTable);
    put32(out, dataTable);
    put32(out, count);
    out.insert(out.end(), idx.begin(), idx.end());
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

static void MakePack(const fs::path& root, const std::string& titleId, const std::string& folder,
                     const std::string& contentId, const std::string& category, bool withData) {
    const fs::path dir = root / titleId / folder;
    fs::create_directories(dir / "sce_sys");
    const auto sfo = MakeSfo({{"CATEGORY", category}, {"CONTENT_ID", contentId}, {"TITLE", "Pack " + folder}});
    std::ofstream(dir / "sce_sys" / "param.sfo", std::ios::binary)
        .write(reinterpret_cast<const char*>(sfo.data()), static_cast<std::streamsize>(sfo.size()));
    std::ofstream(dir / "sce_sys" / "license.dat", std::ios::binary) << "lic";
    if (withData) std::ofstream(dir / "data.bin", std::ios::binary) << "payload";
}

static NpUnifiedEntitlementLabel L(const char* s) {
    NpUnifiedEntitlementLabel l{};
    std::strncpy(l.data, s, kLabelSize - 1);
    return l;
}

int main(int argc, char** argv) {
    // param.sfo parsing
    {
        std::map<std::string, std::string> m;
        const auto sfo = MakeSfo({{"CATEGORY", "ac"}, {"CONTENT_ID", "UP9000-CUSA00473_00-LBPDLCTESTCK0001"}});
        CHECK(ParseParamSfo(sfo, m));
        CHECK(m["CATEGORY"] == "ac");
        CHECK(m["CONTENT_ID"] == "UP9000-CUSA00473_00-LBPDLCTESTCK0001");
        CHECK(m["ATTRIBUTE"] == "0");
        std::vector<uint8_t> bad = sfo;
        bad[1] = 'X';
        CHECK(!ParseParamSfo(bad, m));
        std::vector<uint8_t> cut(sfo.begin(), sfo.begin() + 30);
        CHECK(!ParseParamSfo(cut, m));
    }

    const fs::path root = fs::temp_directory_path() / "lbp3_addconttest";
    fs::remove_all(root);
    MakePack(root, "CUSA00473", "LBPDLCBBBBCK0001", "UP9000-CUSA00473_00-LBPDLCBBBBCK0001", "ac", false);
    MakePack(root, "CUSA00473", "LBPDLCAAAACO0001", "UP9000-CUSA00473_00-LBPDLCAAAACO0001", "ac", false);
    MakePack(root, "CUSA00473", "LBPDLCDATAMP0001", "UP9000-CUSA00473_00-LBPDLCDATAMP0001", "ac", true);
    MakePack(root, "CUSA00473", "NOTADDON", "UP9000-CUSA00473_00-GAMEGAMEGAME0001", "gd", false);
    MakePack(root, "CUSA00473", "SHORTID", "UP9000-CUSA00473_00-SHORT", "ac", false);
    MakePack(root, "BCUS98148", "PLITTLEBIG000005", "UP9000-BCUS98148_00-PLITTLEBIG000005", "ac", false);
    fs::create_directories(root / "CUSA00473" / "EMPTYFOLDER");  // no sce_sys: ignored

    // The scan
    {
        const auto packs = ScanAddcont(root);
        CHECK(packs.size() == 4);
        if (packs.size() == 4) {
            CHECK(packs[0].label == "LBPDLCAAAACO0001");  // sorted
            CHECK(packs[1].label == "LBPDLCBBBBCK0001");
            CHECK(packs[2].label == "LBPDLCDATAMP0001");
            CHECK(packs[3].label == "PLITTLEBIG000005");
            CHECK(packs[0].status == kStatusNoExtraData);
            CHECK(packs[2].status == kStatusInstalled);
            CHECK(packs[3].titleId == "BCUS98148");
            CHECK(packs[0].title == "Pack LBPDLCAAAACO0001");
        }
    }

    // The calls, the way LBP3 makes them (FUN_002463d0)
    SetAddcontRoot(root);
    {
        uint32_t hits = 99;
        CHECK(sceAppContentGetAddcontInfoList(0, nullptr, 0, &hits) == 0);
        CHECK(hits == 4);
        CHECK(sceAppContentGetAddcontInfoList(0, nullptr, 0, nullptr) != 0);  // count with nowhere to put it
        std::vector<AddcontInfo> list(hits);
        uint32_t got = 0;
        CHECK(sceAppContentGetAddcontInfoList(0, list.data(), hits, &got) == 0);
        CHECK(got == 4);
        CHECK(sizeof(AddcontInfo) == 0x18);  // the game's stride
        int accepted = 0;
        for (uint32_t i = 0; i < got; ++i) {
            EntitlementKey key;
            std::memset(&key, 0xcc, sizeof(key));
            const int r = sceAppContentGetEntitlementKey(0, &list[i].label, &key);
            CHECK(r == 0);
            CHECK(key.data[0] == 0 && key.data[15] == 0);
            CHECK(std::strlen(list[i].label.data) == 16);
            if (r == 0 && list[i].status == 0) ++accepted;
        }
        CHECK(accepted == 3);  // the pack with data is INSTALLED, which LBP3 skips here
        // A short list gets the first entries only.
        AddcontInfo two[2];
        CHECK(sceAppContentGetAddcontInfoList(0, two, 2, &got) == 0);
        CHECK(got == 2);
        CHECK(std::strcmp(two[0].label.data, "LBPDLCAAAACO0001") == 0);
        // Other service labels list nothing and have no keys.
        CHECK(sceAppContentGetAddcontInfoList(1, nullptr, 0, &hits) == 0);
        CHECK(hits == 0);
        CHECK(sceAppContentGetAddcontInfoList(2, nullptr, 0, &hits) == 0);
        CHECK(hits == 0);
        EntitlementKey key;
        auto lbl = L("LBPDLCAAAACO0001");
        CHECK(sceAppContentGetEntitlementKey(1, &lbl, &key) == static_cast<int>(0x80D90007));
        auto missing = L("LBPDLCNOPENO0001");
        CHECK(sceAppContentGetEntitlementKey(0, &missing, &key) == static_cast<int>(0x80D90007));
        CHECK(sceAppContentGetEntitlementKey(0, nullptr, &key) != 0);
        AddcontInfo info{};
        CHECK(sceAppContentGetAddcontInfo(0, &lbl, &info) == 0);
        CHECK(info.status == kStatusNoExtraData && std::strcmp(info.label.data, "LBPDLCAAAACO0001") == 0);
        CHECK(sceAppContentGetAddcontInfo(0, &missing, &info) == static_cast<int>(0x80D90005));
        MountPoint mp{};
        CHECK(sceAppContentAddcontMount(0, &lbl, &mp) != 0);
    }

    // LBP3_ADDCONT_DIR overrides the root; a missing folder lists nothing.
    {
        _putenv_s("LBP3_ADDCONT_DIR", (root / "nowhere").string().c_str());
        SetAddcontRoot(root);
        uint32_t hits = 99;
        CHECK(sceAppContentGetAddcontInfoList(0, nullptr, 0, &hits) == 0);
        CHECK(hits == 0);
        _putenv_s("LBP3_ADDCONT_DIR", root.string().c_str());
        SetAddcontRoot(fs::path("C:/definitely/not/here"));
        CHECK(sceAppContentGetAddcontInfoList(0, nullptr, 0, &hits) == 0);
        CHECK(hits == 4);
        _putenv_s("LBP3_ADDCONT_DIR", "");
    }

    // LBP3_ADDCONT=off restores "no add-on content".
    {
        _putenv_s("LBP3_ADDCONT", "off");
        SetAddcontRoot(root);
        uint32_t hits = 99;
        CHECK(sceAppContentGetAddcontInfoList(0, nullptr, 0, &hits) == 0);
        CHECK(hits == 0);
        auto lbl = L("LBPDLCAAAACO0001");
        EntitlementKey key;
        CHECK(sceAppContentGetEntitlementKey(0, &lbl, &key) == static_cast<int>(0x80D90007));
        _putenv_s("LBP3_ADDCONT", "");
    }

    fs::remove_all(root);

    if (argc > 1) {  // a real root: summary only
        const auto packs = ScanAddcont(argv[1]);
        size_t lic = 0;
        for (const auto& p : packs) lic += p.status == kStatusNoExtraData;
        std::printf("real root %s: %zu packs, %zu license-only\n", argv[1], packs.size(), lic);
        for (size_t i = 0; i < packs.size() && i < 3; ++i)
            std::printf("  %s  %s (%s)\n", packs[i].label.c_str(), packs[i].title.c_str(), packs[i].titleId.c_str());
    }

    std::printf("addconttest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
