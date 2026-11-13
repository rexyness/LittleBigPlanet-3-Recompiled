// shadNet, phase (a) (best/shadnet_design.md): with LBP3_SHADNET=1 the shadNet client signs in
// (net/shadnet_service.h; hle/np_offline.cpp answers the sign-in state and online ID from it), and
// every NP call the friends, presence, invite and peer-connection features go through is LOGGED
// here with its arguments: libSceNpWebApi (20 imports), the NpUtility lookups (5), libSceNpSignaling
// (7) and the in-game messages (6).
//
// Online (LBP3_ONLINE=1 or LBP3_SHADNET=1) the PSN Web API is answered here, locally, as LBPPC does
// when there is no PSN behind it: every context, handle, filter and request gets a real (positive)
// id, a request "succeeds" with HTTP 200, the friend and block lists are empty and anything else is
// an empty JSON object. The NpUtility lookups and the signaling context give real ids too; peer
// connections themselves are phase (c), and the in-game messages stay the offline answers.
// With both off nothing here is registered and nothing starts.
//
// The local answers (LocalWebApiBody) are adapted from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC
// at aad6a91), gpu/bbnet/vendor/shadps4/core/libraries/network/http.cpp AnswerLocalWebApi (SPDX
// GPL-2.0-or-later, Copyright 2024-2026 shadPS4 Emulator Project, with LBPPC's "lbppc:" changes); the
// id shapes follow shadPS4's libraries/np/np_web_api/np_web_api_internal.cpp (GPL-2.0-or-later).
#include <algorithm>
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

// --- libSceNpWebApi: friends lists, presence and sessions, answered locally ---------------------

namespace {

constexpr int kWebApiInvalidArgument = static_cast<int>(0x80552902);
constexpr int kWebApiRequestNotFound = static_cast<int>(0x80552906);

struct WebApiRequest {
    std::string apiGroup, path;
    int32_t method = 0;
    bool sent = false;
    std::string body;  // the answer, once sent
    size_t readPos = 0;
};

std::mutex g_webMutex;
int32_t g_webLibCtx = 0, g_webUserCtx = 0, g_webHandle = 0, g_webFilter = 0, g_webCallback = 0;
uint32_t g_webRequestCount = 0;
std::map<int64_t, WebApiRequest> g_webRequests;

// Adapted from LBPPC's http.cpp AnswerLocalWebApi (GPL-2.0-or-later; see the file comment): signed in
// without PSN, the friend and block lists are empty and anything else is an empty JSON object. A
// trailing slash before the query is dropped, as LBPPC's shadNet Web API URL does ("LBP3 reads an
// invitation at .../invitations/<id>/?fields=...").
std::string LocalWebApiBody(std::string path) {
    if (const auto q = path.find('?'); q != std::string::npos && q > 0 && path[q - 1] == '/') path.erase(q - 1, 1);
    else if (q == std::string::npos && path.size() > 1 && path.back() == '/') path.pop_back();
    const auto list = [](const char* name) {
        return std::string("{\"") + name + "\":[],\"start\":0,\"size\":0,\"totalResults\":0}";
    };
    const std::string route = path.substr(0, path.find('?'));
    return route.find("/friendList") != std::string::npos ? list("friendList")
           : route.find("/blockList") != std::string::npos ? list("blockList")
                                                           : std::string("{}");
}

}  // namespace

