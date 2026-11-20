// LBP3_SYNC_PROBE=1: logs LBP3's own online sync check, to find where two players' worlds part.
//
// Every 60 world frames each game hashes its players' matrices (eboot.bin+0x1acaa0, called from the
// check at +0x256db6 with the world in rdi and the frame in r14d) and sends frame and hash to the
// others; the receiver (+0x269eb0, called from +0xb822e5 with the game in rdi and the message in rsi)
// compares them with its own record and ends the session on a difference. Both calls are redirected
// through small stubs that log, then do what the game did: "[sync] sent" lines carry each player's
// position and matrix, "[sync] received" lines the other side's frame and hash. Comparing the two
// players' logs gives the first check where they differ, and which player's matrix did.
// Facts about these sites (and the matrix path world+0x120 -> [i] -> +8 -> +0x40 -> +0x10, matrix at
// +0x40) were found by LBPPC (emurphy04, aad6a91; its src/probe.c, read only for these facts); this
// code is ours. Off by default: nothing is patched without the variable (the session log further down
// is on while online).
//
// With each check it also logs the game's online player slots when they change ("[slots]" lines, and
// every 30 s): eboot.bin+0x130d5c0 points to 8 slots of 0x300 bytes, each a player's NpId (+0), join
// state (+0x2f8), when that state began (+0x2f0, the game's monotonic clock in ns, eboot+0x1b1b0) and
// timeout strikes (+0x2fc). States 4 and 5 time out after 200 s, 6 and 8 after 240 s, 10 after 120 s,
// 11 after 60 s (eboot+0x3ac2a0, x4 the constants at +0xe7a4f4..+0xe7a500, or the server's network
// settings at [+0x130d670]+0xe8..+0xf8 when that block's byte +0x45 is 4 or 5); after 5 strikes the
// player is removed (eboot+0x3acef0 -> +0x34e9f0, which ends the connection). Found by us (Ghidra).
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <set>
#include <thread>

#include "core/lbppc_mode.h"
#include "core/log_stamp.h"
#include "core/session_log.h"
#include "core/runner.h"
#include "core/module.h"
#include "hle/abi.h"
#include "hle/online.h"

namespace core {
namespace {

constexpr uint64_t kHashCall = 0x256db6, kHashFn = 0x1acaa0;     // call 0x1acaa0 (hash of the players)
constexpr uint64_t kReceiveCall = 0xb822e5, kReceiveFn = 0x269eb0;  // call 0x269eb0 (a sync message arrived)
constexpr uint64_t kFetchCall = 0x3add96, kFetchFn = 0x379ab0;      // call 0x379ab0 (take a session message)

bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return static_cast<const uint8_t*>(p) + n <= static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

template <class T>
bool Read(uint64_t addr, T& out) {
    if (!Readable(reinterpret_cast<const void*>(addr), sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(addr), sizeof(T));
    return true;
}

uint8_t* g_base = nullptr;  // eboot.bin's load address

// LBP3_WATCH_JOINFLAG=1 (diagnostics): a hardware write watch on a joining player's room item flag
// (see LogSlots), to find the code that clears it. Debug register 0 of every thread but the arming one.
std::atomic<uint64_t> g_watchAddr{0};
std::atomic<int> g_watchHits{0};

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    const uint64_t addr = g_watchAddr.load();
    uint8_t now = 0xff;
    if (Readable(reinterpret_cast<const void*>(addr), 1)) now = *reinterpret_cast<const uint8_t*>(addr);
    if (g_watchHits.fetch_add(1) < 40) {
        std::fprintf(stderr, "[watch] %s flag at %llx is now %u, written before %s (thread %lu)\n", core::Stamp().text,
                     static_cast<unsigned long long>(addr), now, core::DescribeAddress(c->Rip).c_str(),
                     GetCurrentThreadId());
        // The callers, by the frame-pointer chain (the game's functions keep rbp): [rbp+8] is the
        // return address, [rbp] the caller's rbp.
        uint64_t rbp = c->Rbp;
        for (int k = 0; k < 16 && rbp; ++k) {
            uint64_t next = 0, ret = 0;
            if (!Read(rbp, next) || !Read(rbp + 8, ret)) break;
            std::fprintf(stderr, "[watch]   #%d %s\n", k, core::DescribeAddress(ret).c_str());
            if (next <= rbp) break;
            rbp = next;
        }
    }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmWatch(uint64_t addr) {
    static std::set<uint64_t> armed;
    static const bool on = [] {
        const char* v = std::getenv("LBP3_WATCH_JOINFLAG");
        if (!v || *v != '1') return false;
        AddVectoredExceptionHandler(1, WatchHandler);
        return true;
    }();
    if (!on || !armed.insert(addr).second) return;
    g_watchAddr = addr;
    std::fprintf(stderr, "[watch] %s watching the join flag at %llx\n", core::Stamp().text,
                 static_cast<unsigned long long>(addr));
    std::thread([addr] {
        const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te{sizeof(te)};
        int n = 0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!t) continue;
            if (SuspendThread(t) != static_cast<DWORD>(-1)) {
                CONTEXT c{};
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(t, &c)) {
                    c.Dr0 = addr;
                    c.Dr7 = (c.Dr7 & ~0xf0003ull) | 1ull | (1ull << 16);  // L0, write, 1 byte
                    if (SetThreadContext(t, &c)) ++n;
                }
                ResumeThread(t);
            }
            CloseHandle(t);
        }
        CloseHandle(snap);
        std::fprintf(stderr, "[watch] armed on %d threads\n", n);
    }).detach();
}

