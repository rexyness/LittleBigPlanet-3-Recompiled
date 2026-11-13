// shadNet service: settings, the client's lifetime, sign-in status and the friends list.
// See shadnet_service.h. Ours (GPL-2.0-or-later like the project); the client it drives is
// shadPS4's as vendored by LBPPC (net/shadnet/NOTICE.md).
#include "net/shadnet_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "common/logging/log.h"
#include "shadnet/client.h"

namespace net::shadnet {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// PSN-style online IDs: 3-16 of letters, digits, '-' and '_'.
bool ValidName(const std::string& n) {
    if (n.size() < 3 || n.size() > 16) return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    });
}

// "name=x" / "password: y" lines; other lines are ignored.
void ReadAccountFile(const std::string& text, std::string& name, std::string& password) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t sep = line.find('=');
        if (sep == std::string::npos) sep = line.find(':');
        if (sep == std::string::npos) continue;
        const std::string key = Lower(Trim(line.substr(0, sep)));
        const std::string value = Trim(line.substr(sep + 1));
        if (value.empty()) continue;
        if (key == "name" || key == "online_name" || key == "online name" || key == "onlinename" ||
            key == "username" || key == "npid")
            name = value;
        else if (key == "password" || key == "pass" || key == "shadnet_password")
            password = value;
    }
}

struct State {
    std::mutex mutex;
    std::condition_variable cv;
    Config config;
    std::unique_ptr<ShadNet::ShadNetClient> client;
    std::thread watcher;
    Status status = Status::Off;
    std::vector<Friend> friends;
    std::function<void(Status)> listener;
    std::chrono::steady_clock::time_point started{};
    bool stopping = false;
    std::deque<PushEvent> pushes;
    uint64_t accountId = 0;
    // RequestSignalingInfos waiting for their reply, by packet id.
    std::map<uint64_t, std::function<void(bool, const std::string&, uint16_t)>> resolves;
};

State& S() {
    static State s;
    return s;
}

void SetStatus(Status st, const std::string& why = {}) {
    std::function<void(Status)> listener;
    {
        std::lock_guard lock(S().mutex);
        if (S().status == st) return;
        S().status = st;
        listener = S().listener;
    }
    S().cv.notify_all();
    std::fprintf(stderr, "[shadnet] status: %s%s%s\n", StatusName(st), why.empty() ? "" : " - ", why.c_str());
    if (listener) listener(st);
}

void UpsertFriend(const std::string& npid, bool online) {
    std::lock_guard lock(S().mutex);
    auto& f = S().friends;
    auto it = std::find_if(f.begin(), f.end(), [&](const Friend& x) { return x.npid == npid; });
    if (it == f.end()) f.push_back({npid, online});
    else it->online = online;
}

const char* StateName(ShadNet::ShadNetState s) {
    using ShadNet::ShadNetState;
    switch (s) {
    case ShadNetState::Ok: return "ok";
    case ShadNetState::FailureInput: return "bad input";
    case ShadNetState::FailureResolve: return "server name not found";
    case ShadNetState::FailureConnect: return "could not connect";
    case ShadNetState::FailureServerInfo: return "no server handshake";
    case ShadNetState::FailureAuth: return "login refused";
    case ShadNetState::FailureAlreadyIn: return "already signed in elsewhere";
    case ShadNetState::FailureUsername: return "unknown online name";
    case ShadNetState::FailurePassword: return "wrong password";
    case ShadNetState::FailureToken: return "bad token";
    case ShadNetState::FailureProtocol: return "protocol mismatch";
    default: return "failed";
    }
}

}  // namespace

const char* StatusName(Status s) {
    switch (s) {
    case Status::Off: return "off";
    case Status::Connecting: return "connecting";
    case Status::SignedIn: return "signed in";
    case Status::Failed: return "failed";
    }
    return "?";
}

