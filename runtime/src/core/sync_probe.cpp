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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/log_stamp.h"
#include "core/module.h"
#include "hle/abi.h"

namespace core {
namespace {

constexpr uint64_t kHashCall = 0x256db6, kHashFn = 0x1acaa0;     // call 0x1acaa0 (hash of the players)
constexpr uint64_t kReceiveCall = 0xb822e5, kReceiveFn = 0x269eb0;  // call 0x269eb0 (a sync message arrived)

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

void PS4_SYSV_ABI LogSent(uint64_t world, uint32_t hash, uint32_t frame) {
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
    if (!v || v[0] != '1' || m.fileName != "eboot.bin") return;
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
    FlushInstructionCache(GetCurrentProcess(), mem, 4096);
    const bool a = Redirect(m, kHashCall, kHashFn, sent);
    const bool b = Redirect(m, kReceiveCall, kReceiveFn, received);
    std::fprintf(stderr, "[sync] LBP3_SYNC_PROBE=1: sync checks logged (sent %s, received %s)\n", a ? "yes" : "no",
                 b ? "yes" : "no");
}

}  // namespace core
