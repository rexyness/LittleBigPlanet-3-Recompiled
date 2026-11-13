// shadNet offline (net/shadnet_service.h, best/shadnet_design.md phase (a)):
//  1. the settings: LBP3_SHADNET and its server, name and password (environment and account file);
//  2. off means no client: no thread and no socket when LBP3_SHADNET is unset or incomplete;
//  3. a login against an in-process mock server speaking the shadNet protocol (ServerInfo, the
//     Login request and reply, the friends list), a refused login, an unreachable server;
//  4. the NP sign-in state the game sees (hle/np_offline.cpp) follows the login;
//  5. phase (b): the game's PSN Web API requests go to a mock Web API (127.0.0.1) with the session
//     token, repeated GETs come from the cache, the local answers (friend list from shadNet's friends)
//     when there's no Web API, push events from the mock shadNet reach the game's callbacks, NpId
//     parsing, and the token never reaches the log.
// Nothing here contacts a real server: the mocks listen on 127.0.0.1 only.
//   shadnettest
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "net/shadnet_service.h"
#include "shadnet.pb.h"

using namespace net::shadnet;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

namespace hle::np {
int __attribute__((sysv_abi)) sceNpGetState(int32_t userId, int32_t* state);
}
#define SYSV __attribute__((sysv_abi))
namespace hle::npsn {
int SYSV sceNpWebApiInitialize(int32_t libHttpCtxId, uint64_t poolSize);
int SYSV sceNpWebApiCreateContext(int32_t libCtxId, const void* onlineId);
int SYSV sceNpWebApiDeleteContext(int32_t titleUserCtxId);
int SYSV sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* apiGroup, const char* path, int32_t method,
                                  const void* contentParameter, int64_t* requestId);
int SYSV sceNpWebApiDeleteRequest(int64_t requestId);
int SYSV sceNpWebApiSendRequest(int64_t requestId, const void* data, uint64_t size);
int SYSV sceNpWebApiReadData(int64_t requestId, void* data, uint64_t size);
int SYSV sceNpWebApiGetHttpStatusCode(int64_t requestId, int32_t* status);
int SYSV sceNpWebApiCreatePushEventFilter(int32_t libCtxId, const void* filterParam, uint64_t filterParamNum);
int SYSV sceNpWebApiCreateServicePushEventFilter(int32_t libCtxId, int32_t handleId, const char* serviceName,
                                                 uint32_t serviceLabel, const void* filterParam, uint64_t filterParamNum);
int SYSV sceNpWebApiRegisterPushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb, void* userArg);
int SYSV sceNpWebApiRegisterServicePushEventCallback(int32_t titleUserCtxId, int32_t filterId, void* cb, void* userArg);
int SYSV sceNpWebApiUtilityParseNpId(const char* json, void* npId);
void DispatchPushEvents();
std::wstring InvitationText();
void AnswerInvitation(bool join);
int SYSV sceSystemServiceReceiveEvent(void* event);
int SYSV sceSystemServiceGetStatusShadNet(void* status);
}  // namespace hle::npsn

namespace {

// The process's threads that have a name (the client names its own: "ShadNet:...").
std::string NamedThreads() {
    std::string out;
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            if (!h) continue;
            PWSTR desc = nullptr;
            if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc && *desc) {
                char buf[128];
                WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof(buf), nullptr, nullptr);
                out += std::string(out.empty() ? "" : ", ") + buf;
            }
            if (desc) LocalFree(desc);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return out;
}

int ThreadCount() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    int n = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) ++n;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
}

Config Parse(std::map<std::string, std::string> env, const std::string& file = {}) {
    return ParseConfig([&](const char* k) -> const char* {
        auto it = env.find(k);
        return it == env.end() ? nullptr : it->second.c_str();
    }, file);
}

// --- the mock server: just enough of shadNet for a login ---------------------------------------
constexpr size_t kHeader = 15;

void Put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { b[at] = uint8_t(v); b[at + 1] = uint8_t(v >> 8); }
void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i)); }
void Put64(std::vector<uint8_t>& b, size_t at, uint64_t v) { for (int i = 0; i < 8; ++i) b[at + i] = uint8_t(v >> (8 * i)); }
uint16_t Get16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t Get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = v << 8 | p[i]; return v; }

