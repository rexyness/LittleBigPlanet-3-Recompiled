// Running a helper program with its output captured (launcher_proc.h).
#include "launcher_proc.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>

namespace launcher {

std::wstring Quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::filesystem::path FindOnPath(const wchar_t* exe) {
    wchar_t buf[MAX_PATH];
    if (!SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr)) return {};
    return buf;
}

ProcResult RunCapture(const std::wstring& command_line, const std::filesystem::path& cwd,
                      const std::function<void(const std::string&)>& on_line, const std::atomic<bool>* cancel,
                      unsigned timeout_ms) {
    ProcResult r;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) {
        r.error = "couldn't make a pipe";
        return r;
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = si.hStdError = write;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = command_line;
    // In a job of its own, so cancelling ends everything it started (build.bat's cmake, ninja and
    // compilers; git's helpers), not just the first process.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    const DWORD last = GetLastError();
    if (ok) {
        if (job && !AssignProcessToJobObject(job, pi.hProcess)) CloseHandle(job), job = nullptr;
        ResumeThread(pi.hThread);
    }
    CloseHandle(write);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(read);
        if (job) CloseHandle(job);
        r.error = "couldn't start it (Windows error " + std::to_string(last) + ")";
        return r;
    }
    r.started = true;
    const ULONGLONG start = GetTickCount64();
    std::string pending;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(read, nullptr, 0, nullptr, &avail, nullptr)) break;  // closed: it ended
        if (avail) {
            DWORD n = 0;
            if (!ReadFile(read, buf, std::min<DWORD>(avail, sizeof buf), &n, nullptr) || !n) break;
            r.output.append(buf, n);
            pending.append(buf, n);
            for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (on_line) on_line(line);
                pending.erase(0, nl + 1);
            }
            continue;
        }
        if ((cancel && cancel->load()) || (timeout_ms && GetTickCount64() - start > timeout_ms)) {
            if (!job || !TerminateJobObject(job, 1)) TerminateProcess(pi.hProcess, 1);
            r.error = cancel && cancel->load() ? "cancelled" : "timed out";
            break;
        }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
            // read what's left
            DWORD n = 0;
            while (PeekNamedPipe(read, nullptr, 0, nullptr, &avail, nullptr) && avail &&
                   ReadFile(read, buf, std::min<DWORD>(avail, sizeof buf), &n, nullptr) && n) {
                r.output.append(buf, n);
                pending.append(buf, n);
            }
            break;
        }
    }
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
        std::string line = pending.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (on_line) on_line(line);
        pending.erase(0, nl + 1);
    }
    if (!pending.empty() && on_line) on_line(pending);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<long>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(read);
    if (job) CloseHandle(job);  // anything still running in it (not killed) carries on
    return r;
}

}  // namespace launcher
