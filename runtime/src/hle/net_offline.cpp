// libSceNet and the POSIX socket calls, as on a console with the cable
// unplugged: anything that would leave the machine fails with "network
// unreachable", but loopback works. LBP3 needs that: the Pod runs as a local
// game session whose host and client talk over UDP to 127.0.0.1:3658.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <thread>

#include <cstdarg>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ipexport.h>
#include <icmpapi.h>

#include "hle/kernel_errors.h"
#include "hle/net_delay.h"
#include "hle/net_remote.h"
#include "hle/online.h"
#include "hle/register.h"

namespace hle::net {
namespace {

// FreeBSD errno values.
constexpr int kEWouldBlock = 35;
constexpr int kENetUnreach = 51;
constexpr int kETimedOut = 60;

// sceNet errors are 0x80410100 | errno.
constexpr int NetErr(int e) { return static_cast<int>(0x80410100u | static_cast<uint32_t>(e)); }

constexpr int kAfInet = 2;
constexpr int32_t kFirstId = 1000;
constexpr auto kEpollMaxSleep = std::chrono::milliseconds(100);

std::atomic<int32_t> g_nextId{kFirstId};

int* NetErrno() {
    thread_local int err = 0;
    return &err;
}

// POSIX style: -1 with errno set (both the kernel's and sceNet's copies).
int PosixFail(int e) {
    *kernel::__error() = e;
    *NetErrno() = e;
    return -1;
}

int SceFail(int e) {
    *NetErrno() = e;
    return NetErr(e);
}

// "a.b.c.d:port" from a PS4 sockaddr_in (len, family, port BE, addr).
std::string Addr(const void* sa) {
    if (!sa) return "?";
    const auto* b = static_cast<const uint8_t*>(sa);
    char buf[64];
    // SceNetSockaddrIn: len, family, port (BE), addr, vport (BE, P2P sockets), zero[6]
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u vport %u (len %u)", b[4], b[5], b[6], b[7], (b[2] << 8) | b[3],
                  (b[8] << 8) | b[9], b[0]);
    return buf;
}

void NetLog(const char* what, int id, const std::string& extra) {
    static std::atomic<int> n{0};
    if (n++ < 80) std::fprintf(stderr, "[net] %s(%d) %s\n", what, id, extra.c_str());
}

// --- loopback datagrams -------------------------------------------------------------

struct Datagram {
    std::vector<uint8_t> data;
    uint16_t fromPort, fromVport;
    uint8_t fromAddr[4] = {127, 0, 0, 1};  // an echo reply's sender (online); loopback otherwise
};
struct Socket {
    uint16_t port = 0;   // bound local port, 0 = unbound
    uint16_t vport = 0;  // bound virtual port (P2P sockets, SceNetSockaddrIn.sin_vport)
    std::deque<Datagram> inbox;
    int type = 0, proto = 0;  // as created (3 and 1: a raw ICMP socket)
    uint64_t serial = 0;      // tells a reused descriptor from the socket an echo was sent on
};
struct EpollEntry {
    int socket;
    uint32_t events;
    uint64_t data;
};

std::mutex g_netMutex;
std::atomic<uint64_t> g_recvCalls{0}, g_recvData{0};  // for the traffic summary
std::map<int, Socket> g_sockets;
std::map<int, std::vector<EpollEntry>> g_epolls;
uint16_t g_nextEphemeral = 49152;
constexpr uint32_t kEpollIn = 0x1;
constexpr size_t kMaxQueued = 256;  // per socket, like a full receive buffer
constexpr int kMsgPeek = 0x2;       // MSG_PEEK (FreeBSD): read the next datagram without taking it

// Lock held: a datagram that didn't fit the socket's queue (dropped, as a full UDP buffer does).
void NoteDropped(int s, const char* kind, size_t len) {
    static int logged = 0;
    if (logged++ < 20)
        std::fprintf(stderr, "[net] the queue of socket %d is full (%zu): a %zu-byte %s datagram dropped\n", s,
                     kMaxQueued, len, kind);
}

// PS4 sockaddr_in: len, family, port (big endian), address.
uint16_t PortOf(const void* sa) {
    const auto* b = static_cast<const uint8_t*>(sa);
    return static_cast<uint16_t>((b[2] << 8) | b[3]);
}
uint16_t VportOf(const void* sa) {
    const auto* b = static_cast<const uint8_t*>(sa);
    return static_cast<uint16_t>((b[8] << 8) | b[9]);
}
bool IsLoopback(const void* sa) { return sa && static_cast<const uint8_t*>(sa)[4] == 127; }

// Socket descriptors are small and reused, like FreeBSD file descriptors: games
// put them in select() fd_sets (1024 bits). They used to share a counter with
// epolls, which LBP3's select() creates thousands of times a second, so later
// sockets got numbers past a million and FD_SET wrote far out of bounds.
constexpr int kFirstSocket = 64, kMaxSocket = 1023;

int NewSocket() {
    std::lock_guard lk(g_netMutex);
    for (int id = kFirstSocket; id <= kMaxSocket; ++id) {
        if (g_sockets.contains(id)) continue;
        g_sockets[id];
        return id;
    }
    return -1;
}

void BindSocket(int s, const void* addr) {
    std::lock_guard lk(g_netMutex);
    g_sockets[s].port = addr ? PortOf(addr) : 0;
    g_sockets[s].vport = addr ? VportOf(addr) : 0;
}

// Delivers to the socket bound to the destination port. Returns false if
// the destination isn't on this machine.
bool SendLocal(int s, const void* data, size_t len, const void* to) {
    if (!IsLoopback(to)) return false;
    std::lock_guard lk(g_netMutex);
    Socket& from = g_sockets[s];
    if (from.port == 0) from.port = g_nextEphemeral++;  // implicit bind, like a real stack
    const uint16_t port = PortOf(to);
    {
        // Traffic summary every 5 s: message count and size spread.
        static uint64_t msgs = 0, bytes = 0, minLen = ~0ull, maxLen = 0;
        static ULONGLONG last = 0;
        ++msgs;
        bytes += len;
        minLen = std::min<uint64_t>(minLen, len);
        maxLen = std::max<uint64_t>(maxLen, len);
        const ULONGLONG now = GetTickCount64();
        if (now - last >= 5000) {
            size_t queued = 0;
            for (auto& [id, sock] : g_sockets) queued += sock.inbox.size();
            std::fprintf(stderr, "[net] loopback: %llu messages, %llu bytes (sizes %llu..%llu) in the last 5 s; "
                         "%llu receive calls, %llu got data, %zu queued\n",
                         static_cast<unsigned long long>(msgs), static_cast<unsigned long long>(bytes),
                         static_cast<unsigned long long>(minLen), static_cast<unsigned long long>(maxLen),
                         static_cast<unsigned long long>(g_recvCalls.exchange(0)),
                         static_cast<unsigned long long>(g_recvData.exchange(0)), queued);
            msgs = bytes = maxLen = 0;
            minLen = ~0ull;
            last = now;
        }
    }
    // The first 30 messages, and the first of every new size after that: a new kind of
    // request (e.g. at a level change) shows up as a size not seen before.
    static int logged = 0;
    static std::set<size_t> seenSizes;
    const bool newSize = seenSizes.insert(len).second && seenSizes.size() < 400;
    if (logged++ < 30 || newSize) {
        std::string hex;
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len && i < 160; ++i) {
            char h[4];
            std::snprintf(h, sizeof(h), "%02x", bytes[i]);
            hex += h;
            if (i % 4 == 3) hex += ' ';
        }
        std::string text;
        for (size_t i = 0; i < len && i < 160; ++i) text += (bytes[i] >= 32 && bytes[i] < 127) ? char(bytes[i]) : '.';
        std::fprintf(stderr, "[net] loopback send %zu bytes %u -> %s: %s |%s|\n", len, from.port, Addr(to).c_str(),
                     hex.c_str(), text.c_str());
    }
    for (auto& [id, sock] : g_sockets) {
        if (sock.port != port) continue;
        if (sock.inbox.size() < kMaxQueued) {
            const auto* p = static_cast<const uint8_t*>(data);
            sock.inbox.push_back({std::vector<uint8_t>(p, p + len), from.port, from.vport});
        } else {
            NoteDropped(id, "loopback", len);
        }
        break;
    }
    return true;  // UDP: unanswered datagrams to a closed port are just dropped
}

