#include "core/debugger.h"
#include "core/runner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace core::debugger {
namespace {

constexpr uint8_t kInt3 = 0xCC;
constexpr DWORD kTrapFlag = 0x100;
constexpr int kMaxHitsLogged = 200;  // per breakpoint

struct Breakpoint {
    std::string label;
    uint8_t original = 0;
    std::atomic<int> hits{0};
    std::string dumpReg;   // optional extra dump: register name ...
    uint64_t dumpOffset = 0;  // ... plus offset, e.g. rdi+0xe0
};

uint64_t RegisterValue(const CONTEXT& c, const std::string& r) {
    if (r == "rax") return c.Rax;
    if (r == "rbx") return c.Rbx;
    if (r == "rcx") return c.Rcx;
    if (r == "rdx") return c.Rdx;
    if (r == "rsi") return c.Rsi;
    if (r == "rdi") return c.Rdi;
    if (r == "rbp") return c.Rbp;
    if (r == "rsp") return c.Rsp;
    if (r == "r12") return c.R12;
    if (r == "r13") return c.R13;
    if (r == "r14") return c.R14;
    if (r == "r15") return c.R15;
    return 0;
}

std::mutex g_mutex;
std::unordered_map<uint64_t, Breakpoint> g_breakpoints;
thread_local uint64_t t_rearm = 0;  // breakpoint to restore after the single step

void WriteByte(uint64_t addr, uint8_t value) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(addr), 1, PAGE_EXECUTE_READWRITE, &old);
    *reinterpret_cast<uint8_t*>(addr) = value;
    VirtualProtect(reinterpret_cast<void*>(addr), 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), 1);
}

void DumpQwords(const char* reg, uint64_t p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return;
    auto* q = reinterpret_cast<const uint64_t*>(p);
    std::fprintf(stderr, "    [%s] %016llx %016llx %016llx %016llx\n", reg, q[0], q[1], q[2], q[3]);
}

// Hardware write watchpoint (LBP3_HWWATCH=<eboot offset>): debug register 0 on
// every thread, re-applied as threads appear. Reports the instruction that wrote.
uint64_t g_watchAddr = 0;
const Module* g_watchModule = nullptr;
std::atomic<int> g_watchHits{0};

struct WatchHit {
    int64_t qpc;
    uint64_t rip, value, caller;
    uint32_t tid;
};
constexpr size_t kWatchRing = size_t{1} << 18;
WatchHit g_watchRing[kWatchRing];
std::atomic<uint64_t> g_watchRingNext{0};

// "*off+add": the watched address is (pointer stored at eboot+off) + add,
// resolved once the game has set the pointer (and again if it changes).
uint64_t g_watchPtrOff = 0, g_watchPtrAdd = 0;
bool g_watchDeref = false;

