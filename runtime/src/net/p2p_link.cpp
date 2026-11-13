// See p2p_link.h. Ours.
#include "net/p2p_link.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "core/log_stamp.h"

namespace net::p2p {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint8_t kStunHeader[6] = {0xFF, 0x83, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t kRelayMagic[4] = {'S', 'M', 'R', 'L'};
constexpr uint8_t kMagic[4] = {'L', '3', 'P', '1'};
enum : uint8_t { kHello = 1, kAck = 2, kData = 3, kPing = 4, kPong = 5, kBye = 6, kData2 = 7 };
constexpr size_t kIdLen = 16;
constexpr size_t kHelloLen = 5 + kIdLen * 2 + 6 + 6;
constexpr auto kDirectFresh = std::chrono::seconds(5);  // a direct packet this recent: send directly

int64_t Ms(Clock::duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); }

void PutId(std::vector<uint8_t>& b, const std::string& id) {
    uint8_t buf[kIdLen] = {};
    std::memcpy(buf, id.data(), std::min(id.size(), kIdLen));
    b.insert(b.end(), buf, buf + kIdLen);
}
std::string GetId(const uint8_t* p) { return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), kIdLen)); }
void Put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void PutEndpoint(std::vector<uint8_t>& b, const Endpoint& e) {
    const auto* ip = reinterpret_cast<const uint8_t*>(&e.ip);
    b.insert(b.end(), ip, ip + 4);
    Put16(b, e.port);
}
Endpoint GetEndpoint(const uint8_t* p) {
    Endpoint e;
    std::memcpy(&e.ip, p, 4);
    e.port = Get16(p + 4);
    return e;
}
// A stable 32-bit tag for an online ID (FNV-1a), carried in DATA.
uint32_t NameTag(const std::string& name) {
    uint32_t h = 2166136261u;
    for (const char ch : name) h = (h ^ static_cast<uint8_t>(ch)) * 16777619u;
    return h;
}
void Put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t Get32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void Put64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t Get64(const uint8_t* p) { return uint64_t(Get32(p)) << 32 | Get32(p + 4); }

// QueryPerformanceCounter: one clock for every process on the PC, so two instances' send and
// receive times compare directly.
int64_t Qpc() {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}
int64_t QpcFreq() {
    static const int64_t f = [] {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return v.QuadPart;
    }();
    return f;
}
int64_t QpcUs(int64_t ticks) { return ticks * 1'000'000 / QpcFreq(); }

std::vector<uint8_t> Header(uint8_t type) {
    std::vector<uint8_t> b(kMagic, kMagic + 4);
    b.push_back(type);
    return b;
}

}  // namespace

std::string ToString(const Endpoint& e) {
    const auto* b = reinterpret_cast<const uint8_t*>(&e.ip);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], e.port);
    return buf;
}

Endpoint MakeEndpoint(const char* dottedIp, uint16_t port) {
    Endpoint e;
    in_addr a{};
    if (inet_pton(AF_INET, dottedIp, &a) == 1) e.ip = a.s_addr;
    e.port = port;
    return e;
}

struct Link::Impl {
    struct Peer {
        std::string name;
        State state = State::None;
        bool local = false, remote = false, mutualFired = false;
        Endpoint gameAddr;
        std::vector<Endpoint> candidates;  // where to try directly
        Endpoint direct;                   // where its last direct packet came from
        Clock::time_point lastDirect{};
        Endpoint relayTo;                  // its address as the server sees it (the relay's destination)
        Clock::time_point started{}, lastRx{}, lastHello{}, lastPing{}, lastResolve{};
        bool resolving = false;
        int rttUs = 0;
        Endpoint ext, lan;   // as the peer reports them (HELLO / ACK)
        Endpoint preferred;  // the direct address to keep while it answers (Config::path)
        // Diagnostics (DATA2).
        uint32_t txSeq = 0, rxNext = 0;
        bool rxStarted = false;
        int64_t peerLastTx = 0;  // the latest send time seen from the peer (QPC)
    };
    struct Rec {
        int64_t qpc;
        uint32_t seq;
        uint32_t len;
        int32_t delayUs;  // receive: one-way delay; -1 for sends
        char dir, path;   // 't' / 'r'; 'L' LAN, 'P' public or other direct, 'R' relay
    };
    struct Diag {
        uint64_t rx = 0, gaps = 0, late = 0, pauses = 0, slow = 0;
        int64_t delaySum = 0, delayMin = INT64_MAX, delayMax = 0;
        size_t maxTx = 0, maxRx = 0;
        int logged = 0;
    };