// Pops one datagram (MSG_PEEK: copies it and leaves it queued); -1 if none.
int64_t RecvLocal(int s, void* buf, size_t len, void* from, uint32_t* fromLen, int flags = 0) {
    std::lock_guard lk(g_netMutex);
    ++g_recvCalls;
    {
        // Each kind of read the game makes, once: the flags and the buffer size.
        static std::set<std::pair<int, size_t>> seen;
        if (seen.size() < 20 && seen.insert({flags, len}).second)
            std::fprintf(stderr, "[net] recv on socket %d: flags %#x, %zu-byte buffer\n", s, flags, len);
    }
    auto it = g_sockets.find(s);
    if (it == g_sockets.end() || it->second.inbox.empty()) return -1;
    ++g_recvData;
    if (it->second.inbox.front().data.size() > len) {
        static int truncated = 0;
        if (truncated++ < 20)
            std::fprintf(stderr, "[net] recv on socket %d: a %zu-byte datagram into a %zu-byte buffer (cut short)\n", s,
                         it->second.inbox.front().data.size(), len);
    }
    Datagram d;
    if (flags & kMsgPeek) {
        d = it->second.inbox.front();
    } else {
        d = std::move(it->second.inbox.front());
        it->second.inbox.pop_front();
    }
    static int logged = 0;
    if (logged++ < 30)
        std::fprintf(stderr, "[net] loopback recv %zu bytes on %u from %u\n", d.data.size(), it->second.port,
                     d.fromPort);
    const size_t n = std::min(len, d.data.size());
    if (buf && n) std::memcpy(buf, d.data.data(), n);
    if (from && fromLen && *fromLen >= 16) {
        // The sender's virtual port matters: P2P peers are identified by addr:port:vport.
        uint8_t sa[16] = {16, kAfInet, static_cast<uint8_t>(d.fromPort >> 8), static_cast<uint8_t>(d.fromPort),
                          d.fromAddr[0], d.fromAddr[1], d.fromAddr[2], d.fromAddr[3],
                          static_cast<uint8_t>(d.fromVport >> 8), static_cast<uint8_t>(d.fromVport)};
        std::memcpy(from, sa, sizeof(sa));
        *fromLen = sizeof(sa);
    }
    return static_cast<int64_t>(n);
}

