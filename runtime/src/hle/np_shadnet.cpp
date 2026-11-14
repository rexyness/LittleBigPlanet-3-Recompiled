// shadNet (best/shadnet_design.md): with LBP3_SHADNET=1 the shadNet client signs in
// (net/shadnet_service.h; hle/np_offline.cpp answers the sign-in state and online ID from it), and the
// NP calls the friends, presence, invite and peer-connection features go through are answered here
// and logged with their arguments: libSceNpWebApi (20 imports), the NpUtility lookups (5),
// libSceNpSignaling (7) and the in-game messages (6).
//
// Online (LBP3_ONLINE=1 or LBP3_SHADNET=1) every Web API context, handle, filter and request gets a
// real (positive) id. Signed in to shadNet, requests go to the server's Web API and its push events
// reach the game's callbacks (phase (b)); otherwise they're answered locally as LBPPC does when there
// is no PSN behind it (HTTP 200, the friend list from shadNet's friends, an empty block list, {} for
// the rest). The NpUtility lookups give real ids too. With shadNet on, signaling connections are real
// (phase (c): net/p2p_link.h, directly or through shadNet's relay, friends only; see the signaling
// section), and invitations are received and sent through the game custom data dialog. Signed in,
// the in-game messages are accepted (and go nowhere, as with LBPPC: LBP3 waits at "Connecting" when
// its messaging fails).
// With both off nothing here is registered and nothing starts.
//
// Adapted from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC at aad6a91), all SPDX
// GPL-2.0-or-later (Copyright 2024-2026 shadPS4 Emulator Project, with LBPPC's "lbppc:" changes):
// the local answers from gpu/bbnet/vendor/shadps4/core/libraries/network/http.cpp AnswerLocalWebApi;
// the id shapes, the push event layouts and matching from libraries/np/np_web_api/np_web_api.h,
// np_web_api.cpp and np_web_api_internal.cpp; the path rule from include/core/emulator_settings.h;
// the invitation event from invitation_dialog.h and np_handler_invitation.cpp; the signaling
// constants and connection info from np_types2.h and np_signaling_state.cpp.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "core/log_stamp.h"
#include "core/runner.h"
#include "hle/common_dialog.h"
#include "hle/kernel_errors.h"
#include "hle/net_remote.h"
#include "hle/online.h"
#include "hle/register.h"
#include "hle/users.h"
#include "core/exact_math.h"
#include "net/p2p_link.h"
#include "net/shadnet_service.h"
#include "net/web_request.h"

namespace hle::np {
// The offline in-game message functions (np_offline.cpp), wrapped below.
int PS4_SYSV_ABI sceNpInGameMessageInitialize(size_t, void*);
int PS4_SYSV_ABI sceNpInGameMessageTerminate(int32_t);
int PS4_SYSV_ABI sceNpInGameMessageCreateHandle(int32_t);
int PS4_SYSV_ABI sceNpInGameMessageDeleteHandle(int32_t, int32_t);
int PS4_SYSV_ABI sceNpInGameMessagePrepare(int32_t, int32_t, const void*, void*);
int PS4_SYSV_ABI sceNpInGameMessageSendData(int32_t, int32_t, const void*, const void*, size_t);
}  // namespace hle::np

namespace hle::system {
// hle/system_services.cpp, wrapped below.
struct SystemServiceStatus;
int PS4_SYSV_ABI sceSystemServiceGetStatus(SystemServiceStatus* status);
}  // namespace hle::system

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

// --- libSceNpWebApi: friends lists, presence, sessions and invitations ---------------------------
//
// Signed in to shadNet, a request goes to the server's Web API (net::shadnet::WebApiBase(), with the
// session token as "Authorization: Bearer"), as LBPPC's shadPS4 does (np_web_api_internal.cpp
// sendRequest): the server keeps the friends, their presence, the sessions and the invitations, and
// tells the other player's game through a push event. Otherwise (LBP3_ONLINE only, no token yet,
// the server unreachable, LBP3_SHADNET_WEBAPI=local) it's answered here: the friend list is
// shadNet's friends (none without shadNet), the block list is empty, anything else is {}.
//
// The game asks for its own avatar about once a frame while it has none, so a GET of a profile or a
// friend or block list is answered from a short cache instead of going to the server each time.

