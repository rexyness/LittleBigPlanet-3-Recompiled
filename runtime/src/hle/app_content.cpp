// libSceAppContent: the full game, temporary storage at /temp0, and the user's own add-on
// content (DLC) from extracted packs (hle/app_content.h). Values follow shadPS4, except the
// status of license-only packs (NO_EXTRA_DATA, as on a console; LBP3 ignores any other).
#include "hle/app_content.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>

#include "hle/kernel_errors.h"
#include "hle/register.h"

namespace hle::appcontent {
namespace {

namespace fs = std::filesystem;

constexpr int kErrorParameter = static_cast<int>(0x80D90002);
constexpr int kErrorNotFound = static_cast<int>(0x80D90005);
constexpr int kErrorDrmNoEntitlement = static_cast<int>(0x80D90007);

enum AppParamId : uint32_t { kSkuFlag = 0, kUserParam1 = 1, kUserParam4 = 4 };
constexpr int32_t kSkuFull = 3;
constexpr uint64_t kTempSpaceKb = 1024 * 1024;  // 1 GB free
constexpr size_t kLabelOffset = 20;              // "UP9000-CUSA00473_00-"
constexpr uint32_t kPackServiceLabel = 0;        // every pack is listed under this one

std::mutex g_mutex;
fs::path g_root;  // from SetAddcontRoot (main: <game dir>\CUSA00473-DLC)
bool g_scanned = false;
std::vector<AddcontPack> g_packs;
uint32_t g_keyCalls = 0, g_keyMisses = 0;

bool Enabled() {
    const char* v = std::getenv("LBP3_ADDCONT");
    return !(v && (std::strcmp(v, "off") == 0 || std::strcmp(v, "0") == 0));
}

// The packs, scanned once (under g_mutex).
const std::vector<AddcontPack>& Packs() {
    if (g_scanned) return g_packs;
    g_scanned = true;
    g_packs.clear();
    if (!Enabled()) {
        std::fprintf(stderr, "[addcont] LBP3_ADDCONT=off: no add-on content listed\n");
        return g_packs;
    }
    fs::path root = g_root;
    if (const char* v = std::getenv("LBP3_ADDCONT_DIR"); v && *v) root = v;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) {
        std::fprintf(stderr, "[addcont] no add-on folder (%s): no add-on content listed\n",
                     root.empty() ? "none set" : root.string().c_str());
        return g_packs;
    }
    g_packs = ScanAddcont(root);
    size_t licenseOnly = 0;
    for (const auto& p : g_packs) licenseOnly += p.status == kStatusNoExtraData;
    std::fprintf(stderr, "[addcont] %zu add-on packs in %s (%zu license-only, %zu with data)\n", g_packs.size(),
                 root.string().c_str(), licenseOnly, g_packs.size() - licenseOnly);
    for (const auto& p : g_packs)
        std::fprintf(stderr, "[addcont]   %s %s  %s  (%s)\n", p.label.c_str(),
                     p.status == kStatusNoExtraData ? "license" : "data   ", p.title.c_str(), p.titleId.c_str());
    return g_packs;
}

std::string Label(const NpUnifiedEntitlementLabel* label) {
    return label ? std::string(label->data, strnlen(label->data, kLabelSize - 1)) : std::string("(null)");
}

const AddcontPack* Find(const NpUnifiedEntitlementLabel* label) {
    const std::string want = Label(label);
    for (const auto& p : Packs())
        if (p.label == want) return &p;
    return nullptr;
}

uint32_t Read16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t Read32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool ParseParamSfo(const std::vector<uint8_t>& d, std::map<std::string, std::string>& out) {
    out.clear();
    if (d.size() < 20 || std::memcmp(d.data(), "\0PSF", 4) != 0) return false;
    const uint32_t keyTable = Read32(&d[8]), dataTable = Read32(&d[12]), count = Read32(&d[16]);
    if (20ull + 16ull * count > d.size() || keyTable > d.size() || dataTable > d.size()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &d[20 + 16 * i];
        const uint32_t keyOff = Read16(e), fmt = Read16(e + 2), len = Read32(e + 4), dataOff = Read32(e + 12);
        const size_t k = static_cast<size_t>(keyTable) + keyOff, v = static_cast<size_t>(dataTable) + dataOff;
        if (k >= d.size() || v > d.size() || len > d.size() - v) return false;
        const auto* kp = reinterpret_cast<const char*>(&d[k]);
        const std::string key(kp, strnlen(kp, d.size() - k));
        if (fmt == 0x0404) {  // int32
            if (len < 4) return false;
            out[key] = std::to_string(Read32(&d[v]));
        } else {  // utf-8 (0x0204, NUL-terminated) or utf-8 special (0x0004)
            const auto* vp = reinterpret_cast<const char*>(&d[v]);
            out[key] = std::string(vp, strnlen(vp, len));
        }
    }
    return true;
}

std::vector<AddcontPack> ScanAddcont(const fs::path& root) {
    std::vector<AddcontPack> packs;
    std::set<std::string> seen;
    std::error_code ec;
    for (const auto& title : fs::directory_iterator(root, ec)) {
        if (!title.is_directory()) continue;
        std::error_code ec2;
        for (const auto& pack : fs::directory_iterator(title.path(), ec2)) {
            if (!pack.is_directory()) continue;
            const fs::path sfo = pack.path() / "sce_sys" / "param.sfo";
            std::ifstream f(sfo, std::ios::binary);
            if (!f) continue;
            const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::map<std::string, std::string> psf;
            if (!ParseParamSfo(data, psf)) {
                std::fprintf(stderr, "[addcont] %s: invalid param.sfo, skipped\n", pack.path().string().c_str());
                continue;
            }
            if (psf["CATEGORY"].rfind("ac", 0) != 0) continue;
            const std::string& cid = psf["CONTENT_ID"];
            if (cid.size() < kLabelOffset + 16) {
                std::fprintf(stderr, "[addcont] %s: CONTENT_ID '%s' too short, skipped\n",
                             pack.path().string().c_str(), cid.c_str());
                continue;
            }
            AddcontPack p;
            p.label = cid.substr(kLabelOffset, 16);
            p.contentId = cid;
            p.title = psf["TITLE"];
            p.titleId = title.path().filename().string();
            p.dir = pack.path();
            p.status = kStatusNoExtraData;  // license-only unless the pack carries anything besides sce_sys
            std::error_code ec3;
            for (const auto& entry : fs::directory_iterator(pack.path(), ec3))
                if (entry.path().filename() != "sce_sys") {
                    p.status = kStatusInstalled;
                    break;
                }
            if (seen.insert(p.label).second) packs.push_back(std::move(p));
        }
    }
    std::sort(packs.begin(), packs.end(),
              [](const AddcontPack& a, const AddcontPack& b) { return a.label < b.label; });
    return packs;
}

void SetAddcontRoot(const fs::path& root) {
    std::lock_guard lock(g_mutex);
    g_root = root;
    g_scanned = false;
    g_packs.clear();
    g_keyCalls = g_keyMisses = 0;
}

int PS4_SYSV_ABI sceAppContentInitialize(const void* initParam, uint32_t* bootParam) {
    (void)initParam;
    if (bootParam) std::memset(bootParam, 0, 0x24);  // OrbisAppContentBootParam; no boot attributes
    std::lock_guard lock(g_mutex);
    Packs();  // scan (and log) once, at start-up
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAppContentAppParamGetInt(uint32_t id, int32_t* value) {
    if (!value) return kErrorParameter;
    if (id == kSkuFlag) *value = kSkuFull;
    else if (id >= kUserParam1 && id <= kUserParam4) *value = 0;
    else return kErrorParameter;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAppContentGetAddcontInfoList(uint32_t serviceLabel, AddcontInfo* list, uint32_t listNum,
                                                 uint32_t* hitNum) {
    std::lock_guard lock(g_mutex);
    const auto& packs = Packs();
    const uint32_t total = serviceLabel == kPackServiceLabel ? static_cast<uint32_t>(packs.size()) : 0;
    if (listNum == 0 || !list) {  // the count only
        if (!hitNum) return kErrorParameter;
        *hitNum = total;
        std::fprintf(stderr,
                     "[addcont] GetAddcontInfoList(service %u): %u packs (count; key calls so far %u, %u missed)\n",
                     serviceLabel, total, g_keyCalls, g_keyMisses);
        return kernel::SCE_OK;
    }
    const uint32_t n = std::min(total, listNum);
    for (uint32_t i = 0; i < n; ++i) {
        std::memset(&list[i], 0, sizeof(AddcontInfo));
        std::memcpy(list[i].label.data, packs[i].label.data(), std::min(packs[i].label.size(), kLabelSize - 1));
        list[i].status = packs[i].status;
    }
    if (hitNum) *hitNum = n;
    std::fprintf(stderr, "[addcont] GetAddcontInfoList(service %u, room for %u): listed %u\n", serviceLabel,
                 listNum, n);
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAppContentGetAddcontInfo(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                             AddcontInfo* info) {
    if (!label || !info) return kErrorParameter;
    std::lock_guard lock(g_mutex);
    const AddcontPack* p = serviceLabel == kPackServiceLabel ? Find(label) : nullptr;
    std::fprintf(stderr, "[addcont] GetAddcontInfo(service %u, %s): %s\n", serviceLabel, Label(label).c_str(),
                 p ? "found" : "not found");
    if (!p) return kErrorNotFound;
    std::memset(info, 0, sizeof(*info));
    std::memcpy(info->label.data, p->label.data(), std::min(p->label.size(), kLabelSize - 1));
    info->status = p->status;
    return kernel::SCE_OK;
}

// The key's bytes are zero, as in shadPS4: LBP3 only checks that the call succeeds.
int PS4_SYSV_ABI sceAppContentGetEntitlementKey(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                                EntitlementKey* key) {
    if (!label || !key) return kErrorParameter;
    std::lock_guard lock(g_mutex);
    ++g_keyCalls;
    const AddcontPack* p = serviceLabel == kPackServiceLabel ? Find(label) : nullptr;
    if (!p) {
        ++g_keyMisses;
        std::fprintf(stderr, "[addcont] GetEntitlementKey(service %u, %s): no such pack\n", serviceLabel,
                     Label(label).c_str());
        return kErrorDrmNoEntitlement;
    }
    std::memset(key->data, 0, kKeySize);
    return kernel::SCE_OK;
}

// LBP3 doesn't import it; license-only packs have nothing to mount.
int PS4_SYSV_ABI sceAppContentAddcontMount(uint32_t serviceLabel, const NpUnifiedEntitlementLabel* label,
                                           MountPoint* mount) {
    (void)mount;
    std::fprintf(stderr, "[addcont] AddcontMount(service %u, %s): not supported\n", serviceLabel,
                 Label(label).c_str());
    return kErrorNotFound;
}

int PS4_SYSV_ABI sceAppContentTemporaryDataMount2(uint32_t option, MountPoint* mount) {
    (void)option;
    if (!mount) return kErrorParameter;
    std::snprintf(mount->data, sizeof(mount->data), "/temp0");
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAppContentTemporaryDataGetAvailableSpaceKb(const MountPoint*, uint64_t* kb) {
    if (!kb) return kErrorParameter;
    *kb = kTempSpaceKb;
    return kernel::SCE_OK;
}

}  // namespace hle::appcontent

namespace hle {

void RegisterAppContent(core::Linker& linker) {
    using namespace appcontent;
    const char* lib = "libSceAppContent";
    HLE_REG(linker, lib, sceAppContentInitialize);
    HLE_REG(linker, lib, sceAppContentAppParamGetInt);
    HLE_REG(linker, lib, sceAppContentGetAddcontInfoList);
    HLE_REG(linker, lib, sceAppContentGetAddcontInfo);
    HLE_REG(linker, lib, sceAppContentGetEntitlementKey);
    HLE_REG(linker, lib, sceAppContentAddcontMount);
    HLE_REG(linker, lib, sceAppContentTemporaryDataMount2);
    HLE_REG(linker, lib, sceAppContentTemporaryDataGetAvailableSpaceKb);
}

}  // namespace hle