void LogSlots() {
    uint64_t slots = 0;
    if (!g_base || !Read(reinterpret_cast<uint64_t>(g_base + 0x130d5c0), slots) || !slots) return;
    using Clock = int64_t(PS4_SYSV_ABI*)();
    const int64_t now = reinterpret_cast<Clock>(g_base + 0x1b1b0)();
    std::string line, key;
    for (int i = 0; i < 8; ++i) {
        const uint64_t slot = slots + 0x300ull * i;
        int32_t state = 0, strikes = 0;
        int64_t since = 0;
        if (!Read(slot + 0x2f8, state) || state == 0) continue;
        Read(slot + 0x2f0, since);
        Read(slot + 0x2fc, strikes);
        char name[17] = {};
        if (Readable(reinterpret_cast<const void*>(slot), 16)) std::memcpy(name, reinterpret_cast<const void*>(slot), 16);
        for (char& c : name)
            if (c && (c < 0x20 || c > 0x7e)) c = '.';
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  [%d] %s: state %d for %.0f s, strikes %d", i, name, state,
                      (now - since) / 1e9, strikes);
        line += buf;
        key += std::to_string(i) + name + ":" + std::to_string(state) + "/" + std::to_string(strikes) + ";";
    }
    std::string roles;
    for (int k = 0; k < 4; ++k) {
        uint64_t r = 0;
        Read(slots + 0x1800 + 8ull * k, r);
        roles += " " + (r >= slots && r < slots + 0x1800 ? std::to_string((r - slots) / 0x300) : std::string(r ? "?" : "-"));
    }
    key += roles;
    // The room as the role-1 slot (the host) holds it, which decides a state-9 slot's move to 11
    // (eboot+0x3acef0): slot+0x30 = 4 players of 0xb0 (online ID at +0), each with a list at +0x50
    // (count at +0x58) of 16-byte items; 11 once every item of our own player has its byte +8 set.
    std::string room;
    if (uint64_t host = 0; Read(slots + 0x1808, host) && host >= slots && host < slots + 0x1800) {
        for (int k = 0; k < 4; ++k) {
            const uint64_t e = host + 0x30 + 0xb0ull * k;
            char name[17] = {};
            if (!Readable(reinterpret_cast<const void*>(e), 16)) break;
            std::memcpy(name, reinterpret_cast<const void*>(e), 16);
            if (!name[0]) continue;
            for (char& c : name)
                if (c && (c < 0x20 || c > 0x7e)) c = '.';
            uint64_t items = 0;
            uint32_t count = 0;
            Read(e + 0x50, items);
            Read(e + 0x58, count);
            std::string flags;
            for (uint32_t j = 0; j < count && j < 16; ++j) {
                uint64_t first = 0;
                uint8_t flag = 0;
                Read(items + 16ull * j, first);
                Read(items + 16ull * j + 8, flag);
                if (flag) ArmWatch(items + 16ull * j + 8);
                char b[40];
                std::snprintf(b, sizeof(b), " %llx:%u", static_cast<unsigned long long>(first), flag);
                flags += b;
            }
            room += std::string(" {") + name + " n=" + std::to_string(count) + flags + "}";
        }
        key += room;
    }
    // The game's network object ([eboot+0x130d5b8 + 0x80]) and its connection objects (+0x50, count
    // +0x58): the receive loop (eboot+0x378b60) hands each datagram to the first one whose address
    // (+0x34, port +0x38) matches the sender.
    if (uint64_t net = 0; Read(reinterpret_cast<uint64_t>(g_base + 0x130d5b8 + 0x80), net) && net) {
        uint64_t list = 0;
        uint32_t count = 0;
        Read(net + 0x50, list);
        Read(net + 0x58, count);
        std::string conns = " conns n=" + std::to_string(count);
        for (uint32_t j = 0; j < count && j < 8; ++j) {
            uint64_t c = 0;
            uint32_t ip = 0;
            uint16_t port = 0;
            if (!Read(list + 8ull * j, c) || !c) continue;
            Read(c + 0x34, ip);
            Read(c + 0x38, port);
            const auto* b = reinterpret_cast<const uint8_t*>(&ip);
            char s[96];
            std::snprintf(s, sizeof(s), " %llx=%u.%u.%u.%u:%u", static_cast<unsigned long long>(c & 0xffffff), b[0], b[1],
                          b[2], b[3], port);
            conns += s;
        }
        // Its signaling table (+0x8, count +0x10): 0x34-byte entries, online ID (+0), state (+0x24; 1-3
        // live, 4 = ended by us, kept), connection id (+0x28). Mode at +0x4 (1 = it ends connections).
        uint64_t sig = 0;
        uint32_t sigCount = 0, mode = 0;
        Read(net + 0x8, sig);
        Read(net + 0x10, sigCount);
        Read(net + 0x4, mode);
        conns += "; signaling mode " + std::to_string(mode) + " n=" + std::to_string(sigCount);
        for (uint32_t j = 0; j < sigCount && j < 8; ++j) {
            const uint64_t e = sig + 0x34ull * j;
            char name[17] = {};
            if (!Readable(reinterpret_cast<const void*>(e), 0x2c)) break;
            std::memcpy(name, reinterpret_cast<const void*>(e), 16);
            for (char& ch : name)
                if (ch && (ch < 0x20 || ch > 0x7e)) ch = '.';
            int32_t st = 0, conn = 0;
            Read(e + 0x24, st);
            Read(e + 0x28, conn);
            conns += std::string(" ") + name + ":st" + std::to_string(st) + "/c" + std::to_string(conn);
        }
        room += conns;
        key += conns;
    }
    // The transfer manager (eboot+0x12cc4e8; session messages 0x12 announce, 0x14 answer, 0x13 done):
    // its outgoing records (+0x0, count +0x8; 0x30 bytes: id +0, state +0x20) and incoming objects
    // (+0x10, count +0x18; online ID +0, id +0x24, done +0x200). A 0x12 whose player and id match an
    // object already there is ignored (eboot+0x2281e0).
    {
        const uint64_t mgr = reinterpret_cast<uint64_t>(g_base + 0x12cc4e8);
        uint64_t recs = 0, objs = 0;
        uint32_t nrec = 0, nobj = 0;
        Read(mgr, recs);
        Read(mgr + 0x8, nrec);
        Read(mgr + 0x10, objs);
        Read(mgr + 0x18, nobj);
        std::string x = "; xfer out n=" + std::to_string(nrec);
        for (uint32_t j = 0; j < nrec && j < 8; ++j) {
            int32_t id = 0, st = 0;
            Read(recs + 0x30ull * j, id);
            Read(recs + 0x30ull * j + 0x20, st);
            x += " " + std::to_string(id) + ":st" + std::to_string(st);
        }
        x += " in n=" + std::to_string(nobj);
        for (uint32_t j = 0; j < nobj && j < 8; ++j) {
            uint64_t o = 0;
            if (!Read(objs + 8ull * j, o) || !Readable(reinterpret_cast<const void*>(o), 0x202)) continue;
            char name[17] = {};
            std::memcpy(name, reinterpret_cast<const void*>(o), 16);
            for (char& ch : name)
                if (ch && (ch < 0x20 || ch > 0x7e)) ch = '.';
            int32_t id = 0, kind = 0, a8 = 0, b8 = 0;
            uint8_t done = 0, flags = 0;
            Read(o + 0x24, id);
            Read(o + 0x200, done);
            Read(o + 0x38, kind);   // what is transferred (eboot+0x228a10 answers 10 for kind 9, else 0x14)
            Read(o + 0x40, flags);  // its stream; bit 8 = failed (answered 0x14 with "no")
            Read(o + 0xa8, a8);
            Read(o + 0xb8, b8);
            char b[96];
            std::snprintf(b, sizeof(b), "#%d k%d f%02x a8=%d b8=%d", id, kind, flags, a8, b8);
            x += std::string(" ") + name + b + (done ? ":done" : "");
        }
        room += x;
        key += x;
    }
    // The server's network settings (if any), which can replace the timeouts.
    uint64_t settings = 0;
    std::string net;
    if (Read(reinterpret_cast<uint64_t>(g_base + 0x130d670), settings) && settings) {
        uint8_t mode = 0;
        float t[5] = {};
        Read(settings + 0x45, mode);
        Read(settings + 0xe8, t);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "; settings mode %u, timeouts %.0f %.0f %.0f %.0f %.0f", mode, t[0], t[1], t[2],
                      t[3], t[4]);
        net = buf;
        key += net;
    }
    static std::string last;
    static int calls = 0;
    if (key != last || ++calls % 15 == 0) {
        last = key;
        std::fprintf(stderr, "[slots] %s%s; roles%s%s\n", core::Stamp().text, line.empty() ? " none" : line.c_str(),
                     roles.c_str(), net.c_str());
        if (!room.empty()) std::fprintf(stderr, "[slots]   room:%s\n", room.c_str());
    }
}