Config ParseConfig(const std::function<const char*(const char*)>& getenv, const std::string& accountFile) {
    Config c;
    auto env = [&](const char* k) -> std::string {
        const char* v = getenv(k);
        return v ? Trim(v) : std::string();
    };
    const std::string on = env("LBP3_SHADNET");
    if (on != "1") return c;  // off: nothing else is read

    // The server: "play.lbppc.com" (LBPPC's, through its TLS proxy), "tls://host:port", "host:port".
    std::string server = env("LBP3_SHADNET_SERVER");
    std::string lowered = Lower(server);
    if (lowered == "play.lbppc.com" || lowered == "https://play.lbppc.com" || lowered == "tls://play.lbppc.com") {
        c.host = "shadnet.lbppc.com";
        c.port = 31313;
        c.tls = true;
    } else if (!server.empty()) {
        if (lowered.rfind("tls://", 0) == 0) {
            c.tls = true;
            server = server.substr(6);
        } else if (lowered.rfind("tcp://", 0) == 0) {
            server = server.substr(6);
        }
        while (!server.empty() && server.back() == '/') server.pop_back();
        const size_t colon = server.rfind(':');
        c.host = colon == std::string::npos ? server : server.substr(0, colon);
        int port = 31313;
        if (colon != std::string::npos) port = std::atoi(server.c_str() + colon + 1);
        c.port = port > 0 && port < 65536 ? static_cast<uint16_t>(port) : 0;
    }

    std::string fileName, filePassword;
    ReadAccountFile(accountFile, fileName, filePassword);
    c.name = env("LBP3_SHADNET_NAME");
    if (c.name.empty()) c.name = env("LBP3_ONLINE_NAME");
    if (c.name.empty()) c.name = fileName;
    c.password = env("LBP3_SHADNET_PASSWORD");
    if (c.password.empty()) c.password = filePassword;

    // The Web API: LBPPC's behind its HTTPS proxy, any other server's on shadNet's own port.
    const std::string webApi = env("LBP3_SHADNET_WEBAPI");
    if (Lower(webApi) == "local" || Lower(webApi) == "off") c.webApi.clear();
    else if (!webApi.empty()) c.webApi = webApi;
    else if (c.host == "shadnet.lbppc.com" || c.host == "play.lbppc.com") c.webApi = "https://api.lbppc.com";
    else if (!c.host.empty()) c.webApi = "http://" + c.host + ":31315";
    while (!c.webApi.empty() && c.webApi.back() == '/') c.webApi.pop_back();

    // The UDP service (STUN, relay): on the server's own address, the TCP port + 1 (as LBPPC's
    // shadPS4 does: np_matching2_mm.cpp SetMmShadNetClient).
    const std::string udp = env("LBP3_SHADNET_UDP");
    if (Lower(udp) == "off") {
        c.udpHost.clear();
    } else if (!udp.empty()) {
        const size_t colon = udp.rfind(':');
        c.udpHost = colon == std::string::npos ? udp : udp.substr(0, colon);
        const int port = colon == std::string::npos ? 31314 : std::atoi(udp.c_str() + colon + 1);
        c.udpPort = port > 0 && port < 65536 ? static_cast<uint16_t>(port) : 0;
    } else if (!c.host.empty() && c.port != 0 && c.port < 65535) {
        c.udpHost = c.host;
        c.udpPort = static_cast<uint16_t>(c.port + 1);
    }
    if (c.udpPort == 0) c.udpHost.clear();

    if (c.host.empty() || c.port == 0) c.why = "LBP3_SHADNET_SERVER is not set to a server (\"host:port\", \"tls://host:port\" or \"play.lbppc.com\")";
    else if (!ValidName(c.name)) c.why = "no valid online name (LBP3_SHADNET_NAME / LBP3_ONLINE_NAME / user\\config\\online_account.txt; 3-16 letters, digits, - or _)";
    else if (c.password.empty()) c.why = "no password (LBP3_SHADNET_PASSWORD or user\\config\\online_account.txt)";
    else c.enabled = true;
    return c;
}

const Config& Settings() {
    static const Config c = [] {
        std::string text;
        const char* on = std::getenv("LBP3_SHADNET");
        if (!on || std::string(on) != "1") return Config{};  // off: not even the account file is read
        if (std::ifstream f("user\\config\\online_account.txt"); f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            text = ss.str();
        }
        Config cfg = ParseConfig([](const char* k) { return std::getenv(k); }, text);
        if (!cfg.enabled && !cfg.why.empty())
            std::fprintf(stderr, "[shadnet] LBP3_SHADNET=1 but off: %s\n", cfg.why.c_str());
        return cfg;
    }();
    return c;
}

void Start() {
    if (Settings().enabled) Start(Settings());
}