int PS4_SYSV_ABI sceNpWebApiInitialize(int32_t libHttpCtxId, u64 poolSize) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webLibCtx = g_webLibCtx >= 0x7fff ? 1 : g_webLibCtx + 1;
    }
    Log(__func__, "libHttpCtxId=%d, poolSize=%llu -> libCtxId %d", libHttpCtxId, HEX(poolSize), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiTerminate(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return 0;
}
// A user context's id carries its library context's, as shadPS4's: (libCtxId << 16) | n.
int PS4_SYSV_ABI sceNpWebApiCreateContext(int32_t libCtxId, const void* onlineId) {
    if (libCtxId <= 0) return kWebApiInvalidArgument;
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        g_webUserCtx = g_webUserCtx >= 0xffff ? 1 : g_webUserCtx + 1;
        id = (libCtxId << 16) | g_webUserCtx;
    }
    Log(__func__, "libCtxId=%d, onlineId=%s -> titleUserCtxId %#x", libCtxId, Text(onlineId, 16).c_str(), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiDeleteContext(int32_t titleUserCtxId) {
    Log(__func__, "titleUserCtxId=%#x", titleUserCtxId);
    std::lock_guard lk(g_webMutex);
    std::erase_if(g_webRequests, [&](const auto& r) { return (r.first >> 32) == titleUserCtxId; });
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreateHandle(int32_t libCtxId) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webHandle = g_webHandle >= 0x7fffffff ? 1 : g_webHandle + 1;
    }
    Log(__func__, "libCtxId=%d -> handleId %d", libCtxId, id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiDeleteHandle(int32_t libCtxId, int32_t handleId) {
    Log(__func__, "libCtxId=%d, handleId=%d", libCtxId, handleId);
    return 0;
}
// A request's id carries its user context's, as shadPS4's: (titleUserCtxId << 32) | n.
int PS4_SYSV_ABI sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* apiGroup, const char* path,
                                          int32_t method, const void* contentParameter, int64_t* requestId) {
    if (titleUserCtxId <= 0 || !apiGroup || !path || !requestId) {
        Log(__func__, "titleUserCtxId=%#x, apiGroup=%s, path=%s -> invalid argument", titleUserCtxId,
            Text(apiGroup).c_str(), Text(path, 400).c_str());
        return kWebApiInvalidArgument;
    }
    int64_t id;
    {
        std::lock_guard lk(g_webMutex);
        g_webRequestCount = g_webRequestCount >= 0x7fffffff ? 1 : g_webRequestCount + 1;
        id = (static_cast<int64_t>(titleUserCtxId) << 32) | g_webRequestCount;
        WebApiRequest& r = g_webRequests[id];
        r.apiGroup = apiGroup;
        r.path = path;
        r.method = method;
    }
    *requestId = id;
    Log(__func__, "titleUserCtxId=%#x, apiGroup=%s, path=%s, method=%d, content=%p -> requestId %#llx",
        titleUserCtxId, Text(apiGroup).c_str(), Text(path, 400).c_str(), method, contentParameter, HEX(id));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteRequest(int64_t requestId) {
    Log(__func__, "requestId=%#llx", HEX(requestId));
    std::lock_guard lk(g_webMutex);
    return g_webRequests.erase(requestId) ? 0 : kWebApiRequestNotFound;
}
int PS4_SYSV_ABI sceNpWebApiSendRequest(int64_t requestId, const void* data, u64 size) {
    std::string group, path, body;
    {
        std::lock_guard lk(g_webMutex);
        auto it = g_webRequests.find(requestId);
        if (it == g_webRequests.end()) {
            Log(__func__, "requestId=%#llx -> not found", HEX(requestId));
            return kWebApiRequestNotFound;
        }
        WebApiRequest& r = it->second;
        r.body = LocalWebApiBody(r.path);
        r.sent = true;
        r.readPos = 0;
        group = r.apiGroup;
        path = r.path;
        body = r.body;
    }
    Log(__func__, "requestId=%#llx (%s %s), data=%s, size=%llu -> 200 %s", HEX(requestId), group.c_str(),
        path.c_str(), Text(data, size < 300 ? static_cast<size_t>(size) : 300).c_str(), HEX(size), body.c_str());
    return 0;
}
// The bytes copied, 0 at the end.
int PS4_SYSV_ABI sceNpWebApiReadData(int64_t requestId, void* data, u64 size) {
    if (!data || !size) return kWebApiInvalidArgument;
    std::lock_guard lk(g_webMutex);
    auto it = g_webRequests.find(requestId);
    if (it == g_webRequests.end()) return kWebApiRequestNotFound;
    WebApiRequest& r = it->second;
    const size_t n = std::min<size_t>(static_cast<size_t>(size), r.body.size() - std::min(r.readPos, r.body.size()));
    if (n) std::memcpy(data, r.body.data() + r.readPos, n);
    r.readPos += n;
    return static_cast<int>(n);
}
int PS4_SYSV_ABI sceNpWebApiAbortRequest(int64_t requestId) {
    Log(__func__, "requestId=%#llx", HEX(requestId));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiGetHttpStatusCode(int64_t requestId, int32_t* status) {
    if (!status) return kWebApiInvalidArgument;
    std::lock_guard lk(g_webMutex);
    auto it = g_webRequests.find(requestId);
    if (it == g_webRequests.end()) return kWebApiRequestNotFound;
    *status = 200;
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiGetErrorCode() {
    Log(__func__, "");
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreatePushEventFilter(int32_t libCtxId, const void* filterParam, u64 filterParamNum) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webFilter = g_webFilter >= 0x7fffffff ? 1 : g_webFilter + 1;
    }
    Log(__func__, "libCtxId=%d, filterParam=%p (first: %s), count=%llu -> filterId %d", libCtxId, filterParam,
        Text(filterParam, 64).c_str(), HEX(filterParamNum), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiCreateServicePushEventFilter(int32_t libCtxId, int32_t handleId, const char* serviceName,
                                                         uint32_t serviceLabel, const void* filterParam,
                                                         u64 filterParamNum) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webFilter = g_webFilter >= 0x7fffffff ? 1 : g_webFilter + 1;
    }
    Log(__func__, "libCtxId=%d, handleId=%d, service=%s, label=%u, filterParam=%p (first: %s), count=%llu -> filterId %d",
        libCtxId, handleId, Text(serviceName).c_str(), serviceLabel, filterParam, Text(filterParam, 64).c_str(),
        HEX(filterParamNum), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiDeleteServicePushEventFilter(int32_t libCtxId, int32_t filterId) {
    Log(__func__, "libCtxId=%d, filterId=%d", libCtxId, filterId);
    return 0;
}
// Registered, never called: no push events come without PSN (shadNet's would be phase (b)).
int PS4_SYSV_ABI sceNpWebApiRegisterPushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                      void* userArg) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webCallback = g_webCallback >= 0x7fffffff ? 1 : g_webCallback + 1;
    }
    Log(__func__, "titleUserCtxId=%#x, filterId=%d, cb=%p, arg=%p -> callbackId %d", titleUserCtxId, filterId, cb,
        userArg, id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiRegisterServicePushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                             void* userArg) {
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webCallback = g_webCallback >= 0x7fffffff ? 1 : g_webCallback + 1;
    }
    Log(__func__, "titleUserCtxId=%#x, filterId=%d, cb=%p, arg=%p -> callbackId %d", titleUserCtxId, filterId, cb,
        userArg, id);
    return id;
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

// Ids from 1, as shadPS4's (libraries/np/np_utility/np_utility.cpp).
std::atomic<int32_t> g_lookupCtx{0}, g_lookupRequest{0}, g_signalingCtx{0}, g_signalingConn{0};
constexpr int kCommunityInsufficientArgument = static_cast<int>(0x8055070c);
constexpr int kSignalingInvalidArgument = static_cast<int>(0x80552715);

int PS4_SYSV_ABI sceNpLookupCreateTitleCtx(const void* selfNpId) {
    if (!selfNpId) return kCommunityInsufficientArgument;
    const int32_t id = ++g_lookupCtx;
    Log(__func__, "selfNpId=%s -> titleCtxId %d", Text(selfNpId, 16).c_str(), id);
    return id;
}
int PS4_SYSV_ABI sceNpLookupDeleteTitleCtx(int32_t titleCtxId) {
    Log(__func__, "titleCtxId=%d", titleCtxId);
    return 0;
}
int PS4_SYSV_ABI sceNpLookupCreateRequest(int32_t titleCtxId) {
    const int32_t id = ++g_lookupRequest;
    Log(__func__, "titleCtxId=%d -> reqId %d", titleCtxId, id);
    return id;
}
int PS4_SYSV_ABI sceNpLookupDeleteRequest(int32_t reqId) {
    Log(__func__, "reqId=%d", reqId);
    return 0;
}
// An online ID's NpId: the ID itself (SceNpId: SceNpOnlineId { char data[16]; char term; char dummy[3] },
// then opt[8] and reserved[8]), as a lookup on a community server knows nothing more about a player.
int PS4_SYSV_ABI sceNpLookupNpId(int32_t reqId, const void* onlineId, void* npId, void* option) {
    Log(__func__, "reqId=%d, onlineId=%s, npId=%p, option=%p", reqId, Text(onlineId, 16).c_str(), npId, option);
    if (!onlineId || !npId) return kCommunityInsufficientArgument;
    std::memset(npId, 0, 36);
    std::memcpy(npId, onlineId, 16);
    return 0;
}

// --- libSceNpSignaling: the peer connection (phase (c)) ---------------------------------------

int PS4_SYSV_ABI sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t affinity,
                                          int64_t stackSize) {
    Log(__func__, "memory=%lld, priority=%d, affinity=%#x, stack=%lld", static_cast<long long>(memorySize),
        threadPriority, affinity, static_cast<long long>(stackSize));
    return 0;
}
// A context id from 1 (shadPS4's np_signaling.cpp signature: npId, callback, its argument, the id out).
int PS4_SYSV_ABI sceNpSignalingCreateContext(const void* npId, void* cb, void* cbArg, int32_t* ctxId) {
    if (!npId || !ctxId) return kSignalingInvalidArgument;
    *ctxId = ++g_signalingCtx;
    Log(__func__, "npId=%s, cb=%p, arg=%p -> ctxId %d", Text(npId, 16).c_str(), cb, cbArg, *ctxId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingDeleteContext(int32_t ctxId) {
    Log(__func__, "ctxId=%d", ctxId);
    return 0;
}
// Peer connections are phase (c): a connection gets an id but stays inactive (no address).
int PS4_SYSV_ABI sceNpSignalingActivateConnection(int32_t ctxId, const void* peerNpId, int32_t* connId) {
    if (!peerNpId || !connId) return kSignalingInvalidArgument;
    *connId = ++g_signalingConn;
    Log(__func__, "ctxId=%d, peer=%s -> connId %d (stays inactive: no peer connections yet)", ctxId,
        Text(peerNpId, 16).c_str(), *connId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingTerminateConnection(int32_t ctxId, int32_t connId) {
    Log(__func__, "ctxId=%d, connId=%d", ctxId, connId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingGetConnectionStatus(int32_t ctxId, int32_t connId, uint32_t* status, uint32_t* addr,
                                                   uint16_t* port) {
    Log(__func__, "ctxId=%d, connId=%d -> inactive", ctxId, connId);
    if (status) *status = 0;  // ORBIS_NP_SIGNALING_CONN_STATUS_INACTIVE
    if (addr) *addr = 0;
    if (port) *port = 0;
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

// Registered after the offline NP functions, so these replace them: the Web API, lookups and
// signaling online (LBP3_ONLINE=1 or LBP3_SHADNET=1); the shadNet client and the in-game message
// wrappers only with LBP3_SHADNET=1 and complete settings (net/shadnet_service.h). Starts the
// client here, before the game's first NP call.
void RegisterShadNet(core::Linker& linker) {
    const auto& cfg = net::shadnet::Settings();
    if (!cfg.enabled && !online::Enabled()) return;
    if (cfg.enabled && online::Enabled() && !online::Name().empty() && online::Name() != cfg.name)
        std::fprintf(stderr,
                     "[shadnet] warning: the online name (%s) and the shadNet name (%s) differ; the game uses %s. "
                     "Both accounts should have the same name.\n",
                     online::Name().c_str(), cfg.name.c_str(), online::Name().c_str());
    // A sign-in that settles after the game's first state check reaches the game as a new notice.
    if (cfg.enabled) {
        net::shadnet::SetStatusListener([](net::shadnet::Status s) {
            if (s == net::shadnet::Status::SignedIn || s == net::shadnet::Status::Failed) {
                std::fprintf(stderr, "[shadnet] %s: the sign-in state is reported to the game again\n",
                             s ==net::shadnet::Status::SignedIn ? "signed in" : "sign-in failed");
                np::QueueStateNotice(1);
            }
        });
        net::shadnet::Start();
    }

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
    if (!cfg.enabled) return;
    const char* m = "libSceNpManager";
    HLE_REG(linker, m, sceNpInGameMessageInitialize);
    HLE_REG(linker, m, sceNpInGameMessageTerminate);
    HLE_REG(linker, m, sceNpInGameMessageCreateHandle);
    HLE_REG(linker, m, sceNpInGameMessageDeleteHandle);
    HLE_REG(linker, m, sceNpInGameMessagePrepare);
    HLE_REG(linker, m, sceNpInGameMessageSendData);
}

}  // namespace hle
