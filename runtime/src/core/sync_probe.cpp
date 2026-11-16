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
// code is ours. Off by default: nothing is patched without the variable.
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
#include <set>
#include <thread>

#include "core/log_stamp.h"
#include "core/runner.h"
#include "core/module.h"
#include "hle/abi.h"

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

}  // namespace

void ApplySyncProbe(Module& m) {
    const char* v = std::getenv("LBP3_SYNC_PROBE");
    if (!v || (v[0] != '1' && v[0] != '2') || m.fileName != "eboot.bin") return;
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
    std::fprintf(stderr, "[sync] LBP3_SYNC_PROBE=1: sync checks logged (sent %s, received %s)\n", a ? "yes" : "no",
                 b ? "yes" : "no");
}

}  // namespace core
