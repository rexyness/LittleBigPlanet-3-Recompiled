// The game dump as the launcher sees it: param.sfo, the file check, and the installed DLC packs.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher {

namespace fs = std::filesystem;

// A PS4 param.sfo: its text and number entries. ok = false when it isn't one.
struct Sfo {
    bool ok = false;
    std::map<std::string, std::string> text;
    std::map<std::string, std::uint32_t> number;
    std::string Get(const std::string& key) const {
        auto it = text.find(key);
        return it == text.end() ? std::string() : it->second;
    }
};
Sfo ParseSfo(const std::string& bytes);
// SHA-1 (the 20 raw bytes), as an archive's table stores it.
std::string Sha1Digest(const std::string& data);
Sfo ReadSfo(const fs::path& path);

// One line of the file check.
struct DumpItem {
    enum Level { Ok, Warn, Bad } level = Ok;
    std::string what;
    std::string detail;
};

// A .farc archive's trailer and table (FARC: data, then count x (sha1[20], offset u32, size u32),
// count u32, "FARC", all big-endian; level archives end in "FAR4" with their own table). For a
// FARC, up to `samples` entries (the smallest ones) are read back and their SHA-1 compared.
DumpItem CheckFarc(const fs::path& farc, int samples);

// The whole check: the folder, eboot.bin, param.sfo (CUSA00473, version 01.28), sce_sys, sce_module,
// the GUID map, the five top-level archives (trailer, table and sampled SHA-1s) and a sample of the
// level archives. `progress` gets a short line as it goes; `cancel` stops it early.
std::vector<DumpItem> CheckDump(const fs::path& game_dir, const std::function<void(const std::string&)>& progress,
                                const std::atomic<bool>* cancel);

// An installed DLC pack: <DLC folder>\<title id>\<label>\sce_sys\param.sfo.
struct DlcPack {
    std::string title;       // its TITLE, e.g. "Back To The Future Costume Pack 2"
    std::string label;       // the folder (the entitlement label)
    std::string title_id;    // the folder above (CUSA00473, or BCUS98148 for a PS3 pack)
    std::string content_id;
    bool license_only = true;  // nothing but sce_sys inside
};
std::vector<DlcPack> ListDlc(const fs::path& dlc_dir);

}  // namespace launcher
