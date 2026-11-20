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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
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
    c.lingerMs = 800;
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

// --- An LBPPC player, as its signaling handler and transport run (LBPPC cb4cbe7:
// gpu/bbnet/vendor/shadps4/core/libraries/np/signaling_handler.cpp, np_signaling/np_signaling_state.h,
// core/net/p2p_codec.cpp, core/net/p2p_transport.cpp; GPL-2.0-or-later). Its packets are built from
// these packed structs the way LBPPC builds them (memcpy of a little-endian struct), not from our
// net/p2p_lbppc.h, so a misread offset there fails here. Timings scaled down as PlayerConfig's.
#pragma pack(push, 1)
struct LbEcho {
    uint8_t magic[4] = {'S', 'H', 'A', 'D'};
    uint8_t type = 0;
    uint32_t conn_id = 0;
    uint64_t ts = 0;
};
static_assert(sizeof(LbEcho) == 17);
struct LbHandshake {
    uint8_t magic[4] = {'S', 'H', 'A', 'D'};
    uint8_t type = 0x07;
    uint8_t kind = 0;
    uint8_t online_id_from[16]{};
    uint8_t online_id_to[16]{};
    uint32_t from_conn_id = 0;
    uint32_t to_conn_id = 0;
    uint32_t mapped_addr = 0;
    uint16_t mapped_port = 0;
    uint16_t reserved = 0;
    uint64_t nonce = 0;
};
static_assert(sizeof(LbHandshake) == 0x3e);
struct LbControl {
    uint8_t magic[4] = {'S', 'H', 'A', 'D'};
    uint8_t type = 0x10;
    uint8_t kind = 0;
    uint8_t online_id_from[16]{};
    uint8_t online_id_to[16]{};
    uint32_t from_conn_id = 0;
    uint32_t mapped_addr = 0;
    uint16_t mapped_port = 0;
    uint16_t reason = 0;
};
static_assert(sizeof(LbControl) == 0x32);
#pragma pack(pop)

struct LbppcMock {
    enum class St { Inactive, SendingOffer, WaitOffer, ConnCheck, SendingAccept, WaitAccept, Established };
    struct Transport {
        std::string id;
        Endpoint addr;
        St state = St::Inactive;
        bool local = false, peerActivated = false, relayedSet = false, deadFired = false;
        uint32_t connId = 0;
        int64_t handshakeStart = 0, lastSend = 0, lastPeerRx = 0, lastPing = 0;
        int pongs = 0;
    };
    std::string self;
    Endpoint server, ext;
    std::vector<std::string> friends;
    SOCKET sock = INVALID_SOCKET;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::map<std::string, Transport> transports;
    std::set<uint32_t> relayedIps;  // LBPPC's per-IP relay (p2p_transport.cpp UseRelayFor)
    uint32_t nextConn = 1;
    // What arrived: game datagrams (vports, payload, the raw packet), and control packets by kind.
    struct Got {
        uint16_t sv, dv;
        std::string data;
        std::vector<uint8_t> raw;
        Endpoint from;
    };
    std::vector<Got> got;
    std::vector<LbControl> controls;
    int closes = 0, refused = 0, relayedIn = 0;
    int64_t lastStun = 0;

