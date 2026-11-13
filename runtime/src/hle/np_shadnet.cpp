// shadNet, phase (a) (best/shadnet_design.md): with LBP3_SHADNET=1 the shadNet client signs in
// (net/shadnet_service.h; hle/np_offline.cpp answers the sign-in state and online ID from it), and
// every NP call the friends, presence, invite and peer-connection features go through is LOGGED
// here with its arguments: libSceNpWebApi (20 imports), the NpUtility lookups (5), libSceNpSignaling
// (7) and the in-game messages (6). The answers stay what they were (the generic stub's 0, or the
// offline in-game message functions), so phase (b) can be written against what LBP3 really asks.
// With shadNet off nothing here is registered and nothing starts.
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hle/kernel_errors.h"
#include "hle/online.h"
#include "hle/register.h"
#include "hle/users.h"
#include "net/shadnet_service.h"

namespace hle::np {
// The offline in-game message functions (np_offline.cpp), wrapped below.
int PS4_SYSV_ABI sceNpInGameMessageInitialize(size_t, void*);
int PS4_SYSV_ABI sceNpInGameMessageTerminate(int32_t);
int PS4_SYSV_ABI sceNpInGameMessageCreateHandle(int32_t);
int PS4_SYSV_ABI sceNpInGameMessageDeleteHandle(int32_t, int32_t);
int PS4_SYSV_ABI sceNpInGameMessagePrepare(int32_t, int32_t, const void*, void*);
int PS4_SYSV_ABI sceNpInGameMessageSendData(int32_t, int32_t, const void*, const void*, size_t);
}  // namespace hle::np