std::vector<uint8_t> Packet(uint8_t type, uint16_t cmd, uint64_t id, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> p(kHeader);
    p[0] = type;
    Put16(p, 1, cmd);
    Put32(p, 3, uint32_t(kHeader + payload.size()));
    Put64(p, 7, id);
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

bool SendAll(SOCKET s, const std::vector<uint8_t>& d) {
    size_t sent = 0;
    while (sent < d.size()) {
        const int n = send(s, reinterpret_cast<const char*>(d.data() + sent), int(d.size() - sent), 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

bool RecvAll(SOCKET s, uint8_t* d, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = recv(s, reinterpret_cast<char*>(d + got), int(n - got), 0);
        if (r <= 0) return false;
        got += size_t(r);
    }
    return true;
}

struct Mock {
    SOCKET listener = INVALID_SOCKET;
    uint16_t port = 0;
    std::thread thread;
    std::atomic<bool> done{false};
    // What the client sent.
    std::string npid, password, titleId, titleName;
    std::vector<uint16_t> commands;  // every request after the login
    uint8_t loginError = 0;          // 0 = accept; else the ErrorType byte to refuse with
    std::string token;               // GetToken's answer ("" = refuse it)
    bool pushInvite = false;         // after SetClientVersion: a sessionInvitation push event

    static void Blob(std::vector<uint8_t>& out, const std::string& s) {
        std::vector<uint8_t> len(4);
        Put32(len, 0, uint32_t(s.size()));
        out.insert(out.end(), len.begin(), len.end());
        out.insert(out.end(), s.begin(), s.end());
    }
    // Notification 17, as shadNet's server lays it out (net/shadnet/client.cpp HandleNotification).
    static std::vector<uint8_t> InvitePush() {
        std::vector<uint8_t> p;
        Blob(p, "sessionInvitation");
        std::vector<uint8_t> label(4);
        Put32(label, 0, 0);
        p.insert(p.end(), label.begin(), label.end());
        Blob(p, "np:service:invitation");
        Blob(p, "{\"sessionId\":\"sess-1\",\"invitationId\":\"inv-7\"}");
        Blob(p, "Pal_1");
        Blob(p, "Tester_1");
        std::vector<uint8_t> count(4);
        Put32(count, 0, 1);
        p.insert(p.end(), count.begin(), count.end());
        Blob(p, "sessionId");
        Blob(p, "sess-1");
        return p;
    }

    bool Listen() {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        if (listen(listener, 1) != 0) return false;
        int len = sizeof(a);
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        thread = std::thread([this] { Serve(); });
        return true;
    }

    void Serve() {
        SOCKET c = accept(listener, nullptr, nullptr);
        if (c == INVALID_SOCKET) return;
        std::vector<uint8_t> version(4);
        Put32(version, 0, 1);  // SHAD_PROTOCOL_VERSION
        SendAll(c, Packet(3 /*ServerInfo*/, 0, 0, version));
        for (;;) {
            uint8_t h[kHeader];
            if (!RecvAll(c, h, kHeader)) break;
            const uint16_t cmd = Get16(h + 1);
            const uint32_t total = Get32(h + 3);
            const uint64_t id = Get64(h + 7);
            std::vector<uint8_t> body(total - kHeader);
            if (!body.empty() && !RecvAll(c, body.data(), body.size())) break;
            if (cmd == 0 /*Login*/) {
                shadnet::LoginRequest req;
                if (body.size() >= 4) req.ParseFromArray(body.data() + 4, int(Get32(body.data())));
                npid = req.npid();
                password = req.password();
                titleId = req.title_id();
                titleName = req.title_name();
                std::vector<uint8_t> reply{loginError};
                if (loginError == 0) {
                    shadnet::LoginReply r;
                    r.set_user_id(42);
                    auto* f = r.add_friends();
                    f->set_npid("Pal_1");
                    f->set_online(true);
                    auto* g = r.add_friends();
                    g->set_npid("Pal_2");
                    g->set_online(false);
                    r.add_friend_requests_received("Stranger_9");
                    const std::string blob = r.SerializeAsString();
                    std::vector<uint8_t> len(4);
                    Put32(len, 0, uint32_t(blob.size()));
                    reply.insert(reply.end(), len.begin(), len.end());
                    reply.insert(reply.end(), blob.begin(), blob.end());
                }
                SendAll(c, Packet(1 /*Reply*/, 0, id, reply));
                if (loginError != 0) break;  // like the server: refused, then closed
            } else {
                commands.push_back(cmd);
                if (cmd == 39 /*GetToken*/) {
                    if (token.empty()) {
                        SendAll(c, Packet(1, cmd, id, {1 /*error: no token*/}));
                    } else {
                        shadnet::GetTokenReply r;
                        r.set_token(token);
                        r.set_user_id(42);
                        r.set_npid(npid);
                        std::vector<uint8_t> reply{0};
                        Blob(reply, r.SerializeAsString());
                        SendAll(c, Packet(1, cmd, id, reply));
                    }
                } else if (cmd == 12 /*GetServerFeatures*/) {
                    shadnet::ServerFeaturesReply r;
                    r.set_matching2_enabled(true);
                    const std::string blob = r.SerializeAsString();
                    std::vector<uint8_t> reply{0}, len(4);
                    Put32(len, 0, uint32_t(blob.size()));
                    reply.insert(reply.end(), len.begin(), len.end());
                    reply.insert(reply.end(), blob.begin(), blob.end());
                    SendAll(c, Packet(1, cmd, id, reply));
                } else {
                    SendAll(c, Packet(1, cmd, id, {0}));
                    if (cmd == 13 /*SetClientVersion*/ && pushInvite) SendAll(c, Packet(2, 17, 0, InvitePush()));
                }
            }
        }
        closesocket(c);
        done = true;
    }

    void Close() {
        if (listener != INVALID_SOCKET) closesocket(listener);
        if (thread.joinable()) thread.join();
    }
};

// --- the mock Web API: plain HTTP on 127.0.0.1, one request per connection --------------------
struct WebMock {
    SOCKET listener = INVALID_SOCKET;
    uint16_t port = 0;
    std::thread thread;
    std::mutex m;
    struct Request {
        std::string method, path, authorization, contentType, body;
    };
    std::vector<Request> seen;

    bool Listen() {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        if (listen(listener, 8) != 0) return false;
        int len = sizeof(a);
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        thread = std::thread([this] {
            for (;;) {
                SOCKET c = accept(listener, nullptr, nullptr);
                if (c == INVALID_SOCKET) return;
                Serve(c);
                closesocket(c);
            }
        });
        return true;
    }
    static std::string Header(const std::string& head, const std::string& name) {
        std::istringstream in(head);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = line.substr(0, colon);
            for (char& ch : k) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (k == name) return line.substr(line.find_first_not_of(' ', colon + 1));
        }
        return {};
    }
    void Serve(SOCKET c) {
        std::string data;
        char buf[4096];
        size_t headEnd;
        while ((headEnd = data.find("\r\n\r\n")) == std::string::npos) {
            const int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) return;
            data.append(buf, size_t(n));
        }
        const std::string head = data.substr(0, headEnd);
        const size_t length = size_t(std::atoi(Header(head, "content-length").c_str()));
        std::string body = data.substr(headEnd + 4);
        while (body.size() < length) {
            const int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) return;
            body.append(buf, size_t(n));
        }
        Request s;
        std::istringstream first(head.substr(0, head.find("\r\n")));
        first >> s.method >> s.path;
        s.authorization = Header(head, "authorization");
        s.contentType = Header(head, "content-type");
        s.body = body;
        int status = 200;
        std::string answer;
        const std::string route = s.path.substr(0, s.path.find('?'));
        if (route == "/v1/users/Tester_1/friendList")
            answer = "{\"friendList\":[{\"onlineId\":\"Pal_1\",\"npId\":\"UGFsXzFA\"}],\"start\":0,\"size\":1,\"totalResults\":1}";
        else if (route == "/v1/users/Tester_1/profile")
            answer = "{\"profile\":{\"onlineId\":\"Tester_1\"}}";
        else if (route == "/v1/sessions" && s.method == "POST")
            answer = "{\"sessionId\":\"sess-1\"}";
        else
            status = 404, answer = "{\"error\":{\"code\":2105868,\"message\":\"Not found\"}}";
        {
            std::lock_guard lk(m);
            seen.push_back(std::move(s));
        }
        const std::string reply = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Not Found") +
                                  "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(answer.size()) +
                                  "\r\nConnection: close\r\n\r\n" + answer;
        send(c, reply.data(), int(reply.size()), 0);
        shutdown(c, SD_SEND);
    }
    std::vector<Request> Seen() {
        std::lock_guard lk(m);
        return seen;
    }
    void Close() {
        if (listener != INVALID_SOCKET) closesocket(listener);
        if (thread.joinable()) thread.join();
    }
};