// LBP3_CONN_PROBE=1 (with the sync probe): a "[conn]" line per connection object once a second, from
// a thread of ours that only reads. Per connection (the list of LogSlots): seconds since the last
// keepalive was read (+0x160) and since +0xc0; unacknowledged messages (+0x188); messages held out of
// order (+0x270); the last delivered numbers (+0x50, +0x68); the send budget and cap (+0x118,
// +0x11c), the growth step (+0x12c), the last notice and cut (+0x120, +0x124, raw); bytes sent in the
// last half second (+0x110); the game's RTT in microseconds (+0xf0). Offsets from the research notes
// (netcode-game-netcode-limits, netcode-send-budget-cuts). For the fatal-datagram runs.
void ConnProbeThread() {
    using Clock = int64_t(PS4_SYSV_ABI*)();
    for (;;) {
        Sleep(1000);
        uint64_t net = 0, list = 0;
        uint32_t count = 0;
        if (!g_base || !Read(reinterpret_cast<uint64_t>(g_base + 0x130d5b8 + 0x80), net) || !net) continue;
        Read(net + 0x50, list);
        Read(net + 0x58, count);
        const int64_t now = reinterpret_cast<Clock>(g_base + 0x1b1b0)();
        for (uint32_t j = 0; j < count && j < 8; ++j) {
            uint64_t c = 0;
            if (!Read(list + 8ull * j, c) || !c || !Readable(reinterpret_cast<const void*>(c), 0x280)) continue;
            char name[17] = {};
            std::memcpy(name, reinterpret_cast<const void*>(c + 0x10), 16);
            for (char& ch : name)
                if (ch && (ch < 0x20 || ch > 0x7e)) ch = '.';
            auto u32 = [&](uint32_t off) { uint32_t v = 0; Read(c + off, v); return v; };
            auto i64 = [&](uint32_t off) { int64_t v = 0; Read(c + off, v); return v; };
            const int64_t ka = i64(0x160), c0 = i64(0xc0), bye = i64(0x168);
            std::fprintf(stderr,
                         "[conn] %s %s: keepalive %.2f s ago, +0xc0 %.2f s ago, bye %d; unacked %u, held %u; "
                         "delivered %u/%u; budget %u cap %u step %u notice %08x cut %08x; sent/0.5s %u; rtt %u us\n",
                         core::Stamp().text, name, ka ? (now - ka) / 1e9 : -1.0, c0 ? (now - c0) / 1e9 : -1.0, bye != 0,
                         u32(0x188), u32(0x270), u32(0x50), u32(0x68), u32(0x118), u32(0x11c), u32(0x12c), u32(0x120),
                         u32(0x124), u32(0x110), u32(0xf0));
        }
    }
}

// LBP3_CONN_PROBE=1: the RTT sample the game takes when a message is acknowledged (eboot+0x37f0f1:
// now - the message's first-send time at +0x88, through eboot+0x1b2a0 at +0x37f100, added to the
// current half-second bucket). Logs every sample above 5 s with the message (0xb8 bytes in the
// connection's unacknowledged list: number +0, flags +2 and +3, stream +0x1c, last send +0x80, first
// send +0x88) and the acknowledgement entry (number +0, flags +2 and +3). Returns what the game's
// own function returns.
std::atomic<bool> g_connProbe{false};
// The session log's count of the game's RTT samples, per connection object (8 at most).
struct RttCount {
    std::atomic<uint64_t> conn{0};
    std::atomic<uint32_t> samples{0}, negative{0};
};
RttCount g_rttCounts[8];