namespace hle::npsn {
namespace {

// A guest pointer's text, read only if the memory is readable (the arguments are logged, not trusted).
std::string Text(const void* p, size_t max = 200) {
    if (!p) return "(null)";
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return "(unreadable)";
    const auto* s = static_cast<const char*>(p);
    const size_t avail = static_cast<size_t>(static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize - s);
    std::string out;
    for (size_t i = 0; i < max && i < avail && s[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        out += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.';
    }
    return "\"" + out + "\"";
}

// One line per call: the first 40 calls of each function, then every 1000th with its count.
void Log(const char* fn, const char* format, ...) {
    static std::mutex m;
    static std::map<std::string, uint64_t> counts;
    uint64_t n;
    {
        std::lock_guard lk(m);
        n = ++counts[fn];
    }
    if (n > 40 && n % 1000 != 0) return;
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n > 40) std::fprintf(stderr, "[shadnet] %s(%s) (call %llu)\n", fn, buf, static_cast<unsigned long long>(n));
    else std::fprintf(stderr, "[shadnet] %s(%s)\n", fn, buf);
}

using u64 = uint64_t;
#define HEX(v) static_cast<unsigned long long>(v)

}  // namespace

// --- libSceNpWebApi: friends lists, presence and sessions go through these (to log first) ------

int PS4_SYSV_ABI sceNpWebApiInitialize(int32_t libHttpCtxId, u64 poolSize) {
    Log(__func__, "libHttpCtxId=%d, poolSize=%llu", libHttpCtxId, HEX(poolSize));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiTerminate(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreateContext(int32_t libCtxId, const void* onlineId) {
    Log(__func__, "libCtxId=%d, onlineId=%s", libCtxId, Text(onlineId, 16).c_str());
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteContext(int32_t titleUserCtxId) {
    Log(__func__, "titleUserCtxId=%d", titleUserCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreateHandle(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteHandle(int32_t libCtxId, int32_t handleId) {
    Log(__func__, "libCtxId=%d, handleId=%d", libCtxId, handleId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* apiGroup, const char* path,
                                          int32_t method, const void* contentParameter, int64_t* requestId) {
    Log(__func__, "titleUserCtxId=%d, apiGroup=%s, path=%s, method=%d, content=%p, requestId=%p", titleUserCtxId,
        Text(apiGroup).c_str(), Text(path, 400).c_str(), method, contentParameter, static_cast<void*>(requestId));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteRequest(int64_t requestId) {
    Log(__func__, "requestId=%lld", static_cast<long long>(requestId));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiSendRequest(int64_t requestId, const void* data, u64 size) {
    Log(__func__, "requestId=%lld, data=%s, size=%llu", static_cast<long long>(requestId),
        Text(data, size < 300 ? static_cast<size_t>(size) : 300).c_str(), HEX(size));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiReadData(int64_t requestId, void* data, u64 size) {
    Log(__func__, "requestId=%lld, data=%p, size=%llu", static_cast<long long>(requestId), data, HEX(size));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiAbortRequest(int64_t requestId) {
    Log(__func__, "requestId=%lld", static_cast<long long>(requestId));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiGetHttpStatusCode(int64_t requestId, int32_t* status) {
    Log(__func__, "requestId=%lld, status=%p", static_cast<long long>(requestId), static_cast<void*>(status));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiGetErrorCode() {
    Log(__func__, "");
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreatePushEventFilter(int32_t libCtxId, const void* filterParam, u64 filterParamNum) {
    Log(__func__, "libCtxId=%d, filterParam=%p (first: %s), count=%llu", libCtxId, filterParam,
        Text(filterParam, 64).c_str(), HEX(filterParamNum));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreateServicePushEventFilter(int32_t libCtxId, int32_t handleId, const char* serviceName,
                                                         uint32_t serviceLabel, const void* filterParam,
                                                         u64 filterParamNum) {
    Log(__func__, "libCtxId=%d, handleId=%d, service=%s, label=%u, filterParam=%p (first: %s), count=%llu",
        libCtxId, handleId, Text(serviceName).c_str(), serviceLabel, filterParam, Text(filterParam, 64).c_str(),
        HEX(filterParamNum));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteServicePushEventFilter(int32_t libCtxId, int32_t filterId) {
    Log(__func__, "libCtxId=%d, filterId=%d", libCtxId, filterId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiRegisterPushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                      void* userArg) {
    Log(__func__, "titleUserCtxId=%d, filterId=%d, cb=%p, arg=%p", titleUserCtxId, filterId, cb, userArg);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiRegisterServicePushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                             void* userArg) {
    Log(__func__, "titleUserCtxId=%d, filterId=%d, cb=%p, arg=%p", titleUserCtxId, filterId, cb, userArg);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiUnregisterPushEventCallback(int32_t titleUserCtxId, int32_t callbackId) {
    Log(__func__, "titleUserCtxId=%d, callbackId=%d", titleUserCtxId, callbackId);
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiUtilityParseNpId(const char* json, void* npId) {
    Log(__func__, "json=%s, npId=%p", Text(json, 300).c_str(), npId);
    return 0;
}

// --- libSceNpUtility lookups (online ID -> NpId) -------------------------------------------------

int PS4_SYSV_ABI sceNpLookupCreateTitleCtx(const void* selfNpId) {
    Log(__func__, "selfNpId=%s", Text(selfNpId, 16).c_str());
    return 0;
}
int PS4_SYSV_ABI sceNpLookupDeleteTitleCtx(int32_t titleCtxId) {
    Log(__func__, "titleCtxId=%d", titleCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpLookupCreateRequest(int32_t titleCtxId) {
    Log(__func__, "titleCtxId=%d", titleCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpLookupDeleteRequest(int32_t reqId) {
    Log(__func__, "reqId=%d", reqId);
    return 0;
}
int PS4_SYSV_ABI sceNpLookupNpId(int32_t reqId, const void* onlineId, void* npId, void* option) {
    Log(__func__, "reqId=%d, onlineId=%s, npId=%p, option=%p", reqId, Text(onlineId, 16).c_str(), npId, option);
    return 0;
}

// --- libSceNpSignaling: the peer connection (phase (c)) ---------------------------------------

int PS4_SYSV_ABI sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t affinity,
                                          int64_t stackSize) {
    Log(__func__, "memory=%lld, priority=%d, affinity=%#x, stack=%lld", static_cast<long long>(memorySize),
        threadPriority, affinity, static_cast<long long>(stackSize));
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingCreateContext(const void* npId, void* cb, void* cbArg, int32_t* ctxId) {
    Log(__func__, "npId=%s, cb=%p, arg=%p, ctxId=%p", Text(npId, 16).c_str(), cb, cbArg, static_cast<void*>(ctxId));
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingDeleteContext(int32_t ctxId) {
    Log(__func__, "ctxId=%d", ctxId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingActivateConnection(int32_t ctxId, const void* peerNpId, int32_t* connId) {
    Log(__func__, "ctxId=%d, peer=%s, connId=%p", ctxId, Text(peerNpId, 16).c_str(), static_cast<void*>(connId));
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingTerminateConnection(int32_t ctxId, int32_t connId) {
    Log(__func__, "ctxId=%d, connId=%d", ctxId, connId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingGetConnectionStatus(int32_t ctxId, int32_t connId, uint32_t* status, uint32_t* addr,
                                                   uint16_t* port) {
    Log(__func__, "ctxId=%d, connId=%d", ctxId, connId);
    (void)status, (void)addr, (void)port;
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingGetConnectionInfo(int32_t ctxId, int32_t connId, int32_t code, void* info) {
    Log(__func__, "ctxId=%d, connId=%d, code=%d, info=%p", ctxId, connId, code, info);
    return 0;
}

// --- In-game messages (probably the join requests between players): logged, answered as offline ---

int PS4_SYSV_ABI sceNpInGameMessageInitialize(size_t poolSize, void* option) {
    Log(__func__, "poolSize=%llu, option=%p", HEX(poolSize), option);
    return np::sceNpInGameMessageInitialize(poolSize, option);
}
int PS4_SYSV_ABI sceNpInGameMessageTerminate(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return np::sceNpInGameMessageTerminate(libCtxId);
}
int PS4_SYSV_ABI sceNpInGameMessageCreateHandle(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return np::sceNpInGameMessageCreateHandle(libCtxId);
}
int PS4_SYSV_ABI sceNpInGameMessageDeleteHandle(int32_t libCtxId, int32_t handleId) {
    Log(__func__, "libCtxId=%d, handleId=%d", libCtxId, handleId);
    return np::sceNpInGameMessageDeleteHandle(libCtxId, handleId);
}
int PS4_SYSV_ABI sceNpInGameMessagePrepare(int32_t libCtxId, int32_t handleId, const void* param, void* out) {
    Log(__func__, "libCtxId=%d, handleId=%d, param=%p, out=%p", libCtxId, handleId, param, out);
    return np::sceNpInGameMessagePrepare(libCtxId, handleId, param, out);
}
int PS4_SYSV_ABI sceNpInGameMessageSendData(int32_t libCtxId, int32_t handleId, const void* to, const void* data,
                                            size_t size) {
    Log(__func__, "libCtxId=%d, handleId=%d, to=%s, data=%s, size=%llu", libCtxId, handleId, Text(to, 16).c_str(),
        Text(data, size < 200 ? size : 200).c_str(), HEX(size));
    return np::sceNpInGameMessageSendData(libCtxId, handleId, to, data, size);
}

}  // namespace hle::npsn

namespace hle {

// Registered after the offline NP functions, so the in-game message wrappers replace them; only
// with LBP3_SHADNET=1 and complete settings (net/shadnet_service.h). Starts the client here, before
// the game's first NP call.
void RegisterShadNet(core::Linker& linker) {
    const auto& cfg = net::shadnet::Settings();
    if (!cfg.enabled) return;
    if (online::Enabled() && !online::Name().empty() && online::Name() != cfg.name)
        std::fprintf(stderr,
                     "[shadnet] warning: the online name (%s) and the shadNet name (%s) differ; the game uses %s. "
                     "Both accounts should have the same name.\n",
                     online::Name().c_str(), cfg.name.c_str(), online::Name().c_str());
    // A sign-in that settles after the game's first state check reaches the game as a new notice.
    net::shadnet::SetStatusListener([](net::shadnet::Status s) {
        if (s == net::shadnet::Status::SignedIn || s == net::shadnet::Status::Failed) np::QueueStateNotice(1);
    });
    net::shadnet::Start();

    using namespace npsn;
    const char* w = "libSceNpWebApi";
    HLE_REG(linker, w, sceNpWebApiInitialize);
    HLE_REG(linker, w, sceNpWebApiTerminate);
    HLE_REG(linker, w, sceNpWebApiCreateContext);
    HLE_REG(linker, w, sceNpWebApiDeleteContext);
    HLE_REG(linker, w, sceNpWebApiCreateHandle);
    HLE_REG(linker, w, sceNpWebApiDeleteHandle);
    HLE_REG(linker, w, sceNpWebApiCreateRequest);
    HLE_REG(linker, w, sceNpWebApiDeleteRequest);
    HLE_REG(linker, w, sceNpWebApiSendRequest);
    HLE_REG(linker, w, sceNpWebApiReadData);
    HLE_REG(linker, w, sceNpWebApiAbortRequest);
    HLE_REG(linker, w, sceNpWebApiGetHttpStatusCode);
    HLE_REG(linker, w, sceNpWebApiGetErrorCode);
    HLE_REG(linker, w, sceNpWebApiCreatePushEventFilter);
    HLE_REG(linker, w, sceNpWebApiCreateServicePushEventFilter);
    HLE_REG(linker, w, sceNpWebApiDeleteServicePushEventFilter);
    HLE_REG(linker, w, sceNpWebApiRegisterPushEventCallback);
    HLE_REG(linker, w, sceNpWebApiRegisterServicePushEventCallback);
    HLE_REG(linker, w, sceNpWebApiUnregisterPushEventCallback);
    HLE_REG(linker, w, sceNpWebApiUtilityParseNpId);
    const char* u = "libSceNpUtility";
    HLE_REG(linker, u, sceNpLookupCreateTitleCtx);
    HLE_REG(linker, u, sceNpLookupDeleteTitleCtx);
    HLE_REG(linker, u, sceNpLookupCreateRequest);
    HLE_REG(linker, u, sceNpLookupDeleteRequest);
    HLE_REG(linker, u, sceNpLookupNpId);
    const char* s = "libSceNpSignaling";
    HLE_REG(linker, s, sceNpSignalingInitialize);
    HLE_REG(linker, s, sceNpSignalingCreateContext);
    HLE_REG(linker, s, sceNpSignalingDeleteContext);
    HLE_REG(linker, s, sceNpSignalingActivateConnection);
    HLE_REG(linker, s, sceNpSignalingTerminateConnection);
    HLE_REG(linker, s, sceNpSignalingGetConnectionStatus);
    HLE_REG(linker, s, sceNpSignalingGetConnectionInfo);
    const char* m = "libSceNpManager";
    HLE_REG(linker, m, sceNpInGameMessageInitialize);
    HLE_REG(linker, m, sceNpInGameMessageTerminate);
    HLE_REG(linker, m, sceNpInGameMessageCreateHandle);
    HLE_REG(linker, m, sceNpInGameMessageDeleteHandle);
    HLE_REG(linker, m, sceNpInGameMessagePrepare);
    HLE_REG(linker, m, sceNpInGameMessageSendData);
}

}  // namespace hle