namespace {

constexpr int kWebApiInvalidArgument = static_cast<int>(0x80552902);
constexpr int kWebApiRequestNotFound = static_cast<int>(0x80552906);

// SceNpWebApiContentParameter (shadPS4's np_web_api.h).
struct ContentParameter {
    uint64_t contentLength;
    const char* contentType;
    uint8_t reserved[16];
};

struct WebApiRequest {
    std::string apiGroup, path, contentType;
    int32_t method = 0;
    bool sent = false;
    int status = 0;
    std::string body;  // the answer, once sent
    size_t readPos = 0;
};

// Push event filters and the callbacks registered on them. The layouts of a filter's data types and
// of the callbacks' arguments are shadPS4's (LBPPC aad6a91, libraries/np/np_web_api/np_web_api.h and
// np_web_api_internal.cpp, GPL-2.0-or-later): a data type is char[64 + 1]; the service callback is the
// basic one with the service name and label after the callback id.
constexpr size_t kDataTypeLen = 64;
struct DataType {
    char val[kDataTypeLen + 1];
};
struct OnlineIdC {
    char data[16];
    int8_t term;
    int8_t dummy[3];
};
struct PushPeerAddress {
    OnlineIdC onlineId;
    int32_t platform;
};
using BasicCallback = void(PS4_SYSV_ABI*)(int32_t userCtxId, int32_t callbackId, const PushPeerAddress* to,
                                          const PushPeerAddress* from, const DataType* dataType, const char* data,
                                          u64 dataLen, void* userArg);
using ServiceCallback = void(PS4_SYSV_ABI*)(int32_t userCtxId, int32_t callbackId, const char* serviceName,
                                            uint32_t serviceLabel, const PushPeerAddress* to,
                                            const PushPeerAddress* from, const DataType* dataType,
                                            const char* data, u64 dataLen, void* userArg);

struct PushFilter {
    bool service = false;
    std::string serviceName;
    uint32_t label = 0;
    std::vector<std::string> dataTypes;
};
struct PushCallback {
    int32_t userCtx = 0, filterId = 0;
    void* fn = nullptr;
    void* arg = nullptr;
    bool service = false;
};

struct CachedAnswer {
    std::chrono::steady_clock::time_point at;
    int status = 0;
    std::string body;
};

std::mutex g_webMutex;
int32_t g_webLibCtx = 0, g_webUserCtx = 0, g_webHandle = 0, g_webFilter = 0, g_webCallback = 0;
uint32_t g_webRequestCount = 0;
std::map<int64_t, WebApiRequest> g_webRequests;
std::map<int32_t, PushFilter> g_pushFilters;
std::map<int32_t, PushCallback> g_pushCallbacks;
std::map<std::string, CachedAnswer> g_getCache;

// Guest memory is read only where it's committed and readable.
bool Readable(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return static_cast<const char*>(p) + n <= static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
}

std::string CString(const char* p, size_t max) {
    std::string s;
    if (!p) return s;
    for (size_t i = 0; i < max && Readable(p + i, 1) && p[i]; ++i) s += p[i];
    return s;
}

// A filter's data types: count entries of char[65].
std::vector<std::string> DataTypes(const void* params, u64 count) {
    std::vector<std::string> out;
    const auto* p = static_cast<const DataType*>(params);
    for (u64 i = 0; p && i < count && i < 64; ++i) {
        if (!Readable(&p[i], sizeof(DataType))) break;
        out.emplace_back(p[i].val, strnlen(p[i].val, kDataTypeLen));
    }
    return out;
}

const char* MethodName(int32_t method) {
    switch (method) {
    case 0: return "GET";
    case 1: return "POST";
    case 2: return "PUT";
    case 3: return "DELETE";
    case 4: return "PATCH";
    default: return nullptr;
    }
}

// A path's trailing slash before its query is dropped: LBP3 reads an invitation at
// /v1/users/<id>/invitations/<id>/?fields=..., which shadNet routes only without it (LBPPC's
// emulator_settings.h GetShadNetWebApiUrl, GPL-2.0-or-later).
std::string NormalizedPath(std::string path) {
    const size_t q = path.find('?');
    const size_t end = q == std::string::npos ? path.size() : q;
    if (end > 1 && path[end - 1] == '/') path.erase(end - 1, 1);
    return path;
}

std::string Route(const std::string& path) { return path.substr(0, path.find('?')); }

std::string QueryValue(const std::string& path, const std::string& key) {
    const size_t q = path.find('?');
    if (q == std::string::npos) return {};
    size_t at = q + 1;
    while (at < path.size()) {
        const size_t amp = path.find('&', at);
        const std::string item = path.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
        if (item.rfind(key + "=", 0) == 0) return item.substr(key.size() + 1);
        if (amp == std::string::npos) break;
        at = amp + 1;
    }
    return {};
}

bool EndsWith(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// Seconds a GET's answer is reused (0: never).
int CacheSeconds(const std::string& path) {
    const std::string route = Route(path);
    if (EndsWith(route, "/profile")) return 30;
    if (EndsWith(route, "/blockList")) return 30;
    if (EndsWith(route, "/friendList")) return 5;
    return 0;
}

std::string JsonEscape(const std::string& in) {
    std::string out;
    for (const char c : in) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64(const std::string& in) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = static_cast<uint8_t>(in[i]) << 16 | static_cast<uint8_t>(in[i + 1]) << 8 |
                           static_cast<uint8_t>(in[i + 2]);
        out += kBase64[v >> 18 & 63];
        out += kBase64[v >> 12 & 63];
        out += kBase64[v >> 6 & 63];
        out += kBase64[v & 63];
    }
    if (i < in.size()) {
        uint32_t v = static_cast<uint8_t>(in[i]) << 16;
        if (i + 1 < in.size()) v |= static_cast<uint8_t>(in[i + 1]) << 8;
        out += kBase64[v >> 18 & 63];
        out += kBase64[v >> 12 & 63];
        out += i + 1 < in.size() ? kBase64[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

std::string Unbase64(const std::string& in) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        const char* at = std::strchr(kBase64, c);
        if (!at || !c) continue;  // '=', whitespace
        acc = acc << 6 | static_cast<uint32_t>(at - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>(acc >> bits & 0xff);
        }
    }
    return out;
}

// A stable stand-in account id for a friend answered locally (the server has the real ones).
uint64_t LocalAccountId(const std::string& name) {
    uint64_t h = 1469598103934665603ull;
    for (const char c : name) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return (h & 0x7fffffffffffffffull) | 1;
}

// The friend list in the shape shadNet's server gives (webapi_routes_users.cpp BuildFriendList): who is
// online is known from shadNet, but not whether they're in this game, so an "incontext" presence list
// stays empty.
std::string LocalFriendList(const std::string& path) {
    const auto friends = net::shadnet::Friends();
    const std::string presenceType = QueryValue(path, "presenceType");
    std::string list;
    for (const auto& f : friends) {
        const std::string name = JsonEscape(f.npid);
        const std::string account = std::to_string(LocalAccountId(f.npid));
        std::string e = "{\"user\":{\"onlineId\":\"" + name + "\",\"accountId\":\"" + account + "\"},\"onlineId\":\"" +
                        name + "\",\"accountId\":\"" + account + "\",\"npId\":\"" + Base64(f.npid + "@") +
                        "\",\"personalDetail\":{\"displayName\":\"" + name + "\"}";
        if (!presenceType.empty()) {
            const std::string status = f.online ? "online" : "offline";
            e += ",\"presence\":{\"onlineStatus\":\"" + status + "\"";
            if (presenceType == "incontext") e += ",\"incontextInfoList\":[]";
            else if (presenceType == "platform") e += ",\"platformInfoList\":[]";
            else e += ",\"primaryInfo\":{\"onlineStatus\":\"" + status + "\"}";
            e += "}";
        }
        e += "}";
        list += (list.empty() ? "" : ",") + e;
    }
    const std::string n = std::to_string(friends.size());
    return "{\"friendList\":[" + list + "],\"start\":0,\"size\":" + n + ",\"totalResults\":" + n + "}";
}

// The local answers, adapted from LBPPC's http.cpp AnswerLocalWebApi (GPL-2.0-or-later; see the file
// comment), with shadNet's friends in the friend list.
std::string LocalWebApiBody(const std::string& path) {
    const std::string route = Route(path);
    if (route.find("/friendList") != std::string::npos) return LocalFriendList(path);
    if (route.find("/blockList") != std::string::npos) return "{\"blockList\":[],\"start\":0,\"size\":0,\"totalResults\":0}";
    return "{}";
}

struct Answer {
    int status = 200;
    std::string body;
    const char* source = "local";
};

// Where a request is answered (see the section comment). Runs without g_webMutex held: a server
// request blocks this game thread for its round trip (up to 10 s), as on a console.
Answer AnswerRequest(int32_t method, const std::string& rawPath, const std::string& contentType,
                     const std::string& payload) {
    const std::string path = NormalizedPath(rawPath);
    const std::string base = net::shadnet::WebApiBase();
    const char* verb = MethodName(method);
    if (!base.empty() && verb && net::shadnet::GetStatus() == net::shadnet::Status::SignedIn) {
        const std::string token = net::shadnet::BearerToken();
        if (!token.empty()) {
            const int ttl = method == 0 ? CacheSeconds(path) : 0;
            if (ttl > 0) {
                std::lock_guard lk(g_webMutex);
                if (auto it = g_getCache.find(path);
                    it != g_getCache.end() && std::chrono::steady_clock::now() - it->second.at < std::chrono::seconds(ttl))
                    return {it->second.status, it->second.body, "cache"};
            }
            std::vector<std::pair<std::string, std::string>> headers{{"Authorization", "Bearer " + token}};
            if (!contentType.empty()) headers.emplace_back("Content-Type", contentType);
            const net::web::Response r = net::web::Request(verb, base + path, headers, payload);
            if (r.ok) {
                if (ttl > 0) {
                    std::lock_guard lk(g_webMutex);
                    g_getCache[path] = {std::chrono::steady_clock::now(), r.status, r.body};
                }
                return {r.status, r.body, "server"};
            }
            std::fprintf(stderr, "[shadnet] Web API %s %s: %s; answered locally\n", verb, path.c_str(), r.error.c_str());
        }
    }
    return {200, LocalWebApiBody(path), "local"};
}

// The thread the game sends its Web API requests from, once (they block it).
void LogSendingThread() {
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return;
    std::string name = "(unnamed)";
    PWSTR desc = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &desc)) && desc && *desc) {
        char buf[128];
        WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof(buf), nullptr, nullptr);
        name = buf;
    }
    if (desc) LocalFree(desc);
    std::fprintf(stderr, "[shadnet] Web API requests come from thread %lu \"%s\"\n", GetCurrentThreadId(),
                 name.c_str());
}

