// shadNet offline (net/shadnet_service.h, best/shadnet_design.md phase (a)):
//  1. the settings: LBP3_SHADNET and its server, name and password (environment and account file);
//  2. off means no client: no thread and no socket when LBP3_SHADNET is unset or incomplete;
//  3. a login against an in-process mock server speaking the shadNet protocol (ServerInfo, the
//     Login request and reply, the friends list), a refused login, an unreachable server;
//  4. the NP sign-in state the game sees (hle/np_offline.cpp) follows the login.
// Nothing here contacts a real server: the mock listens on 127.0.0.1 only.
//   shadnettest
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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
                    SendAll(c, Packet(1, cmd, id, {1 /*error: no token*/}));
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

        Config tls = Parse({{"LBP3_SHADNET", "1"}, {"LBP3_SHADNET_SERVER", "tls://example.org:4000"},
                            {"LBP3_SHADNET_NAME", "abc"}, {"LBP3_SHADNET_PASSWORD", "p"}});
        CHECK(tls.enabled && tls.tls && tls.host == "example.org" && tls.port == 4000);

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

    WSACleanup();
    std::printf("shadnettest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