void WatchThreads() {
    std::unordered_map<DWORD, bool> done;
    for (;;) {
        if (g_watchDeref) {
            // The pointer may live in memory the game hasn't mapped yet.
            const auto* slot = reinterpret_cast<const uint64_t*>(
                reinterpret_cast<uint64_t>(g_watchModule->base) + g_watchPtrOff);
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(slot, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                Sleep(200);
                continue;
            }
            const uint64_t ptr = *slot;
            const uint64_t want = ptr ? ((ptr + g_watchPtrAdd) & ~7ull) : 0;
            if (want != g_watchAddr) {
                g_watchAddr = want;
                done.clear();  // re-apply to every thread
                std::fprintf(stderr, "[hwwatch] now watching %016llx\n",static_cast<unsigned long long>(want));
            }
            if (!g_watchAddr) {
                Sleep(200);
                continue;
            }
        }
        if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te{sizeof(te)};
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId() ||
                    done[te.th32ThreadID])
                    continue;
                done[te.th32ThreadID] = true;
                HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                if (!h) continue;
                if (SuspendThread(h) != static_cast<DWORD>(-1)) {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(h, &c)) {
                        c.Dr0 = g_watchAddr;
                        c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);  // L0, write, 8 bytes
                        SetThreadContext(h, &c);
                    }
                    ResumeThread(h);
                }
                CloseHandle(h);
            }
            CloseHandle(snap);
        }
        Sleep(200);
    }
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    CONTEXT& c = *ep->ContextRecord;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_SINGLE_STEP && g_watchAddr && (c.Dr6 & 1)) {
        static const int maxHits = [] {  // LBP3_HWWATCH_HITS=<n> (default 60)
            const char* v = std::getenv("LBP3_HWWATCH_HITS");
            return v ? std::atoi(v) : 60;
        }();
        // LBP3_HWWATCH_RING=1: record every hit in memory instead (cheap); F10 writes them out.
        static const bool ring = std::getenv("LBP3_HWWATCH_RING") != nullptr;
        if (ring) {
            const uint64_t i = g_watchRingNext.fetch_add(1, std::memory_order_relaxed);
            WatchHit& h = g_watchRing[i % kWatchRing];
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            h.qpc = t.QuadPart;
            h.rip = c.Rip;
            h.value = *reinterpret_cast<const uint64_t*>(g_watchAddr);
            const auto* fp = reinterpret_cast<const uint64_t*>(c.Rbp);
            MEMORY_BASIC_INFORMATION mbi;
            h.caller = (fp && VirtualQuery(fp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) ? fp[1] : 0;
            h.tid = GetCurrentThreadId();
        } else if (g_watchHits++ < maxHits) {
            const uint64_t base = reinterpret_cast<uint64_t>(g_watchModule->base);
            const uint64_t value = *reinterpret_cast<const uint64_t*>(g_watchAddr);
            std::fprintf(stderr, "[hwwatch] write to eboot+0x%llx (now %016llx) by eboot+0x%llx, thread %lu\n",
                         static_cast<unsigned long long>(g_watchAddr - base), static_cast<unsigned long long>(value),
                         static_cast<unsigned long long>(c.Rip - base), GetCurrentThreadId());
            const auto* sp = reinterpret_cast<const uint64_t*>(c.Rbp);
            for (int i = 0; i < 6 && sp; ++i) {  // frame-pointer chain
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
                std::fprintf(stderr, "    caller eboot+0x%llx\n", static_cast<unsigned long long>(sp[1] - base));
                if (sp[0] <= reinterpret_cast<uint64_t>(sp)) break;
                sp = reinterpret_cast<const uint64_t*>(sp[0]);
            }
        }
        c.Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && t_rearm) {
        std::lock_guard lock(g_mutex);  // WriteByte's protect/restore must not interleave between threads
        auto it = g_breakpoints.find(t_rearm);
        if (it != g_breakpoints.end() && it->second.hits <= kMaxHitsLogged) WriteByte(t_rearm, kInt3);
        t_rearm = 0;
        c.EFlags &= ~kTrapFlag;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;

    const uint64_t at = c.Rip;  // int3 leaves Rip pointing at the trap byte here
    std::lock_guard lock(g_mutex);
    auto it = g_breakpoints.find(at);
    if (it == g_breakpoints.end()) return EXCEPTION_CONTINUE_SEARCH;
    Breakpoint& bp = it->second;

    if (bp.hits++ < kMaxHitsLogged) {
        std::fprintf(stderr, "[break] %s (hit %d, thread %lu)\n", bp.label.c_str(), bp.hits.load(),
                     GetCurrentThreadId());
        std::fprintf(stderr, "    rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", c.Rax, c.Rbx, c.Rcx, c.Rdx);
        std::fprintf(stderr, "    rsi=%016llx rdi=%016llx r8 =%016llx r9 =%016llx\n", c.Rsi, c.Rdi, c.R8, c.R9);
        std::fprintf(stderr, "    r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", c.R12, c.R13, c.R14, c.R15);
        {  // frame-pointer chain (game code keeps rbp frames)
            char first[24];  // at a function's first instruction the return address is still at [rsp]
            std::snprintf(first, sizeof(first), " [%llx]", static_cast<unsigned long long>(*reinterpret_cast<const uint64_t*>(c.Rsp)));
            std::string chain = first;
            const auto* fp = reinterpret_cast<const uint64_t*>(c.Rbp);
            for (int i = 0; i < 8 && fp; ++i) {
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(fp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
                char h[24];
                std::snprintf(h, sizeof(h), " %llx", static_cast<unsigned long long>(fp[1]));
                chain += h;
                if (fp[0] <= reinterpret_cast<uint64_t>(fp)) break;
                fp = reinterpret_cast<const uint64_t*>(fp[0]);
            }
            std::fprintf(stderr, "    callers:%s\n", chain.c_str());
        }
        DumpQwords("rdi", c.Rdi);
        DumpQwords("rax", c.Rax);
        DumpQwords("rbx", c.Rbx);
        if (!bp.dumpReg.empty()) {
            // "*reg+off" dumps what the pointer stored at reg+off points to.
            const bool deref = bp.dumpReg[0] == '*';
            const std::string reg = deref ? bp.dumpReg.substr(1) : bp.dumpReg;
            uint64_t p = RegisterValue(c, reg) + bp.dumpOffset;
            if (deref) {
                MEMORY_BASIC_INFORMATION mbi;
                p = VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT
                        ? *reinterpret_cast<const uint64_t*>(p)
                        : 0;
            }
            const std::string what = bp.dumpReg + "+" + std::to_string(bp.dumpOffset);
            DumpQwords(what.c_str(), p);
            for (int i = 1; i < (deref ? 4 : 2); ++i) {
                char label[16];
                std::snprintf(label, sizeof(label), "  +0x%x", i * 0x20);
                DumpQwords(label, p + i * 0x20);
            }
        }
        std::fflush(stderr);
    }
    // Run the real instruction once, then put the trap back.
    WriteByte(at, bp.original);
    t_rearm = at;
    c.EFlags |= kTrapFlag;
    return EXCEPTION_CONTINUE_EXECUTION;
}


// LBP3_POLLWATCH="*off+add[,*off+add...]": a background thread re-reads each 8-byte value
// (pointer stored at eboot+off, plus add) about every 20 us and records every change with a
// timestamp, without touching the game's threads. F10 (or a lost GPU device) writes the
// changes to analysis\pollwatch.txt.
struct PollChange {
    int64_t qpc;
    uint64_t value;
    uint32_t slot;
};
constexpr size_t kPollRing = size_t{1} << 18;
PollChange g_pollRing[kPollRing];
std::atomic<uint64_t> g_pollNext{0};
struct PollSpec { uint64_t ptrOff, add; };
std::vector<PollSpec> g_pollSpecs;

void PollThread(const Module* eboot) {
    std::vector<uint64_t> last(g_pollSpecs.size(), ~0ull);
    std::vector<uint64_t> addrs(g_pollSpecs.size(), 0);
    for (uint64_t iter = 0;; ++iter) {
        if (iter % 4096 == 0) {  // re-resolve the pointers now and then
            for (size_t i = 0; i < g_pollSpecs.size(); ++i) {
                const auto* slot = reinterpret_cast<const uint64_t*>(reinterpret_cast<uint64_t>(eboot->base) +
                                                                     g_pollSpecs[i].ptrOff);
                MEMORY_BASIC_INFORMATION mbi;
                uint64_t a = 0;
                if (VirtualQuery(slot, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                    !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && *slot) {
                    a = (*slot + g_pollSpecs[i].add) & ~7ull;
                    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) a = 0;
                }
                if (a != addrs[i]) {
                    addrs[i] = a;
                    std::fprintf(stderr, "[pollwatch] slot %zu now at %016llx\n", i, static_cast<unsigned long long>(a));
                }
            }
        }
        for (size_t i = 0; i < addrs.size(); ++i) {
            if (!addrs[i]) continue;
            const uint64_t v = *reinterpret_cast<const volatile uint64_t*>(addrs[i]);
            if (v != last[i]) {
                last[i] = v;
                LARGE_INTEGER t;
                QueryPerformanceCounter(&t);
                g_pollRing[g_pollNext.fetch_add(1, std::memory_order_relaxed) % kPollRing] =
                    PollChange{t.QuadPart, v, static_cast<uint32_t>(i)};
            }
        }
        for (int k = 0; k < 64; ++k) _mm_pause();
    }
}
}  // namespace

void ArmNow(const std::vector<std::string>& specs, const std::vector<Module*>& modules);

// LBP3_BREAK_DELAY=<seconds> arms the breakpoints later (from a background
// thread), so their 200 logged hits cover a stall rather than the boot.
void Arm(const std::vector<std::string>& specs, const std::vector<Module*>& modules) {
    if (const char* pw = std::getenv("LBP3_POLLWATCH"); pw && !modules.empty()) {
        const std::string list = pw;
        size_t pos = 0;
        while (pos < list.size()) {
            size_t comma = list.find_first_of(",/", pos);  // ',' or '/' (PowerShell splits on commas)
            if (comma == std::string::npos) comma = list.size();
            std::string item = list.substr(pos, comma - pos);
            if (!item.empty() && item[0] == '*') item.erase(0, 1);
            const size_t plus = item.find('+');
            const uint64_t off = std::strtoull(item.substr(0, plus).c_str(), nullptr, 16);
            const uint64_t add =
                plus == std::string::npos ? 0 : std::strtoull(item.substr(plus + 1).c_str(), nullptr, 16);
            g_pollSpecs.push_back({off, add});
            pos = comma + 1;
        }
        std::fprintf(stderr, "[pollwatch] watching %zu value(s)\n", g_pollSpecs.size());
        std::thread(PollThread, modules[0]).detach();
    }
    if (const char* w = std::getenv("LBP3_HWWATCH"); w && !modules.empty()) {
        g_watchModule = modules[0];  // eboot.bin is loaded first
        if (w[0] == '*') {
            char* end = nullptr;
            g_watchPtrOff = std::strtoull(w + 1, &end, 16);
            g_watchPtrAdd = (end && *end == '+') ? std::strtoull(end + 1, nullptr, 16) : 0;
            g_watchDeref = true;
        } else {
            g_watchAddr = (reinterpret_cast<uint64_t>(modules[0]->base) + std::strtoull(w, nullptr, 16)) & ~7ull;
        }
        AddVectoredExceptionHandler(1, Handler);
        std::thread(WatchThreads).detach();
        std::fprintf(stderr, "[hwwatch] watching writes to eboot+%s\n", w);
        if (specs.empty()) return;
    }
    if (specs.empty()) return;
    AddVectoredExceptionHandler(1, Handler);  // ahead of the crash handler
    if (const char* v = std::getenv("LBP3_BREAK_DELAY")) {
        const int seconds = std::atoi(v);
        std::thread([specs, modules, seconds] {
            Sleep(static_cast<DWORD>(seconds) * 1000);
            ArmNow(specs, modules);
        }).detach();
        return;
    }
    ArmNow(specs, modules);
}

void ArmNow(const std::vector<std::string>& specs, const std::vector<Module*>& modules) {
    for (const auto& full : specs) {
        // "module+0xoffset" or "module+0xoffset:reg+0xoff"
        const size_t colon = full.find(':');
        const std::string spec = full.substr(0, colon);
        std::string dumpReg;
        uint64_t dumpOffset = 0;
        if (colon != std::string::npos) {
            const std::string extra = full.substr(colon + 1);
            const size_t p = extra.find_first_of("+-");
            dumpReg = extra.substr(0, p);
            if (p != std::string::npos) {
                dumpOffset = std::stoull(extra.substr(p + 1), nullptr, 16);
                if (extra[p] == '-') dumpOffset = 0 - dumpOffset;  // wraps, so reg + offset subtracts
            }
        }
        const size_t plus = spec.find('+');
        if (plus == std::string::npos) continue;
        const std::string name = spec.substr(0, plus);
        const uint64_t off = std::stoull(spec.substr(plus + 1), nullptr, 16);
        for (Module* m : modules) {
            if (_stricmp(m->fileName.c_str(), name.c_str()) != 0 || off >= m->size) continue;
            const uint64_t addr = reinterpret_cast<uint64_t>(m->base) + off;
            std::lock_guard lock(g_mutex);
            const bool already = g_breakpoints.contains(addr);
            Breakpoint& bp = g_breakpoints[addr];
            bp.label = full;
            bp.dumpReg = dumpReg;
            bp.dumpOffset = dumpOffset;
            if (!already) {  // a second spec on the same address must not save the int3 as "original"
                bp.original = *reinterpret_cast<uint8_t*>(addr);
                WriteByte(addr, kInt3);
            }
            std::fprintf(stderr, "[break] armed %s\n", spec.c_str());
        }
    }
}


void DumpPollWatch() {
    const uint64_t next = g_pollNext.load();
    if (!next) return;
    FILE* f = std::fopen("analysis\\pollwatch.txt", "w");
    if (!f) return;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const uint64_t first = next > kPollRing ? next - kPollRing : 0;
    const int64_t t0 = g_pollRing[first % kPollRing].qpc;
    for (uint64_t i = first; i < next; ++i) {
        const PollChange& c = g_pollRing[i % kPollRing];
        std::fprintf(f, "%.3f slot%u %016llx\n", 1000.0 * static_cast<double>(c.qpc + 0 * t0) / static_cast<double>(freq.QuadPart),
                     c.slot, static_cast<unsigned long long>(c.value));
    }
    std::fclose(f);
    std::fprintf(stderr, "[pollwatch] wrote %llu changes to analysis\\pollwatch.txt\n",
                 static_cast<unsigned long long>(next - first));
}

void DumpWatchRing() {
    DumpPollWatch();
    const uint64_t next = g_watchRingNext.load();
    if (!next) return;
    FILE* f = std::fopen("analysis\\hwwatch_ring.txt", "w");
    if (!f) return;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const uint64_t first = next > kWatchRing ? next - kWatchRing : 0;
    const int64_t t0 = g_watchRing[first % kWatchRing].qpc;
    std::unordered_map<uint32_t, std::string> names;
    for (uint64_t i = first; i < next; ++i) {
        const WatchHit& h = g_watchRing[i % kWatchRing];
        auto it = names.find(h.tid);
        if (it == names.end()) {
            std::string n = std::to_string(h.tid);
            if (HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, h.tid)) {
                PWSTR w = nullptr;
                if (SUCCEEDED(GetThreadDescription(th, &w)) && w && *w) {
                    n.clear();
                    for (const wchar_t* c = w; *c; ++c)
                        if (*c != L'\n') n += static_cast<char>(*c < 128 ? *c : '?');
                }
                if (w) LocalFree(w);
                CloseHandle(th);
            }
            it = names.emplace(h.tid, n).first;
        }
        std::fprintf(f, "%.3f %s %016llx by %s from %s\n",
                     1000.0 * static_cast<double>(h.qpc + 0 * t0) / static_cast<double>(freq.QuadPart),
                     it->second.c_str(), static_cast<unsigned long long>(h.value), DescribeAddress(h.rip).c_str(),
                     h.caller ? DescribeAddress(h.caller).c_str() : "?");
    }
    std::fclose(f);
    std::fprintf(stderr, "[hwwatch] wrote %llu hits to analysis\hwwatch_ring.txt\n",
                 static_cast<unsigned long long>(next - first));
}

}  // namespace core::debugger