std::string Clip(const std::string& s, size_t n = 300) { return s.size() <= n ? s : s.substr(0, n) + "..."; }

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
    std::erase_if(g_pushCallbacks, [&](const auto& c) { return c.second.userCtx == titleUserCtxId; });
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
    std::string contentType;
    if (Readable(contentParameter, sizeof(ContentParameter)))
        contentType = CString(static_cast<const ContentParameter*>(contentParameter)->contentType, 200);
    int64_t id;
    {
        std::lock_guard lk(g_webMutex);
        g_webRequestCount = g_webRequestCount >= 0x7fffffff ? 1 : g_webRequestCount + 1;
        id = (static_cast<int64_t>(titleUserCtxId) << 32) | g_webRequestCount;
        WebApiRequest& r = g_webRequests[id];
        r.apiGroup = apiGroup;
        r.path = path;
        r.method = method;
        r.contentType = contentType;
    }
    *requestId = id;
    Log(__func__, "titleUserCtxId=%#x, apiGroup=%s, path=%s, method=%d, content type \"%s\" -> requestId %#llx",
        titleUserCtxId, Text(apiGroup).c_str(), Text(path, 400).c_str(), method, contentType.c_str(), HEX(id));
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiDeleteRequest(int64_t requestId) {
    Log(__func__, "requestId=%#llx", HEX(requestId));
    std::lock_guard lk(g_webMutex);
    return g_webRequests.erase(requestId) ? 0 : kWebApiRequestNotFound;
}
int PS4_SYSV_ABI sceNpWebApiSendRequest(int64_t requestId, const void* data, u64 size) {
    LogSendingThread();
    std::string group, path, contentType;
    int32_t method = 0;
    {
        std::lock_guard lk(g_webMutex);
        auto it = g_webRequests.find(requestId);
        if (it == g_webRequests.end()) {
            Log(__func__, "requestId=%#llx -> not found", HEX(requestId));
            return kWebApiRequestNotFound;
        }
        group = it->second.apiGroup;
        path = it->second.path;
        contentType = it->second.contentType;
        method = it->second.method;
    }
    std::string payload;
    if (data && size && size < (64u << 20) && Readable(data, static_cast<size_t>(size)))
        payload.assign(static_cast<const char*>(data), static_cast<size_t>(size));
    const Answer a = AnswerRequest(method, path, contentType, payload);
    {
        std::lock_guard lk(g_webMutex);
        if (auto it = g_webRequests.find(requestId); it != g_webRequests.end()) {
            it->second.status = a.status;
            it->second.body = a.body;
            it->second.sent = true;
            it->second.readPos = 0;
        }
    }
    Log(__func__, "requestId=%#llx (%s %s %s), %llu bytes -> %d from %s: %s", HEX(requestId), MethodName(method) ? MethodName(method) : "?",
        group.c_str(), path.c_str(), HEX(size), a.status, a.source, Clip(a.body).c_str());
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
    *status = it->second.sent ? it->second.status : 0;
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiGetErrorCode() {
    Log(__func__, "");
    return 0;
}
int PS4_SYSV_ABI sceNpWebApiCreatePushEventFilter(int32_t libCtxId, const void* filterParam, u64 filterParamNum) {
    PushFilter f;
    f.dataTypes = DataTypes(filterParam, filterParamNum);
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webFilter = g_webFilter >= 0x7fffffff ? 1 : g_webFilter + 1;
        g_pushFilters[id] = f;
    }
    std::string types;
    for (const auto& t : f.dataTypes) types += (types.empty() ? "" : ", ") + t;
    Log(__func__, "libCtxId=%d, data types [%s] -> filterId %d", libCtxId, types.c_str(), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiCreateServicePushEventFilter(int32_t libCtxId, int32_t handleId, const char* serviceName,
                                                         uint32_t serviceLabel, const void* filterParam,
                                                         u64 filterParamNum) {
    PushFilter f;
    f.service = true;
    f.serviceName = CString(serviceName, 64);
    f.label = serviceLabel;
    f.dataTypes = DataTypes(filterParam, filterParamNum);
    int32_t id;
    {
        std::lock_guard lk(g_webMutex);
        id = g_webFilter = g_webFilter >= 0x7fffffff ? 1 : g_webFilter + 1;
        g_pushFilters[id] = f;
    }
    std::string types;
    for (const auto& t : f.dataTypes) types += (types.empty() ? "" : ", ") + t;
    Log(__func__, "libCtxId=%d, handleId=%d, service \"%s\", label %u, data types [%s] -> filterId %d", libCtxId,
        handleId, f.serviceName.c_str(), serviceLabel, types.c_str(), id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiDeleteServicePushEventFilter(int32_t libCtxId, int32_t filterId) {
    Log(__func__, "libCtxId=%d, filterId=%d", libCtxId, filterId);
    std::lock_guard lk(g_webMutex);
    g_pushFilters.erase(filterId);
    return 0;
}
static int32_t RegisterPushCallback(int32_t titleUserCtxId, int32_t filterId, void* cb, void* userArg, bool service) {
    std::lock_guard lk(g_webMutex);
    const int32_t id = g_webCallback = g_webCallback >= 0x7fffffff ? 1 : g_webCallback + 1;
    g_pushCallbacks[id] = {titleUserCtxId, filterId, cb, userArg, service};
    return id;
}
int PS4_SYSV_ABI sceNpWebApiRegisterPushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                      void* userArg) {
    const int32_t id = RegisterPushCallback(titleUserCtxId, filterId, cb, userArg, false);
    Log(__func__, "titleUserCtxId=%#x, filterId=%d, cb=%p, arg=%p -> callbackId %d", titleUserCtxId, filterId, cb,
        userArg, id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiRegisterServicePushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb,
                                                             void* userArg) {
    const int32_t id = RegisterPushCallback(titleUserCtxId, filterId, cb, userArg, true);
    Log(__func__, "titleUserCtxId=%#x, filterId=%d, cb=%p, arg=%p -> callbackId %d", titleUserCtxId, filterId, cb,
        userArg, id);
    return id;
}
int PS4_SYSV_ABI sceNpWebApiUnregisterPushEventCallback(int32_t titleUserCtxId, int32_t callbackId) {
    Log(__func__, "titleUserCtxId=%d, callbackId=%d", titleUserCtxId, callbackId);
    std::lock_guard lk(g_webMutex);
    g_pushCallbacks.erase(callbackId);
    return 0;
}
// A Web API npId ("npId" in a friend list): base64 of "<online ID>@<options>", decoded into a SceNpId
// as shadPS4's does (LBPPC aad6a91 np_web_api.cpp, GPL-2.0-or-later): the handle before '@', the
// options' characters (without '/' and '.') into opt, and reserved[0] = 1.
int PS4_SYSV_ABI sceNpWebApiUtilityParseNpId(const char* json, void* npId) {
    if (!json) return static_cast<int>(0x80550003);  // SCE_NP_ERROR_INVALID_ARGUMENT
    const std::string decoded = Unbase64(CString(json, 256));
    const size_t at = decoded.find('@');
    const std::string handle = decoded.substr(0, at);
    std::string opt;
    if (at != std::string::npos)
        for (size_t i = at + 1; i < decoded.size(); ++i)
            if (decoded[i] != '/' && decoded[i] != '.') opt += decoded[i];
    if (npId) {
        uint8_t* out = static_cast<uint8_t*>(npId);
        std::memset(out, 0, 36);
        std::memcpy(out, handle.data(), std::min<size_t>(handle.size(), 16));
        std::memcpy(out + 20, opt.data(), std::min<size_t>(opt.size(), 8));
        out[28] = 1;
    }
    Log(__func__, "json=%s -> \"%s\"", Text(json, 100).c_str(), handle.c_str());
    return 0;
}

void NoteInvitation(const net::shadnet::PushEvent& ev);  // below

// The push events shadNet received, to the game's callbacks whose filter takes them, on the game's
// thread (from sceNpCheckCallback, as the console's NP library does). The matching is shadPS4's (LBPPC
// aad6a91 np_web_api_internal.cpp DrainPushEvents, GPL-2.0-or-later): a service filter takes its
// service's events, a basic filter only events without a service, both only of their data types.
void DispatchPushEvents() {
    std::vector<net::shadnet::PushEvent> events = net::shadnet::TakePushEvents();
    if (events.empty()) return;
    const std::string self = net::shadnet::Name();
    for (const auto& ev : events) {
        if (ev.service == "sessionInvitation") NoteInvitation(ev);
        DataType dt{};
        std::snprintf(dt.val, sizeof(dt.val), "%s", ev.dataType.c_str());
        PushPeerAddress to{}, from{};
        const std::string toName = ev.to.empty() ? self : ev.to;
        std::memcpy(to.onlineId.data, toName.data(), std::min<size_t>(toName.size(), 16));
        std::memcpy(from.onlineId.data, ev.from.data(), std::min<size_t>(ev.from.size(), 16));
        struct Call {
            int32_t id;
            PushCallback cb;
            std::string serviceName;
            uint32_t label;
        };
        std::vector<Call> calls;
        {
            std::lock_guard lk(g_webMutex);
            for (const auto& [id, cb] : g_pushCallbacks) {
                auto fit = g_pushFilters.find(cb.filterId);
                if (fit == g_pushFilters.end() || !cb.fn || cb.service != fit->second.service) continue;
                const PushFilter& f = fit->second;
                if (f.service) {
                    const bool catchAll = f.serviceName.empty() && f.label == 0xffffffffu;
                    if (catchAll ? !ev.service.empty() : ev.service != f.serviceName) continue;
                } else if (!ev.service.empty()) {
                    continue;
                }
                if (std::find(f.dataTypes.begin(), f.dataTypes.end(), ev.dataType) == f.dataTypes.end()) continue;
                calls.push_back({id, cb, f.serviceName, f.label});
            }
        }
        std::fprintf(stderr, "[shadnet] push event \"%s\" / \"%s\" from \"%s\": %zu callback(s)\n", ev.service.c_str(),
                     ev.dataType.c_str(), ev.from.c_str(), calls.size());
        const char* data = ev.data.empty() ? nullptr : ev.data.c_str();
        for (const Call& c : calls) {
            if (c.cb.service)
                reinterpret_cast<ServiceCallback>(c.cb.fn)(c.cb.userCtx, c.id,
                                                           c.serviceName.empty() ? nullptr : c.serviceName.c_str(),
                                                           c.label, &to, &from, &dt, data, ev.data.size(), c.cb.arg);
            else
                reinterpret_cast<BasicCallback>(c.cb.fn)(c.cb.userCtx, c.id, &to, &from, &dt, data, ev.data.size(),
                                                         c.cb.arg);
        }
    }
}

// --- Invitations received, and joining one ------------------------------------------------------
//
// A friend's invitation arrives as a "sessionInvitation" push event (the game's own callback gets it
// too, above). LBP3 shows its own prompt and, when the player takes it, opens the game custom data
// dialog in mode 2; that dialog asks here whether to join (common_dialog.h GameCustomDataHook). "Join"
// raises SESSION_INVITATION through libSceSystemService, as the PS4's system does when an invitation
// is accepted, and the game joins the session. Shapes from shadPS4 (LBPPC aad6a91, GPL-2.0-or-later):
// invitation_dialog.h OrbisNpSessionInvitationEventParam, systemservice.h OrbisSystemServiceEvent, and
// np_handler_invitation.cpp PostSessionInvitationEvent.

namespace {

struct Invitation {
    std::string sessionId, invitationId, from;
    uint64_t fromAccount = 0;
    int64_t validUntil = 0;  // ms since 1970; 0 = no limit
};

constexpr int32_t kEventSessionInvitation = 0x10000002;
constexpr size_t kEventSize = 4 + 8192;  // SceSystemServiceEvent: the type, then an 8 KiB union
constexpr int kSystemServiceNoEvent = static_cast<int>(0x80A10004);
constexpr int kSystemServiceParameter = static_cast<int>(0x80A10003);

// SceNpSessionInvitationEventParam (176 bytes): the session id (48 + term + 2), the invitation id
// (60 + term), the flag at 112, the accepting player's online ID at 120 and user id at 140, the
// inviter's online ID at 144 and account id at 168.
constexpr size_t kInvSessionId = 0, kInvInvitationId = 51, kInvFlag = 112, kInvOnlineId = 120, kInvUserId = 140,
                 kInvReferralOnlineId = 144, kInvReferralAccountId = 168;

std::mutex g_inviteMutex;
std::vector<Invitation> g_invites;                 // received, not yet answered; newest last
std::deque<std::vector<uint8_t>> g_systemEvents;   // for sceSystemServiceReceiveEvent

// A string field of a flat JSON object ("key":"value"), or "".
std::string JsonField(const std::string& json, const std::string& key) {
    const std::string tag = "\"" + key + "\"";
    size_t at = json.find(tag);
    if (at == std::string::npos) return {};
    at = json.find(':', at + tag.size());
    if (at == std::string::npos) return {};
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos) return {};
    if (json[at] == '"') {
        const size_t end = json.find('"', at + 1);
        return end == std::string::npos ? std::string() : json.substr(at + 1, end - at - 1);
    }
    const size_t end = json.find_first_of(",} \t\r\n", at);
    return json.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::wstring Widen(const std::string& s) {
    std::wstring w;
    for (const char c : s) w += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return w;
}

// Invitations still valid, newest last (lock held).
void DropExpired() {
    const int64_t now = NowMs();
    std::erase_if(g_invites, [&](const Invitation& i) { return i.validUntil != 0 && now > i.validUntil; });
}

std::vector<uint8_t> InvitationEvent(const Invitation& inv, const std::string& self) {
    std::vector<uint8_t> e(kEventSize, 0);
    std::memcpy(e.data(), &kEventSessionInvitation, 4);
    uint8_t* p = e.data() + 4;
    std::memcpy(p + kInvSessionId, inv.sessionId.data(), std::min<size_t>(inv.sessionId.size(), 48));
    std::memcpy(p + kInvInvitationId, inv.invitationId.data(), std::min<size_t>(inv.invitationId.size(), 60));
    const int32_t flag = inv.invitationId.empty() ? 0 : 1;  // FLAG_INVITATION
    std::memcpy(p + kInvFlag, &flag, 4);
    std::memcpy(p + kInvOnlineId, self.data(), std::min<size_t>(self.size(), 16));
    const int32_t userId = 1;
    std::memcpy(p + kInvUserId, &userId, 4);
    std::memcpy(p + kInvReferralOnlineId, inv.from.data(), std::min<size_t>(inv.from.size(), 16));
    std::memcpy(p + kInvReferralAccountId, &inv.fromAccount, 8);
    return e;
}

}  // namespace

void NoteInvitation(const net::shadnet::PushEvent& ev) {
    Invitation inv;
    inv.from = ev.from;
    inv.fromAccount = ev.fromAccount;
    for (const auto& [k, v] : ev.extd) {
        if (k == "sessionId") inv.sessionId = v;
        else if (k == "invitationId") inv.invitationId = v;
        else if (k == "validUntil") inv.validUntil = std::strtoll(v.c_str(), nullptr, 10);
        else if (k == "fromAccountId" && !inv.fromAccount) inv.fromAccount = std::strtoull(v.c_str(), nullptr, 10);
    }
    if (inv.sessionId.empty()) inv.sessionId = JsonField(ev.data, "sessionId");
    if (inv.invitationId.empty()) inv.invitationId = JsonField(ev.data, "invitationId");
    if (inv.sessionId.empty()) {
        std::fprintf(stderr, "[shadnet] invitation push without a session: %s\n", Clip(ev.data).c_str());
        return;
    }
    std::fprintf(stderr, "[shadnet] invitation from %s to session %s (invitation %s)\n", inv.from.c_str(),
                 inv.sessionId.c_str(), inv.invitationId.c_str());
    std::lock_guard lk(g_inviteMutex);
    std::erase_if(g_invites, [&](const Invitation& i) {
        return !inv.invitationId.empty() ? i.invitationId == inv.invitationId : i.sessionId == inv.sessionId;
    });
    g_invites.push_back(std::move(inv));
    if (g_invites.size() > 16) g_invites.erase(g_invites.begin());
}

// common_dialog.h GameCustomDataHook.
std::wstring InvitationText() {
    std::lock_guard lk(g_inviteMutex);
    DropExpired();
    if (g_invites.empty()) return {};
    const Invitation& inv = g_invites.back();
    return Widen(inv.from.empty() ? std::string("A friend") : inv.from) +
           L" invited you to play LittleBigPlanet 3.\n\nJoin their game?";
}

void AnswerInvitation(bool join) {
    std::lock_guard lk(g_inviteMutex);
    DropExpired();
    if (g_invites.empty()) return;
    const Invitation inv = g_invites.back();
    g_invites.pop_back();
    if (!join) {
        std::fprintf(stderr, "[shadnet] invitation from %s: not joined\n", inv.from.c_str());
        return;
    }
    g_systemEvents.push_back(InvitationEvent(inv, net::shadnet::Name()));
    std::fprintf(stderr, "[shadnet] invitation from %s: joining session %s (SESSION_INVITATION to the game)\n",
                 inv.from.c_str(), inv.sessionId.c_str());
}

void CustomDataOpened(int32_t mode, const void* param) {
    std::string bytes;
    if (Readable(param, 0x90))
        for (size_t i = 0x30; i < 0x50; ++i) {
            char b[4];
            std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t*>(param)[i]);
            bytes += b;
        }
    std::fprintf(stderr, "[shadnet] game custom data dialog, mode %d (param +0x30: %s)\n", mode, bytes.c_str());
}

// Sending (the game custom data dialog, mode 1). LBP3 (eboot.bin+0xadeda0) fills the param's mode
// data (param+0x48) from its own request: three text pointers at +0x00, +0x08 and +0x20, a
// pointer/size pair at +0x38/+0x40 (a cached image: the thumbnail) and at +0x48/+0x50 (its data),
// "PS4" as the one platform (+0x60 count, +0x68 the list), and a pointer/count pair at +0x78/+0x80
// from its request (read here as the players it names). The PS4's system then sends it as game
// custom data; shadNet's Web API takes it at POST /v1/gameCustomData/items (webapi_routes_game_custom_
// data.cpp: a "message" JSON part with toAccountIds and the item, "game-data-binary-0" and
// "game-data-thumbnail-0"). The field meanings are read from the call site, not yet seen in a run:
// every Open logs them (the first real send will confirm or correct them).

namespace {

struct PendingSend {
    std::string name, description, message;
    std::vector<uint8_t> data, thumbnail;
};
std::mutex g_sendMutex;
PendingSend g_pendingSend;

const void* PtrAt(const uint8_t* p, size_t off) {
    const void* v = nullptr;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}
uint32_t U32At(const uint8_t* p, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}
std::vector<uint8_t> Bytes(const void* p, uint32_t size) {
    if (!p || !size || size > (4u << 20) || !Readable(p, size)) return {};
    const auto* b = static_cast<const uint8_t*>(p);
    return std::vector<uint8_t>(b, b + size);
}
bool IsImage(const std::vector<uint8_t>& b) {
    return (b.size() > 3 && b[0] == 0xFF && b[1] == 0xD8) || (b.size() > 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N');
}
std::string Hex(const void* p, size_t n) {
    std::string out;
    if (!Readable(p, n)) return "(unreadable)";
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t*>(p)[i]);
        out += b;
    }
    return out;
}
bool PrintableId(const uint8_t* p) {
    size_t n = 0;
    while (n < 16 && p[n] && (std::isalnum(p[n]) || p[n] == '-' || p[n] == '_')) ++n;
    return n >= 3 && (n == 16 || p[n] == 0);
}

}  // namespace

std::vector<std::string> PrepareSend(const void* param) {
    std::vector<std::string> to;
    if (!Readable(param, 0x90)) return to;
    const auto* m = static_cast<const uint8_t*>(PtrAt(static_cast<const uint8_t*>(param), 0x48));
    if (!Readable(m, 0x88)) {
        std::fprintf(stderr, "[shadnet] custom data send: no readable mode data\n");
        return to;
    }
    PendingSend ps;
    ps.name = CString(static_cast<const char*>(PtrAt(m, 0x00)), 256);
    ps.description = CString(static_cast<const char*>(PtrAt(m, 0x08)), 1024);
    ps.message = CString(static_cast<const char*>(PtrAt(m, 0x20)), 1024);
    std::vector<uint8_t> a = Bytes(PtrAt(m, 0x38), U32At(m, 0x40)), b = Bytes(PtrAt(m, 0x48), U32At(m, 0x50));
    if (IsImage(b) && !IsImage(a)) std::swap(a, b);  // whichever is the picture is the thumbnail
    ps.thumbnail = std::move(a);
    ps.data = std::move(b);
    // The players the game named: online IDs (SceNpId, 36 bytes each) or account ids (8 bytes each).
    const auto* list = static_cast<const uint8_t*>(PtrAt(m, 0x78));
    const uint32_t count = U32At(m, 0x80);
    if (list && count && count <= 16) {
        if (Readable(list, 36) && PrintableId(list)) {
            for (uint32_t i = 0; i < count && Readable(list + i * 36, 36); ++i)
                to.push_back(CString(reinterpret_cast<const char*>(list + i * 36), 16));
        } else if (Readable(list, count * 8)) {
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t id = 0;
                std::memcpy(&id, list + i * 8, 8);
                if (id) to.push_back(std::to_string(id));
            }
        }
    }
    std::fprintf(stderr,
                 "[shadnet] custom data send: name \"%s\", description \"%s\", message \"%s\", data %zu bytes, "
                 "thumbnail %zu bytes (%s), %u named player(s) [%s] (mode data: %s)\n",
                 ps.name.c_str(), Clip(ps.description, 80).c_str(), Clip(ps.message, 80).c_str(), ps.data.size(),
                 ps.thumbnail.size(), IsImage(ps.thumbnail) ? "an image" : "not an image", count,
                 list ? Hex(list, std::min<size_t>(count ? count * 8 : 0, 40)).c_str() : "", Hex(m, 0x88).c_str());
    if (to.empty())
        for (const auto& f : net::shadnet::Friends()) to.push_back(f.npid);
    std::lock_guard lk(g_sendMutex);
    g_pendingSend = std::move(ps);
    return to;
}

// The multipart body shadNet's server takes for a game custom data item.
std::string CustomDataBody(const PendingSend& ps, const std::string& to, const std::string& boundary) {
    // An online ID or an account id, as a string either way (an account id as a JSON number would
    // lose digits).
    const std::string recipient = "\"" + JsonEscape(to) + "\"";
    const std::string name = ps.name.empty() ? "LittleBigPlanet 3" : ps.name;
    const std::string description = ps.description.empty() ? name : ps.description;
    const std::string json = "{\"toAccountIds\":[" + recipient + "],\"item\":{\"message\":\"" + JsonEscape(ps.message) +
                             "\",\"gameDataDetail\":{\"dataName\":\"" + JsonEscape(name) + "\",\"dataDescription\":\"" +
                             JsonEscape(description) + "\",\"expireMinutes\":0,\"availablePlatforms\":[\"PS4\"]}}}";
    std::string body;
    const auto part = [&](const char* type, const char* description, const std::string& data) {
        body += "--" + boundary + "\r\nContent-Type: " + type + "\r\nContent-Description: " + description + "\r\n\r\n";
        body += data;
        body += "\r\n";
    };
    part("application/json; charset=utf-8", "message", json);
    part("application/octet-stream", "game-data-binary-0", std::string(ps.data.begin(), ps.data.end()));
    part(IsImage(ps.thumbnail) && ps.thumbnail[0] == 0x89 ? "image/png" : "image/jpeg", "game-data-thumbnail-0",
         std::string(ps.thumbnail.begin(), ps.thumbnail.end()));
    body += "--" + boundary + "--\r\n";
    return body;
}

bool SendCustomData(const std::string& to) {
    PendingSend ps;
    {
        std::lock_guard lk(g_sendMutex);
        ps = g_pendingSend;
    }
    const std::string base = net::shadnet::WebApiBase(), token = net::shadnet::BearerToken();
    if (base.empty() || token.empty()) {
        std::fprintf(stderr, "[shadnet] custom data send to %s: no Web API session\n", to.c_str());
        return false;
    }
    const std::string boundary = "lbp3customdata";
    const net::web::Response r =
        net::web::Request("POST", base + "/v1/gameCustomData/items",
                          {{"Authorization", "Bearer " + token}, {"Content-Type", "multipart/mixed; boundary=" + boundary}},
                          CustomDataBody(ps, to, boundary));
    std::fprintf(stderr, "[shadnet] custom data send to %s: %s %d %s\n", to.c_str(), r.ok ? "HTTP" : "failed:",
                 r.status, r.ok ? Clip(r.body, 200).c_str() : r.error.c_str());
    return r.ok && r.status >= 200 && r.status < 300;
}

const common_dialog::GameCustomDataHook kCustomDataHook{&InvitationText, &AnswerInvitation, &CustomDataOpened,
                                                     &PrepareSend, &SendCustomData};

// libSceSystemService: the events above, for the game. Registered only with shadNet on (otherwise the
// game sees no events, as offline).
int PS4_SYSV_ABI sceSystemServiceReceiveEvent(void* event) {
    if (!event) return kSystemServiceParameter;
    std::lock_guard lk(g_inviteMutex);
    if (g_systemEvents.empty()) return kSystemServiceNoEvent;
    std::memcpy(event, g_systemEvents.front().data(), kEventSize);
    g_systemEvents.pop_front();
    std::fprintf(stderr, "[shadnet] system event SESSION_INVITATION taken by the game\n");
    return 0;
}

int PS4_SYSV_ABI sceSystemServiceGetStatusShadNet(void* status) {
    const int r = system::sceSystemServiceGetStatus(static_cast<system::SystemServiceStatus*>(status));
    if (r == 0 && status) {
        std::lock_guard lk(g_inviteMutex);
        const int32_t n = static_cast<int32_t>(g_systemEvents.size());
        std::memcpy(status, &n, sizeof(n));  // eventNum
    }
    return r;
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

// --- libSceNpSignaling: peer connections (phase (c)) --------------------------------------------
//
// With shadNet on, a connection is net/p2p_link.h's: ActivateConnection asks it to connect to the
// peer (the server says where they are; directly, else through shadNet's relay), the game's P2P
// datagrams to the peer's address go through it (hle/net_remote.h), and its events reach the game's
// signaling callback from sceNpCheckCallback, on the game's thread: ESTABLISHED for a connection the
// game activated, PEER_ACTIVATED when another player connected to us (to every context: LBP3 has
// one, and its host learns of a joining player only so; LBPPC's np_signaling_state.cpp found that),
// MUTUAL_ACTIVATED when both did, DEAD when it's gone. Join protection (phase (d)): a player who isn't
// a friend (or named in LBP3_JOIN_ALLOW) can't connect to us; we can still connect to anyone.
// Online without shadNet, a connection gets an id and stays inactive.
// Constants and the connection info layouts are shadPS4's (LBPPC aad6a91 np_types2.h and
// np_signaling_state.cpp, GPL-2.0-or-later); the bandwidth isn't measured but must not be 0 (LBPPC:
// 0 bits per second made LBP3 drop a working connection).

namespace {

using SignalingHandler = void(PS4_SYSV_ABI*)(uint32_t ctxId, uint32_t connId, int32_t event, int32_t error,
                                             void* userArg);
constexpr int32_t kSigEventDead = 0, kSigEventEstablished = 1, kSigEventPeerActivated = 10,
                  kSigEventMutualActivated = 12;
constexpr uint32_t kSigStatusInactive = 0, kSigStatusPending = 1, kSigStatusActive = 2;
constexpr int32_t kSigInfoRtt = 1, kSigInfoBandwidth = 2, kSigInfoPeerNpId = 3, kSigInfoPeerAddr = 4,
                  kSigInfoMappedAddr = 5, kSigInfoPacketLoss = 6;
constexpr int kSigErrCtxNotFound = static_cast<int>(0x80552705), kSigErrConnNotFound = static_cast<int>(0x8055270E),
              kSigErrPeerUnreachable = static_cast<int>(0x8055270F), kSigErrTerminatedByPeer = static_cast<int>(0x80552710),
              kSigErrTimeout = static_cast<int>(0x80552711), kSigErrInProgress = static_cast<int>(0x80552714),
              kSigErrOwnNpId = static_cast<int>(0x80552716);
constexpr int kSigInvalidArgument = static_cast<int>(0x80552715);
constexpr uint32_t kAssumedBandwidthBps = 10'000'000;

struct SigContext {
    SignalingHandler cb = nullptr;
    void* arg = nullptr;
};
struct SigConn {
    int32_t ctx = 0;
    std::string peer;
};

std::mutex g_sigMutex;
std::map<int32_t, SigContext> g_sigContexts;
std::map<int32_t, SigConn> g_sigConns;
std::atomic<int32_t> g_sigNextCtx{0}, g_sigNextConn{0};
std::unique_ptr<net::p2p::Link> g_link;  // with shadNet on, from sceNpSignalingInitialize

std::string PeerName(const void* npId) {
    if (!Readable(npId, 16)) return {};
    return std::string(static_cast<const char*>(npId), strnlen(static_cast<const char*>(npId), 16));
}

// Lock held: the game's connection to that peer in that context (0 = none).
int32_t ConnFor(int32_t ctx, const std::string& peer) {
    for (const auto& [id, c] : g_sigConns)
        if (c.ctx == ctx && c.peer == peer) return id;
    return 0;
}

// The join check (phase (d)): shadNet friends, and LBP3_JOIN_ALLOW (comma separated, for tests).
bool MayConnect(const std::string& peer) {
    for (const auto& f : net::shadnet::Friends())
        if (f.npid == peer) return true;
    if (const char* allow = std::getenv("LBP3_JOIN_ALLOW")) {
        std::string list = allow;
        size_t at = 0;
        while (at <= list.size()) {
            const size_t comma = list.find(',', at);
            std::string name = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            if (name == peer) return true;
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    return false;
}

bool LinkSend(uint32_t ip, uint16_t port, uint16_t srcVport, uint16_t dstVport, const void* data, size_t len) {
    net::p2p::Link* link = g_link.get();
    return link && link->SendData(net::p2p::Endpoint{ip, port}, srcVport, dstVport, data, len);
}

// The link's UDP service endpoint: the configured host's IPv4 address.
net::p2p::Endpoint UdpServer() {
    const std::string host = net::shadnet::UdpHost();
    const uint16_t port = net::shadnet::UdpPort();
    net::p2p::Endpoint e;
    if (host.empty() || !port) return e;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
        e.ip = reinterpret_cast<const sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
        e.port = port;
        freeaddrinfo(res);
    } else {
        std::fprintf(stderr, "[p2p] the server's UDP host %s wasn't found: no relay, no address from the server\n",
                     host.c_str());
    }
    return e;
}

void StartLink() {
    if (g_link || !net::shadnet::Settings().enabled) return;
    net::p2p::Config c;
    c.self = net::shadnet::Settings().name;
    c.server = UdpServer();
    if (const char* p = std::getenv("LBP3_P2P_PORT")) c.localPort = static_cast<uint16_t>(std::atoi(p));
    if (const char* p = std::getenv("LBP3_P2P_PATH")) {
        const std::string v = p;
        c.path = v == "lan"      ? net::p2p::Config::Path::Lan
                 : v == "public" ? net::p2p::Config::Path::Public
                 : v == "relay"  ? net::p2p::Config::Path::Relay
                 : v == "any"    ? net::p2p::Config::Path::Any
                                 : net::p2p::Config::Path::Auto;
        std::fprintf(stderr, "[p2p] LBP3_P2P_PATH=%s\n", p);
    }
    c.exactMath = core::exactmath::Active();  // HELLO says so; a player whose game differs is refused
    std::fprintf(stderr, "[p2p] exact maths: %s\n", c.exactMath ? "on" : "off");
    if (const char* p = std::getenv("LBP3_P2P_DATA3"); p && *p == '0') {
        c.data3 = false;
        std::fprintf(stderr, "[p2p] LBP3_P2P_DATA3=0: no gap-free delivery\n");
    }
    if (const char* p = std::getenv("LBP3_P2P_TEST_LOSS"); p && *p) {
        // "N" or "N,burst": drop `burst` incoming game datagrams out of every N (tests).
        c.testLossEvery = std::atoi(p);
        if (const char* comma = std::strchr(p, ',')) c.testLossBurst = std::max(1, std::atoi(comma + 1));
        std::fprintf(stderr, "[p2p] LBP3_P2P_TEST_LOSS=%s: dropping incoming game datagrams (a test)\n", p);
    }
    if (const char* p = std::getenv("LBP3_P2P_TEST_LATE"); p && *p) c.testLateEvery = std::atoi(p);
    if (const char* p = std::getenv("LBP3_P2P_TEST_DUP"); p && *p) c.testDupEvery = std::atoi(p);
    if (const char* p = std::getenv("LBP3_P2P_DIAG"); p && *p == '1') {
        c.diag = true;
        std::fprintf(stderr, "[p2p] LBP3_P2P_DIAG=1: sequence numbers and send times on game datagrams "
                             "(the other player needs it too)\n");
    }
    c.mayConnect = &MayConnect;
    c.resolve = [](const std::string& peer, std::function<void(bool, net::p2p::Endpoint)> done) {
        net::shadnet::ResolvePeer(peer, [done](bool ok, const std::string& ip, uint16_t port) {
            done(ok, ok ? net::p2p::MakeEndpoint(ip.c_str(), port) : net::p2p::Endpoint{});
        });
    };
    auto link = std::make_unique<net::p2p::Link>();
    link->SetDataSink([](const net::p2p::Endpoint& from, uint16_t srcVport, uint16_t dstVport, const uint8_t* data,
                         size_t len) {
        if (!p2pnet::DeliverRemote(from.ip, from.port, srcVport, dstVport, data, len)) {
            static std::atomic<int> logged{0};
            if (logged++ < 10)
                std::fprintf(stderr, "[p2p] a game datagram for vport %u: no socket of the game is bound to it\n",
                             dstVport);
        }
    });
    if (!link->Start(c)) {
        std::fprintf(stderr, "[p2p] no UDP socket: online co-op is off\n");
        return;
    }
    g_link = std::move(link);
    p2pnet::SetRemoteSend(&LinkSend);
    // Tests only (the join protection, live): connect to this player although the game didn't ask.
    if (const char* p = std::getenv("LBP3_P2P_TEST_CONNECT"); p && *p) {
        const std::string peer = p;
        std::fprintf(stderr, "[p2p] LBP3_P2P_TEST_CONNECT: connecting to %s in 20 s (a test)\n", peer.c_str());
        std::thread([peer] {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            if (net::p2p::Link* l = g_link.get()) l->Activate(peer);
        }).detach();
    }
}

int SignalingError(int linkError) {
    switch (linkError) {
    case net::p2p::kErrorPeerGone: return kSigErrTerminatedByPeer;
    case net::p2p::kErrorUnreachable: return kSigErrPeerUnreachable;
    default: return kSigErrTimeout;
    }
}

}  // namespace

// The link's events, to the game's signaling callbacks (from sceNpCheckCallback).
void DispatchSignalingEvents() {
    net::p2p::Link* link = g_link.get();
    if (!link) return;
    const auto events = link->TakeEvents();
    if (events.empty()) return;
    struct Call {
        SignalingHandler cb;
        int32_t ctx, conn, event, error;
        void* arg;
    };
    std::vector<Call> calls;
    {
        std::lock_guard lk(g_sigMutex);
        for (const auto& e : events) {
            switch (e.kind) {
            case net::p2p::Event::PeerActivated: {
                // Another player connected to us: a connection for them in every context.
                for (const auto& [ctx, c] : g_sigContexts) {
                    int32_t conn = ConnFor(ctx, e.peer);
                    if (!conn) {
                        conn = ++g_sigNextConn;
                        g_sigConns[conn] = {ctx, e.peer};
                    }
                    calls.push_back({c.cb, ctx, conn, kSigEventPeerActivated, 0, c.arg});
                }
                break;
            }
            case net::p2p::Event::Established:
            case net::p2p::Event::Mutual:
                for (const auto& [conn, sc] : g_sigConns) {
                    if (sc.peer != e.peer) continue;
                    auto it = g_sigContexts.find(sc.ctx);
                    if (it == g_sigContexts.end()) continue;
                    calls.push_back({it->second.cb, sc.ctx, conn,
                                     e.kind == net::p2p::Event::Mutual ? kSigEventMutualActivated : kSigEventEstablished,
                                     0, it->second.arg});
                }
                break;
            case net::p2p::Event::Dead:
                for (auto it = g_sigConns.begin(); it != g_sigConns.end();) {
                    if (it->second.peer != e.peer) {
                        ++it;
                        continue;
                    }
                    if (auto c = g_sigContexts.find(it->second.ctx); c != g_sigContexts.end())
                        calls.push_back({c->second.cb, it->second.ctx, it->first, kSigEventDead,
                                         SignalingError(e.error), c->second.arg});
                    it = g_sigConns.erase(it);
                }
                break;
            }
        }
    }
    for (const Call& c : calls) {
        std::fprintf(stderr, "[shadnet] signaling event %d (error %#x) on context %d, connection %d\n", c.event,
                     static_cast<unsigned>(c.error), c.ctx, c.conn);
        if (c.cb) c.cb(static_cast<uint32_t>(c.ctx), static_cast<uint32_t>(c.conn), c.event, c.error, c.arg);
    }
}

int PS4_SYSV_ABI sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t affinity,
                                          int64_t stackSize) {
    Log(__func__, "memory=%lld, priority=%d, affinity=%#x, stack=%lld", static_cast<long long>(memorySize),
        threadPriority, affinity, static_cast<long long>(stackSize));
    StartLink();
    return 0;
}
// A context id from 1 (shadPS4's np_signaling.cpp signature: npId, callback, its argument, the id out).
int PS4_SYSV_ABI sceNpSignalingCreateContext(const void* npId, void* cb, void* cbArg, int32_t* ctxId) {
    if (!npId || !ctxId) return kSigInvalidArgument;
    *ctxId = ++g_sigNextCtx;
    {
        std::lock_guard lk(g_sigMutex);
        g_sigContexts[*ctxId] = {reinterpret_cast<SignalingHandler>(cb), cbArg};
    }
    Log(__func__, "npId=%s, cb=%p, arg=%p -> ctxId %d", Text(npId, 16).c_str(), cb, cbArg, *ctxId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingDeleteContext(int32_t ctxId) {
    Log(__func__, "ctxId=%d", ctxId);
    std::vector<std::string> peers;
    {
        std::lock_guard lk(g_sigMutex);
        g_sigContexts.erase(ctxId);
        for (auto it = g_sigConns.begin(); it != g_sigConns.end();) {
            if (it->second.ctx == ctxId) {
                peers.push_back(it->second.peer);
                it = g_sigConns.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (g_link)
        for (const auto& p : peers) g_link->Terminate(p);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingActivateConnection(int32_t ctxId, const void* peerNpId, int32_t* connId) {
    if (!peerNpId || !connId) return kSigInvalidArgument;
    const std::string peer = PeerName(peerNpId);
    if (!g_link) {
        *connId = ++g_sigNextConn;
        Log(__func__, "ctxId=%d, peer=%s -> connId %d (stays inactive: no shadNet)", ctxId, peer.c_str(), *connId);
        return 0;
    }
    if (peer.empty()) return kSigInvalidArgument;
    if (peer == net::shadnet::Settings().name) return kSigErrOwnNpId;
    {
        std::lock_guard lk(g_sigMutex);
        if (!g_sigContexts.contains(ctxId)) return kSigErrCtxNotFound;
        int32_t conn = ConnFor(ctxId, peer);
        if (!conn) {
            conn = ++g_sigNextConn;
            g_sigConns[conn] = {ctxId, peer};
        }
        *connId = conn;
    }
    g_link->Activate(peer);
    Log(__func__, "ctxId=%d, peer=%s -> connId %d", ctxId, peer.c_str(), *connId);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingTerminateConnection(int32_t ctxId, int32_t connId) {
    Log(__func__, "ctxId=%d, connId=%d", ctxId, connId);
    {
        // Who ended it: the caller, then the game's return addresses found on the stack.
        std::fprintf(stderr, "[shadnet] %s the game ends connection %d, called from %s\n", core::Stamp().text, connId,
                     core::DescribeAddress(reinterpret_cast<uint64_t>(__builtin_return_address(0))).c_str());
        auto* sp = static_cast<uint64_t*>(__builtin_frame_address(0));
        int shown = 0;
        for (int k = 0; k < 1024 && shown < 16; ++k) {
            const std::string d = core::DescribeAddress(sp[k]);
            if (d.find(".prx+") == std::string::npos && d.find(".bin+") == std::string::npos) continue;
            std::fprintf(stderr, "[shadnet]   [sp+0x%x] %s\n", k * 8, d.c_str());
            ++shown;
        }
    }
    std::string peer;
    {
        std::lock_guard lk(g_sigMutex);
        auto it = g_sigConns.find(connId);
        if (it == g_sigConns.end() || it->second.ctx != ctxId) return g_link ? kSigErrConnNotFound : 0;
        peer = it->second.peer;
        g_sigConns.erase(it);
    }
    if (g_link) g_link->Terminate(peer);
    return 0;
}

namespace {
// Lock not held. The peer of the game's connection ("" = none).
std::string ConnPeer(int32_t ctxId, int32_t connId) {
    std::lock_guard lk(g_sigMutex);
    auto it = g_sigConns.find(connId);
    return it == g_sigConns.end() || it->second.ctx != ctxId ? std::string() : it->second.peer;
}
}  // namespace

int PS4_SYSV_ABI sceNpSignalingGetConnectionStatus(int32_t ctxId, int32_t connId, uint32_t* status, uint32_t* addr,
                                                   uint16_t* port) {
    if (!status) return kSigInvalidArgument;
    *status = kSigStatusInactive;
    if (addr) *addr = 0;
    if (port) *port = 0;
    const std::string peer = g_link ? ConnPeer(ctxId, connId) : std::string();
    if (!peer.empty()) {
        const net::p2p::PeerInfo info = g_link->Info(peer);
        if (info.state == net::p2p::State::Active) {
            *status = kSigStatusActive;
            if (addr) *addr = info.gameAddr.ip;
            if (port) *port = htons(info.gameAddr.port);
        } else if (info.state == net::p2p::State::Pending) {
            *status = kSigStatusPending;
        }
    }
    Log(__func__, "ctxId=%d, connId=%d (%s) -> status %u", ctxId, connId, peer.c_str(), *status);
    return 0;
}
int PS4_SYSV_ABI sceNpSignalingGetConnectionInfo(int32_t ctxId, int32_t connId, int32_t code, void* info) {
    Log(__func__, "ctxId=%d, connId=%d, code=%d, info=%p", ctxId, connId, code, info);
    if (!info) return kSigInvalidArgument;
    if (!g_link) return 0;
    const std::string peer = ConnPeer(ctxId, connId);
    if (peer.empty()) return kSigErrConnNotFound;
    const net::p2p::PeerInfo p = g_link->Info(peer);
    const bool active = p.state == net::p2p::State::Active;
    auto* out = static_cast<uint8_t*>(info);
    switch (code) {
    case kSigInfoRtt: {
        if (!active) return kSigErrInProgress;
        const uint32_t rtt = static_cast<uint32_t>(std::max(p.rttUs, 1000));
        std::memcpy(out, &rtt, 4);
        return 0;
    }
    case kSigInfoBandwidth: {
        if (!active) return kSigErrInProgress;
        std::memcpy(out, &kAssumedBandwidthBps, 4);
        return 0;
    }
    case kSigInfoPeerNpId: {
        std::memset(out, 0, 36);  // SceNpId: the online ID (16), its terminator and padding, opt, reserved
        std::memcpy(out, peer.data(), std::min<size_t>(peer.size(), 16));
        return 0;
    }
    case kSigInfoPeerAddr:
    case kSigInfoMappedAddr: {
        if (!active) return kSigErrInProgress;
        const net::p2p::Endpoint e = code == kSigInfoPeerAddr ? p.gameAddr : g_link->External();
        const uint16_t port = htons(e.port);
        std::memcpy(out, &e.ip, 4);
        std::memcpy(out + 4, &port, 2);
        return 0;
    }
    case kSigInfoPacketLoss: {
        const uint32_t loss = 0;
        std::memcpy(out, &loss, 4);
        return 0;
    }
    default:
        return kSigInvalidArgument;
    }
}

// --- This player's network identity, with shadNet on (libSceNetCtl, hle/np_offline.cpp) ------------
// Offline every console answers the same MAC and LAN address. Online, two players reporting one MAC
// and one address are one machine to the game, so each player gets their own: a locally administered
// MAC made from the online ID, this PC's real LAN address (the route to the internet; nothing is
// sent), and as the NAT's mapped address the public one shadNet's STUN reply gave.

namespace {

uint64_t NameHash(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (const char c : s) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return h;
}

}  // namespace

bool PlayerMac(uint8_t mac[6]) {
    if (!net::shadnet::Settings().enabled) return false;
    const uint64_t h = NameHash(net::shadnet::Settings().name);
    mac[0] = 0x02;  // locally administered, unicast
    for (int i = 1; i < 6; ++i) mac[i] = static_cast<uint8_t>(h >> (8 * i));
    return true;
}

bool PlayerLanIp(uint32_t* ipNet) {
    if (!net::shadnet::Settings().enabled) return false;
    static const uint32_t ip = [] {
        uint32_t found = 0;
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            sockaddr_in to{};
            to.sin_family = AF_INET;
            to.sin_port = htons(53);
            inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);  // only picks the route: a UDP connect sends nothing
            if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
                sockaddr_in me{};
                int len = sizeof(me);
                if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) found = me.sin_addr.s_addr;
            }
            closesocket(s);
        }
        if (!found) {  // no route: a private address of the player's own
            const uint64_t h = NameHash(net::shadnet::Settings().name);
            const uint8_t b[4] = {10, static_cast<uint8_t>(h), static_cast<uint8_t>(h >> 8),
                                  static_cast<uint8_t>(2 + (h >> 16) % 250)};
            std::memcpy(&found, b, 4);
        }
        return found;
    }();
    *ipNet = ip;
    return true;
}

bool PlayerMappedAddr(uint32_t* ipNet) {
    if (!net::shadnet::Settings().enabled) return false;
    if (net::p2p::Link* link = g_link.get(); link && link->External().Valid()) {
        *ipNet = link->External().ip;
        return true;
    }
    return PlayerLanIp(ipNet);
}

// Everything that reaches the game from its sceNpCheckCallback calls.
void OnCheckCallback() {
    DispatchPushEvents();
    DispatchSignalingEvents();
}

// --- In-game messages: accepted while signed in to shadNet, the offline answers otherwise ---------
// shadNet has no in-game messages, and the game imports nothing to receive them: what it sends goes
// nowhere (logged), as with LBPPC.

namespace {
bool SignedIn() { return net::shadnet::GetStatus() == net::shadnet::Status::SignedIn; }
std::atomic<int32_t> g_messageHandle{0};
}  // namespace

int PS4_SYSV_ABI sceNpInGameMessageInitialize(size_t poolSize, void* option) {
    Log(__func__, "poolSize=%llu, option=%p", HEX(poolSize), option);
    return np::sceNpInGameMessageInitialize(poolSize, option);
}
int PS4_SYSV_ABI sceNpInGameMessageTerminate(int32_t libCtxId) {
    Log(__func__, "libCtxId=%d", libCtxId);
    return np::sceNpInGameMessageTerminate(libCtxId);
}
int PS4_SYSV_ABI sceNpInGameMessageCreateHandle(int32_t libCtxId) {
    if (SignedIn()) {
        const int32_t id = ++g_messageHandle;
        Log(__func__, "libCtxId=%d -> handleId %d", libCtxId, id);
        return id;
    }
    Log(__func__, "libCtxId=%d (not signed in)", libCtxId);
    return np::sceNpInGameMessageCreateHandle(libCtxId);
}
int PS4_SYSV_ABI sceNpInGameMessageDeleteHandle(int32_t libCtxId, int32_t handleId) {
    Log(__func__, "libCtxId=%d, handleId=%d", libCtxId, handleId);
    return np::sceNpInGameMessageDeleteHandle(libCtxId, handleId);
}
int PS4_SYSV_ABI sceNpInGameMessagePrepare(int32_t libCtxId, int32_t handleId, const void* param, void* out) {
    Log(__func__, "libCtxId=%d, handleId=%d, param=%p, out=%p%s", libCtxId, handleId, param, out,
        SignedIn() ? "" : " (not signed in)");
    return SignedIn() ? 0 : np::sceNpInGameMessagePrepare(libCtxId, handleId, param, out);
}
int PS4_SYSV_ABI sceNpInGameMessageSendData(int32_t libCtxId, int32_t handleId, const void* to, const void* data,
                                            size_t size) {
    Log(__func__, "libCtxId=%d, handleId=%d, to=%s, data=%s, size=%llu%s", libCtxId, handleId, Text(to, 16).c_str(),
        Text(data, size < 200 ? size : 200).c_str(), HEX(size), SignedIn() ? " (dropped: nothing carries it)" : " (not signed in)");
    return SignedIn() ? 0 : np::sceNpInGameMessageSendData(libCtxId, handleId, to, data, size);
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
        // The push events reach the game from its own sceNpCheckCallback calls.
        np::SetCheckCallbackHook(&npsn::OnCheckCallback);
        // An invitation received is joined from the game custom data dialog (mode 2), which raises
        // SESSION_INVITATION through libSceSystemService.
        common_dialog::SetGameCustomDataHook(&npsn::kCustomDataHook);
        linker.RegisterHle("libSceSystemService", "sceSystemServiceGetStatus",
                           reinterpret_cast<void*>(&npsn::sceSystemServiceGetStatusShadNet));
        linker.RegisterHle("libSceSystemService", "sceSystemServiceReceiveEvent",
                           reinterpret_cast<void*>(&npsn::sceSystemServiceReceiveEvent));
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
