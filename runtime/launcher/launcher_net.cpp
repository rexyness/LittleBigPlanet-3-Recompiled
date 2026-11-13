// shadNet for the launcher, the connected parts (launcher_net.h).
#include <winsock2.h>
#include <ws2tcpip.h>

#include "launcher_net.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <vector>

#include <openssl/ssl.h>

#include "launcher_accounts.h"
#include "net/shadnet/client.h"  // the runtime's copy (not shadPS4's own src/shadnet)

namespace launcher {
namespace {

constexpr int kHeader = 15;  // type u8, command u16, total size u32, packet id u64 (little-endian)

std::string Varint(std::uint64_t v) {
    std::string out;
    do {
        unsigned char b = v & 0x7f;
        v >>= 7;
        out.push_back(static_cast<char>(b | (v ? 0x80 : 0)));
    } while (v);
    return out;
}
std::string ProtoString(int field, const std::string& text) {
    return std::string(1, static_cast<char>(field << 3 | 2)) + Varint(text.size()) + text;
}

// A plain or TLS socket to the shadNet server.
struct Conn {
    SOCKET s = INVALID_SOCKET;
    SSL* ssl = nullptr;
    ~Conn() {
        if (ssl) SSL_shutdown(ssl), SSL_free(ssl);
        if (s != INVALID_SOCKET) closesocket(s);
    }
    bool Send(const std::string& d) {
        size_t sent = 0;
        while (sent < d.size()) {
            const int n = ssl ? SSL_write(ssl, d.data() + sent, static_cast<int>(d.size() - sent))
                              : send(s, d.data() + sent, static_cast<int>(d.size() - sent), 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }
    bool Recv(char* d, size_t n) {
        size_t got = 0;
        while (got < n) {
            const int r = ssl ? SSL_read(ssl, d + got, static_cast<int>(n - got)) : recv(s, d + got, static_cast<int>(n - got), 0);
            if (r <= 0) return false;
            got += static_cast<size_t>(r);
        }
        return true;
    }
    // One packet: its type, command and body.
    bool Packet(int* type, int* command, std::string* body) {
        char h[kHeader];
        if (!Recv(h, kHeader)) return false;
        *type = static_cast<unsigned char>(h[0]);
        *command = static_cast<unsigned char>(h[1]) | static_cast<unsigned char>(h[2]) << 8;
        std::uint32_t total = 0;
        std::memcpy(&total, h + 3, 4);
        if (total < kHeader || total > (1u << 23)) return false;
        body->assign(total - kHeader, '\0');
        return body->empty() || Recv(body->data(), body->size());
    }
};

bool Connect(Conn& c, const ShadnetEndpoint& at, std::string* error) {
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(at.host.c_str(), std::to_string(at.port).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "the shadNet server's address couldn't be found (offline?)";
        return false;
    }
    for (addrinfo* a = res; a && c.s == INVALID_SOCKET; a = a->ai_next) {
        SOCKET s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        DWORD timeout = 15000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) c.s = s;
        else closesocket(s);
    }
    freeaddrinfo(res);
    if (c.s == INVALID_SOCKET) {
        if (error) *error = "couldn't connect to the shadNet server (offline, or it is down)";
        return false;
    }
    if (at.tls) {
        c.ssl = ShadNet::TlsHandshake(c.s, at.host, at.port);  // verifies the certificate (Windows' roots)
        if (!c.ssl) {
            if (error) *error = "the secure connection to the shadNet server failed";
            return false;
        }
    }
    return true;
}

}  // namespace

// ---- the Friends page's session ----

FriendsSession::FriendsSession() = default;
FriendsSession::~FriendsSession() { Stop(); }

bool FriendsSession::Active() const {
    std::lock_guard lock(m_mutex);
    return m_view.status == Status::SigningIn || m_view.status == Status::SignedIn;
}

FriendsSession::View FriendsSession::Snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_view;
}

void FriendsSession::Start(const ShadnetEndpoint& at, const std::string& name, const std::string& password) {
    Stop();
    std::lock_guard lock(m_mutex);
    m_view = View{};
    m_view.status = Status::SigningIn;
    m_view.name = name;
    m_client = std::make_unique<ShadNet::ShadNetClient>();
    ShadNet::ShadNetClient* c = m_client.get();
    ShadNet::SetTlsWanted(at.tls);
    ShadNet::SetTitle("CUSA00473", "LittleBigPlanet\xE2\x84\xA2 3");
    c->onLoginResult = [this](const ShadNet::LoginResult& r) {
        std::lock_guard l(m_mutex);
        FriendsList list;
        for (const auto& f : r.friends) list.friends.push_back({f.npid, f.online});
        list.sent = r.requestsSent;
        list.received = r.requestsReceived;
        list.blocked = r.blocked;
        SortFriends(list);
        m_view.list = std::move(list);
    };
    c->onFriendNew = [this](const ShadNet::NotifyFriendNew& n) {
        std::lock_guard l(m_mutex);
        FriendAdded(m_view.list, n.npid, n.online);
        SortFriends(m_view.list);
        m_view.note = n.npid + " is your friend now";
    };
    c->onFriendLost = [this](const ShadNet::NotifyFriendLost& n) {
        std::lock_guard l(m_mutex);
        FriendLost(m_view.list, n.npid);
    };
    c->onFriendStatus = [this](const ShadNet::NotifyFriendStatus& n) {
        std::lock_guard l(m_mutex);
        FriendCameOnline(m_view.list, n.npid, n.online);
        SortFriends(m_view.list);
    };
    c->onFriendQuery = [this](const ShadNet::NotifyFriendQuery& n) {
        std::lock_guard l(m_mutex);
        RequestReceived(m_view.list, n.fromNpid);
        m_view.note = n.fromNpid + " sent you a friend request";
    };
    c->onAsyncReply = [this](ShadNet::CommandType cmd, u64 id, ShadNet::ErrorType error, const std::vector<u8>&) {
        if (cmd != ShadNet::CommandType::AddFriend && cmd != ShadNet::CommandType::RemoveFriend) return;
        std::lock_guard l(m_mutex);
        std::string name;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
            if (it->first == id) {
                name = it->second.second;
                m_pending.erase(it);
                break;
            }
        if (error != ShadNet::ErrorType::NoError) {
            m_view.note = (name.empty() ? std::string() : name + ": ") + ExplainFriendError(static_cast<int>(error));
            return;
        }
        if (cmd == ShadNet::CommandType::AddFriend) {
            const bool accept = std::any_of(m_view.list.received.begin(), m_view.list.received.end(),
                                            [&](const std::string& s) { return s == name; });
            AddFriendDone(m_view.list, name);
            m_view.note = accept ? name + " is your friend now" : "Friend request sent to " + name;
        } else {
            RemoveFriendDone(m_view.list, name);
            m_view.note = "Removed " + name;
        }
        SortFriends(m_view.list);
    };
    c->Start(at.host, at.port, name, password);
    m_waiter = std::thread([this, c] {
        ShadNet::ShadNetState st = c->WaitForConnection();
        if (st == ShadNet::ShadNetState::Ok) st = c->WaitForAuthenticated();  // the login's answer
        std::lock_guard l(m_mutex);
        if (m_client.get() != c) return;  // stopped meanwhile
        if (st == ShadNet::ShadNetState::Ok && c->IsAuthenticated()) {
            m_view.status = Status::SignedIn;
        } else {
            m_view.status = Status::Failed;
            m_view.error = ExplainShadnetState(static_cast<int>(st == ShadNet::ShadNetState::Ok ? ShadNet::ShadNetState::FailureAuth : st));
        }
    });
}

void FriendsSession::Stop() {
    std::unique_ptr<ShadNet::ShadNetClient> client;
    std::thread waiter;
    {
        std::lock_guard lock(m_mutex);
        client = std::move(m_client);
        waiter = std::move(m_waiter);
        if (m_view.status == Status::SigningIn || m_view.status == Status::SignedIn) m_view.status = Status::Off;
        m_pending.clear();
    }
    if (client) client->Stop();  // signs out now; its threads end
    // A sign-in still waiting for its answer ends by itself (30 s at most); the client must outlive
    // that wait, so both go to a cleanup thread and Stop returns now (Play doesn't wait for it).
    if (waiter.joinable()) {
        std::thread([w = std::move(waiter), c = std::move(client)]() mutable {
            w.join();
            c.reset();
        }).detach();
    }
}

void FriendsSession::Add(const std::string& name) {
    std::lock_guard lock(m_mutex);
    if (!m_client || m_view.status != Status::SignedIn) return;
    m_pending.push_back({m_client->AddFriend(name), {8, name}});
    m_view.note = "Asking the server...";
}

void FriendsSession::Remove(const std::string& name) {
    std::lock_guard lock(m_mutex);
    if (!m_client || m_view.status != Status::SignedIn) return;
    m_pending.push_back({m_client->RemoveFriend(name), {9, name}});
    m_view.note = "Asking the server...";
}

std::string TestShadnetSignIn(const ShadnetEndpoint& at, const std::string& name, const std::string& password) {
    ShadNet::ShadNetClient c;
    ShadNet::SetTlsWanted(at.tls);
    ShadNet::SetTitle("CUSA00473", "LittleBigPlanet\xE2\x84\xA2 3");
    c.Start(at.host, at.port, name, password);
    ShadNet::ShadNetState st = c.WaitForConnection();
    if (st == ShadNet::ShadNetState::Ok) st = c.WaitForAuthenticated();
    const bool ok = st == ShadNet::ShadNetState::Ok && c.IsAuthenticated();
    c.Stop();
    return ok ? std::string() : ExplainShadnetState(static_cast<int>(st == ShadNet::ShadNetState::Ok ? ShadNet::ShadNetState::FailureAuth : st));
}

// ---- creating an account ----

int CreateShadnetAccount(const ShadnetEndpoint& at, const std::string& name, const std::string& password, const std::string& email,
                         std::string* error) {
    Conn c;
    if (!Connect(c, at, error)) return -1;
    int type = 0, command = 0;
    std::string body;
    if (!c.Packet(&type, &command, &body) || type != 3 /*ServerInfo*/) {
        if (error) *error = "that doesn't look like a shadNet server";
        return -1;
    }
    const std::string request = ProtoString(1, name) + ProtoString(2, password) + ProtoString(4, email);
    std::string payload(4, '\0');
    const std::uint32_t len = static_cast<std::uint32_t>(request.size());
    std::memcpy(payload.data(), &len, 4);
    payload += request;
    std::string packet(kHeader, '\0');
    packet[0] = 0;  // request
    packet[1] = 2;  // Create
    const std::uint32_t total = static_cast<std::uint32_t>(kHeader + payload.size());
    std::memcpy(packet.data() + 3, &total, 4);
    const std::uint64_t id = 1;
    std::memcpy(packet.data() + 7, &id, 8);
    if (!c.Send(packet + payload)) {
        if (error) *error = "the shadNet server closed the connection";
        return -1;
    }
    for (int i = 0; i < 64; ++i) {  // notifications may come first
        if (!c.Packet(&type, &command, &body)) {
            if (error) *error = "the shadNet server closed the connection";
            return -1;
        }
        if (type == 1 /*Reply*/ && command == 2) {
            const int code = body.empty() ? 1 : static_cast<unsigned char>(body[0]);
            if (code && error) {
                switch (code) {
                case 3: *error = "shadNet didn't accept the name, password or e-mail"; break;
                case 4: *error = "too many attempts; try again in a minute"; break;
                case 11: *error = "that online name is taken on shadNet"; break;
                case 12: *error = "shadNet doesn't allow that e-mail provider"; break;
                case 13: *error = "that e-mail is in use on shadNet"; break;
                default: *error = "shadNet couldn't create it (error " + std::to_string(code) + ")"; break;
                }
            }
            return code;
        }
    }
    if (error) *error = "no answer from shadNet";
    return -1;
}

CreateResult CreateOnlineAccounts(const std::string& config_dir, const std::string& refresh_server, const ShadnetEndpoint& shadnet,
                                  const std::string& name, const std::string& password,
                                  const std::function<void(const std::string&)>& log) {
    CreateResult r;
    auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    AccountFile a;
    a.name = name;
    a.password = password;
    a.server = refresh_server;
    a.email = AccountEmail(a);
    // Saved first (so the password isn't lost if a step fails), as a pending file the launcher and
    // the game don't take for an account; it becomes one only once shadNet has made it.
    const std::filesystem::path pending = std::filesystem::path(config_dir) / ("online_account_" + name + ".pending");
    std::string error;
    std::error_code ec;
    std::filesystem::remove(pending, ec);  // our own leftover from an earlier try
    if (!WriteAccountFile(pending, a, &error)) {
        r.message = error;
        return r;
    }
    say("Saved the new password on this PC");
    const int code = CreateShadnetAccount(shadnet, name, password, a.email, &error);
    if (code > 0) {  // refused (taken, not allowed...): no account was made, so no account file either
        std::filesystem::remove(pending, ec);
        r.message = "shadNet account: not created (" + error + "). Nothing was sent to the website.";
        say(r.message);
        return r;
    }
    if (code < 0) {  // couldn't tell: kept as .pending, in case the server made it after all
        r.message = "shadNet account: not created (" + error + "). Nothing was sent to the website.";
        say(r.message);
        return r;
    }
    const std::filesystem::path file = NewAccountFile(config_dir, name);
    std::filesystem::rename(pending, file, ec);
    if (ec) {
        r.message = "The shadNet account exists, but its file couldn't be saved as " + file.filename().string() + " (" + ec.message() +
                    "); the password is in " + pending.filename().string();
        say(r.message);
        return r;
    }
    r.file = file.string();
    say("shadNet account: created (saved as " + file.filename().string() + ")");
    const AccountResult web = CreateRefreshAccountWith(refresh_server, name, password, a.email);
    if (!web.ok) {
        r.message = "Website account: not created (" + web.message + "). The shadNet account exists with the saved password.";
        say(r.message);
        return r;
    }
    say("Website account: " + web.message);
    r.ok = true;
    r.message = "Both accounts are ready for \"" + name + "\".";
    return r;
}

}  // namespace launcher