float PS4_SYSV_ABI LogRttSample(int64_t ns, uint64_t msg, uint64_t conn, uint64_t ack) {
    using Seconds = float(PS4_SYSV_ABI*)(int64_t);
    for (RttCount& c : g_rttCounts) {
        uint64_t have = c.conn.load(std::memory_order_relaxed);
        if (have == 0 && c.conn.compare_exchange_strong(have, conn)) have = conn;
        if (have != conn) continue;
        c.samples.fetch_add(1, std::memory_order_relaxed);
        if (ns < 0) c.negative.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    // LBP3_RTT_HIST=1 (a test): every 500 samples, how the raw samples of this process spread.
    static const bool hist = [] {
        const char* v = std::getenv("LBP3_RTT_HIST");
        return v && *v == '1';
    }();
    if (hist) {
        static std::atomic<uint32_t> bins[7];
        static std::atomic<uint32_t> total{0};
        const int64_t ms = 1'000'000;
        const int b = ns < 0 ? 0 : ns == 0 ? 1 : ns < ms ? 2 : ns < 8 * ms ? 3 : ns < 30 * ms ? 4 : ns < 40 * ms ? 5 : 6;
        bins[b].fetch_add(1, std::memory_order_relaxed);
        if (total.fetch_add(1, std::memory_order_relaxed) % 500 == 499)
            std::fprintf(stderr,
                         "[rtt-hist] %s raw samples so far: below 0: %u, exactly 0: %u, under 1 ms: %u, 1 to 8 ms: %u, "
                         "8 to 30 ms: %u, 30 to 40 ms: %u, 40 ms and up: %u\n",
                         core::Stamp().text, bins[0].load(), bins[1].load(), bins[2].load(), bins[3].load(),
                         bins[4].load(), bins[5].load(), bins[6].load());
    }
    // LBP3_RTT_FLOOR_MS=<n> (a test): a sample below n ms, negative ones included, counts as n ms.
    // In the LBPPC co-op mode (core/lbppc_mode.h) one frame by default: LBPPC players have no gap-free
    // delivery, and on a path faster than a frame the port's clock can show the game an ack before its
    // message was sent, after which any lost datagram brings the 10 s kick (#1 tickets 13 and 32; the
    // floor is #1's chosen first step). LBP3_RTT_FLOOR_MS=0 turns it off there.
    static const int64_t floorNs = [] {
        const char* v = std::getenv("LBP3_RTT_FLOOR_MS");
        if (v && *v) return static_cast<int64_t>(std::atof(v) * 1e6);
        if (core::lbppc::Mode()) {
            std::fprintf(stderr, "[rtt] the LBPPC co-op mode: an RTT sample under one frame counts as one frame\n");
            return int64_t{33'333'333};
        }
        return int64_t{0};
    }();
    if (floorNs > 0 && ns < floorNs) ns = floorNs;
    // The session log only counts: the sample goes to the game as it is.
    if (!g_connProbe.load(std::memory_order_relaxed)) return reinterpret_cast<Seconds>(g_base + 0x1b2a0)(ns);
    // LBP3_RTT_CLAMP=1 (a test): a negative sample counts as 0. The game's function takes the
    // nanoseconds as unsigned, so -100 ns reads as 18 billion seconds.
    static const bool clamp = [] {
        const char* v = std::getenv("LBP3_RTT_CLAMP");
        return v && *v == '1';
    }();
    if (clamp && ns < 0) {
        static std::atomic<int> clamped{0};
        const int n = clamped.fetch_add(1) + 1;
        if (n <= 5 || n % 100 == 0)
            std::fprintf(stderr, "[rtt] %s a sample of %lld ns counted as 0 (LBP3_RTT_CLAMP; %d so far)\n",
                         core::Stamp().text, static_cast<long long>(ns), n);
        ns = 0;
    }
    const float s = reinterpret_cast<Seconds>(g_base + 0x1b2a0)(ns);
    static std::atomic<int> logged{0};
    if ((s > 5.0f || s < 0.0f) && logged.fetch_add(1) < 400) {
        char name[17] = {};
        if (Readable(reinterpret_cast<const void*>(conn + 0x10), 16))
            std::memcpy(name, reinterpret_cast<const void*>(conn + 0x10), 16);
        uint16_t num = 0, ackNum = 0;
        uint8_t f2 = 0, f3 = 0, a2 = 0, a3 = 0;
        uint32_t stream = 0, left = 0, unacked = 0;
        int64_t last = 0, first = 0;
        uint64_t list = 0;
        Read(msg, num); Read(msg + 2, f2); Read(msg + 3, f3); Read(msg + 0x1c, stream);
        Read(msg + 0x80, last); Read(msg + 0x88, first); Read(msg + 0x90, left);
        Read(ack, ackNum); Read(ack + 2, a2); Read(ack + 3, a3);
        Read(conn + 0x180, list); Read(conn + 0x188, unacked);
        std::fprintf(stderr,
                     "[rtt] %s %s: sample %.3f s (%lld ns); message %u stream %u flags %u/%u, first send %lld, last send "
                     "%lld, +0x90 %u, entry %llu of %u; ack %u flags %u/%u\n",
                     core::Stamp().text, name, s, static_cast<long long>(ns), num, stream, f2, f3,
                     static_cast<long long>(first), static_cast<long long>(last), left,
                     static_cast<unsigned long long>(list ? (msg - list) / 0xb8 : 0), unacked, ackNum, a2, a3);
    }
    return s;
}

void PS4_SYSV_ABI LogSent(uint64_t world, uint32_t hash, uint32_t frame) {
    LogSlots();
    uint32_t count = 0;
    uint64_t players = 0;
    Read(world + 0x128, count);
    Read(world + 0x120, players);
    static bool fpLogged = false;
    if (!fpLogged) {
        // The thread's floating-point control state at the check: game code must run with the PS4's
        // (MXCSR 0x9fc0, x87 0x037f; kernel_pthread.cpp SetGuestFpState), else results depend on it.
        fpLogged = true;
        unsigned int mxcsr = 0;
        unsigned short fcw = 0;
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
        __asm__ volatile("fnstcw %0" : "=m"(fcw));
        std::fprintf(stderr, "[sync] the sync check runs with MXCSR %#x, x87 control %#x%s\n", mxcsr, fcw,
                     mxcsr == 0x9fc0 && fcw == 0x037f ? "" : " (NOT the PS4's 0x9fc0 / 0x37f)");
    }
    std::fprintf(stderr, "[sync] %s sent frame %u hash %08x, %u player(s)\n", core::Stamp().text, frame, hash, count);
    for (uint32_t i = 0; i < count && i < 4; ++i) {
        uint64_t p = 0;
        if (!Read(players + 8 * i, p) || !Read(p + 8, p) || !Read(p + 0x40, p) || !Read(p + 0x10, p)) continue;
        uint32_t m[16];
        if (!Read(p + 0x40, m)) continue;
        float f[16];
        std::memcpy(f, m, sizeof(f));
        char hex[16 * 9 + 1] = {};
        for (int k = 0; k < 16; ++k) std::snprintf(hex + 9 * k, 10, " %08x", m[k]);
        std::fprintf(stderr, "[sync]   player %u at %.6f %.6f %.6f;%s\n", i, f[12], f[13], f[14], hex);
    }
}

void PS4_SYSV_ABI LogReceived(uint64_t, uint64_t message) {
    uint32_t frame = 0, hash = 0, session = 0;
    char id[0x24 + 1] = {};
    Read(message + 0x2c, frame);
    Read(message + 0x30, hash);
    Read(message + 0x34, session);
    if (Readable(reinterpret_cast<const void*>(message), 0x24)) std::memcpy(id, reinterpret_cast<const void*>(message), 0x24);
    for (char& c : id)
        if (c && (c < 0x20 || c > 0x7e)) c = '.';
    std::fprintf(stderr, "[sync] %s received frame %u hash %08x from \"%s\" (session %u)\n", core::Stamp().text, frame,
                 hash, id, session);
}

// LBP3_SYNC_PROBE=2: every session message the main loop takes from a player's connection
// (eboot+0x3add00 -> +0x379ab0(net, channel, the player's slot, out, ...); out+0x30 data, +0x38 length).
void PS4_SYSV_ABI LogMessage(int32_t channel, uint64_t slot, uint64_t out) {
    static std::atomic<int> lines{0};
    if (lines.fetch_add(1) >= 20000) return;
    char name[17] = {};
    if (Readable(reinterpret_cast<const void*>(slot), 16)) std::memcpy(name, reinterpret_cast<const void*>(slot), 16);
    for (char& c : name)
        if (c && (c < 0x20 || c > 0x7e)) c = '.';
    uint64_t data = 0;
    uint32_t len = 0;
    Read(out + 0x30, data);
    Read(out + 0x38, len);
    char hex[3 * 24 + 1] = {};
    if (Readable(reinterpret_cast<const void*>(data), std::min<uint32_t>(len, 24)))
        for (uint32_t k = 0; k < len && k < 24; ++k)
            std::snprintf(hex + 3 * k, 4, " %02x", reinterpret_cast<const uint8_t*>(data)[k]);
    std::fprintf(stderr, "[msg] %s from %s ch %d len %u:%s\n", core::Stamp().text, name, channel, len, hex);
}

// Executable memory within reach of a rel32 call from the image.
uint8_t* AllocNear(const uint8_t* anchor, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t start = reinterpret_cast<uintptr_t>(anchor) & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000u; d += gran) {
        for (const uintptr_t at : {start - d, start + d}) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

void Put(uint8_t*& o, std::initializer_list<uint8_t> bytes) {
    for (const uint8_t b : bytes) *o++ = b;
}
void Put64(uint8_t*& o, uint64_t v) {
    std::memcpy(o, &v, 8);
    o += 8;
}

// Redirects the 5-byte "call rel32" at `site` (which must call `expectTarget`) to `stub`.
bool Redirect(Module& m, uint64_t site, uint64_t expectTarget, const uint8_t* stub) {
    uint8_t* at = m.Addr(site);
    int32_t rel;
    std::memcpy(&rel, at + 1, 4);
    if (at[0] != 0xe8 || m.Addr(site + 5) + rel != m.Addr(expectTarget)) {
        std::fprintf(stderr, "[sync] eboot.bin+0x%llx is not the expected call: probe not installed\n",
                     static_cast<unsigned long long>(site));
        return false;
    }
    const int64_t d = stub - (at + 5);
    if (d != static_cast<int32_t>(d)) return false;
    const int32_t nrel = static_cast<int32_t>(d);
    std::memcpy(at + 1, &nrel, 4);
    return true;
}

// The RTT stub: LogRttSample(rdi = ns, rbx = the message, r12 = the connection, r14 = the ack).
bool InstallRttStub(Module& m, uint8_t* rtt) {
    static bool done = false, ok = false;
    if (done) return ok;
    done = true;
    uint8_t* q = rtt;
    Put(q, {0x48, 0x83, 0xec, 0x08});  // sub rsp, 8
    Put(q, {0x48, 0x89, 0xde});        // mov rsi, rbx
    Put(q, {0x4c, 0x89, 0xe2});        // mov rdx, r12
    Put(q, {0x4c, 0x89, 0xf1});        // mov rcx, r14
    Put(q, {0x48, 0xb8});              // mov rax, imm64
    Put64(q, reinterpret_cast<uint64_t>(&LogRttSample));
    Put(q, {0xff, 0xd0});              // call rax
    Put(q, {0x48, 0x83, 0xc4, 0x08});  // add rsp, 8
    Put(q, {0xc3});                    // ret
    FlushInstructionCache(GetCurrentProcess(), rtt, 64);
    return ok = Redirect(m, 0x37f100, 0x1b2a0, rtt);
}

// ---- The session log: the game's side (core/session_log.h) ----
//
// Always on while online. A thread of ours reads the game's memory ten times a second and prints
//   - a "[ses] ... players:" line when a player slot changes state (the table of LogSlots, and on the
//     host the host thread's own table: eboot+0x1322760, four slots of 0x140 at +0x500, state +0x130);
//   - a "[ses] ... join block" line when the "simulation running" byte ([eboot+0x130d5e8]+0x45) goes
//     to 0 and back, and every 2 s in between: who hasn't reported the game state loaded (the map at
//     +0x2d8 of the host thread's object +0xa10; a tree of nodes with left, parent, right at +0, +8,
//     +0x10, "is the head" at +0x19, the "loaded" byte at +0x40 and the slot at +0x48) and the
//     resource transfer manager's places (eboot+0x1315b18: 32 sending places of 0x618 at +0x8f8,
//     asker +0xc8, length +0xf0, sent +0xf4; 8 download places of 0x108 at +0xb8, size +0x8,
//     received +0x100);
//   - a "[ses] ... catch-up loading screen" line when its flag (eboot+0x12c9454) changes;
//   - every 5 s in a session, a "[ses-win]" line (flips, simulation steps, flips with no step and the
//     longest run of them; on the host the frames it made and the INPUTS that reached it) and a
//     "[ses-conn]" line per connection (send budget, cuts, the game's RTT and its samples).
// Three calls of the game are redirected, only while online: the RTT sample (counted, never changed),
// and on the host thread the call that restarts a slot's timer at every INPUTS message (eboot+0x39e822)
// and the call that sends a frame it built to everyone (eboot+0x39f09e). Offsets from the research notes
// (docs/research/netcode-*.md). LBP3_SESSION_LOG=0 turns it all off.
using SesClock = std::chrono::steady_clock;
int64_t SesNowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(SesClock::now().time_since_epoch()).count();
}
constexpr int64_t kSesHourUs = 3600ll * 1000 * 1000;

struct FlipCounts {
    std::atomic<uint32_t> flips{0}, steps{0}, noStep{0}, multi{0}, longestRun{0}, jumps{0};
} g_flip;
std::atomic<uint32_t> g_socketDrops{0};

// Test settings of the session log's thread (the set-conditions runs; all off without their variable):
//   LBP3_TEST_FREEZE=1: a number of milliseconds written to analysis\freeze_cmd.txt (in the working
//     directory) stops the game's frame thread for that long at its next flip, as a shader build does.
//     The link's thread and the others run on.
//   LBP3_TEST_CATCHUP_FRAMES=n: FramesBeforeAgressiveCatchup in the game's network settings
//     ([eboot+0x130d670]+0x68, 30 by default) is kept at n.
//   LBP3_TEST_KICK_SECONDS=n: the game's kick limit (the float at eboot+0x1061c4c, 10.0) is set to n.
std::atomic<int> g_freezeMs{0};
int TestInt(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : 0;
}
void SesTestSettings() {
    static const bool freeze = TestInt("LBP3_TEST_FREEZE") == 1;
    static const int catchUp = TestInt("LBP3_TEST_CATCHUP_FRAMES");
    if (freeze) {
        if (std::FILE* f = std::fopen("analysis/freeze_cmd.txt", "r")) {
            int ms = 0;
            const bool ok = std::fscanf(f, "%d", &ms) == 1;
            std::fclose(f);
            std::remove("analysis/freeze_cmd.txt");
            if (ok && ms > 0 && ms <= 60000) g_freezeMs = ms;
        }
    }
    if (catchUp > 0) {
        uint64_t settings = 0;
        int32_t have = 0;
        if (Read(reinterpret_cast<uint64_t>(g_base + 0x130d670), settings) && settings && Read(settings + 0x68, have) &&
            have != catchUp && have > 0 && have <= 1000) {
            std::memcpy(reinterpret_cast<void*>(settings + 0x68), &catchUp, sizeof(catchUp));
            std::fprintf(stderr, "[ses] %s LBP3_TEST_CATCHUP_FRAMES: FramesBeforeAgressiveCatchup was %d, now %d (a test)\n",
                         core::Stamp().text, have, catchUp);
        }
    }
}
std::atomic<core::seslog::LinkRtt> g_linkRtt{nullptr};

// The host thread's side: INPUTS as they reach the game, per slot of its table, and the frames made.
struct InputCounts {
    std::atomic<int64_t> lastUs{0};
    std::atomic<uint32_t> count{0}, longestGapUs{0};
} g_inputs[4];
struct FrameCounts {
    std::atomic<int64_t> lastUs{0};
    std::atomic<uint32_t> made{0}, longestGapUs{0}, waits{0}, catchUp{0};
} g_frames;

void RaiseMax(std::atomic<uint32_t>& a, uint32_t v) {
    uint32_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

// Host thread: an INPUTS message from the PC of this slot is being handled.
void PS4_SYSV_ABI NoteInputs(uint64_t slot) {
    const uint64_t first = reinterpret_cast<uint64_t>(g_base) + 0x1322760 + 0x500;
    if (slot < first || slot >= first + 4 * 0x140) return;
    InputCounts& c = g_inputs[(slot - first) / 0x140];
    const int64_t now = SesNowUs();
    const int64_t last = c.lastUs.exchange(now, std::memory_order_relaxed);
    if (last && now - last < kSesHourUs) RaiseMax(c.longestGapUs, static_cast<uint32_t>(now - last));
    c.count.fetch_add(1, std::memory_order_relaxed);
}

// Host thread: a frame for everyone was built and is being sent. A wait is a pause of more than 50 ms
// between two frames (they are 33.3 ms apart); a catch-up frame comes within 10 ms of the one before.
void PS4_SYSV_ABI NoteFrame() {
    const int64_t now = SesNowUs();
    const int64_t last = g_frames.lastUs.exchange(now, std::memory_order_relaxed);
    if (last && now - last < kSesHourUs) {
        RaiseMax(g_frames.longestGapUs, static_cast<uint32_t>(now - last));
        if (now - last > 50000) g_frames.waits.fetch_add(1, std::memory_order_relaxed);
        if (now - last < 10000) g_frames.catchUp.fetch_add(1, std::memory_order_relaxed);
    }
    g_frames.made.fetch_add(1, std::memory_order_relaxed);
}

std::string SesName(uint64_t at) {
    char name[17] = {};
    if (Readable(reinterpret_cast<const void*>(at), 16)) std::memcpy(name, reinterpret_cast<const void*>(at), 16);
    for (char& c : name)
        if (c && (c < 0x20 || c > 0x7e)) c = '.';
    return name;
}

// Each player's last state as the session log's thread saw it: the game clears a slot before it ends
// the connection, so the line at that moment needs what was there.
struct LastState {
    std::string name;
    int state = 0;
    int64_t us = 0;
};
std::mutex g_lastStateMutex;
LastState g_lastState[8];

// The game's table of the other players: "[i] name state n", and the same for the host thread's own.
std::string SesPlayers(bool& any) {
    std::string out;
    any = false;
    uint64_t slots = 0;
    if (Read(reinterpret_cast<uint64_t>(g_base + 0x130d5c0), slots) && slots) {
        for (int i = 0; i < 8; ++i) {
            int32_t state = 0;
            if (!Read(slots + 0x300ull * i + 0x2f8, state) || state == 0) continue;
            any = true;
            const std::string name = SesName(slots + 0x300ull * i);
            out += " [" + std::to_string(i) + "] " + name + " state " + std::to_string(state);
            std::lock_guard lk(g_lastStateMutex);
            g_lastState[i] = {name, state, SesNowUs()};
        }
    }
    if (out.empty()) out = " none";
    // The host thread's table means something only while this PC hosts: its own INPUTS reach it then.
    std::string host;
    const uint64_t h = reinterpret_cast<uint64_t>(g_base) + 0x1322760 + 0x500;
    const int64_t own = g_frames.lastUs.load(std::memory_order_relaxed);
    for (int i = 0; i < 4 && own && SesNowUs() - own < 30000000; ++i) {
        int32_t state = 0;
        if (!Read(h + 0x140ull * i + 0x130, state) || state == 0) continue;
        host += " [" + std::to_string(i) + "] state " + std::to_string(state);
    }
    if (!host.empty()) out += "; the host thread's slots:" + host;
    return out;
}

// The host thread's game state exchange: who has reported the state loaded and who is awaited ("" = no
// exchange is open).
std::string SesExchange() {
    std::string out;
    const uint64_t hostObj = reinterpret_cast<uint64_t>(g_base) + 0x1322760;
    uint64_t head = 0, root = 0;
    if (Read(hostObj + 0xa10 + 0x2e0, head) && head && Read(head + 8, root)) {
        // The tree, from its root, 16 nodes at most. It is read from another thread: every step is
        // checked, and a torn read only costs this line.
        // Two kinds of entry share the map (docs/research/netcode-awaited-entries-after-join.md): those of
        // a game state exchange, and those of a data labels message (key type 0x42 at +0x38). A labels
        // message without labels has an all-zero hash (+0x24, 16 bytes); nobody ever reports those
        // loaded and the game keeps them for the session, so they are left out here. 512 nodes are
        // looked at, which is over a hundred level loads with four players.
        uint64_t stack[64];
        int depth = 0, seen = 0, active = 0;
        std::string waiting, loaded;
        if (root != head) stack[depth++] = root;
        while (depth > 0 && seen < 512) {
            const uint64_t n = stack[--depth];
            uint8_t isHead = 1, done = 0;
            uint64_t left = 0, right = 0, slot = 0;
            if (!Read(n + 0x19, isHead) || isHead || !Read(n, left) || !Read(n + 0x10, right)) continue;
            ++seen;
            if (depth < 63 && left && left != head) stack[depth++] = left;
            if (depth < 63 && right && right != head) stack[depth++] = right;
            int32_t type = 0;
            uint64_t hash[2] = {};
            Read(n + 0x38, type);
            Read(n + 0x24, hash);
            const bool labels = type == 0x42;
            if (labels && !hash[0] && !hash[1]) continue;
            ++active;
            Read(n + 0x40, done);
            Read(n + 0x48, slot);
            const uint64_t first = hostObj + 0x500;
            const std::string who =
                slot >= first && slot < first + 4 * 0x140 ? std::to_string((slot - first) / 0x140) : std::string("?");
            (done ? loaded : waiting) += " [" + who + (labels ? ", labels]" : "]");
        }
        if (active)
            out += " loaded by the host thread's slots:" + (loaded.empty() ? std::string(" none") : loaded) +
                   ", awaited:" + (waiting.empty() ? std::string(" none") : waiting);
    }
    return out;
}

// What the resource transfer manager is moving ("" = nothing).
std::string SesResources() {
    std::string out;
    const uint64_t mgr = reinterpret_cast<uint64_t>(g_base) + 0x1315b18;
    std::string sending, fetching;
    for (int i = 0; i < 32; ++i) {
        const uint64_t place = mgr + 0x8f8 + 0x618ull * i;
        uint32_t length = 0, sent = 0, hash = 0;
        if (!Read(place + 0xf0, length) || !length) continue;
        Read(place + 0xf4, sent);
        Read(place, hash);
        const std::string asker = SesName(place + 0xc8);
        if (asker.empty()) continue;
        char b[96];
        std::snprintf(b, sizeof(b), " %08x to %s %u of %u", hash, asker.c_str(), sent, length);
        sending += b;
    }
    for (int i = 0; i < 8; ++i) {
        const uint64_t place = mgr + 0xb8 + 0x108ull * i;
        uint32_t size = 0, got = 0;
        if (!Read(place + 0x8, size) || !Read(place + 0x100, got) || (!size && !got)) continue;
        char b[64];
        std::snprintf(b, sizeof(b), " %u of %u", got, size);
        fetching += b;
    }
    if (!sending.empty()) out += " sending" + sending + ";";
    if (!fetching.empty()) out += " fetching" + fetching + ";";
    if (!out.empty()) out.pop_back();
    return out;
}

std::string SesBlockDetail() {
    const std::string x = SesExchange(), r = SesResources();
    return (x.empty() ? std::string() : "; game state" + x) + (r.empty() ? std::string() : "; resources:" + r);
}

void SessionLogThread() {
    struct ConnSeen {
        uint64_t conn = 0;
        uint32_t budget = 0, cutRaw = 0, cuts = 0, samples = 0, negative = 0;
        int64_t lastCutUs = 0;
    };
    std::vector<ConnSeen> seen;
    std::string lastPlayers, lastExchange;
    bool resourcesMoving = false;
    int64_t exchangeSince = 0, resourcesLastLine = 0;
    bool inSession = false, blockOpen = false, loading = false;
    int64_t blockSince = 0, blockLastLine = 0, loadingSince = 0, windowStart = SesNowUs();
    using Clock = int64_t(PS4_SYSV_ABI*)();
    for (;;) {
        Sleep(100);
        SesTestSettings();
        const int64_t now = SesNowUs();
        bool any = false;
        const std::string players = SesPlayers(any);
        if (players != lastPlayers && (any || inSession)) {
            std::fprintf(stderr, "[ses] %s players:%s\n", core::Stamp().text, players.c_str());
            lastPlayers = players;
        }
        const bool was = inSession;
        inSession = any;
        if (!inSession && !was) {
            windowStart = now;
            continue;
        }
        // The join's block, and the catch-up loading screen.
        uint64_t game = 0;
        uint8_t running = 1, flag = 0;
        if (Read(reinterpret_cast<uint64_t>(g_base + 0x130d5e8), game) && game) Read(game + 0x45, running);
        Read(reinterpret_cast<uint64_t>(g_base + 0x12c9454), flag);
        if (!running && !blockOpen) {
            blockOpen = true;
            blockSince = blockLastLine = now;
            std::fprintf(stderr, "[ses] %s join block: the simulation is stopped for a game state exchange%s\n",
                         core::Stamp().text, SesBlockDetail().c_str());
        } else if (!running && now - blockLastLine >= 2000000) {
            blockLastLine = now;
            std::fprintf(stderr, "[ses] %s join block: %.1f s so far%s\n", core::Stamp().text, (now - blockSince) / 1e6,
                         SesBlockDetail().c_str());
        } else if (running && blockOpen) {
            blockOpen = false;
            std::fprintf(stderr, "[ses] %s join block: over after %.1f s%s\n", core::Stamp().text,
                         (now - blockSince) / 1e6, SesBlockDetail().c_str());
        }
        // The exchange outlasts the block on the host: a line whenever who has loaded changes.
        if (const std::string x = SesExchange(); x != lastExchange) {
            if (lastExchange.empty()) exchangeSince = now;
            if (x.empty())
                std::fprintf(stderr, "[ses] %s game state exchange: over after %.1f s\n", core::Stamp().text,
                             (now - exchangeSince) / 1e6);
            else std::fprintf(stderr, "[ses] %s game state exchange:%s\n", core::Stamp().text, x.c_str());
            lastExchange = x;
        }
        // Resources on the move outside a block: a line every 2 s, and one when they stop.
        if (const std::string r = SesResources(); !r.empty() && running && now - resourcesLastLine >= 2000000) {
            resourcesLastLine = now;
            resourcesMoving = true;
            std::fprintf(stderr, "[ses] %s resources:%s\n", core::Stamp().text, r.c_str());
        } else if (r.empty() && resourcesMoving) {
            resourcesMoving = false;
            std::fprintf(stderr, "[ses] %s resources: none on the move now\n", core::Stamp().text);
        }
        if ((flag != 0) != loading) {
            loading = flag != 0;
            if (loading) {
                loadingSince = now;
                std::fprintf(stderr, "[ses] %s catch-up loading screen: shown\n", core::Stamp().text);
            } else {
                std::fprintf(stderr, "[ses] %s catch-up loading screen: hidden after %.1f s\n", core::Stamp().text,
                             (now - loadingSince) / 1e6);
            }
        }
        // The connections' send budgets are watched every pass, so a cut is timed to 0.1 s.
        uint64_t net = 0, list = 0;
        uint32_t count = 0;
        if (Read(reinterpret_cast<uint64_t>(g_base + 0x130d5b8 + 0x80), net) && net) {
            Read(net + 0x50, list);
            Read(net + 0x58, count);
        }
        std::vector<uint64_t> conns;
        for (uint32_t j = 0; j < count && j < 8; ++j) {
            uint64_t c = 0;
            if (Read(list + 8ull * j, c) && c && Readable(reinterpret_cast<const void*>(c), 0x280)) conns.push_back(c);
        }
        std::erase_if(seen, [&](const ConnSeen& s) { return std::find(conns.begin(), conns.end(), s.conn) == conns.end(); });
        for (const uint64_t c : conns) {
            auto it = std::find_if(seen.begin(), seen.end(), [&](const ConnSeen& s) { return s.conn == c; });
            uint32_t budget = 0, cutRaw = 0;
            Read(c + 0x118, budget);
            Read(c + 0x124, cutRaw);
            if (it == seen.end()) {
                ConnSeen s;
                s.conn = c;
                s.budget = budget;
                s.cutRaw = cutRaw;
                seen.push_back(s);
            } else {
                if (cutRaw != it->cutRaw || budget < it->budget) {
                    ++it->cuts;
                    it->lastCutUs = now;
                }
                it->budget = budget;
                it->cutRaw = cutRaw;
            }
        }
        if (now - windowStart < 5000000) continue;
        const double secs = (now - windowStart) / 1e6;
        windowStart = now;
        const uint32_t flips = g_flip.flips.exchange(0), steps = g_flip.steps.exchange(0),
                       noStep = g_flip.noStep.exchange(0), multi = g_flip.multi.exchange(0),
                       run = g_flip.longestRun.exchange(0), drops = g_socketDrops.exchange(0),
                       jumps = g_flip.jumps.exchange(0);
        std::string host;
        const uint32_t made = g_frames.made.exchange(0), frameGap = g_frames.longestGapUs.exchange(0),
                       waits = g_frames.waits.exchange(0), catchUp = g_frames.catchUp.exchange(0);
        if (made) {
            char b[200];
            std::snprintf(b, sizeof(b),
                          "; as host: %u frames made, longest %.0f ms between two, %u waits over 50 ms, %u catch-up "
                          "frames; INPUTS",
                          made, frameGap / 1e3, waits, catchUp);
            host = b;
            for (int i = 0; i < 4; ++i) {
                const uint32_t n = g_inputs[i].count.exchange(0), gap = g_inputs[i].longestGapUs.exchange(0);
                const int64_t last = g_inputs[i].lastUs.load();
                if (!n && (!last || now - last > 60000000)) continue;
                std::snprintf(b, sizeof(b), " [%d] %u, longest gap %.0f ms, last %.0f ms ago", i, n, gap / 1e3,
                              last ? std::max<int64_t>(now - last, 0) / 1e3 : -1.0);
                host += b;
            }
        }
        std::fprintf(stderr,
                     "[ses-win] %s %.1f s: %u flips, %u steps, %u flips with no step (longest run %u), %u with two or "
                     "more, %u step counter jumps; simulation %s, loading screen %s; %u datagrams dropped at the "
                     "socket queue%s\n",
                     core::Stamp().text, secs, flips, steps, noStep, run, multi, jumps, running ? "running" : "stopped",
                     loading ? "up" : "off", drops, host.c_str());
        const int64_t gameNow = reinterpret_cast<Clock>(g_base + 0x1b1b0)();
        const auto linkRtt = g_linkRtt.load();
        for (ConnSeen& s : seen) {
            const uint64_t c = s.conn;
            if (!Readable(reinterpret_cast<const void*>(c), 0x280)) continue;
            auto u32 = [&](uint32_t off) {
                uint32_t v = 0;
                Read(c + off, v);
                return v;
            };
            int64_t ka = 0;
            Read(c + 0x160, ka);
            const std::string name = SesName(c + 0x10);
            uint32_t samples = 0, negative = 0;
            for (RttCount& r : g_rttCounts)
                if (r.conn.load() == c) {
                    samples = r.samples.load();
                    negative = r.negative.load();
                }
            char cut[48] = "never seen";
            if (s.lastCutUs) std::snprintf(cut, sizeof(cut), "%.1f s ago", (now - s.lastCutUs) / 1e6);
            char link[32] = "none";
            if (const int us = linkRtt ? linkRtt(name.c_str()) : -1; us >= 0)
                std::snprintf(link, sizeof(link), "%.1f ms", us / 1e3);
            const uint32_t rtt = u32(0xf0);
            std::fprintf(stderr,
                         "[ses-conn] %s %s: send budget %u (cap %u, growth step %u), %u cuts in the window, last cut %s; "
                         "the game's RTT %.1f ms%s, the link's %s; RTT samples %u, negative %u; unacknowledged %u, held "
                         "out of order %u, keepalive %.2f s ago\n",
                         core::Stamp().text, name.c_str(), u32(0x118), u32(0x11c), u32(0x12c), s.cuts, cut, rtt / 1e3,
                         rtt > 5000000 ? " (WRONG)" : "", link, samples - s.samples, negative - s.negative, u32(0x188),
                         u32(0x270), ka ? (gameNow - ka) / 1e9 : -1.0);
            s.cuts = 0;
            s.samples = samples;
            s.negative = negative;
        }
        // A connection object's address can be used again: forget the counts of the ones that are gone.
        for (RttCount& r : g_rttCounts) {
            const uint64_t c = r.conn.load();
            if (c && std::find(conns.begin(), conns.end(), c) == conns.end()) {
                r.samples = 0;
                r.negative = 0;
                r.conn = 0;
            }
        }
    }
}

void ApplySessionLog(Module& m, uint8_t* mem) {
    // INPUTS stub: NoteInputs(rdi = the slot) with the argument registers kept, then on to the game's.
    uint8_t* inputs = mem;
    uint8_t* o = inputs;
    Put(o, {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51});  // push rdi, rsi, rdx, rcx, r8, r9
    Put(o, {0x48, 0x83, 0xec, 0x08});                          // sub rsp, 8
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(&NoteInputs));
    Put(o, {0xff, 0xd0});                                      // call rax
    Put(o, {0x48, 0x83, 0xc4, 0x08});                          // add rsp, 8
    Put(o, {0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f});  // pop r9, r8, rcx, rdx, rsi, rdi
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(m.Addr(0x3abeb0)));
    Put(o, {0xff, 0xe0});                                      // jmp rax
    // Frame stub: NoteFrame() with the argument registers kept, then on to the game's send of the frame.
    uint8_t* frame = mem + 128;
    o = frame;
    Put(o, {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51});  // push rdi, rsi, rdx, rcx, r8, r9
    Put(o, {0x48, 0x83, 0xec, 0x08});                          // sub rsp, 8
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(&NoteFrame));
    Put(o, {0xff, 0xd0});                                      // call rax
    Put(o, {0x48, 0x83, 0xc4, 0x08});                          // add rsp, 8
    Put(o, {0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f});  // pop r9, r8, rcx, rdx, rsi, rdi
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(m.Addr(0x39e9f0)));
    Put(o, {0xff, 0xe0});                                      // jmp rax
    FlushInstructionCache(GetCurrentProcess(), mem, 256);
    const bool a = Redirect(m, 0x39e822, 0x3abeb0, inputs);
    const bool b = Redirect(m, 0x39f09e, 0x39e9f0, frame);
    const bool r = InstallRttStub(m, mem + 256);
    if (const int kick = TestInt("LBP3_TEST_KICK_SECONDS"); kick > 0) {
        float was = 0;
        std::memcpy(&was, m.Addr(0x1061c4c), sizeof(was));
        const float now = static_cast<float>(kick);
        if (was == 10.0f) std::memcpy(m.Addr(0x1061c4c), &now, sizeof(now));
        std::fprintf(stderr, "[ses] LBP3_TEST_KICK_SECONDS: the kick limit was %.1f s, %s (a test)\n", was,
                     was == 10.0f ? "now set" : "left alone: not the 10.0 expected");
    }
    std::thread(SessionLogThread).detach();
    std::fprintf(stderr, "[ses] the session log is on (INPUTS %s, frames %s, RTT samples %s)\n", a ? "yes" : "no",
                 b ? "yes" : "no", r ? "yes" : "no");
}

}  // namespace