// What the game's push event callbacks were given.
struct PushSeen {
    int calls = 0;
    int32_t ctx = 0, cbId = 0;
    std::string service, dataType, data, from, to;
    void* arg = nullptr;
};
PushSeen g_servicePush, g_basicPush;
struct PeerAddr {
    char onlineId[16];
    int8_t term;
    int8_t dummy[3];
    int32_t platform;
};
void SYSV OnServicePush(int32_t ctx, int32_t cbId, const char* service, uint32_t, const PeerAddr* to, const PeerAddr* from,
                        const char* dataType, const char* data, uint64_t len, void* arg) {
    ++g_servicePush.calls;
    g_servicePush.ctx = ctx;
    g_servicePush.cbId = cbId;
    g_servicePush.service = service ? service : "";
    g_servicePush.dataType = dataType ? dataType : "";
    g_servicePush.data = data ? std::string(data, len) : "";
    g_servicePush.from = from ? std::string(from->onlineId, strnlen(from->onlineId, 16)) : "";
    g_servicePush.to = to ? std::string(to->onlineId, strnlen(to->onlineId, 16)) : "";
    g_servicePush.arg = arg;
}
void SYSV OnBasicPush(int32_t, int32_t, const PeerAddr*, const PeerAddr*, const char*, const char*, uint64_t, void*) {
    ++g_basicPush.calls;
}

