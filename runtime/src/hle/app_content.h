// libSceAppContent's add-on content (DLC), from the user's own extracted packs.
//
// The packs live outside the game dump, by default in "<game dir>\CUSA00473-DLC", as the
// user's pkg extractor writes them: <root>\<title id>\<entitlement label>\sce_sys\param.sfo.
// Every pack whose param.sfo says CATEGORY "ac" is listed; its entitlement label is the
// CONTENT_ID after the first 20 characters ("UP9000-CUSA00473_00-" + 16). A pack holding
// nothing but sce_sys is a license-only add-on (status NO_EXTRA_DATA, 0), anything else is
// INSTALLED (4), as on a console.
//
// LBP3 (eboot FUN_002463d0) lists the add-ons of service labels 0..2 and unlocks a pack's
// items only when sceAppContentGetEntitlementKey succeeds AND the status is 0; it never
// looks at the key's bytes. All packs are listed under service label 0 (the game handles
// every service label alike). LBP3_ADDCONT=off lists none (the behaviour before this);
// LBP3_ADDCONT_DIR=<folder> picks another root.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "hle/abi.h"

namespace hle::appcontent {

constexpr uint32_t kStatusNoExtraData = 0;
constexpr uint32_t kStatusInstalled = 4;
constexpr size_t kLabelSize = 17;  // 16 characters and a NUL
constexpr size_t kKeySize = 16;

struct AddcontPack {
    std::string label;      // entitlement label, 16 characters
    std::string contentId;  // UP9000-CUSA00473_00-LBPDLC2KADCK0001
    std::string title;      // param.sfo TITLE
    std::string titleId;    // the folder it was found under (CUSA00473, BCUS98148, ...)
    uint32_t status = kStatusNoExtraData;
    std::filesystem::path dir;
};

// param.sfo: the string and integer entries (integers as decimal strings). False if malformed.
bool ParseParamSfo(const std::vector<uint8_t>& data, std::map<std::string, std::string>& out);

// The packs under root (see above), sorted by label, duplicates dropped.
std::vector<AddcontPack> ScanAddcont(const std::filesystem::path& root);

// Where main found the game; the add-on root defaults to "<that>\CUSA00473-DLC". Forgets any
// earlier scan (the tests switch roots).
void SetAddcontRoot(const std::filesystem::path& root);

struct NpUnifiedEntitlementLabel {
    char data[kLabelSize];
    char padding[3];
};
struct AddcontInfo {
    NpUnifiedEntitlementLabel label;
    uint32_t status;
};
struct EntitlementKey {
    char data[kKeySize];
};
struct MountPoint {
    char data[16];
};

int PS4_SYSV_ABI sceAppContentGetAddcontInfoList(uint32_t serviceLabel, AddcontInfo* list, uint32_t listNum,
                                                 uint32_t* hitNum);
int PS4_SYSV_ABI sceAppContentGetAddcontInfo(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                             AddcontInfo* info);
int PS4_SYSV_ABI sceAppContentGetEntitlementKey(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                                EntitlementKey* key);
int PS4_SYSV_ABI sceAppContentAddcontMount(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                           MountPoint* mount);

}  // namespace hle::appcontent
