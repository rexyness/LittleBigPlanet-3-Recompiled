// shadNet for the launcher against a fake server on 127.0.0.1 (never a real one): Create, a sign-in
// test (good and wrong password), and the Friends session (list, accept, request, remove).
// Included by launchertest.cpp.

namespace fake {
constexpr size_t kHeader = 15;
void Put32(std::string& b, size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = char(v >> (8 * i)); }
std::string Packet(std::uint8_t type, std::uint16_t cmd, std::uint64_t id, const std::string& payload) {
    std::string p(kHeader, '\0');
    p[0] = char(type);
    p[1] = char(cmd & 0xff);
    p[2] = char(cmd >> 8);
    Put32(p, 3, std::uint32_t(kHeader + payload.size()));
    for (int i = 0; i < 8; ++i) p[7 + i] = char(id >> (8 * i));
    return p + payload;
}
std::string Proto(const std::string& blob) {
    std::string len(4, '\0');
    Put32(len, 0, std::uint32_t(blob.size()));
    return len + blob;
}
bool SendAll(SOCKET s, const std::string& d) {
    size_t sent = 0;
    while (sent < d.size()) {
        const int n = send(s, d.data() + sent, int(d.size() - sent), 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}
bool RecvAll(SOCKET s, char* d, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int r = recv(s, d + got, int(n - got), 0);
        if (r <= 0) return false;
        got += size_t(r);
    }
    return true;
}

// One shadNet server: accepts connections one after another until closed.
struct Server {
    SOCKET listener = INVALID_SOCKET;
    std::uint16_t port = 0;
    std::thread thread;
    std::mutex m;
    std::string password = "good-pass-1";
    std::vector<std::string> created;            // names Create made
    std::vector<std::pair<int, std::string>> friendCommands;  // (8 add / 9 remove, name)

    bool Listen() {
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(listener, 4) != 0) return false;
        int len = sizeof a;
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
    void Close() {
        if (listener != INVALID_SOCKET) closesocket(listener), listener = INVALID_SOCKET;
        if (thread.joinable()) thread.join();
    }
    void Serve(SOCKET c) {
        std::string version(4, '\0');
        Put32(version, 0, 1);
        SendAll(c, Packet(3 /*ServerInfo*/, 0, 0, version));
        for (;;) {
            char h[kHeader];
            if (!RecvAll(c, h, kHeader)) return;
            const std::uint16_t cmd = std::uint16_t(std::uint8_t(h[1]) | std::uint8_t(h[2]) << 8);
            std::uint32_t total = 0;
            std::memcpy(&total, h + 3, 4);
            std::uint64_t id = 0;
            std::memcpy(&id, h + 7, 8);
            std::string body(total - kHeader, '\0');
            if (!body.empty() && !RecvAll(c, body.data(), body.size())) return;
            const std::string msg = body.size() >= 4 ? body.substr(4) : std::string();
            if (cmd == 2 /*Create*/) {
                shadnet::LoginRequest any;  // fields 1 (name) and 2 (password) share LoginRequest's numbers
                any.ParseFromString(msg);
                std::lock_guard lock(m);
                const bool taken = std::find(created.begin(), created.end(), any.npid()) != created.end();
                if (!taken) created.push_back(any.npid());
                SendAll(c, Packet(1, cmd, id, std::string(1, char(taken ? 11 : 0))));
                return;
            }
            if (cmd == 0 /*Login*/) {
                shadnet::LoginRequest req;
                req.ParseFromString(msg);
                if (req.password() != password) {
                    SendAll(c, Packet(1, 0, id, std::string(1, char(8 /*LoginInvalidPassword*/))));
                    return;
                }
                shadnet::LoginReply r;
                r.set_user_id(7);
                auto* f = r.add_friends();
                f->set_npid("Pal_1");
                f->set_online(true);
                auto* g = r.add_friends();
                g->set_npid("Pal_2");
                r.add_friend_requests_received("Stranger_9");
                r.add_friend_requests_sent("Waiting_3");
                SendAll(c, Packet(1, 0, id, std::string(1, '\0') + Proto(r.SerializeAsString())));
                continue;
            }
            if (cmd == 8 || cmd == 9) {
                shadnet::FriendCommandRequest req;
                req.ParseFromString(msg);
                {
                    std::lock_guard lock(m);
                    friendCommands.emplace_back(cmd, req.npid());
                }
                const bool exists = req.npid() != "Nobody_0";
                SendAll(c, Packet(1, cmd, id, std::string(1, char(exists ? 0 : 26 /*NotFound*/))));
                if (exists && cmd == 8 && req.npid() == "Stranger_9") {  // they had asked: now friends
                    shadnet::NotifyFriendNew n;
                    n.set_npid("Stranger_9");
                    n.set_online(true);
                    SendAll(c, Packet(2 /*Notification*/, 6 /*FriendNew*/, 0, Proto(n.SerializeAsString())));
                }
                continue;
            }
            if (cmd == 39 /*GetToken*/) {
                SendAll(c, Packet(1, cmd, id, std::string(1, '\x01')));
            } else if (cmd == 12 /*GetServerFeatures*/) {
                shadnet::ServerFeaturesReply r;
                SendAll(c, Packet(1, cmd, id, std::string(1, '\0') + Proto(r.SerializeAsString())));
            } else {
                SendAll(c, Packet(1, cmd, id, std::string(1, '\0')));
            }
        }
    }
};
}  // namespace fake

static bool WaitFor(const std::function<bool()>& done, int ms) {
    for (int t = 0; t < ms; t += 20) {
        if (done()) return true;
        Sleep(20);
    }
    return done();
}

static void TestNetFake() {
    fake::Server server;
    CHECK(server.Listen());
    if (!server.port) return;
    ShadnetEndpoint at{"127.0.0.1", server.port, false};
    std::string error;
    // Create, then the same name again
    CHECK(CreateShadnetAccount(at, "Fresh_1", "good-pass-1", "fresh_1@lbppc.invalid", &error) == 0);
    CHECK(CreateShadnetAccount(at, "Fresh_1", "good-pass-1", "fresh_1@lbppc.invalid", &error) == 11 && error.find("taken") != std::string::npos);
    // the sign-in test
    CHECK(TestShadnetSignIn(at, "Tester_1", "good-pass-1").empty());
    const std::string wrong = TestShadnetSignIn(at, "Tester_1", "bad-pass");
    std::printf("  wrong password: %s\n", wrong.c_str());
    CHECK(wrong.find("wrong password") != std::string::npos);
    // the Friends session
    {
        FriendsSession s;
        s.Start(at, "Tester_1", "good-pass-1");
        CHECK(WaitFor([&] { return s.Snapshot().status != FriendsSession::Status::SigningIn; }, 10000));
        FriendsSession::View v = s.Snapshot();
        CHECK(v.status == FriendsSession::Status::SignedIn && v.name == "Tester_1");
        CHECK(v.list.friends.size() == 2 && v.list.friends[0].name == "Pal_1" && v.list.friends[0].online);
        CHECK(v.list.received.size() == 1 && v.list.sent.size() == 1);
        s.Add("Stranger_9");  // accept
        CHECK(WaitFor([&] { return s.Snapshot().list.friends.size() == 3; }, 5000));
        s.Add("New_Pal");  // a request
        CHECK(WaitFor([&] { return s.Snapshot().list.sent.size() == 2; }, 5000));
        s.Remove("Pal_2");
        CHECK(WaitFor([&] {
            const auto f = s.Snapshot().list.friends;
            return std::none_of(f.begin(), f.end(), [](const Friend& x) { return x.name == "Pal_2"; });
        }, 5000));
        s.Add("Nobody_0");
        CHECK(WaitFor([&] { return s.Snapshot().note.find("no player") != std::string::npos; }, 5000));
        v = s.Snapshot();
        CHECK(v.list.received.empty() && v.list.friends.size() == 2);
        s.Stop();
        CHECK(!s.Active() && s.Snapshot().status == FriendsSession::Status::Off);
    }
    {
        FriendsSession s;
        s.Start(at, "Tester_1", "bad-pass");
        CHECK(WaitFor([&] { return s.Snapshot().status == FriendsSession::Status::Failed; }, 10000));
        CHECK(s.Snapshot().error.find("wrong password") != std::string::npos);
    }
    // the whole Create account: file first, then shadNet; the website step fails here (no server),
    // and the saved file stays for the player.
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_create_" + NowStamp());
    std::vector<std::string> log;
    const CreateResult r = CreateOnlineAccounts((tmp / "config").string(), "http://127.0.0.1:1", at, "Second_2", "pass-word-22",
                                                [&](const std::string& l) { log.push_back(l); });
    CHECK(!r.ok && r.message.find("Website account: not created") != std::string::npos);
    CHECK(fs::exists(tmp / "config" / "online_account.txt"));  // shadNet made it: it is an account (the website can follow)
    CHECK(!fs::exists(tmp / "config" / "online_account_Second_2.pending"));
    bool password_logged = false;
    for (const auto& l : log) password_logged |= l.find("pass-word-22") != std::string::npos;
    CHECK(!password_logged);
    const CreateResult again = CreateOnlineAccounts((tmp / "config").string(), "http://127.0.0.1:1", at, "Second_2", "pass-word-22", nullptr);
    CHECK(!again.ok && again.message.find("taken") != std::string::npos);
    CHECK(ListAccounts(tmp / "config").size() == 1);  // a refused name leaves no account file behind
    CHECK(!fs::exists(tmp / "config" / "online_account_Second_2.pending"));
    server.Close();
    {
        std::lock_guard lock(server.m);
        CHECK(server.created.size() == 2);  // Fresh_1, Second_2
        CHECK(server.friendCommands.size() == 4);
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
}