void CloseSocket(int s) {
    std::lock_guard lk(g_netMutex);
    g_sockets.erase(s);
    for (auto& [ep, entries] : g_epolls)
        std::erase_if(entries, [s](const EpollEntry& e) { return e.socket == s; });
}

std::atomic<uint64_t> g_nextSerial{1};

void SetKind(int s, int type, int proto) {
    std::lock_guard lk(g_netMutex);
    if (auto it = g_sockets.find(s); it != g_sockets.end()) {
        it->second.type = type;
        it->second.proto = proto;
        it->second.serial = g_nextSerial++;
    }
}

// --- online: the server ping (a raw ICMP echo) ---------------------------------------
//
// LBP3 pings its CDN host ("https://play.lbppc.com/lbp", looked up as a name: see the resolver)
// over a raw ICMP socket: sendto of an 8-byte echo request. Windows sends an echo without admin
// rights only through IcmpSendEcho, so that is what sends it, on a thread of its own; the reply
// is queued on the game's socket as a raw socket delivers it (an IPv4 header, then the ICMP echo
// reply with the game's own id and sequence). Only online, only for raw ICMP sockets, and only
// after the loopback path (SendLocal) has declined the packet, so the game session's loopback
// traffic is untouched.

constexpr int kSockRaw = 3, kProtoIcmp = 1;
constexpr uint8_t kIcmpEchoRequest = 8, kIcmpEchoReply = 0;