// One Web API request, as the game makes it: create, send, status, read everything, delete.
struct WebResult {
    int sendResult = -1, status = 0;
    std::string body;
};
WebResult WebRequest(int32_t ctx, const char* path, int32_t method = 0, const char* contentType = nullptr,
                     const std::string& payload = {}) {
    struct ContentParameter {
        uint64_t contentLength;
        const char* contentType;
        uint8_t reserved[16];
    } cp{payload.size(), contentType, {}};
    WebResult r;
    int64_t id = 0;
    if (hle::npsn::sceNpWebApiCreateRequest(ctx, "userProfile", path, method, contentType ? &cp : nullptr, &id) != 0)
        return r;
    r.sendResult = hle::npsn::sceNpWebApiSendRequest(id, payload.empty() ? nullptr : payload.data(), payload.size());
    hle::npsn::sceNpWebApiGetHttpStatusCode(id, &r.status);
    char buf[7];  // small, so the body is read in pieces
    for (int n; (n = hle::npsn::sceNpWebApiReadData(id, buf, sizeof(buf))) > 0;) r.body.append(buf, size_t(n));
    hle::npsn::sceNpWebApiDeleteRequest(id);
    return r;
}

Config Local(uint16_t port) {
    Config c;
    c.enabled = true;
    c.host = "127.0.0.1";
    c.port = port;
    c.name = "Tester_1";
    c.password = "pw-123";
    return c;
}