namespace seslog {
bool Enabled() {
    static const bool on = [] {
        const char* off = std::getenv("LBP3_SESSION_LOG");
        if (off && *off == '0') return false;
        const char* sn = std::getenv("LBP3_SHADNET");
        return hle::online::Enabled() || (sn && *sn == '1');
    }();
    return on;
}
void NoteFlip() {
    static const bool on = Enabled();
    if (!on || !g_base) return;
    if (const int ms = g_freezeMs.exchange(0, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[ses] %s test freeze: the frame thread stops for %d ms\n", core::Stamp().text, ms);
        Sleep(static_cast<DWORD>(ms));
        std::fprintf(stderr, "[ses] %s test freeze: over\n", core::Stamp().text);
    }
    using Counter = int(PS4_SYSV_ABI*)();
    const int sim = reinterpret_cast<Counter>(g_base + 0x7c79e0)();
    static int last = sim;
    static uint32_t run = 0;
    const int d = sim - last;
    last = sim;
    g_flip.flips.fetch_add(1, std::memory_order_relaxed);
    if (d < 0 || d > 30) {
        // The step counter went back or leapt (a level load, a prediction undone): not steps.
        g_flip.jumps.fetch_add(1, std::memory_order_relaxed);
        run = 0;
    } else if (d > 0) {
        g_flip.steps.fetch_add(static_cast<uint32_t>(d), std::memory_order_relaxed);
        if (d > 1) g_flip.multi.fetch_add(1, std::memory_order_relaxed);
        run = 0;
    } else {
        g_flip.noStep.fetch_add(1, std::memory_order_relaxed);
        RaiseMax(g_flip.longestRun, ++run);
    }
}
void NoteSocketDrop() { g_socketDrops.fetch_add(1, std::memory_order_relaxed); }
void SetLinkRtt(LinkRtt fn) { g_linkRtt = fn; }
std::string PlayerState(const std::string& name) {
    uint64_t slots = 0;
    if (!g_base || !Read(reinterpret_cast<uint64_t>(g_base + 0x130d5c0), slots) || !slots) return "no table";
    for (int i = 0; i < 8; ++i) {
        int32_t state = 0;
        if (!Read(slots + 0x300ull * i + 0x2f8, state) || state == 0 || SesName(slots + 0x300ull * i) != name) continue;
        return "slot " + std::to_string(i) + " in state " + std::to_string(state);
    }
    std::lock_guard lk(g_lastStateMutex);
    for (const LastState& l : g_lastState) {
        if (l.name != name) continue;
        char b[80];
        std::snprintf(b, sizeof(b), "no slot now (state %d %.1f s ago)", l.state, (SesNowUs() - l.us) / 1e6);
        return b;
    }
    return "no slot";
}
}  // namespace seslog

void ApplySyncProbe(Module& m) {
    if (m.fileName != "eboot.bin") return;
    if (seslog::Enabled()) {
        g_base = m.Addr(0);
        if (uint8_t* mem = AllocNear(m.Addr(kHashCall), 4096)) ApplySessionLog(m, mem);
        else std::fprintf(stderr, "[ses] no memory near the image: the session log is off\n");
    }
    const char* v = std::getenv("LBP3_SYNC_PROBE");
    if (!v || (v[0] != '1' && v[0] != '2')) return;
    g_base = m.Addr(0);
    uint8_t* mem = AllocNear(m.Addr(kHashCall), 4096);
    if (!mem) {
        std::fprintf(stderr, "[sync] no memory near the image: probe not installed\n");
        return;
    }
    // Hash stub: rbx = world; eax = the game's hash(world); LogSent(world, hash, r14d = frame); return the hash.
    uint8_t* sent = mem;
    uint8_t* o = sent;
    Put(o, {0x53});                    // push rbx
    Put(o, {0x48, 0x89, 0xfb});        // mov rbx, rdi
    Put(o, {0x48, 0xb8});              // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(m.Addr(kHashFn)));
    Put(o, {0xff, 0xd0});              // call rax
    Put(o, {0x50});                    // push rax
    Put(o, {0x48, 0x83, 0xec, 0x08});  // sub rsp, 8
    Put(o, {0x48, 0x89, 0xdf});        // mov rdi, rbx
    Put(o, {0x89, 0xc6});              // mov esi, eax
    Put(o, {0x44, 0x89, 0xf2});        // mov edx, r14d
    Put(o, {0x48, 0xb8});              // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(&LogSent));
    Put(o, {0xff, 0xd0});              // call rax
    Put(o, {0x48, 0x83, 0xc4, 0x08});  // add rsp, 8
    Put(o, {0x58});                    // pop rax
    Put(o, {0x5b});                    // pop rbx
    Put(o, {0xc3});                    // ret
    // Receive stub: LogReceived(game, message) with the argument registers kept, then on to the game's.
    uint8_t* received = mem + 128;
    o = received;
    Put(o, {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51});  // push rdi, rsi, rdx, rcx, r8, r9
    Put(o, {0x48, 0x83, 0xec, 0x08});                          // sub rsp, 8
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(&LogReceived));
    Put(o, {0xff, 0xd0});                                      // call rax
    Put(o, {0x48, 0x83, 0xc4, 0x08});                          // add rsp, 8
    Put(o, {0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f});  // pop r9, r8, rcx, rdx, rsi, rdi
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(m.Addr(kReceiveFn)));
    Put(o, {0xff, 0xe0});                                      // jmp rax
    // Message stub (LBP3_SYNC_PROBE=2): the game's fetch, then LogMessage(channel, slot, out) when it
    // returned one; the fetch's result is returned.
    uint8_t* fetched = mem + 256;
    o = fetched;
    Put(o, {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51});  // push rdi, rsi, rdx, rcx, r8, r9
    Put(o, {0x48, 0x83, 0xec, 0x08});                          // sub rsp, 8
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(m.Addr(kFetchFn)));
    Put(o, {0xff, 0xd0});                                      // call rax
    Put(o, {0x48, 0x89, 0x04, 0x24});                          // mov [rsp], rax
    Put(o, {0x84, 0xc0});                                      // test al, al
    Put(o, {0x74, 0x1a});                                      // jz +26
    Put(o, {0x8b, 0x7c, 0x24, 0x28});                          // mov edi, [rsp+0x28] (channel)
    Put(o, {0x48, 0x8b, 0x74, 0x24, 0x20});                    // mov rsi, [rsp+0x20] (slot)
    Put(o, {0x48, 0x8b, 0x54, 0x24, 0x18});                    // mov rdx, [rsp+0x18] (out)
    Put(o, {0x48, 0xb8});                                      // mov rax, imm64
    Put64(o, reinterpret_cast<uint64_t>(&LogMessage));
    Put(o, {0xff, 0xd0});                                      // call rax
    Put(o, {0x48, 0x8b, 0x04, 0x24});                          // mov rax, [rsp]
    Put(o, {0x48, 0x83, 0xc4, 0x08});                          // add rsp, 8
    Put(o, {0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f});  // pop r9, r8, rcx, rdx, rsi, rdi
    Put(o, {0xc3});                                            // ret
    FlushInstructionCache(GetCurrentProcess(), mem, 4096);
    const bool a = Redirect(m, kHashCall, kHashFn, sent);
    const bool b = Redirect(m, kReceiveCall, kReceiveFn, received);
    if (v[0] == '2' || std::getenv("LBP3_MSG_PROBE")) {
        const bool c = Redirect(m, kFetchCall, kFetchFn, fetched);
        std::fprintf(stderr, "[sync] session messages logged: %s\n", c ? "yes" : "no");
    }
    if (const char* cp = std::getenv("LBP3_CONN_PROBE"); cp && *cp == '1') {
        g_connProbe = true;
        const bool r = InstallRttStub(m, mem + 2048);
        std::fprintf(stderr, "[sync] RTT samples above 5 s logged: %s\n", r ? "yes" : "no");
        std::thread(ConnProbeThread).detach();
        std::fprintf(stderr, "[sync] LBP3_CONN_PROBE=1: a [conn] line per connection every second\n");
    }
    std::fprintf(stderr, "[sync] LBP3_SYNC_PROBE=1: sync checks logged (sent %s, received %s)\n", a ? "yes" : "no",
                 b ? "yes" : "no");
}

}  // namespace core
