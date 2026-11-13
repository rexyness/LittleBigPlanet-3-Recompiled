// Online co-op's peer connections, offline (net/p2p_link.h, hle/net_remote.h; best/shadnet_design.md
// phase (c) and (d)):
//  1. two players connect directly (127.0.0.1), with a mock of shadNet's UDP service (STUN pings) and
//     the server's answer to "where is that player"; game datagrams both ways; keepalive RTT;
//  2. join protection: a player who isn't allowed is refused and their connection dies;
//  3. the relay: the addresses the server reports don't reach the players (as across two NATs), so
//     the handshake and the datagrams go through the mock's relay ("SMRL");
//  4. leaving: BYE ends the other side's connection;
//  5. the socket bridge: a datagram from a peer reaches the game's P2P socket (recvfrom, epoll),
//     and the game's sendto to a non-loopback address goes to the remote side, never loopback's.
// Everything stays on 127.0.0.x.
//   p2ptest
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hle/net_remote.h"
#include "net/p2p_link.h"

using namespace net::p2p;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

#define SYSV __attribute__((sysv_abi))
namespace hle::net {
int SYSV socket(int family, int type, int proto);
int SYSV bind(int s, const void* addr, uint32_t len);
int64_t SYSV recvfrom(int s, void* buf, size_t len, int flags, void* from, uint32_t* fromLen);
int64_t SYSV sendto(int s, const void* data, size_t len, int flags, const void* addr, uint32_t addrLen);
int SYSV sceNetSocketClose(int s);
}  // namespace hle::net

namespace {

// The mock of shadNet's UDP service: STUN pings (it reports `fake` addresses when asked to, as a NAT
// in front of each player would make them look) and the relay.
struct UdpMock {
    SOCKET sock = INVALID_SOCKET;
    Endpoint self;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::mutex m;
    bool fakeAddresses = false;
    std::map<std::string, Endpoint> real, ext;  // by online ID
    int relayed = 0, pings = 0;