    Config cfg;
    SOCKET sock = INVALID_SOCKET;
    std::thread thread;
    std::atomic<bool> stop{false};
    mutable std::mutex m;
    Endpoint external;
    uint16_t localPort = 0;
    uint32_t lanIp = 0;
    std::map<std::string, Peer> peers;
    std::deque<Event> events;
    DataSink sink;
    Stats stats;
    Clock::time_point lastStun{};
    bool stunAnswered = false;
    std::set<std::string> refusedLogged;
    Diag diag;
    std::deque<Rec> ring;  // Config::diag: game datagrams of the last 20 s
    int pathLogs = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    void SendRaw(const Endpoint& to, const std::vector<uint8_t>& b) {
        if (!to.Valid() || sock == INVALID_SOCKET) return;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = to.ip;
        a.sin_port = htons(to.port);
        sendto(sock, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0,
               reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    }
    void SendRelayed(const Endpoint& to, const std::vector<uint8_t>& b) {
        if (!cfg.server.Valid() || !to.Valid()) return;
        std::vector<uint8_t> r(kRelayMagic, kRelayMagic + 4);
        r.push_back(0x02);
        PutEndpoint(r, to);
        r.insert(r.end(), b.begin(), b.end());
        SendRaw(cfg.server, r);
    }
    bool DirectFresh(const Peer& p, Clock::time_point now) const {
        return p.direct.Valid() && now - p.lastDirect < kDirectFresh;
    }
    bool SendsRelayed(const Peer& p, Clock::time_point now) const {
        return (cfg.path == Config::Path::Relay && cfg.server.Valid() && p.relayTo.Valid()) || !DirectFresh(p, now);
    }
    // 'L' the peer's LAN address, 'P' another direct one, 'R' the relay.
    char PathOf(const Peer& p, Clock::time_point now) const {
        if (SendsRelayed(p, now)) return 'R';
        return p.lan.Valid() && p.direct == p.lan ? 'L' : 'P';
    }
    static const char* PathName(char c) { return c == 'L' ? "direct, LAN" : c == 'P' ? "direct" : "relayed"; }
    // Lock held: which direct address to keep (Config::path).
    void UpdatePreferred(Peer& p) const {
        switch (cfg.path) {
        case Config::Path::Lan: p.preferred = p.lan; break;
        case Config::Path::Public: p.preferred = p.ext; break;
        case Config::Path::Relay: p.preferred = {}; break;
        case Config::Path::Auto:
            // Behind our public address: the same network, so not through the router's hairpin.
            p.preferred = p.lan.Valid() && p.ext.Valid() && external.Valid() && p.ext.ip == external.ip ? p.lan
                                                                                                         : Endpoint{};
            break;
        }
    }
    // Lock held. Directly when the peer's packets come directly, else through the relay.
    void SendToPeer(Peer& p, const std::vector<uint8_t>& b, Clock::time_point now) {
        if (cfg.path == Config::Path::Relay && cfg.server.Valid() && p.relayTo.Valid()) {
            SendRelayed(p.relayTo, b);
            ++stats.sentRelayed;
        } else if (DirectFresh(p, now)) {
            SendRaw(p.direct, b);
            ++stats.sentDirect;
        } else if (cfg.server.Valid() && p.relayTo.Valid()) {
            SendRelayed(p.relayTo, b);
            ++stats.sentRelayed;
        } else if (!p.candidates.empty()) {
            SendRaw(p.candidates.front(), b);
            ++stats.sentDirect;
        }
    }
    // Lock held: the same way a packet came in.
    void Reply(const Endpoint& src, bool relayed, const std::vector<uint8_t>& b) {
        if (relayed) SendRelayed(src, b);
        else SendRaw(src, b);
    }

    std::vector<uint8_t> Hello(uint8_t type, const std::string& to) const {
        auto b = Header(type);
        PutId(b, cfg.self);
        PutId(b, to);
        PutEndpoint(b, external);
        Endpoint lan;
        lan.ip = cfg.reportLan ? lanIp : 0;
        lan.port = localPort;
        PutEndpoint(b, lan);
        return b;
    }
    std::vector<uint8_t> Ping(uint8_t type, uint64_t t) const {
        auto b = Header(type);
        PutId(b, cfg.self);
        for (int i = 7; i >= 0; --i) b.push_back(static_cast<uint8_t>(t >> (8 * i)));
        return b;
    }

    static void AddCandidate(Peer& p, const Endpoint& e) {
        if (e.Valid() && std::find(p.candidates.begin(), p.candidates.end(), e) == p.candidates.end())
            p.candidates.push_back(e);
    }

    // Lock held: the peer a packet from `src` belongs to.
    Peer* PeerFrom(const Endpoint& src, bool relayed) {
        for (auto& [name, p] : peers) {
            if (relayed ? p.relayTo == src : (p.direct == src || std::find(p.candidates.begin(), p.candidates.end(), src) != p.candidates.end()))
                return &p;
        }
        return nullptr;
    }

    // Lock held: a packet from the peer arrived (`src` as it came: direct, or the relay's sender).
    void Heard(Peer& p, const Endpoint& src, bool relayed, Clock::time_point now) {
        p.lastRx = now;
        if (relayed) {
            p.relayTo = src;
        } else {
            AddCandidate(p, src);
            // While the preferred address answers, packets that come another way don't move us off it.
            const bool keep = p.preferred.Valid() && p.direct == p.preferred && src != p.preferred &&
                              now - p.lastDirect < kDirectFresh;
            if (keep) return;
            if (p.direct != src && p.direct.Valid() && pathLogs++ < 20)
                std::fprintf(stderr, "[p2p] %s %s: its packets now come from %s%s\n", core::Stamp().text,
                             p.name.c_str(), ToString(src).c_str(),
                             src == p.lan ? " (its LAN address)" : src == p.ext ? " (its public address)" : "");
            p.direct = src;
            p.lastDirect = now;
        }
    }

    // Lock held: a DATA2's diagnostics (sequence, one-way delay, sender pauses).
    void NoteDiag(Peer& p, uint32_t seq, int64_t txQpc, size_t len, bool relayed) {
        const int64_t rxQpc = Qpc();
        const int64_t delayUs = QpcUs(rxQpc - txQpc);
        ++diag.rx;
        diag.delaySum += delayUs;
        diag.delayMin = std::min(diag.delayMin, delayUs);
        diag.delayMax = std::max(diag.delayMax, delayUs);
        diag.maxRx = std::max(diag.maxRx, len);
        const bool log = diag.logged < 400;
        if (!p.rxStarted) {
            p.rxStarted = true;
            p.rxNext = seq + 1;
        } else {
            const int32_t ahead = static_cast<int32_t>(seq - p.rxNext);
            if (ahead == 0) {
                ++p.rxNext;
            } else if (ahead > 0) {
                diag.gaps += static_cast<uint32_t>(ahead);
                if (log && ++diag.logged)
                    std::fprintf(stderr, "[p2p-diag] %s from %s: seq %u..%u missing (got %u)\n", core::Stamp().text,
                                 p.name.c_str(), p.rxNext, seq - 1, seq);
                p.rxNext = seq + 1;
            } else {
                ++diag.late;
                if (log && ++diag.logged)
                    std::fprintf(stderr, "[p2p-diag] %s from %s: seq %u late (expected %u), %.1f ms on the way\n",
                                 core::Stamp().text, p.name.c_str(), seq, p.rxNext, delayUs / 1000.0);
            }
        }
        if (p.peerLastTx && txQpc - p.peerLastTx > QpcFreq() / 10) {
            ++diag.pauses;
            if (log && ++diag.logged)
                std::fprintf(stderr, "[p2p-diag] %s from %s: seq %u sent %.1f ms after its previous datagram\n",
                             core::Stamp().text, p.name.c_str(), seq, QpcUs(txQpc - p.peerLastTx) / 1000.0);
        }
        p.peerLastTx = std::max(p.peerLastTx, txQpc);
        if (delayUs > 30'000) {
            ++diag.slow;
            if (log && ++diag.logged)
                std::fprintf(stderr, "[p2p-diag] %s from %s: seq %u took %.1f ms (%s)\n", core::Stamp().text,
                             p.name.c_str(), seq, delayUs / 1000.0, relayed ? "relayed" : "direct");
        }
        Remember({rxQpc, seq, static_cast<uint32_t>(len), static_cast<int32_t>(std::min<int64_t>(delayUs, INT32_MAX)),
                  'r', relayed ? 'R' : (p.lan.Valid() && p.direct == p.lan ? 'L' : 'P')});
    }

    // Lock held.
    void Remember(const Rec& r) {
        ring.push_back(r);
        while (!ring.empty() && r.qpc - ring.front().qpc > 20 * QpcFreq()) ring.pop_front();
    }
    // Lock held.
    void DumpRecentLocked(const char* why) {
        if (!cfg.diag) return;
        const int64_t now = Qpc();
        std::fprintf(stderr, "[p2p-ring] %s: the game datagrams of the last 20 s before %s (%zu):\n", core::Stamp().text,
                     why, ring.size());
        for (const Rec& r : ring) {
            if (r.dir == 't')
                std::fprintf(stderr, "[p2p-ring] %8.3f s  sent seq %u, %u bytes, %c\n", -QpcUs(now - r.qpc) / 1e6, r.seq,
                             r.len, r.path);
            else
                std::fprintf(stderr, "[p2p-ring] %8.3f s  got  seq %u, %u bytes, %c, %.1f ms on the way\n",
                             -QpcUs(now - r.qpc) / 1e6, r.seq, r.len, r.path, r.delayUs / 1000.0);
        }
        ring.clear();
    }

    // Lock held: the connection is up (once).
    void SetActive(Peer& p) {
        if (p.state != State::Active) {
            p.state = State::Active;
            if (p.local) events.push_back({Event::Established, p.name, 0});
            else events.push_back({Event::PeerActivated, p.name, 0});
            std::fprintf(stderr, "[p2p] %s: connected (%s, game address %s)\n", p.name.c_str(),
                         p.direct.Valid() ? "direct" : "relayed", ToString(p.gameAddr).c_str());
        }
        if (p.local && p.remote && !p.mutualFired) {
            p.mutualFired = true;
            events.push_back({Event::Mutual, p.name, 0});
        }
    }

    void HandlePeerPacket(const uint8_t* d, size_t n, const Endpoint& src, bool relayed, Clock::time_point now) {
        if (n < 5 || std::memcmp(d, kMagic, 4) != 0) return;
        const uint8_t type = d[4];
        std::lock_guard lk(m);
        ++stats.received;
        switch (type) {
        case kHello:
        case kAck: {
            if (n < kHelloLen) return;
            const std::string from = GetId(d + 5), to = GetId(d + 5 + kIdLen);
            if (to != cfg.self || from.empty() || from == cfg.self) return;
            const Endpoint ext = GetEndpoint(d + 5 + 2 * kIdLen), lan = GetEndpoint(d + 5 + 2 * kIdLen + 6);
            auto it = peers.find(from);
            if (it == peers.end()) {
                if (type == kAck) return;
                if (!cfg.mayConnect || !cfg.mayConnect(from)) {
                    ++stats.refused;
                    if (refusedLogged.insert(from).second)
                        std::fprintf(stderr, "[p2p] refused a connection from %s (not a friend)\n", from.c_str());
                    return;
                }
                it = peers.emplace(from, Peer{}).first;
                it->second.name = from;
                it->second.started = now;
            }
            Peer& p = it->second;
            p.gameAddr = ext.Valid() ? ext : lan.Valid() ? lan : src;
            p.ext = ext;
            p.lan = lan;
            UpdatePreferred(p);
            AddCandidate(p, ext);
            AddCandidate(p, lan);
            Heard(p, src, relayed, now);
            if (type == kHello) {
                p.remote = true;
                Reply(src, relayed, Hello(kAck, from));
            }
            SetActive(p);
            break;
        }
        case kData:
        case kData2: {
            const size_t head = type == kData2 ? 25 : 13;
            if (n < head) return;
            const uint32_t tag = Get32(d + 9);
            Peer* p = nullptr;
            for (auto& [name, peer] : peers)
                if (NameTag(name) == tag) p = &peer;
            if (!p || p->state != State::Active) {
                if (++stats.dataUnknown <= 10)
                    std::fprintf(stderr, "[p2p] a game datagram from %s%s, from no connected player: dropped\n",
                                 ToString(src).c_str(), relayed ? " (relayed)" : "");
                return;
            }
            Heard(*p, src, relayed, now);
            if (++stats.dataReceived <= 5 || stats.dataReceived % 3000 == 0)
                std::fprintf(stderr, "[p2p] game datagram %llu from %s (%zu bytes, %s)\n",
                             static_cast<unsigned long long>(stats.dataReceived), p->name.c_str(), n - head,
                             relayed ? "relayed" : "direct");
            if (type == kData2) NoteDiag(*p, Get32(d + 13), static_cast<int64_t>(Get64(d + 17)), n - head, relayed);
            if (sink) sink(p->gameAddr, Get16(d + 5), Get16(d + 7), d + head, n - head);
            break;
        }
        case kPing:
        case kPong: {
            if (n < 5 + kIdLen + 8) return;
            const std::string from = GetId(d + 5);
            auto it = peers.find(from);
            if (it == peers.end()) return;
            Peer& p = it->second;
            Heard(p, src, relayed, now);
            uint64_t t = 0;
            for (int i = 0; i < 8; ++i) t = t << 8 | d[5 + kIdLen + i];
            if (type == kPing) {
                Reply(src, relayed, Ping(kPong, t));
            } else {
                const uint64_t nowUs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
                if (nowUs >= t) p.rttUs = static_cast<int>(std::min<uint64_t>(nowUs - t, 10'000'000));
            }
            break;
        }
        case kBye: {
            if (n < 5 + 2 * kIdLen) return;
            const std::string from = GetId(d + 5);
            if (GetId(d + 5 + kIdLen) != cfg.self) return;
            if (peers.erase(from)) {
                events.push_back({Event::Dead, from, kErrorPeerGone});
                std::fprintf(stderr, "[p2p] %s %s: left\n", core::Stamp().text, from.c_str());
                DumpRecentLocked("the peer left");
            }
            break;
        }
        default:
            break;
        }
    }

    void HandlePacket(const uint8_t* d, size_t n, const Endpoint& src, Clock::time_point now) {
        if (cfg.server.Valid() && src == cfg.server) {
            if (n >= 12 && std::memcmp(d, kStunHeader, 6) == 0) {
                std::lock_guard lk(m);
                const Endpoint e = GetEndpoint(d + 6);
                if (e != external) std::fprintf(stderr, "[p2p] the server sees us at %s\n", ToString(e).c_str());
                external = e;
                stunAnswered = true;
                return;
            }
            if (n > 11 && std::memcmp(d, kRelayMagic, 4) == 0 && d[4] == 0x03) {
                HandlePeerPacket(d + 11, n - 11, GetEndpoint(d + 5), true, now);
                return;
            }
            return;
        }
        HandlePeerPacket(d, n, src, false, now);
    }

    Clock::time_point lastSummary{};
    uint64_t summarySent = 0, summaryReceived = 0;

    // Lock held: what flowed in the last 5 s, while anything does.
    void Summary(Clock::time_point now) {
        if (now - lastSummary < std::chrono::seconds(5)) return;
        lastSummary = now;
        const uint64_t sent = stats.dataSent - summarySent, received = stats.dataReceived - summaryReceived;
        summarySent = stats.dataSent;
        summaryReceived = stats.dataReceived;
        if (!sent && !received) return;
        std::string who;
        for (const auto& [name, p] : peers)
            who += " " + name + (p.state == State::Active ? std::string(" (") + PathName(PathOf(p, now)) : " (pending") +
                   ", rtt " + std::to_string(p.rttUs / 1000) + " ms)";
        std::fprintf(stderr, "[p2p] %s last 5 s: %llu game datagrams sent, %llu received;%s\n", core::Stamp().text,
                     static_cast<unsigned long long>(sent), static_cast<unsigned long long>(received), who.c_str());
        if (cfg.diag && diag.rx) {
            std::fprintf(stderr,
                         "[p2p-diag] %s last 5 s: one-way delay %.1f / %.1f / %.1f ms (min / avg / max), %llu missing, "
                         "%llu late, %llu sender pauses, %llu over 30 ms; largest sent %zu, received %zu bytes\n",
                         core::Stamp().text, diag.delayMin / 1000.0, diag.delaySum / 1000.0 / diag.rx,
                         diag.delayMax / 1000.0, static_cast<unsigned long long>(diag.gaps),
                         static_cast<unsigned long long>(diag.late), static_cast<unsigned long long>(diag.pauses),
                         static_cast<unsigned long long>(diag.slow), diag.maxTx, diag.maxRx);
        }
        const int logged = diag.logged;
        diag = {};
        diag.logged = logged;
    }

    void Tick(Clock::time_point now) {
        std::vector<std::string> toResolve;
        {
            std::lock_guard lk(m);
            Summary(now);
            if (cfg.server.Valid() && now - lastStun >= std::chrono::milliseconds(stunAnswered ? cfg.stunMs : 1000)) {
                lastStun = now;
                std::vector<uint8_t> b(kStunHeader, kStunHeader + 6);
                b.push_back(0x01);
                PutId(b, cfg.self);
                const auto* ip = reinterpret_cast<const uint8_t*>(&lanIp);
                b.insert(b.end(), ip, ip + 4);
                SendRaw(cfg.server, b);
            }
            const uint64_t nowUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
            for (auto it = peers.begin(); it != peers.end();) {
                Peer& p = it->second;
                if (p.state == State::Pending) {
                    if (Ms(now - p.started) > cfg.timeoutMs) {
                        events.push_back({Event::Dead, p.name, kErrorUnreachable});
                        std::fprintf(stderr, "[p2p] %s: no answer in %d s\n", p.name.c_str(), cfg.timeoutMs / 1000);
                        it = peers.erase(it);
                        continue;
                    }
                    if (p.candidates.empty() && !p.relayTo.Valid() && !p.resolving &&
                        now - p.lastResolve >= std::chrono::seconds(1)) {
                        p.resolving = true;
                        p.lastResolve = now;
                        toResolve.push_back(p.name);
                    }
                    if (now - p.lastHello >= std::chrono::milliseconds(cfg.resendMs)) {
                        p.lastHello = now;
                        const auto hello = Hello(kHello, p.name);
                        for (const Endpoint& c : p.candidates) SendRaw(c, hello);
                        if (Ms(now - p.started) >= cfg.relayAfterMs && p.relayTo.Valid()) SendRelayed(p.relayTo, hello);
                    }
                } else if (p.state == State::Active) {
                    if (Ms(now - p.lastRx) > cfg.deadAfterMs) {
                        events.push_back({Event::Dead, p.name, kErrorTimeout});
                        std::fprintf(stderr, "[p2p] %s %s: nothing heard for %d s, connection dead\n",
                                     core::Stamp().text, p.name.c_str(), cfg.deadAfterMs / 1000);
                        DumpRecentLocked("the connection died");
                        it = peers.erase(it);
                        continue;
                    }
                    if (now - p.lastPing >= std::chrono::milliseconds(cfg.keepaliveMs)) {
                        p.lastPing = now;
                        const auto ping = Ping(kPing, nowUs);
                        SendToPeer(p, ping, now);
                        // Not direct yet: keep trying the direct addresses (a hole may open).
                        if (!DirectFresh(p, now))
                            for (const Endpoint& c : p.candidates) SendRaw(c, ping);
                        // The preferred address (Config::path) isn't the one in use: try it.
                        UpdatePreferred(p);
                        if (p.preferred.Valid() && p.direct != p.preferred && cfg.path != Config::Path::Relay)
                            SendRaw(p.preferred, ping);
                    }
                }
                ++it;
            }
        }
        for (const std::string& name : toResolve) {
            if (!cfg.resolve) continue;
            std::weak_ptr<std::atomic<bool>> weakAlive = alive;
            cfg.resolve(name, [this, name, weakAlive](bool ok, Endpoint e) {
                auto a = weakAlive.lock();
                if (!a || !a->load()) return;
                std::lock_guard lk(m);
                auto it = peers.find(name);
                if (it == peers.end()) return;
                it->second.resolving = false;
                if (!ok || !e.Valid()) return;
                it->second.relayTo = e;
                AddCandidate(it->second, e);
                if (!it->second.gameAddr.Valid()) it->second.gameAddr = e;
                std::fprintf(stderr, "[p2p] %s: the server says %s\n", name.c_str(), ToString(e).c_str());
            });
        }
    }

    void Run() {
        std::vector<uint8_t> buf(65536);
        while (!stop.load()) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(sock, &rd);
            timeval tv{0, 20000};
            const int r = select(0, &rd, nullptr, nullptr, &tv);
            const auto now = Clock::now();
            if (r > 0) {
                for (int i = 0; i < 64; ++i) {
                    sockaddr_in from{};
                    int fromLen = sizeof(from);
                    u_long avail = 0;
                    if (ioctlsocket(sock, FIONREAD, &avail) != 0 || avail == 0) {
                        if (i > 0) break;
                    }
                    const int n = recvfrom(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
                                           reinterpret_cast<sockaddr*>(&from), &fromLen);
                    if (n <= 0) break;  // includes WSAECONNRESET from an ICMP "port unreachable"
                    Endpoint src;
                    src.ip = from.sin_addr.s_addr;
                    src.port = ntohs(from.sin_port);
                    HandlePacket(buf.data(), static_cast<size_t>(n), src, now);
                }
            }
            Tick(Clock::now());
        }
    }
};