    static int64_t NowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static void SetId(uint8_t* dst, const std::string& id) { std::memcpy(dst, id.data(), std::min<size_t>(id.size(), 16)); }
    static std::string Id(const uint8_t* p) { return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 16)); }

    bool Start(const std::string& name, const Endpoint& srv, std::vector<std::string> fr) {
        self = name;
        server = srv;
        friends = std::move(fr);
        sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        DWORD timeout = 10;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl(sock, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
        thread = std::thread([this] { Run(); });
        return true;
    }
    void Stop() {
        stop = true;
        if (thread.joinable()) thread.join();
        closesocket(sock);
    }
    void Raw(const Endpoint& to, const std::vector<uint8_t>& b) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = to.ip;
        a.sin_port = htons(to.port);
        ::sendto(sock, reinterpret_cast<const char*>(b.data()), int(b.size()), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    // The transport's send: through the relay to an IP marked relayed, else directly.
    void SendTo(const Endpoint& to, const std::vector<uint8_t>& packet) {
        if (relayedIps.contains(to.ip)) {
            std::vector<uint8_t> r{'S', 'M', 'R', 'L', 2};
            const auto* ip = reinterpret_cast<const uint8_t*>(&to.ip);
            r.insert(r.end(), ip, ip + 4);
            r.push_back(uint8_t(to.port >> 8));
            r.push_back(uint8_t(to.port));
            r.insert(r.end(), packet.begin(), packet.end());
            Raw(server, r);
        } else {
            Raw(to, packet);
        }
    }
    static std::vector<uint8_t> Frame(uint16_t sv, uint16_t dv, const void* body, size_t n) {
        std::vector<uint8_t> b{0xFF, 0x83, uint8_t(sv >> 8), uint8_t(sv), uint8_t(dv >> 8), uint8_t(dv)};
        const auto* p = static_cast<const uint8_t*>(body);
        b.insert(b.end(), p, p + n);
        return b;
    }
    template <class T>
    void Signal(const Endpoint& to, const T& pkt) { SendTo(to, Frame(0xFFFF, 0xFFFF, &pkt, sizeof(pkt))); }
    LbControl MakeControl(const Transport& t, uint8_t kind) {
        LbControl c;
        c.kind = kind;
        c.from_conn_id = t.connId;
        SetId(c.online_id_from, self);
        SetId(c.online_id_to, t.id);
        c.mapped_addr = ext.ip;
        return c;
    }
    LbHandshake MakeHandshake(const Transport& t, uint8_t kind) {
        LbHandshake h;
        h.kind = kind;
        h.from_conn_id = t.connId;
        SetId(h.online_id_from, self);
        SetId(h.online_id_to, t.id);
        h.mapped_addr = ext.ip;
        return h;
    }
    void SendHandshake(Transport& t, const LbHandshake& h) {
        t.lastSend = NowUs();
        if (!t.handshakeStart) t.handshakeStart = t.lastSend;
        Signal(t.addr, h);
    }
    // StartTransportHandshakeInitiator.
    void Initiate(Transport& t) {
        uint8_t kind = 1;
        switch (t.state) {
        case St::SendingAccept: kind = 2; break;
        case St::WaitOffer: t.state = St::SendingOffer; kind = 1; break;
        case St::ConnCheck: kind = 3; break;
        case St::SendingOffer:
        case St::WaitAccept: kind = 1; break;
        case St::Established: return;
        default: t.state = St::SendingOffer; kind = 1; break;
        }
        LbHandshake h = MakeHandshake(t, kind);
        if (kind == 3) h.nonce = uint64_t(NowUs());
        SendHandshake(t, h);
    }
    void Establish(Transport& t) {
        if (t.state == St::Established) return;
        t.state = St::Established;
        Signal(t.addr, MakeControl(t, 4));
    }
    // The game activates a connection to `id`, whom the server reports at `at`.
    void Activate(const std::string& id, const Endpoint& at) {
        std::lock_guard lk(m);
        Transport& t = transports[id];
        t.id = id;
        t.addr = at;
        t.local = true;
        t.connId = nextConn++;
        Signal(t.addr, MakeControl(t, 1));
        Initiate(t);
    }
    void Close(const std::string& id) {
        std::lock_guard lk(m);
        auto it = transports.find(id);
        if (it == transports.end()) return;
        LbControl c = MakeControl(it->second, 3);
        c.reason = 0;
        Signal(it->second.addr, c);
        transports.erase(it);
    }
    bool Established(const std::string& id) {
        std::lock_guard lk(m);
        auto it = transports.find(id);
        return it != transports.end() && it->second.state == St::Established;
    }
    bool SendData(const std::string& id, const std::string& data) {
        std::lock_guard lk(m);
        auto it = transports.find(id);
        if (it == transports.end() || it->second.state != St::Established) return false;
        SendTo(it->second.addr, Frame(3658, 3658, data.data(), data.size()));
        return true;
    }
    size_t Count() {
        std::lock_guard lk(m);
        return got.size();
    }
    void HandleControl(const LbControl& c, const Endpoint& from) {
        const std::string fromId = Id(c.online_id_from);
        if (Id(c.online_id_to) != self) return;
        controls.push_back(c);
        auto it = transports.find(fromId);
        if (c.kind == 1) {  // ActivationRequest: the join check (friends, or one we asked for)
            const bool listed = std::find(friends.begin(), friends.end(), fromId) != friends.end();
            const bool ours = it != transports.end() && it->second.local;
            if (!listed && !ours) {
                ++refused;
                return;
            }
            Transport& t = transports[fromId];
            if (!t.connId) t.connId = nextConn++;
            t.id = fromId;
            t.peerActivated = true;
            t.addr = from;
            if (t.state == St::Inactive) t.state = St::WaitOffer;
            Signal(t.addr, MakeControl(t, 2));
        } else if (it == transports.end()) {
            return;
        } else if (c.kind == 2) {  // ActivationAck: its mapped address, our port
            it->second.peerActivated = true;
            if (c.mapped_addr) it->second.addr.ip = c.mapped_addr;
        } else if (c.kind == 3) {
            ++closes;
            transports.erase(it);
        }
    }
    void HandleHandshake(const LbHandshake& h, const Endpoint& from) {
        auto it = transports.find(Id(h.online_id_from));
        if (it == transports.end()) return;
        Transport& t = it->second;
        t.lastPeerRx = NowUs();
        if (h.kind == 1) {
            t.addr = from;
            t.peerActivated = true;
            if (t.state != St::Established) t.state = St::SendingAccept;
            SendHandshake(t, MakeHandshake(t, 2));
        } else if (h.kind == 2) {
            t.peerActivated = true;
            t.addr = from;
            if (t.state == St::SendingOffer || t.state == St::WaitAccept || t.state == St::SendingAccept ||
                t.state == St::WaitOffer) {
                t.state = St::ConnCheck;
                LbHandshake r = MakeHandshake(t, 3);
                r.nonce = uint64_t(NowUs());
                SendHandshake(t, r);
            }
        } else if (h.kind == 3) {
            LbHandshake r = MakeHandshake(t, 4);
            r.nonce = h.nonce;
            SendHandshake(t, r);
            if (t.state == St::SendingAccept || t.state == St::WaitOffer) t.state = St::ConnCheck;
        } else if (h.kind == 4) {
            Establish(t);
        }
    }
    void HandlePacket(const uint8_t* d, int n, Endpoint from) {
        static const uint8_t stun[6] = {0xFF, 0x83, 0xFF, 0xFF, 0xFF, 0xFF};
        if (from == server) {
            if (n == 12 && std::memcmp(d, stun, 6) == 0) {
                std::memcpy(&ext.ip, d + 6, 4);
                ext.port = uint16_t(d[10] << 8 | d[11]);
                return;
            }
            if (n > 11 && std::memcmp(d, "SMRL", 4) == 0 && d[4] == 3) {
                std::memcpy(&from.ip, d + 5, 4);
                from.port = uint16_t(d[9] << 8 | d[10]);
                relayedIps.insert(from.ip);  // answer the way it came
                ++relayedIn;
                d += 11;
                n -= 11;
            } else {
                return;
            }
        }
        if (n < 6 || d[0] != 0xFF || d[1] != 0x83) return;  // their codec: anything else is dropped
        const uint16_t sv = uint16_t(d[2] << 8 | d[3]), dv = uint16_t(d[4] << 8 | d[5]);
        const uint8_t* b = d + 6;
        const size_t len = size_t(n) - 6;
        if (sv == 0xFFFF && dv == 0xFFFF) {
            if (len < 5 || std::memcmp(b, "SHAD", 4) != 0) return;
            if (len == sizeof(LbControl) && b[4] == 0x10) {
                LbControl c;
                std::memcpy(&c, b, sizeof(c));
                HandleControl(c, from);
            } else if (len == sizeof(LbEcho) && b[4] == 0x05) {
                LbEcho e;
                std::memcpy(&e, b, sizeof(e));
                LbEcho pong;
                pong.type = 0x06;
                pong.conn_id = e.conn_id;
                pong.ts = e.ts;
                Signal(from, pong);
            } else if (len == sizeof(LbEcho) && b[4] == 0x06) {
                for (auto& [id, t] : transports)
                    if (t.connId == reinterpret_cast<const LbEcho*>(b)->conn_id) ++t.pongs;
            } else if (len == sizeof(LbHandshake) && b[4] == 0x07) {
                LbHandshake h;
                std::memcpy(&h, b, sizeof(h));
                HandleHandshake(h, from);
            }
            return;
        }
        got.push_back({sv, dv, std::string(reinterpret_cast<const char*>(b), len), std::vector<uint8_t>(d, d + n), from});
    }
    // StepPendingConnections and the echo pings, with PlayerConfig's timings (100 ms, 300 ms, 2 s).
    void Step() {
        const int64_t now = NowUs();
        if (now - lastStun > 200'000) {
            lastStun = now;
            std::vector<uint8_t> b{0xFF, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
            uint8_t id[16]{};
            SetId(id, self);
            b.insert(b.end(), id, id + 16);
            const uint32_t lan = MakeEndpoint("127.0.0.1", 0).ip;
            const auto* ip = reinterpret_cast<const uint8_t*>(&lan);
            b.insert(b.end(), ip, ip + 4);
            Raw(server, b);
        }
        for (auto it = transports.begin(); it != transports.end();) {
            Transport& t = it->second;
            if (t.state == St::Established) {
                if (now - t.lastPing > 100'000) {
                    t.lastPing = now;
                    LbEcho e;
                    e.type = 0x05;
                    e.conn_id = t.connId;
                    e.ts = uint64_t(now);
                    Signal(t.addr, e);
                }
                ++it;
                continue;
            }
            if (t.state == St::Inactive) {
                ++it;
                continue;
            }
            if (t.handshakeStart && now - t.handshakeStart > 2'000'000) {
                it = transports.erase(it);  // timeout: DEAD to its game
                continue;
            }
            if (!t.relayedSet && t.handshakeStart && t.lastPeerRx < t.handshakeStart && now - t.handshakeStart > 300'000) {
                t.relayedSet = true;
                relayedIps.insert(t.addr.ip);
                if (t.local) Signal(t.addr, MakeControl(t, 1));  // the peer may have missed it
            }
            if (!t.lastSend || now - t.lastSend >= 100'000) Initiate(t);
            ++it;
        }
    }
    void Run() {
        uint8_t buf[2048];
        while (!stop) {
            sockaddr_in from{};
            int fl = sizeof(from);
            const int n = ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
            std::lock_guard lk(m);
            if (n > 0) HandlePacket(buf, n, Endpoint{from.sin_addr.s_addr, ntohs(from.sin_port)});
            Step();
        }
    }
};

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

        // --- 2b. the same maths: Erin runs the exact maths (core/exact_math.h); Frank, her friend,
        // doesn't (an older version) and refuses her; Gina, who runs them too, connects -------------
        {
            Config ec = PlayerConfig("Erin_5", mock, {});
            ec.exactMath = true;
            Config fc = PlayerConfig("Frank_6", mock, {"Erin_5"});
            Config gc = PlayerConfig("Gina_7", mock, {"Erin_5"});
            gc.exactMath = true;
            Link e, f, g;
            CHECK(e.Start(ec));
            CHECK(f.Start(fc));
            CHECK(g.Start(gc));
            CHECK(WaitFor([&] { return e.External().Valid() && f.External().Valid() && g.External().Valid(); }));
            e.Activate("Frank_6");
            e.Activate("Gina_7");
            std::vector<Event> evE;
            CHECK(WaitFor([&] { return HasEvent(evE, e, Event::Established, "Gina_7"); }));
            CHECK(WaitFor([&] { return HasEvent(evE, e, Event::Dead, "Frank_6"); }, 5000));
            CHECK(f.Info("Erin_5").state == State::None);
            CHECK(f.GetStats().refused > 0);
            CHECK(g.Info("Erin_5").state == State::Active);
            std::printf("  the same maths: an exact-maths player and an older one refused (%llu), two exact ones connect\n",
                        static_cast<unsigned long long>(f.GetStats().refused));
            e.Stop();
            f.Stop();
            g.Stop();
        }

        // --- 4. leaving ------------------------------------------------------------------------
        // Both sides close at once for the game (DEAD on Bob's), but the games' last datagrams still
        // pass both ways for lingerMs (a PS4's P2P socket isn't tied to the connection); then it's gone.
        const Endpoint toB = a.Info("Bob_2").gameAddr, toA = b.Info("Alice_1").gameAddr;
        a.Terminate("Bob_2");
        CHECK(a.Info("Bob_2").state == State::Closing);
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::Dead, "Alice_1"); }));
        CHECK(b.Info("Alice_1").state == State::Closing);
        {
            const size_t nA = atA.Count(), nB = atB.Count();
            const std::string last = "last words";
            CHECK(a.SendData(toB, 3658, 3658, last.data(), last.size()));
            CHECK(b.SendData(toA, 3658, 3658, last.data(), last.size()));
            CHECK(WaitFor([&] { return atB.Count() > nB && atA.Count() > nA; }, 2000));
        }
        CHECK(WaitFor([&] { return a.Info("Bob_2").state == State::None && b.Info("Alice_1").state == State::None; },
                      3000));
        {
            const std::string late = "too late";
            CHECK(!a.SendData(toB, 3658, 3658, late.data(), late.size()));
        }
        std::printf("  leaving: closing on both sides, the last datagrams passed both ways, then gone\n");
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

    // --- 6. one network: both behind one public address; diagnostics on ------------------------
    {
        UdpMock mock;
        mock.fakeAddresses = true;  // both "behind" 127.0.0.2, which reaches nobody: the LAN address must
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        Config ca = PlayerConfig("Alice_1", mock, {}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
        ca.diag = cb.diag = true;
        CHECK(a.Start(ca));
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        a.Activate("Bob_2");
        std::vector<Event> evA, evB;
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
        CHECK(WaitFor([&] { return !a.Info("Bob_2").relayed && !b.Info("Alice_1").relayed; }));
        // DATA2: the payload arrives without the sequence number and send time.
        CHECK(a.SendData(mock.Ext("Bob_2"), 3658, 3658, "seq one", 7));
        CHECK(a.SendData(mock.Ext("Bob_2"), 3658, 3658, "seq two", 7));
        CHECK(WaitFor([&] { return atB.Count() == 2; }));
        CHECK(atB.Count() == 2 && std::string(atB.data[1].begin(), atB.data[1].end()) == "seq two");
        CHECK(atB.srcVport == 3658 && atB.dstVport == 3658 && atB.from == mock.Ext("Alice_1"));
        a.DumpRecent("the end of test 6");
        a.Stop();
        b.Stop();
        mock.Stop();
    }

    // --- 7. the relay forced (LBP3_P2P_PATH=relay) although direct works ------------------------
    {
        UdpMock mock;
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        Config ca = PlayerConfig("Alice_1", mock, {}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
        ca.path = cb.path = Config::Path::Relay;
        CHECK(a.Start(ca));
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        a.Activate("Bob_2");
        std::vector<Event> evA, evB;
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
        CHECK(a.Info("Bob_2").relayed);
        const uint64_t relayedBefore = a.GetStats().sentRelayed;
        CHECK(a.SendData(b.External(), 3658, 3658, "relayed", 7));
        CHECK(WaitFor([&] { return atB.Count() == 1; }));
        CHECK(a.GetStats().sentRelayed > relayedBefore);
        {
            std::lock_guard lk(mock.m);
            CHECK(mock.relayed > 0);
        }
        a.Stop();
        b.Stop();
        mock.Stop();
    }

    // --- 9..12. gap-free delivery (DATA3) with lost datagrams -------------------------------------
    // Sends `count` numbered datagrams A -> B and returns what B's game got, in arrival order.
    struct Faults {
        int lossEvery = 0, lossBurst = 1, lateEvery = 0, dupEvery = 0;
    };
    std::vector<std::string> gotA;  // what A's game got from B in the last run
    auto run = [&](const char* what, bool relay, bool data3B, Faults f, int count, Link::Stats* statsA,
                   Link::Stats* statsB) {
        UdpMock mock;
        mock.fakeAddresses = relay;
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        Config ca = PlayerConfig("Alice_1", mock, {}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
        if (relay) ca.reportLan = cb.reportLan = false;
        cb.data3 = data3B;
        for (Config* c : {&ca, &cb}) {
            c->testLossEvery = f.lossEvery;
            c->testLossBurst = f.lossBurst;
            c->testLateEvery = f.lateEvery;
            c->testDupEvery = f.dupEvery;
        }
        CHECK(a.Start(ca));
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        a.Activate("Bob_2");
        std::vector<Event> evA, evB;
        CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
        CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
        // Both directions at once, faults on both sides.
        const Endpoint toB = a.Info("Bob_2").gameAddr, toA = b.Info("Alice_1").gameAddr;
        for (int i = 0; i < count; ++i) {
            const std::string msg = "frame " + std::to_string(i);
            a.SendData(toB, 3658, 3658, msg.data(), msg.size());
            b.SendData(toA, 3658, 3658, msg.data(), msg.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        WaitFor([&] { return atB.Count() >= static_cast<size_t>(count) && atA.Count() >= static_cast<size_t>(count); },
                6000);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // nothing more may come
        std::vector<std::string> got;
        {
            std::lock_guard lk(atB.m);
            for (const auto& d : atB.data) got.emplace_back(d.begin(), d.end());
        }
        gotA.clear();
        {
            std::lock_guard lk(atA.m);
            for (const auto& d : atA.data) gotA.emplace_back(d.begin(), d.end());
        }
        if (statsA) *statsA = a.GetStats();
        if (statsB) *statsB = b.GetStats();
        std::printf("  %s: %zu and %zu of %d arrived\n", what, got.size(), gotA.size(), count);
        a.Stop();
        b.Stop();
        mock.Stop();
        return got;
    };
    auto inOrder = [](const std::vector<std::string>& got, int count) {
        if (got.size() != static_cast<size_t>(count)) return false;
        for (int i = 0; i < count; ++i)
            if (got[i] != "frame " + std::to_string(i)) return false;
        return true;
    };
    {
        Link::Stats sa, sb;
        // 9. Every 5th datagram lost (both ways): each is filled in from the next one's copies.
        auto got = run("every 5th lost", false, true, {5, 1}, 200, &sa, &sb);
        CHECK(inOrder(got, 200) && inOrder(gotA, 200));
        CHECK(sb.testDropped >= 30 && sb.recovered > 0 && sb.skipped == 0 && sa.recovered > 0);
        // 10. Bursts of 4 lost (more than the 2 copies): the rest come back on request (NACK).
        got = run("4 in a row lost every 25", false, true, {25, 4}, 200, &sa, &sb);
        CHECK(inOrder(got, 200) && inOrder(gotA, 200));
        CHECK(sb.nacks > 0 && sa.resent > 0 && sb.skipped == 0 && sa.skipped == 0);
        // 11. A player whose link doesn't have DATA3 (an older version): plain DATA, losses and all.
        got = run("older link, every 10th lost", false, false, {10, 1}, 100, &sa, &sb);
        CHECK(got.size() >= 85 && got.size() < 100 && gotA.size() >= 85 && gotA.size() < 100);
        CHECK(sa.resent == 0 && sb.recovered == 0 && sa.recovered == 0);
        // 12. Through the relay, every 7th lost (the last one too): still all, in order.
        got = run("relayed, every 7th lost", true, true, {7, 1}, 120, &sa, &sb);
        CHECK(inOrder(got, 120) && inOrder(gotA, 120));
        CHECK(sa.sentRelayed > 0);
        // 13. Every 6th held back 50 ms (its successors overtake it): delivered in order anyway.
        got = run("every 6th late", false, true, {0, 1, 6, 0}, 150, &sa, &sb);
        CHECK(inOrder(got, 150) && inOrder(gotA, 150));
        CHECK(sb.testLate > 0 && sb.skipped == 0);
        // 14. Every 4th arriving twice: each delivered once.
        got = run("every 4th doubled", false, true, {0, 1, 0, 4}, 150, &sa, &sb);
        CHECK(inOrder(got, 150) && inOrder(gotA, 150));
        CHECK(sb.testDuplicated > 0);
        // 15. All at once, through the relay: lost, late and doubled.
        got = run("relayed: lost, late and doubled", true, true, {9, 2, 5, 7}, 150, &sa, &sb);
        CHECK(inOrder(got, 150) && inOrder(gotA, 150));
        CHECK(sb.skipped == 0 && sa.skipped == 0);
    }

    // --- 15a..15c. the test delay, jitter and hold (Config::testDelayMs, testJitterMs, testHoldMs) ---
    {
        struct Slow {
            std::vector<std::string> gotB;  // what B's game got from A, in arrival order
            Link::Stats a, b;
            int rttUsA = 0;      // A's RTT to B, once measured
            int firstMs = 0;     // from A's first send to B's first delivery
            int longestGapMs = 0;  // the longest time B's game got nothing while A was sending
            PeerInfo ia, ib;       // the session log's counts: A's for B, B's for A
            int serverRttUsA = 0;
        };
        // A sends `count` numbered datagrams 2 ms apart; B's deliveries are sampled every 2 ms.
        auto slowRun = [&](const char* what, int delayA, int delayB, int jitterB, int holdB, int holdAfterB, int count,
                           int lossEveryB = 0, int lossBurstB = 1) {
            Slow out;
            UdpMock mock;
            CHECK(mock.Start());
            Link a, b;
            Received atA, atB;
            Sink(a, atA);
            Sink(b, atB);
            Config ca = PlayerConfig("Alice_1", mock, {}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
            ca.testDelayMs = delayA;
            cb.testDelayMs = delayB;
            cb.testJitterMs = jitterB;
            cb.testHoldMs = holdB;
            cb.testHoldAfterMs = holdAfterB;
            cb.testLossEvery = lossEveryB;
            cb.testLossBurst = lossBurstB;
            CHECK(a.Start(ca));
            CHECK(b.Start(cb));
            CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
            a.Activate("Bob_2");
            std::vector<Event> evA, evB;
            CHECK(WaitFor([&] { return HasEvent(evA, a, Event::Established, "Bob_2"); }));
            CHECK(WaitFor([&] { return HasEvent(evB, b, Event::PeerActivated, "Alice_1"); }));
            CHECK(WaitFor([&] { return a.Info("Bob_2").rttUs > 0; }, 6000));
            out.rttUsA = a.Info("Bob_2").rttUs;
            const Endpoint toB = a.Info("Bob_2").gameAddr;
            using SteadyClock = std::chrono::steady_clock;
            const auto start = SteadyClock::now();
            auto lastChange = start;
            size_t seen = 0;
            auto sample = [&] {
                const auto now = SteadyClock::now();
                const size_t c = atB.Count();
                if (c != seen) {
                    const int gap = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChange).count());
                    if (seen == 0) out.firstMs = gap;
                    else out.longestGapMs = std::max(out.longestGapMs, gap);
                    seen = c;
                    lastChange = now;
                }
            };
            for (int i = 0; i < count; ++i) {
                const std::string msg = "frame " + std::to_string(i);
                a.SendData(toB, 3658, 3658, msg.data(), msg.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                sample();
            }
            WaitFor([&] { sample(); return atB.Count() >= static_cast<size_t>(count); }, 6000);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));  // nothing more may come
            {
                std::lock_guard lk(atB.m);
                for (const auto& d : atB.data) out.gotB.emplace_back(d.begin(), d.end());
            }
            out.a = a.GetStats();
            out.b = b.GetStats();
            out.ia = a.Info("Bob_2");
            out.ib = b.Info("Alice_1");
            out.serverRttUsA = a.ServerRttUs();
            std::printf("  %s: %zu of %d arrived; rtt %d ms, first after %d ms, longest gap %d ms\n", what, out.gotB.size(),
                        count, out.rttUsA / 1000, out.firstMs, out.longestGapMs);
            a.Stop();
            b.Stop();
            mock.Stop();
            return out;
        };
        // 15a. 60 ms each way: the link's own RTT shows both, and a datagram takes the one-way delay.
        Slow r = slowRun("60 ms each way", 60, 60, 0, 0, 0, 100);
        CHECK(inOrder(r.gotB, 100));
        CHECK(r.rttUsA >= 115'000 && r.rttUsA < 300'000);
        CHECK(r.firstMs >= 55 && r.firstMs < 250);
        CHECK(r.a.testDelayed > 0 && r.b.testDelayed >= 100 && r.b.testHeld == 0);
        // The session log's counts (PeerInfo): what the game got, the RTT's spread, packets and bytes.
        CHECK(r.ib.delivered == 100 && r.ia.delivered == 0);
        CHECK(r.ib.gapsOpened == 0 && r.ib.longestDeliverGapMs < 150);
        // How long the connection has been up, and since the game last got a datagram (-1 = never).
        CHECK(r.ia.msSinceActive >= 0 && r.ib.msSinceActive >= 0 && r.ib.msSinceActive < 60'000);
        CHECK(r.ib.msSinceDelivered >= 0 && r.ib.msSinceDelivered <= r.ib.msSinceActive);
        CHECK(r.ia.msSinceDelivered == -1);
        CHECK(net::p2p::PeerInfo{}.msSinceActive == -1 && net::p2p::PeerInfo{}.msSinceDelivered == -1);
        CHECK(r.ia.pings > 0 && r.ia.pongs > 0 && r.ia.pongs <= r.ia.pings);
        CHECK(r.ia.rttMinUs >= 115'000 && r.ia.rttMaxUs >= r.ia.rttMinUs && r.ia.rttMaxUs < 400'000);
        CHECK(r.ia.txPackets >= 100 && r.ia.txBytes > 100 * 7 && r.ib.rxPackets >= 100 && r.ib.rxBytes > 100 * 7);
        CHECK(r.ib.directHeard >= 100);
        CHECK(r.serverRttUsA > 0 && r.serverRttUsA < 1'000'000);
        // A steady delay is no "extra" delay: each direction stays near its own lowest.
        CHECK(r.ia.maxExtraOutUs < 60'000 && r.ia.maxExtraBackUs < 60'000);
        // 15b. 20 ms and up to 40 ms of jitter, one direction only: still in the order sent, so the
        // gap-free link has nothing to repair.
        r = slowRun("20 ms + 40 ms jitter, one way", 0, 20, 40, 0, 0, 150);
        CHECK(inOrder(r.gotB, 150));
        CHECK(r.a.testDelayed == 0 && r.b.testDelayed >= 150);
        CHECK(r.b.nacks == 0 && r.b.skipped == 0 && r.b.recovered == 0);
        CHECK(r.rttUsA >= 15'000);
        // 15c. One direction held for 600 ms, 100 ms after the first datagram: B's game gets nothing
        // that long, then all of it, in order, with none lost and none asked for again.
        r = slowRun("held 600 ms, one way", 0, 0, 0, 600, 100, 500);
        CHECK(inOrder(r.gotB, 500));
        CHECK(r.b.testHeld >= 200 && r.a.testHeld == 0);
        CHECK(r.longestGapMs >= 450 && r.longestGapMs < 1200);
        CHECK(r.b.skipped == 0);
        // The session log saw it: the game's longest wait, and which direction was slow. A's PINGs
        // reached B late (out), B's answers came back at once; B's own PONGs from A came late (back).
        CHECK(r.ib.longestDeliverGapMs >= 450 && r.ib.longestDeliverGapMs < 1200);
        CHECK(r.ia.maxExtraOutUs >= 250'000 && r.ia.maxExtraBackUs < 150'000);
        CHECK(r.ib.maxExtraBackUs >= 250'000 && r.ib.maxExtraOutUs < 150'000);
        CHECK(r.ib.gapsOpened == 0);
        // 15d. Four in a row lost every 25: the gaps are counted, with how each closed.
        r = slowRun("4 in a row lost every 25, counted", 0, 0, 0, 0, 0, 200, 25, 4);
        CHECK(inOrder(r.gotB, 200));
        CHECK(r.ib.delivered == 200);
        CHECK(r.ib.gapsOpened > 0 && r.ib.gapsAsked > 0 && r.ib.gapsGivenUp == 0);
        CHECK(r.ib.gapsFilled + r.ib.gapsAsked == r.ib.gapsOpened);
        CHECK(r.ib.longestGapMs < 2000);  // on loopback a gap closes within a millisecond
        CHECK(r.ia.gapsOpened == 0);
        // Off: none of it counts anything (the path with the settings off is the one before them).
        Link::Stats sa, sb;
        run("settings off", false, true, {}, 50, &sa, &sb);
        CHECK(sa.testDelayed == 0 && sb.testDelayed == 0 && sa.testHeld == 0 && sb.testHeld == 0);
    }

    // --- 16..18. leaving and joining again: DATA3 counts from 0 again on both sides ---------------
    {
        UdpMock mock;
        CHECK(mock.Start());
        Link a, b;
        Received atA, atB;
        Sink(a, atA);
        Sink(b, atB);
        Config ca = PlayerConfig("Alice_1", mock, {"Bob_2"}), cb = PlayerConfig("Bob_2", mock, {"Alice_1"});
        cb.testNoBye = true;  // Bob's BYEs are "lost"
        CHECK(a.Start(ca));
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return a.External().Valid() && b.External().Valid(); }));
        auto bothUp = [&] {
            return WaitFor([&] {
                return a.Info("Bob_2").state == State::Active && b.Info("Alice_1").state == State::Active;
            });
        };
        // `count` numbered datagrams each way; true if both sides got all, in order, once.
        auto exchange = [&](int count) {
            for (Received* r : {&atA, &atB}) {
                std::lock_guard lk(r->m);
                r->data.clear();
            }
            const Endpoint toB = a.Info("Bob_2").gameAddr, toA = b.Info("Alice_1").gameAddr;
            for (int i = 0; i < count; ++i) {
                const std::string msg = "frame " + std::to_string(i);
                a.SendData(toB, 3658, 3658, msg.data(), msg.size());
                b.SendData(toA, 3658, 3658, msg.data(), msg.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            WaitFor([&] { return atA.Count() >= static_cast<size_t>(count) && atB.Count() >= static_cast<size_t>(count); });
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            for (Received* r : {&atA, &atB}) {
                std::lock_guard lk(r->m);
                if (r->data.size() != static_cast<size_t>(count)) return false;
                for (int i = 0; i < count; ++i)
                    if (std::string(r->data[i].begin(), r->data[i].end()) != "frame " + std::to_string(i)) return false;
            }
            return true;
        };
        a.Activate("Bob_2");
        CHECK(bothUp());
        CHECK(exchange(40));
        // 16. Alice leaves (her BYE arrives) and connects again while both sides are still closing
        // (Activate and the new HELLO start afresh: new counts both ways).
        a.Terminate("Bob_2");
        CHECK(WaitFor([&] { return b.Info("Alice_1").state == State::Closing; }));
        a.Activate("Bob_2");
        CHECK(bothUp());
        CHECK(exchange(40));
        // 17. Bob leaves but his BYE is lost: Alice still has him, with her old counts. He connects again.
        b.Terminate("Alice_1");
        CHECK(a.Info("Bob_2").state == State::Active);
        b.Activate("Alice_1");
        CHECK(bothUp());
        CHECK(exchange(40));
        // 18. Bob's link starts again (no BYE either), on another port.
        b.Stop();
        CHECK(b.Start(cb));
        CHECK(WaitFor([&] { return b.External().Valid(); }));
        b.Activate("Alice_1");
        CHECK(bothUp());
        CHECK(WaitFor([&] { return a.Info("Bob_2").gameAddr == b.External(); }));
        CHECK(exchange(40));
        a.Stop();
        b.Stop();
        mock.Stop();
    }

    // --- 9. the LBPPC co-op mode (Config::lbppc, net/p2p_lbppc.h): our link with LBPPC players -----
    {
        UdpMock mock;
        CHECK(mock.Start());
        auto modeCfg = [&](const std::string& name, std::vector<std::string> allowed) {
            Config c = PlayerConfig(name, mock, std::move(allowed));
            c.lbppc = true;
            c.exactMath = true;
            return c;
        };
        // 9a. we join an LBPPC host.
        Link h;
        Received atH;
        Sink(h, atH);
        CHECK(h.Start(modeCfg("Hana_8", {"Lbp_9"})));
        LbppcMock lb;
        CHECK(lb.Start("Lbp_9", mock.self, {"Hana_8"}));
        CHECK(WaitFor([&] { return h.External().Valid() && mock.Ext("Lbp_9").Valid(); }));
        h.Activate("Lbp_9");
        std::vector<Event> evH;
        CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Established, "Lbp_9"); }));
        CHECK(WaitFor([&] { return lb.Established("Hana_8"); }));
        PeerInfo ih = h.Info("Lbp_9");
        CHECK(ih.state == State::Active && ih.lbppc && ih.locallyActivated && !ih.relayed);
        {
            std::lock_guard lk(lb.m);
            bool marked = false;  // ours, so a host of ours waits for our HELLO; LBPPC never reads it
            for (const auto& c : lb.controls) marked |= c.kind == 1 && c.reason == 0x334C;
            CHECK(marked);
        }
        // Game datagrams both ways, in the PS4's own frame.
        CHECK(h.SendData(ih.gameAddr, 3658, 3658, "to-lbppc", 8));
        CHECK(WaitFor([&] { return lb.Count() >= 1; }));
        {
            std::lock_guard lk(lb.m);
            CHECK(!lb.got.empty() && lb.got[0].sv == 3658 && lb.got[0].dv == 3658 && lb.got[0].data == "to-lbppc");
            const uint8_t head[6] = {0xFF, 0x83, 0x0E, 0x4A, 0x0E, 0x4A};
            CHECK(!lb.got.empty() && lb.got[0].raw.size() == 14 && std::memcmp(lb.got[0].raw.data(), head, 6) == 0);
        }
        CHECK(lb.SendData("Hana_8", "from-lbppc"));
        CHECK(WaitFor([&] { return atH.Count() >= 1; }));
        {
            std::lock_guard lk(atH.m);
            CHECK(!atH.data.empty() && std::string(atH.data[0].begin(), atH.data[0].end()) == "from-lbppc");
            CHECK(atH.srcVport == 3658 && atH.dstVport == 3658 && atH.from == ih.gameAddr);
        }
        // Round trips: our echo pings answered, and theirs.
        CHECK(WaitFor([&] { return h.Info("Lbp_9").rttUs > 0; }));
        CHECK(WaitFor([&] {
            std::lock_guard lk(lb.m);
            return lb.transports.contains("Hana_8") && lb.transports["Hana_8"].pongs > 0;
        }));
        // 9b. it leaves (Close): DEAD for our game.
        lb.Close("Hana_8");
        CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Dead, "Lbp_9"); }));
        // 9c. it joins us (its game activates, ours hears PEER_ACTIVATED); our game activates too.
        lb.Activate("Hana_8", mock.Ext("Hana_8"));
        CHECK(WaitFor([&] { return HasEvent(evH, h, Event::PeerActivated, "Lbp_9"); }));
        CHECK(WaitFor([&] { return lb.Established("Hana_8"); }));
        ih = h.Info("Lbp_9");
        CHECK(ih.state == State::Active && ih.lbppc && ih.peerActivated && !ih.locallyActivated);
        CHECK(lb.SendData("Hana_8", "again"));
        CHECK(WaitFor([&] { return atH.Count() >= 2; }));
        CHECK(h.SendData(ih.gameAddr, 3658, 3658, "back", 4));
        CHECK(WaitFor([&] { return lb.Count() >= 2; }));
        h.Activate("Lbp_9");
        CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Mutual, "Lbp_9"); }));
        // We leave: it gets Close.
        h.Terminate("Lbp_9");
        CHECK(WaitFor([&] {
            std::lock_guard lk(lb.m);
            return lb.closes >= 1;
        }));
        // 9d. an LBPPC player who isn't a friend is refused and never reaches the game.
        {
            LbppcMock stranger;
            CHECK(stranger.Start("Lbp_X", mock.self, {"Hana_8"}));
            CHECK(WaitFor([&] { return mock.Ext("Lbp_X").Valid(); }));
            const uint64_t refusedBefore = h.GetStats().refused;
            stranger.Activate("Hana_8", mock.Ext("Hana_8"));
            CHECK(WaitFor([&] { return h.GetStats().refused > refusedBefore; }));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            CHECK(!stranger.Established("Hana_8"));
            CHECK(h.Info("Lbp_X").state == State::None);
            bool heard = false;
            for (const auto& e : h.TakeEvents()) heard |= e.peer == "Lbp_X";
            CHECK(!heard);
            stranger.Stop();
        }
        // 9e. two of ours in the mode keep our own protocol (gap-free delivery and all).
        {
            Link i;
            CHECK(i.Start(modeCfg("Ivo_10", {"Hana_8"})));
            CHECK(WaitFor([&] { return i.External().Valid(); }));
            h.Activate("Ivo_10");
            CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Established, "Ivo_10"); }));
            CHECK(h.Info("Ivo_10").state == State::Active && !h.Info("Ivo_10").lbppc);
            CHECK(i.Info("Hana_8").state == State::Active && !i.Info("Hana_8").lbppc);
            // 9f. one of ours without the mode refuses one with it (their worlds would drift apart).
            Config jc = PlayerConfig("Jo_11", mock, {"Hana_8"});
            jc.exactMath = true;
            Link j;
            CHECK(j.Start(jc));
            CHECK(WaitFor([&] { return j.External().Valid(); }));
            h.Activate("Jo_11");
            CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Dead, "Jo_11"); }, 5000));
            CHECK(j.Info("Hana_8").state == State::None && j.GetStats().refused > 0);
            // 9g. without the mode, LBPPC's packets are nobody's: an LBPPC player never connects.
            lb.Activate("Jo_11", mock.Ext("Jo_11"));
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            CHECK(!lb.Established("Jo_11"));
            CHECK(j.Info("Lbp_9").state == State::None);
            i.Stop();
            j.Stop();
        }
        lb.Stop();
        h.Stop();
        mock.Stop();
        std::printf("  the LBPPC co-op mode: we join an LBPPC host and it joins us, a stranger refused, two of ours keep "
                    "our protocol, a mode mismatch refused\n");
    }
    // 9h. the same through the relay: the server's addresses for both reach nobody (two NATs).
    {
        UdpMock mock;
        mock.fakeAddresses = true;
        CHECK(mock.Start());
        Config hc = PlayerConfig("Hana_8", mock, {"Lbp_9"});
        hc.lbppc = true;
        hc.exactMath = true;
        Link h;
        Received atH;
        Sink(h, atH);
        CHECK(h.Start(hc));
        LbppcMock lb;
        CHECK(lb.Start("Lbp_9", mock.self, {"Hana_8"}));
        CHECK(WaitFor([&] { return h.External().Valid() && mock.Ext("Lbp_9").Valid(); }));
        h.Activate("Lbp_9");
        std::vector<Event> evH;
        CHECK(WaitFor([&] { return HasEvent(evH, h, Event::Established, "Lbp_9"); }));
        CHECK(WaitFor([&] { return lb.Established("Hana_8"); }));
        const PeerInfo ih = h.Info("Lbp_9");
        CHECK(ih.state == State::Active && ih.lbppc && ih.relayed);
        CHECK(h.SendData(ih.gameAddr, 3658, 3658, "relayed", 7));
        CHECK(WaitFor([&] { return lb.Count() >= 1; }));
        CHECK(lb.SendData("Hana_8", "relayed-back"));
        CHECK(WaitFor([&] { return atH.Count() >= 1; }));
        CHECK(h.GetStats().sentRelayed > 0);
        {
            std::lock_guard lk(mock.m);
            CHECK(mock.relayed > 0);
        }
        lb.Stop();
        h.Stop();
        mock.Stop();
        std::printf("  the LBPPC co-op mode through the relay: connected and datagrams both ways\n");
    }

    // --- 8. MSG_PEEK leaves the datagram queued -----------------------------------------------
    {
        const int s = hle::net::socket(2, 6, 0);
        const auto bindAddr = PsAddr(Endpoint{0, 3659}, 3659);
        CHECK(hle::net::bind(s, bindAddr.data(), 16) == 0);
        const Endpoint peer = MakeEndpoint("203.0.113.8", 50124);
        CHECK(hle::p2pnet::DeliverRemote(peer.ip, peer.port, 3659, 3659, reinterpret_cast<const uint8_t*>("peek"), 4));
        uint8_t buf[16], from[16];
        uint32_t fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0x2, from, &fromLen) == 4);
        fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0, from, &fromLen) == 4 && std::memcmp(buf, "peek", 4) == 0);
        fromLen = sizeof(from);
        CHECK(hle::net::recvfrom(s, buf, sizeof(buf), 0, from, &fromLen) < 0);
        hle::net::sceNetSocketClose(s);
    }

    WSACleanup();
    std::printf("p2ptest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