    bool Start() {
        sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        int len = sizeof(a);
        getsockname(sock, reinterpret_cast<sockaddr*>(&a), &len);
        self = MakeEndpoint("127.0.0.1", ntohs(a.sin_port));
        DWORD timeout = 50;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        thread = std::thread([this] { Run(); });
        return true;
    }
    void Stop() {
        stop = true;
        if (thread.joinable()) thread.join();
        closesocket(sock);
    }
    void SendTo(const Endpoint& to, const std::vector<uint8_t>& b) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = to.ip;
        a.sin_port = htons(to.port);
        ::sendto(sock, reinterpret_cast<const char*>(b.data()), int(b.size()), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    static void PutEp(std::vector<uint8_t>& b, const Endpoint& e) {
        const auto* ip = reinterpret_cast<const uint8_t*>(&e.ip);
        b.insert(b.end(), ip, ip + 4);
        b.push_back(uint8_t(e.port >> 8));
        b.push_back(uint8_t(e.port));
    }
    void Run() {
        uint8_t buf[2048];
        while (!stop) {
            sockaddr_in from{};
            int fl = sizeof(from);
            const int n = ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
            if (n <= 0) continue;
            const Endpoint src{from.sin_addr.s_addr, ntohs(from.sin_port)};
            static const uint8_t stun[6] = {0xFF, 0x83, 0xFF, 0xFF, 0xFF, 0xFF};
            if (n >= 27 && std::memcmp(buf, stun, 6) == 0 && buf[6] == 1) {
                const std::string id(reinterpret_cast<char*>(buf + 7), strnlen(reinterpret_cast<char*>(buf + 7), 16));
                Endpoint e;
                {
                    std::lock_guard lk(m);
                    ++pings;
                    real[id] = src;
                    // A NAT's address for each player: 127.0.0.2 and a port nobody listens on.
                    if (fakeAddresses && !ext.contains(id)) ext[id] = MakeEndpoint("127.0.0.2", uint16_t(9 + 4 * ext.size()));
                    if (!fakeAddresses) ext[id] = src;
                    e = ext[id];
                }
                std::vector<uint8_t> r(stun, stun + 6);
                PutEp(r, e);
                SendTo(src, r);
            } else if (n > 11 && std::memcmp(buf, "SMRL", 4) == 0 && buf[4] == 2) {
                Endpoint dest;
                std::memcpy(&dest.ip, buf + 5, 4);
                dest.port = uint16_t(buf[9] << 8 | buf[10]);
                Endpoint realDest, senderExt;
                {
                    std::lock_guard lk(m);
                    for (auto& [id, e] : ext) {
                        if (e == dest) realDest = real[id];
                        if (real[id] == src) senderExt = e;
                    }
                    if (!realDest.Valid() || !senderExt.Valid()) continue;
                    ++relayed;
                }
                std::vector<uint8_t> out{'S', 'M', 'R', 'L', 3};
                PutEp(out, senderExt);
                out.insert(out.end(), buf + 11, buf + n);
                SendTo(realDest, out);
            }
        }
    }
    Endpoint Ext(const std::string& id) {
        std::lock_guard lk(m);
        auto it = ext.find(id);
        return it == ext.end() ? Endpoint{} : it->second;
    }
};

struct Received {
    std::mutex m;
    std::vector<std::vector<uint8_t>> data;
    Endpoint from;
    uint16_t srcVport = 0, dstVport = 0;
    size_t Count() {
        std::lock_guard lk(m);
        return data.size();
    }
};

Config PlayerConfig(const std::string& name, UdpMock& mock, std::vector<std::string> allowed) {
    Config c;
    c.self = name;
    c.server = mock.self;
    c.lanIp = MakeEndpoint("127.0.0.1", 0).ip;
    c.mayConnect = [allowed](const std::string& peer) {
        for (const auto& a : allowed)
            if (a == peer) return true;
        return false;
    };
    c.resolve = [&mock](const std::string& peer, std::function<void(bool, Endpoint)> done) {
        const Endpoint e = mock.Ext(peer);
        done(e.Valid(), e);
    };
    c.resendMs = 100;
    c.relayAfterMs = 300;
    c.timeoutMs = 2000;
    c.keepaliveMs = 100;
    c.deadAfterMs = 1500;
    c.stunMs = 200;
    return c;
}

template <class F>
bool WaitFor(F f, int ms = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return f();
}

bool HasEvent(std::vector<Event>& got, Link& link, Event::Kind kind, const std::string& peer) {
    for (auto& e : link.TakeEvents()) got.push_back(e);
    for (const auto& e : got)
        if (e.kind == kind && e.peer == peer) return true;
    return false;
}

void Sink(Link& link, Received& r) {
    link.SetDataSink([&r](const Endpoint& from, uint16_t sv, uint16_t dv, const uint8_t* d, size_t n) {
        std::lock_guard lk(r.m);
        r.data.emplace_back(d, d + n);
        r.from = from;
        r.srcVport = sv;
        r.dstVport = dv;
    });
}

// The game's view: a PS4 sockaddr_in (len, family, port BE, address, vport BE, zero).
std::vector<uint8_t> PsAddr(const Endpoint& e, uint16_t vport) {
    std::vector<uint8_t> a(16, 0);
    a[0] = 16;
    a[1] = 2;
    a[2] = uint8_t(e.port >> 8);
    a[3] = uint8_t(e.port);
    std::memcpy(a.data() + 4, &e.ip, 4);
    a[8] = uint8_t(vport >> 8);
    a[9] = uint8_t(vport);
    return a;
}

struct Captured {
    uint32_t ip = 0;
    uint16_t port = 0, srcVport = 0, dstVport = 0;
    std::string data;
    int calls = 0;
} g_captured;
bool CaptureSend(uint32_t ip, uint16_t port, uint16_t sv, uint16_t dv, const void* d, size_t n) {
    ++g_captured.calls;
    g_captured.ip = ip;
    g_captured.port = port;
    g_captured.srcVport = sv;
    g_captured.dstVport = dv;
    g_captured.data.assign(static_cast<const char*>(d), n);
    return true;
}

}  // namespace

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // --- 1. a direct connection ----------------------------------------------------------------
    {
        UdpMock mock;
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        CHECK(a.Start(PlayerConfig("Alice_1", mock, {})));
        CHECK(b.Start(PlayerConfig("Bob_2", mock, {"Alice_1"})));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        CHECK(a.External().port == a.LocalPort());  // the mock reports the real address here
        a.Activate("Bob_2");
        CHECK(a.Info("Bob_2").state != State::None);
        std::vector<Event> evA, evB;
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
        const PeerInfo ia = a.Info("Bob_2"), ib = b.Info("Alice_1");
        CHECK(ia.state == State::Active && ia.locallyActivated && !ia.relayed);
        CHECK(ib.state == State::Active && ib.peerActivated && !ib.locallyActivated);
        CHECK(ia.gameAddr == b.External());
        CHECK(ib.gameAddr == a.External());
        CHECK(a.PeerAt(b.External()) == "Bob_2");
        // Game datagrams both ways, with their virtual ports.
        CHECK(a.SendData(b.External(), 3658, 3658, "hello", 5));
        CHECK(WaitFor([&] { return atB.Count() == 1; }));
        CHECK(atB.Count() == 1 && std::string(atB.data[0].begin(), atB.data[0].end()) == "hello");
        CHECK(atB.from == a.External() && atB.srcVport == 3658 && atB.dstVport == 3658);
        // The game may address the peer by its IP and another port: still them (one peer on that IP).
        Endpoint otherPort = a.External();
        otherPort.port = 3658;
        CHECK(b.SendData(otherPort, 3658, 3658, "back", 4));
        CHECK(WaitFor([&] { return atA.Count() == 1; }));
        CHECK(!b.SendData(MakeEndpoint("10.1.2.3", 3658), 3658, 3658, "x", 1));  // nobody there
        // B activates too: mutual on both sides.
        b.Activate("Alice_1");
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::Established, "Alice_1"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::Mutual, "Alice_1"); }));
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Mutual, "Bob_2"); }));
        // Keepalives measure the round trip.
        CHECK(WaitFor([&] { return a.Info("Bob_2").rttUs > 0; }));
        CHECK(a.GetStats().sentRelayed == 0 && a.GetStats().sentDirect > 0);

        // --- 2. join protection: Carol isn't allowed by Bob --------------------------------------
        Link c;
        CHECK(c.Start(PlayerConfig("Carol_3", mock, {})));
        CHECK(WaitFor([&] { return c.External().Valid(); }));
        c.Activate("Bob_2");
        std::vector<Event> evC;
        CHECK(WaitFor([&] { return HasEvent(evC, c, Event::Dead, "Carol_3") || HasEvent(evC, c, Event::Dead, "Bob_2"); }, 5000));
        CHECK(b.Info("Carol_3").state == State::None);
        CHECK(b.GetStats().refused > 0);
        bool bobSawCarol = false;
        for (const auto& e : b.TakeEvents()) bobSawCarol |= e.peer == "Carol_3";
        CHECK(!bobSawCarol);
        c.Stop();

        // --- 4. leaving ------------------------------------------------------------------------
        a.Terminate("Bob_2");
        CHECK(a.Info("Bob_2").state == State::None);
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::Dead, "Alice_1"); }));
        CHECK(b.Info("Alice_1").state == State::None);
        a.Stop();
        b.Stop();
        mock.Stop();
    }

    // --- 3. through the relay ------------------------------------------------------------------
    {
        UdpMock mock;
        mock.fakeAddresses = true;
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        Config ca = PlayerConfig("Alice_1", mock, {}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
        ca.reportLan = cb.reportLan = false;
        CHECK(a.Start(ca));
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        CHECK(a.External() == mock.Ext("Alice_1"));  // the "NAT" address, which reaches nobody
        a.Activate("Bob_2");
        std::vector<Event> evA, evB;
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
        CHECK(a.Info("Bob_2").relayed && b.Info("Alice_1").relayed);
        CHECK(a.Info("Bob_2").gameAddr == mock.Ext("Bob_2"));
        CHECK(a.SendData(mock.Ext("Bob_2"), 3658, 3658, "via relay", 9));
        CHECK(WaitFor([&] { return atB.Count() == 1; }));
        CHECK(atB.Count() == 1 && atB.from == mock.Ext("Alice_1"));
        CHECK(b.SendData(mock.Ext("Alice_1"), 3658, 3658, "and back", 8));
        CHECK(WaitFor([&] { return atA.Count() == 1; }));
        CHECK(a.GetStats().sentRelayed > 0);
        {
            std::lock_guard lk(mock.m);
            CHECK(mock.relayed > 0);
        }
        // Nothing heard for deadAfterMs (B gone without a word): dead.
        b.Stop();
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Dead, "Bob_2"); }, 6000));
        a.Stop();
        mock.Stop();
    }

    // --- 5. the socket bridge (hle/net_remote.h) ------------------------------------------------
    {
        const int s = hle::net::socket(2, 6, 0);  // a P2P datagram socket, as LBP3's
        const auto bindAddr = PsAddr(Endpoint{0, 3658}, 3658);
        CHECK(hle::net::bind(s, bindAddr.data(), 16) == 0);
        uint8_t buf[64], from[16];
        uint32_t fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0, from, &fromLen) < 0);  // nothing yet
        const Endpoint peer = MakeEndpoint("203.0.113.7", 50123);
        CHECK(hle::p2pnet::DeliverRemote(peer.ip, peer.port, 3658, 3658, reinterpret_cast<const uint8_t*>("tick"), 4));
        fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0, from, &fromLen) == 4);
        CHECK(std::memcmp(buf, "tick", 4) == 0);
        CHECK(std::memcmp(from + 4, &peer.ip, 4) == 0 && from[2] == (50123 >> 8) && from[3] == (50123 & 0xff));
        CHECK(from[8] == (3658 >> 8) && from[9] == (3658 & 0xff));  // the sender's virtual port
        CHECK(!hle::p2pnet::DeliverRemote(peer.ip, peer.port, 3658, 999, reinterpret_cast<const uint8_t*>("x"), 1));
        // sendto: unset, a remote address is unreachable (offline, as before); set, it goes there.
        const auto to = PsAddr(peer, 3658);
        CHECK(hle::net::sendto(s, "out", 3, 0, to.data(), 16) < 0);
        hle::p2pnet::SetRemoteSend(&CaptureSend);
        CHECK(hle::net::sendto(s, "out", 3, 0, to.data(), 16) == 3);
        CHECK(g_captured.calls == 1 && g_captured.ip == peer.ip && g_captured.port == 50123);
        CHECK(g_captured.srcVport == 3658 && g_captured.dstVport == 3658 && g_captured.data == "out");
        // Loopback stays loopback: the remote side never sees it.
        const auto self = PsAddr(MakeEndpoint("127.0.0.1", 3658), 3658);
        CHECK(hle::net::sendto(s, "loop", 4, 0, self.data(), 16) == 4);
        CHECK(g_captured.calls == 1);
        fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0, from, &fromLen) == 4 && std::memcmp(buf, "loop", 4) == 0);
        hle::p2pnet::SetRemoteSend(nullptr);
        hle::net::sceNetSocketClose(s);
    }

    WSACleanup();
    std::printf("p2ptest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
