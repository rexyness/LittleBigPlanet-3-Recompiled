// fpsample <pid> [samples=20] [interval_ms=250]: for every thread of a process, how often it was seen
// with each floating-point control state (MXCSR, x87 control word). Each thread is suspended for a
// moment, its context read, and resumed. Read-only; for comparing two ports' game threads.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fpsample <pid> [samples] [interval_ms]\n");
        return 2;
    }
    const DWORD pid = std::strtoul(argv[1], nullptr, 10);
    const int samples = argc > 2 ? std::atoi(argv[2]) : 20;
    const int interval = argc > 3 ? std::atoi(argv[3]) : 250;
    struct Seen {
        std::wstring name;
        std::map<std::pair<DWORD, WORD>, int> states;
    };
    std::map<DWORD, Seen> threads;
    for (int s = 0; s < samples; ++s) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof(te)};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                                  te.th32ThreadID);
            if (!t) continue;
            Seen& seen = threads[te.th32ThreadID];
            if (seen.name.empty()) {
                PWSTR d = nullptr;
                if (SUCCEEDED(GetThreadDescription(t, &d)) && d) {
                    seen.name = d;
                    LocalFree(d);
                }
                if (seen.name.empty()) seen.name = L"(unnamed)";
            }
            if (SuspendThread(t) != static_cast<DWORD>(-1)) {
                alignas(16) CONTEXT c{};
                c.ContextFlags = CONTEXT_FLOATING_POINT | CONTEXT_CONTROL;
                if (GetThreadContext(t, &c)) ++seen.states[{c.MxCsr, c.FltSave.ControlWord}];
                ResumeThread(t);
            }
            CloseHandle(t);
        }
        CloseHandle(snap);
        Sleep(interval);
    }
    // Totals by state, then each thread.
    std::map<std::pair<DWORD, WORD>, int> total;
    for (const auto& [id, s] : threads)
        for (const auto& [st, n] : s.states) total[st] += n;
    std::printf("totals over %zu threads, %d samples:\n", threads.size(), samples);
    for (const auto& [st, n] : total) std::printf("  MXCSR %#06lx  x87 %#06x : %d\n", st.first, st.second, n);
    std::printf("threads:\n");
    for (const auto& [id, s] : threads) {
        std::printf("  %6lu %-36ls", id, s.name.c_str());
        for (const auto& [st, n] : s.states) std::printf("  %#06lx/%#06x x%d", st.first, st.second, n);
        std::printf("\n");
    }
    return 0;
}