// The first 60 lines about the ping (NetLog stops after 80 lines of everything).
void IcmpLog(const char* format, ...) {
    static std::atomic<int> n{0};
    if (n++ >= 60) return;
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    std::fprintf(stderr, "[net] icmp: %s\n", buf);
}

bool IsIcmpSocket(int s, uint64_t* serial) {
    std::lock_guard lk(g_netMutex);
    auto it = g_sockets.find(s);
    if (it == g_sockets.end() || it->second.type != kSockRaw || it->second.proto != kProtoIcmp) return false;
    if (serial) *serial = it->second.serial;
    return true;
}

uint16_t InetChecksum(const uint8_t* p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += static_cast<uint32_t>((p[i] << 8) | p[i + 1]);
    if (n & 1) sum += static_cast<uint32_t>(p[n - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

void EchoWorker(int s, uint64_t serial, std::vector<uint8_t> request, uint32_t dest) {
    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        IcmpLog("IcmpCreateFile failed (%lu)", GetLastError());
        return;
    }
    const std::vector<uint8_t> payload(request.begin() + 8, request.end());
    std::vector<uint8_t> replyBuf(sizeof(ICMP_ECHO_REPLY) + payload.size() + 8 + 16 /* IO_STATUS_BLOCK */ + 64);
    const DWORD count = IcmpSendEcho2(icmp, nullptr, nullptr, nullptr, dest,
                                      payload.empty() ? nullptr : const_cast<uint8_t*>(payload.data()),
                                      static_cast<WORD>(payload.size()), nullptr, replyBuf.data(),
                                      static_cast<DWORD>(replyBuf.size()), 2000);
    const DWORD err = count ? 0 : GetLastError();
    IcmpCloseHandle(icmp);
    const auto* reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuf.data());
    const auto* d = reinterpret_cast<const uint8_t*>(&dest);
    if (!count || reply->Status != IP_SUCCESS) {
        IcmpLog("echo to %u.%u.%u.%u: no reply (error %lu, status %lu)", d[0], d[1], d[2], d[3], err,
                count ? reply->Status : 0);
        return;
    }
    // ICMP echo reply: type 0, code 0, checksum, then the request's id, sequence and data.
    std::vector<uint8_t> icmpReply(request);
    icmpReply[0] = kIcmpEchoReply;
    icmpReply[1] = 0;
    icmpReply[2] = icmpReply[3] = 0;
    const uint16_t ck = InetChecksum(icmpReply.data(), icmpReply.size());
    icmpReply[2] = static_cast<uint8_t>(ck >> 8);
    icmpReply[3] = static_cast<uint8_t>(ck);
    // In front of it the IPv4 header, from the sender to this machine's address (sceNetCtlGetInfo's).
    const uint16_t total = static_cast<uint16_t>(20 + icmpReply.size());
    const auto* src = reinterpret_cast<const uint8_t*>(&reply->Address);
    uint8_t ip[20] = {0x45, 0, static_cast<uint8_t>(total >> 8), static_cast<uint8_t>(total), 0, 0, 0, 0,
                      reply->Options.Ttl ? reply->Options.Ttl : static_cast<uint8_t>(64), kProtoIcmp, 0, 0,
                      src[0], src[1], src[2], src[3], 192, 168, 1, 50};
    const uint16_t ipck = InetChecksum(ip, sizeof(ip));
    ip[10] = static_cast<uint8_t>(ipck >> 8);
    ip[11] = static_cast<uint8_t>(ipck);
    Datagram dg;
    dg.data.assign(ip, ip + sizeof(ip));
    dg.data.insert(dg.data.end(), icmpReply.begin(), icmpReply.end());
    dg.fromPort = dg.fromVport = 0;
    std::memcpy(dg.fromAddr, src, 4);
    bool queued = false;
    {
        std::lock_guard lk(g_netMutex);
        auto it = g_sockets.find(s);  // not operator[]: a closed socket stays closed
        if (it != g_sockets.end() && it->second.serial == serial && it->second.inbox.size() < kMaxQueued) {
            it->second.inbox.push_back(std::move(dg));
            queued = true;
        }
    }
    IcmpLog("echo reply from %u.%u.%u.%u in %lu ms (ttl %u)%s", src[0], src[1], src[2], src[3],
            reply->RoundTripTime, reply->Options.Ttl, queued ? ", queued for the game" : ", the socket is gone");
}

// True if this was an echo request on a raw ICMP socket, now on its way.
bool SendIcmpEcho(int s, const void* data, size_t len, const void* to, void* caller) {
    uint64_t serial = 0;
    if (!to || !data || len < 8 || len > 1500 || !IsIcmpSocket(s, &serial)) return false;
    const auto* sa = static_cast<const uint8_t*>(to);
    const auto* req = static_cast<const uint8_t*>(data);
    if (sa[1] != kAfInet || req[0] != kIcmpEchoRequest) return false;
    uint32_t dest;
    std::memcpy(&dest, sa + 4, 4);
    if (!dest) {
        IcmpLog("echo to 0.0.0.0 from %p: unreachable", caller);
        return false;
    }
    IcmpLog("echo request on %d (id %u, seq %u, %zu bytes) to %u.%u.%u.%u from %p", s, (req[4] << 8) | req[5],
            (req[6] << 8) | req[7], len, sa[4], sa[5], sa[6], sa[7], caller);
    const auto* p = static_cast<const uint8_t*>(data);
    std::thread(EchoWorker, s, serial, std::vector<uint8_t>(p, p + len), dest).detach();
    return true;
}

// --- online co-op: other players' games (hle/net_remote.h) ------------------------------------

std::atomic<p2pnet::RemoteSend> g_remoteSend{nullptr};

// After the loopback path declined it (not for 127.x): a P2P datagram for another player's game.
bool SendRemote(int s, const void* data, size_t len, const void* to) {
    const p2pnet::RemoteSend send = g_remoteSend.load(std::memory_order_relaxed);
    if (!send || !to) return false;
    uint16_t srcVport = 0;
    {
        std::lock_guard lk(g_netMutex);
        auto it = g_sockets.find(s);
        // Datagram sockets only: a raw ICMP socket's echo (the server ping) is the ping's business.
        if (it == g_sockets.end() || it->second.type == kSockRaw) return false;
        srcVport = it->second.vport;
    }
    const auto* b = static_cast<const uint8_t*>(to);
    uint32_t ip;
    std::memcpy(&ip, b + 4, 4);
    return send(ip, PortOf(to), srcVport, VportOf(to), data, len);
}

}  // namespace
}  // namespace hle::net