int NpState() {
    int32_t s = 0;
    hle::np::sceNpGetState(1, &s);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const bool slow = argc > 1 && std::strcmp(argv[1], "--slow") == 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // --- 1. settings -------------------------------------------------------------------------
    {
        Config off = Parse({});
        CHECK(!off.enabled);
        CHECK(off.why.empty());  // not asked for: nothing to complain about
        Config other = Parse({{"LBP3_SHADNET", "0"}, {"LBP3_SHADNET_SERVER", "play.lbppc.com"}});
        CHECK(!other.enabled);

        Config lbppc = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "play.lbppc.com"},
                              {"LBP3_SHADNET_NAME", "Tester_1"}, {"LBP3_SHADNET_PASSWORD", "secret"}});
        CHECK(lbppc.enabled);
        CHECK(lbppc.host == "shadnet.lbppc.com");
        CHECK(lbppc.port == 31313);
        CHECK(lbppc.tls);
        CHECK(lbppc.name == "Tester_1");
        CHECK(lbppc.password == "secret");
        CHECK(lbppc.webApi == "https://api.lbppc.com");

        Config tls = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "tls://example.org:4000"},
                            {"LBP3_SHADNET_NAME", "abc"}, {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(tls.enabled && tls.tls && tls.host == "example.org" && tls.port == 4000);
        CHECK(tls.webApi == "http://example.org:31315");  // shadNet's own Web API port
        Config localApi = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "play.lbppc.com"}, {"LBP3_SHADNET_NAME", "abc"},
                                 {"LBP3_SHADNET_PASSWORD", "p"}, {"LBP3_SHADNET_WEBAPI", "local"}});
        CHECK(localApi.enabled && localApi.webApi.empty());
        Config ownApi = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "h:1"}, {"LBP3_SHADNET_NAME", "abc"},
                               {"LBP3_SHADNET_PASSWORD", "p"}, {"LBP3_SHADNET_WEBAPI", "https://api.example.org/"}});
        CHECK(ownApi.webApi == "https://api.example.org");

        Config plain = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "127.0.0.1:31313"},
                              {"LBP3_SHADNET_NAME", "abc"}, {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(plain.enabled && !plain.tls && plain.host == "127.0.0.1" && plain.port == 31313);

        Config noport = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "tcp://myhost"},
                               {"LBP3_SHADNET_NAME", "abc"}, {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(noport.enabled && !noport.tls && noport.host == "myhost" && noport.port == 31313);

        // Name from LBP3_ONLINE_NAME, password from the account file.
        Config file = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "127.0.0.1:1"}, {"LBP3_ONLINE_NAME", "Onl-Name"}},
                            "# my account\r\nname = FromFile\r\nPassword: hunter2\r\n");
        CHECK(file.enabled);
        CHECK(file.name == "Onl-Name");  // the environment wins over the file
        CHECK(file.password == "hunter2");
        Config fileOnly = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "127.0.0.1:1"}},
                                "online_name=FileUser\npassword=x\n");
        CHECK(fileOnly.enabled && fileOnly.name == "FileUser" && fileOnly.password == "x");

        // Incomplete: off, with the reason.
        Config noServer = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_NAME", "abc"}, {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(!noServer.enabled && !noServer.why.empty());
        Config badName = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "h:1"}, {"LBP3_SHADNET_NAME", "ab"},
                                {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(!badName.enabled && badName.why.find("name") != std::string::npos);
        Config badChars = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "h:1"}, {"LBP3_SHADNET_NAME", "bad name!"},
                                 {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(!badChars.enabled);
        Config noPass = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "h:1"}, {"LBP3_SHADNET_NAME", "abc"}});
        CHECK(!noPass.enabled && noPass.why.find("password") != std::string::npos);
        Config badPort = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "h:99999"}, {"LBP3_SHADNET_NAME", "abc"},
                                {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(!badPort.enabled);
    }

    // --- 2. off: no client, no thread, no socket ---------------------------------------------------
    {
        CHECK(!Settings().enabled);  // this process has no LBP3_SHADNET
        const int before = ThreadCount();
        Start();                // the process's own settings: off
        Start(Parse({}));       // explicitly off
        Start(Parse({{"LBP3_SHADNET", "1"}}));  // incomplete: off
        CHECK(!Active());
        CHECK(GetStatus() == Status::Off);
        CHECK(Name().empty());
        CHECK(Friends().empty());
        CHECK(ThreadCount() == before);
        CHECK(NpState() == 1);  // signed out, as without shadNet
    }

    // --- 3a. a login against the mock ------------------------------------------------------------
    const int baseThreads = ThreadCount();
    {
        Mock mock;
        CHECK(mock.Listen());
        std::atomic<int> notices{0};
        SetStatusListener([&](Status) { ++notices; });
        Start(Local(mock.port));
        CHECK(Active());
        const Status s = WaitSettled(10000);
        CHECK(s == Status::SignedIn);
        CHECK(GetStatus() == Status::SignedIn);
        CHECK(Name() == "Tester_1");
        CHECK(mock.npid == "Tester_1");
        CHECK(mock.password == "pw-123");
        CHECK(mock.titleId == "CUSA00473");
        CHECK(mock.titleName.find("LittleBigPlanet") == 0);
        const auto friends = Friends();
        CHECK(friends.size() == 2);
        CHECK(friends.size() == 2 && friends[0].npid == "Pal_1" && friends[0].online);
        CHECK(friends.size() == 2 && friends[1].npid == "Pal_2" && !friends[1].online);
        CHECK(notices.load() >= 1);
        // The game's NP state: Settings() is off in this process, so the game still sees "signed
        // out"; with LBP3_SHADNET=1 np_offline.cpp answers from GetStatus() (checked above).
        CHECK(NpState() == 1);
        // After the login the client asks for its token and reports its version.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        bool sawToken = false, sawVersion = false;
        for (uint16_t c : mock.commands) {
            sawToken |= c == 39;
            sawVersion |= c == 13;
        }
        CHECK(sawToken);
        CHECK(sawVersion);
        Start(Local(mock.port));  // idempotent: already running
        Stop();
        CHECK(!Active());
        CHECK(GetStatus() == Status::Off);
        mock.Close();
        SetStatusListener(nullptr);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::printf("threads: %d before the client, %d after Stop(); named: [%s]\n", baseThreads, ThreadCount(),
                NamedThreads().c_str());
    // Every thread the client and the service started is gone. (Windows may keep a socket or
    // thread-pool worker of its own; the client's threads are the named ones.)
    CHECK(NamedThreads().find("ShadNet") == std::string::npos);

    // --- 3b. a refused login (wrong password) -------------------------------------------------------
    {
        Mock mock;
        mock.loginError = 8;  // LoginInvalidPassword
        CHECK(mock.Listen());
        Start(Local(mock.port));
        CHECK(WaitSettled(10000) == Status::Failed);
        CHECK(GetStatus() == Status::Failed);
        CHECK(Friends().empty());
        Stop();
        mock.Close();
    }

    // --- 3c. a server that can't be reached ------------------------------------------------------
    // The client tries 4 times (up to 10 s each, with 1 + 2 + 4 s back-off) before it gives up, up to
    // ~50 s: "shadnettest --slow" waits for that. Always: while it's still trying, the status is
    // Connecting (the game's sign-in notice waits up to 20 s), and Stop() ends it promptly.
    // 0.0.0.0 never leaves this PC.
    {
        Config c = Local(31313);
        c.host = "0.0.0.0";
        Start(c);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        CHECK(GetStatus() == Status::Connecting);
        if (slow) CHECK(WaitSettled(70000) == Status::Failed);
        const auto t0 = std::chrono::steady_clock::now();
        Stop();
        const double stopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("Stop() while connecting took %.2f s\n", stopSeconds);
        CHECK(stopSeconds < 12.0);
        CHECK(GetStatus() == Status::Off);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(NamedThreads().find("ShadNet") == std::string::npos);

    // --- 5. phase (b): the Web API and push events ---------------------------------------------------
    // stderr goes to a file meanwhile, to check what's logged (and that the token isn't).
    const std::string logPath = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\shadnettest_b.log";
    std::fflush(stderr);
    const int savedErr = _dup(2);
    FILE* logFile = std::fopen(logPath.c_str(), "w");
    _dup2(_fileno(logFile), 2);
    {
        Mock mock;
        mock.token = "tok-SECRET-123";
        mock.pushInvite = true;
        WebMock web;
        CHECK(mock.Listen());
        CHECK(web.Listen());
        Config c = Local(mock.port);
        c.webApi = "http://127.0.0.1:" + std::to_string(web.port);
        Start(c);
        CHECK(WaitSettled(10000) == Status::SignedIn);
        for (int i = 0; i < 100 && BearerToken().empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(BearerToken() == "tok-SECRET-123");
        CHECK(WebApiBase() == c.webApi);

        const int lib = hle::npsn::sceNpWebApiInitialize(1, 0x20000);
        CHECK(lib > 0);
        const int ctx = hle::npsn::sceNpWebApiCreateContext(lib, "Tester_1");
        CHECK(ctx > 0);

        // A friend list: the server's answer, read back in pieces, with the token as a bearer.
        const char* friendPath =
            "/v1/users/Tester_1/friendList?friendStatus=friend&fields=onlineId,npId,avatarUrl,personalDetail"
            "&presenceType=incontext&presenceDetail=true&sort=onlineId&limit=500&offset=0";
        WebResult fl = WebRequest(ctx, friendPath);
        CHECK(fl.sendResult == 0);
        CHECK(fl.status == 200);
        CHECK(fl.body.find("\"onlineId\":\"Pal_1\"") != std::string::npos);
        auto seen = web.Seen();
        CHECK(seen.size() == 1);
        CHECK(!seen.empty() && seen[0].method == "GET" && seen[0].path == friendPath);
        CHECK(!seen.empty() && seen[0].authorization == "Bearer tok-SECRET-123");

        // A session: POST with the game's content type and body; "/v1/sessions/" loses its slash.
        WebResult ss = WebRequest(ctx, "/v1/sessions/", 1, "multipart/mixed; boundary=xyz", "--xyz\r\nbody\r\n--xyz--\r\n");
        CHECK(ss.status == 200 && ss.body == "{\"sessionId\":\"sess-1\"}");
        seen = web.Seen();
        CHECK(seen.size() == 2);
        CHECK(seen.size() == 2 && seen[1].method == "POST" && seen[1].path == "/v1/sessions");
        CHECK(seen.size() == 2 && seen[1].contentType == "multipart/mixed; boundary=xyz");
        CHECK(seen.size() == 2 && seen[1].body == "--xyz\r\nbody\r\n--xyz--\r\n");

        // An invitation path with a slash before its query (LBP3's), and a PUT and a 404.
        WebResult inv = WebRequest(ctx, "/v1/users/Tester_1/invitations/inv-7/?fields=sessionId");
        CHECK(inv.status == 404);
        seen = web.Seen();
        CHECK(seen.size() == 3 && seen[2].path == "/v1/users/Tester_1/invitations/inv-7?fields=sessionId");
        WebResult put = WebRequest(ctx, "/v1/users/Tester_1/presence/gameStatus", 2, "application/json", "{\"gameStatus\":\"Pod\"}");
        seen = web.Seen();
        CHECK(seen.size() == 4 && seen[3].method == "PUT" && seen[3].body == "{\"gameStatus\":\"Pod\"}");
        CHECK(put.status == 404);

        // The avatar, asked for again and again: one server request, then the cache.
        for (int i = 0; i < 5; ++i) {
            WebResult pr = WebRequest(ctx, "/v1/users/Tester_1/profile?fields=avatarUrl&avatarSize=l");
            CHECK(pr.status == 200 && pr.body.find("Tester_1") != std::string::npos);
        }
        CHECK(web.Seen().size() == 5);

        // Push events: a service filter for invitations takes the mock's push; a basic filter doesn't.
        char types[2][65] = {};
        std::snprintf(types[0], 65, "%s", "np:service:invitation");
        std::snprintf(types[1], 65, "%s", "np:service:other");
        const int sf = hle::npsn::sceNpWebApiCreateServicePushEventFilter(lib, 1, "sessionInvitation", 0, types, 2);
        const int bf = hle::npsn::sceNpWebApiCreatePushEventFilter(lib, types, 1);
        CHECK(sf > 0 && bf > 0 && sf != bf);
        int argMarker = 0;
        const int scb = hle::npsn::sceNpWebApiRegisterServicePushEventCallback(ctx, sf, reinterpret_cast<void*>(&OnServicePush), &argMarker);
        const int bcb = hle::npsn::sceNpWebApiRegisterPushEventCallback(ctx, bf, reinterpret_cast<void*>(&OnBasicPush), nullptr);
        CHECK(scb > 0 && bcb > 0);
        for (int i = 0; i < 150 && g_servicePush.calls == 0; ++i) {
            hle::npsn::DispatchPushEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(g_servicePush.calls == 1);
        CHECK(g_servicePush.ctx == ctx && g_servicePush.cbId == scb && g_servicePush.arg == &argMarker);
        CHECK(g_servicePush.service == "sessionInvitation");
        CHECK(g_servicePush.dataType == "np:service:invitation");
        CHECK(g_servicePush.data.find("\"invitationId\":\"inv-7\"") != std::string::npos);
        CHECK(g_servicePush.from == "Pal_1" && g_servicePush.to == "Tester_1");
        CHECK(g_basicPush.calls == 0);  // a service's event isn't a basic one
        hle::npsn::DispatchPushEvents();
        CHECK(g_servicePush.calls == 1);  // delivered once

        // The invitation, behind the game custom data dialog's mode 2; joining it raises
        // SESSION_INVITATION through libSceSystemService.
        uint8_t status[160] = {};
        CHECK(hle::npsn::sceSystemServiceGetStatusShadNet(status) == 0);
        int32_t eventNum = -1;
        std::memcpy(&eventNum, status, 4);
        CHECK(eventNum == 0);
        std::vector<uint8_t> event(4 + 8192);
        CHECK(hle::npsn::sceSystemServiceReceiveEvent(event.data()) == int(0x80A10004));  // no event
        const std::wstring question = hle::npsn::InvitationText();
        CHECK(question.find(L"Pal_1 invited you") == 0);
        hle::npsn::AnswerInvitation(true);
        CHECK(hle::npsn::InvitationText().empty());  // answered
        CHECK(hle::npsn::sceSystemServiceGetStatusShadNet(status) == 0);
        std::memcpy(&eventNum, status, 4);
        CHECK(eventNum == 1);
        CHECK(hle::npsn::sceSystemServiceReceiveEvent(event.data()) == 0);
        int32_t type = 0, flag = 0, user = 0;
        std::memcpy(&type, event.data(), 4);
        std::memcpy(&flag, event.data() + 4 + 112, 4);
        std::memcpy(&user, event.data() + 4 + 140, 4);
        CHECK(type == 0x10000002);
        CHECK(std::string(reinterpret_cast<const char*>(event.data() + 4)) == "sess-1");
        CHECK(std::string(reinterpret_cast<const char*>(event.data() + 4 + 51)) == "inv-7");
        CHECK(flag == 1 && user == 1);
        CHECK(std::string(reinterpret_cast<const char*>(event.data() + 4 + 120)) == "Tester_1");
        CHECK(std::string(reinterpret_cast<const char*>(event.data() + 4 + 144)) == "Pal_1");
        CHECK(hle::npsn::sceSystemServiceReceiveEvent(event.data()) == int(0x80A10004));
        hle::npsn::AnswerInvitation(false);  // nothing left: no-op
        CHECK(hle::npsn::sceSystemServiceGetStatusShadNet(status) == 0);
        std::memcpy(&eventNum, status, 4);
        CHECK(eventNum == 0);

        // ParseNpId: the npId of a friend list entry.
        uint8_t npId[36];
        std::memset(npId, 0xcc, sizeof(npId));
        CHECK(hle::npsn::sceNpWebApiUtilityParseNpId("UGFsXzFA", npId) == 0);  // "Pal_1@"
        CHECK(std::memcmp(npId, "Pal_1\0", 6) == 0 && npId[16] == 0 && npId[28] == 1);

        Stop();
        // Signed out: answered locally (and nothing reaches the server).
        WebResult off = WebRequest(ctx, friendPath);
        CHECK(off.status == 200 && off.body == "{\"friendList\":[],\"start\":0,\"size\":0,\"totalResults\":0}");
        CHECK(web.Seen().size() == 5);
        hle::npsn::sceNpWebApiDeleteContext(ctx);
        mock.Close();
        web.Close();
    }
    {
        // No Web API (LBP3_SHADNET_WEBAPI=local): the friend list is shadNet's friends.
        Mock mock;
        CHECK(mock.Listen());
        Start(Local(mock.port));
        CHECK(WaitSettled(10000) == Status::SignedIn);
        const int ctx = hle::npsn::sceNpWebApiCreateContext(1, "Tester_1");
        WebResult fl = WebRequest(ctx, "/v1/users/Tester_1/friendList?friendStatus=friend&presenceType=incontext");
        CHECK(fl.status == 200);
        CHECK(fl.body.find("\"onlineId\":\"Pal_1\"") != std::string::npos);
        CHECK(fl.body.find("\"onlineId\":\"Pal_2\"") != std::string::npos);
        CHECK(fl.body.find("\"npId\":\"UGFsXzFA\"") != std::string::npos);
        CHECK(fl.body.find("\"totalResults\":2") != std::string::npos);
        CHECK(fl.body.find("\"incontextInfoList\":[]") != std::string::npos);
        WebResult other = WebRequest(ctx, "/v1/users/Tester_1/profile?fields=avatarUrl");
        CHECK(other.status == 200 && other.body == "{}");
        hle::npsn::sceNpWebApiDeleteContext(ctx);
        Stop();
        mock.Close();
    }
    std::fflush(stderr);
    _dup2(savedErr, 2);
    _close(savedErr);
    std::fclose(logFile);
    {
        std::ifstream in(logPath);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string log = ss.str();
        CHECK(log.find("push event") != std::string::npos);
        CHECK(log.find("from server") != std::string::npos);
        CHECK(log.find("from cache") != std::string::npos);
        CHECK(log.find("tok-SECRET-123") == std::string::npos);  // the token is never logged
        std::printf("phase (b) log: %zu bytes (%s)\n", log.size(), logPath.c_str());
    }

    WSACleanup();
    std::printf("shadnettest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
