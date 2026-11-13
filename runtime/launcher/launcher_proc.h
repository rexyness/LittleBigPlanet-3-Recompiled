// Running a helper program (git, build.bat, pkg_extractor.exe) without a console window, with its
// output read line by line. Used by the updater and the DLC installer; never for lbp3.exe itself.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct ProcResult {
    bool started = false;
    long exit_code = -1;
    std::string output;  // everything it printed (stdout and stderr together)
    std::string error;   // why it didn't start
};

// `command_line` as Windows parses it (quote the program and arguments with spaces). Each output
// line goes to `on_line` as it arrives. `cancel` kills it. Waits at most `timeout_ms` (0 = forever).
ProcResult RunCapture(const std::wstring& command_line, const std::filesystem::path& cwd,
                      const std::function<void(const std::string&)>& on_line, const std::atomic<bool>* cancel,
                      unsigned timeout_ms = 0);

// "a b" -> "\"a b\"" (Windows quoting for one argument).
std::wstring Quote(const std::wstring& arg);
// The full path of a program on PATH (e.g. L"git.exe"); empty if absent.
std::filesystem::path FindOnPath(const wchar_t* exe);

}  // namespace launcher