namespace hle::p2pnet {

void SetRemoteSend(RemoteSend send) { net::g_remoteSend.store(send); }

bool DeliverRemote(uint32_t ip, uint16_t port, uint16_t srcVport, uint16_t dstVport, const uint8_t* data, size_t len) {
    using namespace hle::net;
    std::lock_guard lk(g_netMutex);
    // The game's P2P socket: bound to that virtual port (else to that port, as a plain UDP socket).
    Socket* target = nullptr;
    for (auto& [id, sock] : g_sockets)
        if (sock.vport == dstVport && sock.port != 0) {
            target = &sock;
            break;
        }
    if (!target)
        for (auto& [id, sock] : g_sockets)
            if (sock.port == dstVport) {
                target = &sock;
                break;
            }
    if (!target) return false;
    if (target->inbox.size() >= kMaxQueued) {  // a full receive buffer: dropped, as UDP does
        NoteDropped(-1, "remote", len);
        return true;
    }
    Datagram d;
    d.data.assign(data, data + len);
    d.fromPort = port;
    d.fromVport = srcVport;
    std::memcpy(d.fromAddr, &ip, 4);
    target->inbox.push_back(std::move(d));
    return true;
}

}  // namespace hle::p2pnet

namespace hle::net {

// --- POSIX ------------------------------------------------------------------------

int PS4_SYSV_ABI socket(int family, int type, int proto) {
    const int id = NewSocket();
    if (id < 0) return PosixFail(24);  // EMFILE
    NetLog("posix socket", id, "family " + std::to_string(family) + " type " + std::to_string(type) + " proto " +
                                   std::to_string(proto));
    SetKind(id, type, proto);
    return id;
}
int PS4_SYSV_ABI bind(int s, const void* addr, uint32_t) {
    NetLog("posix bind", s, Addr(addr));
    BindSocket(s, addr);
    return 0;
}
int64_t PS4_SYSV_ABI recvfrom(int s, void* buf, size_t len, int flags, void* from, uint32_t* fromLen) {
    // The game reads its ping's echo replies here too (polling, as for its loopback datagrams).
    const int64_t n = RecvLocal(s, buf, len, from, fromLen, flags);
    return n >= 0 ? n : PosixFail(kEWouldBlock);
}
int64_t PS4_SYSV_ABI recv(int s, void* buf, size_t len, int flags) { return recvfrom(s, buf, len, flags, nullptr, nullptr); }
int64_t PS4_SYSV_ABI sendto(int s, const void* data, size_t len, int, const void* addr, uint32_t) {
    if (SendLocal(s, data, len, addr)) return static_cast<int64_t>(len);
    if (SendRemote(s, data, len, addr)) return static_cast<int64_t>(len);
    if (online::Enabled() && SendIcmpEcho(s, data, len, addr, __builtin_return_address(0)))
        return static_cast<int64_t>(len);
    NetLog("posix sendto", s, std::to_string(len) + " bytes to " + Addr(addr) + " -> unreachable");
    return PosixFail(kENetUnreach);
}

// --- libSceNet ----------------------------------------------------------------------

int PS4_SYSV_ABI sceNetInit() { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceNetTerm() { return kernel::SCE_OK; }
int* PS4_SYSV_ABI sceNetErrnoLoc() { return NetErrno(); }
int PS4_SYSV_ABI sceNetPoolCreate(const char*, int, int) { return g_nextId++; }
int PS4_SYSV_ABI sceNetPoolDestroy(int) { return kernel::SCE_OK; }

int PS4_SYSV_ABI sceNetSocket(const char* name, int family, int type, int proto) {
    const int id = NewSocket();
    NetLog("socket", id, std::string(name ? name : "") + " family " + std::to_string(family) + " type " +
                             std::to_string(type) + " proto " + std::to_string(proto));
    SetKind(id, type, proto);
    return id;
}
int PS4_SYSV_ABI sceNetSocketClose(int s) {
    CloseSocket(s);
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceNetBind(int s, const void* addr, uint32_t) {
    NetLog("bind", s, Addr(addr));
    BindSocket(s, addr);
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceNetListen(int s, int backlog) {
    NetLog("listen", s, std::to_string(backlog));
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceNetSetsockopt(int, int, int, const void*, uint32_t) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceNetConnect(int s, const void* addr, uint32_t) {
    NetLog("connect", s, Addr(addr) + " -> unreachable");
    return SceFail(kENetUnreach);
}
int PS4_SYSV_ABI sceNetAccept(int, void*, uint32_t*) { return SceFail(kEWouldBlock); }
int PS4_SYSV_ABI sceNetRecv(int s, void* buf, size_t len, int flags) {
    const int64_t n = RecvLocal(s, buf, len, nullptr, nullptr, flags);
    return n >= 0 ? static_cast<int>(n) : SceFail(kEWouldBlock);
}
int PS4_SYSV_ABI sceNetSend(int, const void*, size_t, int) { return SceFail(kENetUnreach); }

int PS4_SYSV_ABI sceNetEpollCreate(const char*, int) {
    const int id = g_nextId++;
    std::lock_guard lk(g_netMutex);
    g_epolls[id];
    return id;
}
int PS4_SYSV_ABI sceNetEpollDestroy(int ep) {
    std::lock_guard lk(g_netMutex);
    g_epolls.erase(ep);
    return kernel::SCE_OK;
}

// SceNetEpollEvent: events, reserved, ident, user data.
struct EpollEvent {
    uint32_t events, reserved;
    uint64_t ident;
    uint64_t data;
};

int PS4_SYSV_ABI sceNetEpollControl(int ep, int op, int id, EpollEvent* ev) {
    constexpr int kAdd = 1, kMod = 2, kDel = 3;
    std::lock_guard lk(g_netMutex);
    auto& entries = g_epolls[ep];
    std::erase_if(entries, [id](const EpollEntry& e) { return e.socket == id; });
    if ((op == kAdd || op == kMod) && ev) entries.push_back({id, ev->events, ev->data});
    (void)kDel;
    return kernel::SCE_OK;
}

// Reports sockets with queued datagrams as readable. Long or infinite waits
// are capped so the caller keeps looping.
int PS4_SYSV_ABI sceNetEpollWait(int ep, EpollEvent* events, int maxEvents, int timeoutUs) {
    // Microsecond timeouts, not truncated to whole milliseconds, and no oversleeping the deadline:
    // the game host thread sends each simulation tick when a 33,333 us wait ends, and a tick that
    // drifted against the frames gave 0 then 2 sim steps (camera snaps).
    const auto deadline = std::chrono::steady_clock::now() +
                          (timeoutUs < 0 ? std::chrono::duration_cast<std::chrono::microseconds>(kEpollMaxSleep)
                                         : std::min<std::chrono::microseconds>(std::chrono::microseconds(timeoutUs),
                                                                               kEpollMaxSleep));
    for (;;) {
        int n = 0;
        {
            std::lock_guard lk(g_netMutex);
            for (const auto& e : g_epolls[ep]) {
                if (n >= maxEvents) break;
                auto it = g_sockets.find(e.socket);
                if (!(e.events & kEpollIn) || it == g_sockets.end() || it->second.inbox.empty()) continue;
                if (events) events[n] = {kEpollIn, 0, static_cast<uint64_t>(e.socket), e.data};
                ++n;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (n > 0 || now >= deadline) return n;
        if (deadline - now > std::chrono::microseconds(1500)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else std::this_thread::yield();
    }
}

uint16_t PS4_SYSV_ABI sceNetHtons(uint16_t v) { return static_cast<uint16_t>((v << 8) | (v >> 8)); }

int PS4_SYSV_ABI sceNetInetPton(int af, const char* src, void* dst) {
    unsigned a, b, c, d;
    if (af != kAfInet || !src || !dst || std::sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    const uint8_t bytes[4] = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c),
                              static_cast<uint8_t>(d)};
    std::memcpy(dst, bytes, 4);
    return 1;
}

const char* PS4_SYSV_ABI sceNetInetNtop(int af, const void* src, char* dst, uint32_t size) {
    if (af != kAfInet || !src || !dst) return nullptr;
    const auto* b = static_cast<const uint8_t*>(src);
    std::snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return dst;
}

int PS4_SYSV_ABI sceNetResolverCreate(const char*, int, int) { return g_nextId++; }
int PS4_SYSV_ABI sceNetResolverDestroy(int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceNetResolverStartNtoa(int, const char* host, void* addr, int, int, int) {
    std::fprintf(stderr, "[net] resolve %s\n", host ? host : "?");
    if (online::Enabled()) {  // hle/online.h
        // As a real resolver: the game's own server name is the community server; other host
        // names resolve through DNS. LBP3 also looks up its CDN *URL* (Refresh's network_settings.nws
        // gives "https://play.lbppc.com/lbp" as CDNHostName and TelemetryServer) for its server
        // ping: a URL resolves its host, as LBPPC's resolver does (net_resolver.cpp, GPL-2.0-or-later).
        std::string name = host ? host : "";
        if (const auto scheme = name.find("://"); scheme != std::string::npos) {
            name = name.substr(scheme + 3);
            name = name.substr(0, name.find_first_of("/:?#"));
        }
        if (!name.empty() && name.back() == '.') name.pop_back();
        uint32_t ip = 0;
        if (!name.empty() && name.find_first_of("/:") == std::string::npos)
            ip = name.size() >= 9 && name.compare(name.size() - 9, 9, ".scee.com") == 0 ? online::ServerIPv4()
                                                                                    : online::ResolveIPv4(name);
        const auto* b = reinterpret_cast<const uint8_t*>(&ip);
        std::fprintf(stderr, "[net] resolve %s: %s -> %u.%u.%u.%u\n", host ? host : "?", name.c_str(), b[0], b[1],
                     b[2], b[3]);
        if (ip && addr) {
            std::memcpy(addr, &ip, sizeof(ip));
            return kernel::SCE_OK;
        }
        return SceFail(kETimedOut);
    }
    UnreachableDelay("sceNetResolverStartNtoa");
    return SceFail(kETimedOut);
}

}  // namespace hle::net

namespace hle {

void RegisterNetOffline(core::Linker& linker) {
    using namespace net;
    const char* posix = "libScePosix";
    HLE_REG(linker, posix, socket);
    HLE_REG(linker, posix, bind);
    HLE_REG(linker, posix, recv);
    HLE_REG(linker, posix, recvfrom);
    HLE_REG(linker, posix, sendto);
    const char* lib = "libSceNet";
    HLE_REG(linker, lib, sceNetInit);
    HLE_REG(linker, lib, sceNetTerm);
    HLE_REG(linker, lib, sceNetErrnoLoc);
    HLE_REG(linker, lib, sceNetPoolCreate);
    HLE_REG(linker, lib, sceNetPoolDestroy);
    HLE_REG(linker, lib, sceNetSocket);
    HLE_REG(linker, lib, sceNetSocketClose);
    HLE_REG(linker, lib, sceNetBind);
    HLE_REG(linker, lib, sceNetListen);
    HLE_REG(linker, lib, sceNetSetsockopt);
    HLE_REG(linker, lib, sceNetConnect);
    HLE_REG(linker, lib, sceNetAccept);
    HLE_REG(linker, lib, sceNetRecv);
    HLE_REG(linker, lib, sceNetSend);
    HLE_REG(linker, lib, sceNetEpollCreate);
    HLE_REG(linker, lib, sceNetEpollDestroy);
    HLE_REG(linker, lib, sceNetEpollControl);
    HLE_REG(linker, lib, sceNetEpollWait);
    HLE_REG(linker, lib, sceNetHtons);
    HLE_REG(linker, lib, sceNetInetPton);
    HLE_REG(linker, lib, sceNetInetNtop);
    HLE_REG(linker, lib, sceNetResolverCreate);
    HLE_REG(linker, lib, sceNetResolverDestroy);
    HLE_REG(linker, lib, sceNetResolverStartNtoa);
}

}  // namespace hle