Link::Link() : impl_(std::make_unique<Impl>()) {}
Link::~Link() { Stop(); }

bool Link::Start(const Config& config) {
    Stop();
    Impl& I = *impl_;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    I.cfg = config;
    I.stop = false;
    I.alive = std::make_shared<std::atomic<bool>>(true);
    I.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (I.sock == INVALID_SOCKET) return false;
    // An ICMP "port unreachable" must not break the socket for everyone else (Windows reports it on
    // the next recvfrom as WSAECONNRESET).
    BOOL off = FALSE;
    DWORD ret = 0;
    WSAIoctl(I.sock, _WSAIOW(IOC_VENDOR, 12) /*SIO_UDP_CONNRESET*/, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(config.localPort);
    if (bind(I.sock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closesocket(I.sock);
        I.sock = INVALID_SOCKET;
        return false;
    }
    int len = sizeof(a);
    getsockname(I.sock, reinterpret_cast<sockaddr*>(&a), &len);
    I.localPort = ntohs(a.sin_port);
    I.lanIp = config.lanIp;
    if (!I.lanIp && config.server.Valid()) {
        // The LAN address the route to the server leaves from.
        SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in s{};
        s.sin_family = AF_INET;
        s.sin_addr.s_addr = config.server.ip;
        s.sin_port = htons(config.server.port);
        if (probe != INVALID_SOCKET && connect(probe, reinterpret_cast<sockaddr*>(&s), sizeof(s)) == 0) {
            sockaddr_in me{};
            int ml = sizeof(me);
            if (getsockname(probe, reinterpret_cast<sockaddr*>(&me), &ml) == 0) I.lanIp = me.sin_addr.s_addr;
        }
        if (probe != INVALID_SOCKET) closesocket(probe);
    }
    std::fprintf(stderr, "[p2p] listening on UDP %u (LAN %s), server %s\n", I.localPort,
                 ToString(Endpoint{I.lanIp, I.localPort}).c_str(),
                 config.server.Valid() ? ToString(config.server).c_str() : "none");
    I.thread = std::thread([this] {
        SetThreadDescription(GetCurrentThread(), L"P2P link");
        impl_->Run();
    });
    return true;
}

void Link::Stop() {
    Impl& I = *impl_;
    if (I.alive) I.alive->store(false);
    I.stop = true;
    if (I.thread.joinable()) I.thread.join();
    std::lock_guard lk(I.m);
    if (I.sock != INVALID_SOCKET) {
        for (auto& [name, p] : I.peers) {
            if (p.state != State::Active) continue;
            auto b = Header(kBye);
            PutId(b, I.cfg.self);
            PutId(b, name);
            I.SendToPeer(p, b, Clock::now());
        }
        closesocket(I.sock);
        I.sock = INVALID_SOCKET;
    }
    I.peers.clear();
    I.external = {};
    I.stunAnswered = false;
}

bool Link::Running() const { return impl_->sock != INVALID_SOCKET; }
uint16_t Link::LocalPort() const { return impl_->localPort; }
Endpoint Link::External() const {
    std::lock_guard lk(impl_->m);
    return impl_->external;
}

void Link::Activate(const std::string& peer) {
    Impl& I = *impl_;
    std::lock_guard lk(I.m);
    auto it = I.peers.find(peer);
    if (it == I.peers.end()) {
        it = I.peers.emplace(peer, Impl::Peer{}).first;
        it->second.name = peer;
        it->second.state = State::Pending;
        it->second.started = Clock::now();
    }
    Impl::Peer& p = it->second;
    if (!p.local) {
        p.local = true;
        std::fprintf(stderr, "[p2p] connecting to %s\n", peer.c_str());
        if (p.state == State::Active) {
            // It connected to us first: our activation completes at once.
            I.events.push_back({Event::Established, peer, 0});
            if (p.remote && !p.mutualFired) {
                p.mutualFired = true;
                I.events.push_back({Event::Mutual, peer, 0});
            }
            I.SendToPeer(p, I.Hello(kHello, peer), Clock::now());
        }
    }
}

void Link::Terminate(const std::string& peer) {
    Impl& I = *impl_;
    std::lock_guard lk(I.m);
    auto it = I.peers.find(peer);
    if (it == I.peers.end()) return;
    if (it->second.state == State::Active) {
        auto b = Header(kBye);
        PutId(b, I.cfg.self);
        PutId(b, peer);
        I.SendToPeer(it->second, b, Clock::now());
    }
    I.peers.erase(it);
    I.DumpRecentLocked("our game ended the connection");
}

void Link::DumpRecent(const char* why) {
    std::lock_guard lk(impl_->m);
    impl_->DumpRecentLocked(why);
}

PeerInfo Link::Info(const std::string& peer) const {
    const Impl& I = *impl_;
    std::lock_guard lk(I.m);
    PeerInfo info;
    auto it = I.peers.find(peer);
    if (it == I.peers.end()) return info;
    const auto& p = it->second;
    info.state = p.state;
    info.gameAddr = p.gameAddr;
    info.relayed = I.SendsRelayed(p, Clock::now());
    info.rttUs = p.rttUs;
    info.locallyActivated = p.local;
    info.peerActivated = p.remote;
    return info;
}

std::string Link::PeerAt(const Endpoint& gameAddr) const {
    const Impl& I = *impl_;
    std::lock_guard lk(I.m);
    std::string byIp;
    int ipMatches = 0;
    for (const auto& [name, p] : I.peers) {
        if (p.gameAddr == gameAddr) return name;
        if (p.gameAddr.ip == gameAddr.ip) {
            byIp = name;
            ++ipMatches;
        }
    }
    return ipMatches == 1 ? byIp : std::string();
}

std::vector<Event> Link::TakeEvents() {
    std::lock_guard lk(impl_->m);
    std::vector<Event> out(impl_->events.begin(), impl_->events.end());
    impl_->events.clear();
    return out;
}

bool Link::SendData(const Endpoint& gameAddr, uint16_t srcVport, uint16_t dstVport, const void* data, size_t len) {
    const std::string name = PeerAt(gameAddr);
    Impl& I = *impl_;
    std::lock_guard lk(I.m);
    auto it = name.empty() ? I.peers.end() : I.peers.find(name);
    if (it == I.peers.end() || it->second.state != State::Active) {
        if (++I.stats.dataNoPeer <= 10)
            std::fprintf(stderr, "[p2p] the game sent a datagram to %s (vport %u): no connected player there\n",
                         ToString(gameAddr).c_str(), dstVport);
        return false;
    }
    Impl::Peer& peer = it->second;
    auto b = Header(I.cfg.diag ? kData2 : kData);
    Put16(b, srcVport);
    Put16(b, dstVport);
    Put32(b, NameTag(I.cfg.self));
    const uint32_t seq = peer.txSeq++;
    const int64_t sentAt = Qpc();
    if (I.cfg.diag) {
        Put32(b, seq);
        Put64(b, static_cast<uint64_t>(sentAt));
    }
    const auto* p = static_cast<const uint8_t*>(data);
    b.insert(b.end(), p, p + len);
    const auto now = Clock::now();
    if (++I.stats.dataSent <= 5 || I.stats.dataSent % 3000 == 0)
        std::fprintf(stderr, "[p2p] game datagram %llu to %s (%zu bytes, %s)\n",
                     static_cast<unsigned long long>(I.stats.dataSent), name.c_str(), len,
                     Impl::PathName(I.PathOf(peer, now)));
    if (I.cfg.diag) {
        I.diag.maxTx = std::max(I.diag.maxTx, len);
        I.Remember({sentAt, seq, static_cast<uint32_t>(len), -1, 't', I.PathOf(peer, now)});
    }
    I.SendToPeer(peer, b, now);
    return true;
}

void Link::SetDataSink(DataSink sink) {
    std::lock_guard lk(impl_->m);
    impl_->sink = std::move(sink);
}

Link::Stats Link::GetStats() const {
    std::lock_guard lk(impl_->m);
    return impl_->stats;
}

}  // namespace net::p2p