void Start(const Config& config) {
    if (!config.enabled) return;
    std::unique_lock lock(S().mutex);
    if (S().client) return;
    S().config = config;
    S().status = Status::Connecting;
    S().friends.clear();
    S().pushes.clear();
    S().stopping = false;
    S().started = std::chrono::steady_clock::now();
    // The client's own Info lines ("TCP connected", "Logged in ...") are worth seeing here.
    Common::Log::g_class_levels[static_cast<int>(Common::Log::Class::ShadNet)] = Common::Log::Level::Info;

    ShadNet::SetTlsWanted(config.tls);
    ShadNet::SetTitle("CUSA00473", "LittleBigPlanet™ 3");
    auto client = std::make_unique<ShadNet::ShadNetClient>();
    client->onLoginResult = [](const ShadNet::LoginResult& r) {
        if (r.error == ShadNet::ErrorType::NoError) {
            {
                std::lock_guard l(S().mutex);
                S().friends.clear();
                for (const auto& f : r.friends) S().friends.push_back({f.npid, f.online});
                S().accountId = r.userId;
            }
            std::fprintf(stderr, "[shadnet] signed in as %s: %zu friend(s), %zu request(s) received, %zu sent\n",
                         S().config.name.c_str(), r.friends.size(), r.requestsReceived.size(), r.requestsSent.size());
            for (const auto& f : r.friends)
                std::fprintf(stderr, "[shadnet] friend %s: %s\n", f.npid.c_str(), f.online ? "online" : "offline");
            SetStatus(Status::SignedIn);
        } else {
            SetStatus(Status::Failed, "login refused (error " + std::to_string(static_cast<int>(r.error)) + ")");
        }
    };
    client->onFriendNew = [](const ShadNet::NotifyFriendNew& n) {
        UpsertFriend(n.npid, n.online);
        std::fprintf(stderr, "[shadnet] new friend %s (%s)\n", n.npid.c_str(), n.online ? "online" : "offline");
    };
    client->onFriendLost = [](const ShadNet::NotifyFriendLost& n) {
        {
            std::lock_guard l(S().mutex);
            std::erase_if(S().friends, [&](const Friend& f) { return f.npid == n.npid; });
        }
        std::fprintf(stderr, "[shadnet] friend removed: %s\n", n.npid.c_str());
    };
    client->onFriendStatus = [](const ShadNet::NotifyFriendStatus& n) {
        UpsertFriend(n.npid, n.online);
        std::fprintf(stderr, "[shadnet] friend %s is now %s\n", n.npid.c_str(), n.online ? "online" : "offline");
    };
    client->onFriendQuery = [](const ShadNet::NotifyFriendQuery& n) {
        std::fprintf(stderr, "[shadnet] friend request from %s\n", n.fromNpid.c_str());
    };
    client->onClientRejected = [](const ShadNet::NotifyClientRejected& n) {
        SetStatus(Status::Failed, "the server rejected this client (reason " + std::to_string(n.reason) +
                                      ", version \"" + n.version + "\"): " + n.message);
    };
    client->onAsyncReply = [](ShadNet::CommandType cmd, uint64_t id, ShadNet::ErrorType error,
                              const std::vector<uint8_t>& body) {
        if (cmd != ShadNet::CommandType::RequestSignalingInfos) return;
        std::function<void(bool, const std::string&, uint16_t)> done;
        {
            std::lock_guard l(S().mutex);
            auto it = S().resolves.find(id);
            if (it == S().resolves.end()) return;
            done = std::move(it->second);
            S().resolves.erase(it);
        }
        std::string npid, ip;
        uint16_t port = 0;
        const bool ok = error == ShadNet::ErrorType::NoError && ShadNet::ShadNetClient::ParseSignalingInfos(body, npid, ip, port);
        if (!ok)
            std::fprintf(stderr, "[shadnet] the server doesn't know where that player is (error %d)\n",
                         static_cast<int>(error));
        done(ok, ip, port);
    };
    client->onWebApiPushEvent = [](const ShadNet::NotifyWebApiPushEvent& n) {
        PushEvent e;
        e.service = n.npServiceName;
        e.label = n.npServiceLabel;
        e.dataType = n.dataType;
        e.data = n.data;
        e.from = n.fromNpid;
        e.to = n.toNpid;
        e.extd = n.extdData;
        e.fromAccount = n.fromAccountId;
        e.toAccount = n.toAccountId;
        std::fprintf(stderr, "[shadnet] push event: service \"%s\", type \"%s\", from \"%s\", %zu bytes\n",
                     e.service.c_str(), e.dataType.c_str(), e.from.c_str(), e.data.size());
        std::lock_guard l(S().mutex);
        if (S().pushes.size() >= 256) S().pushes.pop_front();
        S().pushes.push_back(std::move(e));
    };
    client->onServiceUnavailable = [](const ShadNet::NotifyServiceUnavailable& n) {
        std::fprintf(stderr, "[shadnet] server refused service %u (reason %u)\n", n.service, n.reason);
    };
    std::fprintf(stderr, "[shadnet] connecting to %s:%u%s as %s\n", config.host.c_str(), config.port,
                 config.tls ? " (TLS)" : "", config.name.c_str());
    client->Start(config.host, config.port, config.name, config.password);
    S().client = std::move(client);
    // Connection failures have no callback: watch the connect result, then wait for the login reply.
    S().watcher = std::thread([] {
        ShadNet::ShadNetClient* c = nullptr;
        {
            std::lock_guard l(S().mutex);
            c = S().client.get();
        }
        if (!c) return;
        const ShadNet::ShadNetState st = c->WaitForConnection();
        if (st != ShadNet::ShadNetState::Ok) {
            SetStatus(Status::Failed, StateName(st));
            return;
        }
        std::unique_lock l(S().mutex);
        const bool answered = S().cv.wait_for(l, std::chrono::seconds(30), [] {
            return S().stopping || S().status != Status::Connecting;
        });
        if (!answered && !S().stopping) {
            l.unlock();
            SetStatus(Status::Failed, "no login reply in 30 s");
        }
    });
}

void Stop() {
    std::unique_ptr<ShadNet::ShadNetClient> client;
    std::thread watcher;
    std::map<uint64_t, std::function<void(bool, const std::string&, uint16_t)>> resolves;
    {
        std::lock_guard lock(S().mutex);
        S().stopping = true;
        client = std::move(S().client);
        watcher = std::move(S().watcher);
        S().status = Status::Off;
        S().friends.clear();
        S().pushes.clear();
        resolves.swap(S().resolves);
    }
    S().cv.notify_all();
    if (client) client->Stop();
    if (watcher.joinable()) watcher.join();
    for (auto& [id, done] : resolves) done(false, {}, 0);
}

bool Active() {
    std::lock_guard lock(S().mutex);
    return S().client != nullptr;
}

Status GetStatus() {
    std::lock_guard lock(S().mutex);
    // Signed in only while the connection holds (no reconnect yet: phase (a)).
    if (S().status == Status::SignedIn && S().client && !S().client->IsAuthenticated()) return Status::Failed;
    return S().status;
}

std::string Name() {
    std::lock_guard lock(S().mutex);
    return S().client ? S().config.name : std::string();
}

uint64_t AccountId() {
    std::lock_guard lock(S().mutex);
    return S().client && S().status == Status::SignedIn ? S().accountId : 0;
}

std::vector<Friend> Friends() {
    std::lock_guard lock(S().mutex);
    return S().friends;
}

Status WaitSettled(int ms) {
    std::unique_lock lock(S().mutex);
    S().cv.wait_for(lock, std::chrono::milliseconds(ms), [] { return S().status != Status::Connecting; });
    return S().status;
}

void SetStatusListener(std::function<void(Status)> listener) {
    std::lock_guard lock(S().mutex);
    S().listener = std::move(listener);
}

double SecondsSinceStart() {
    std::lock_guard lock(S().mutex);
    if (!S().client) return -1.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - S().started).count();
}

std::string WebApiBase() {
    std::lock_guard lock(S().mutex);
    return S().client ? S().config.webApi : std::string();
}

std::string BearerToken() {
    std::lock_guard lock(S().mutex);
    return S().client ? S().client->GetBearerToken() : std::string();
}

std::vector<PushEvent> TakePushEvents() {
    std::lock_guard lock(S().mutex);
    std::vector<PushEvent> out(std::make_move_iterator(S().pushes.begin()), std::make_move_iterator(S().pushes.end()));
    S().pushes.clear();
    return out;
}

void ResolvePeer(const std::string& npid, std::function<void(bool, const std::string&, uint16_t)> done) {
    {
        std::lock_guard lock(S().mutex);
        if (S().client && S().client->IsAuthenticated()) {
            // The reply can't come before the id is filed: the client's reader thread takes this lock.
            const uint64_t id = S().client->RequestSignalingInfos(npid);
            S().resolves[id] = std::move(done);
            return;
        }
    }
    done(false, {}, 0);
}

std::string UdpHost() {
    std::lock_guard lock(S().mutex);
    return S().client ? S().config.udpHost : std::string();
}

uint16_t UdpPort() {
    std::lock_guard lock(S().mutex);
    return S().client ? S().config.udpPort : 0;
}

}  // namespace net::shadnet
