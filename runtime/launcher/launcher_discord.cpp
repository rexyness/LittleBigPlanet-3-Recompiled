// Discord Rich Presence over the local IPC pipe (launcher_discord.h).
#include "launcher_discord.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>

#include "launcher_account.h"  // JsonEscape, JsonString

namespace launcher {

std::string EncodeFrame(std::uint32_t op, const std::string& json) {
    std::string f(8, '\0');
    const std::uint32_t len = static_cast<std::uint32_t>(json.size());
    for (int i = 0; i < 4; ++i) {
        f[i] = static_cast<char>((op >> (8 * i)) & 0xFF);
        f[4 + i] = static_cast<char>((len >> (8 * i)) & 0xFF);
    }
    return f + json;
}

bool DecodeFrameHeader(const std::string& h, std::uint32_t* op, std::uint32_t* len) {
    if (h.size() < 8) return false;
    auto u = [&](int at) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(h[at])) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(h[at + 1])) << 8 |
               static_cast<std::uint32_t>(static_cast<unsigned char>(h[at + 2])) << 16 |
               static_cast<std::uint32_t>(static_cast<unsigned char>(h[at + 3])) << 24;
    };
    *op = u(0);
    *len = u(4);
    return *len < (1u << 20);
}

std::string ActivityJson(std::uint32_t pid, const std::string& details, const std::string& state, std::int64_t start_unix,
                         const std::string& nonce) {
    std::string activity = "null";
    if (!details.empty()) {
        activity = "{\"details\":\"" + JsonEscape(details) + "\"";
        if (!state.empty()) activity += ",\"state\":\"" + JsonEscape(state) + "\"";
        if (start_unix > 0) activity += ",\"timestamps\":{\"start\":" + std::to_string(start_unix) + "}";
        activity += "}";
    }
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":" + activity +
           "},\"nonce\":\"" + JsonEscape(nonce) + "\"}";
}

bool DiscordRpc::Send(std::uint32_t op, const std::string& json) {
    if (!m_pipe) return false;
    const std::string f = EncodeFrame(op, json);
    DWORD n = 0;
    if (!WriteFile(static_cast<HANDLE>(m_pipe), f.data(), static_cast<DWORD>(f.size()), &n, nullptr) || n != f.size()) {
        m_why = "the connection to Discord closed";
        Close();
        return false;
    }
    return true;
}

bool DiscordRpc::Receive(std::uint32_t* op, std::string* json) {
    if (!m_pipe) return false;
    // Wait (up to 5 s) for the reply instead of blocking on a pipe that never answers.
    for (int waited = 0;; waited += 20) {
        DWORD avail = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(m_pipe), nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail >= 8) break;
        if (waited >= 5000) return false;
        Sleep(20);
    }
    char h[8];
    DWORD n = 0;
    if (!ReadFile(static_cast<HANDLE>(m_pipe), h, 8, &n, nullptr) || n != 8) return false;
    std::uint32_t len = 0;
    if (!DecodeFrameHeader(std::string(h, 8), op, &len)) return false;
    json->assign(len, '\0');
    DWORD got = 0;
    while (got < len) {
        if (!ReadFile(static_cast<HANDLE>(m_pipe), json->data() + got, len - got, &n, nullptr) || !n) return false;
        got += n;
    }
    return true;
}

bool DiscordRpc::Connect(const std::string& app_id) {
    Close();
    std::string prefix = "\\\\.\\pipe\\discord-ipc-";
    char custom[256];
    if (GetEnvironmentVariableA("LBP3_LAUNCHER_DISCORD_PIPE", custom, sizeof custom) - 1u < sizeof custom - 1) prefix = custom;
    for (int i = 0; i < 10 && !m_pipe; ++i) {
        const std::string name = prefix + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) m_pipe = h;
    }
    if (!m_pipe) {
        m_why = "Discord isn't running";
        return false;
    }
    if (!Send(0, "{\"v\":1,\"client_id\":\"" + JsonEscape(app_id) + "\"}")) return false;
    std::uint32_t op = 0;
    std::string reply;
    if (!Receive(&op, &reply)) {
        m_why = "Discord didn't answer";
        Close();
        return false;
    }
    if (op != 1 || JsonString(reply, "evt") != "READY") {
        const std::string msg = JsonString(reply, "message");
        m_why = "Discord refused" + (msg.empty() ? std::string() : ": " + msg);
        Close();
        return false;
    }
    m_why.clear();
    return true;
}

bool DiscordRpc::SetActivity(const std::string& details, const std::string& state, std::int64_t start_unix) {
    if (!m_pipe) return false;
    if (!Send(1, ActivityJson(GetCurrentProcessId(), details, state, start_unix, "lbp3-" + std::to_string(++m_nonce))))
        return false;
    std::uint32_t op = 0;
    std::string reply;
    if (!Receive(&op, &reply)) {
        m_why = "Discord didn't answer";
        Close();
        return false;
    }
    if (JsonString(reply, "evt") == "ERROR") {
        m_why = "Discord: " + JsonString(reply, "message");
        return false;
    }
    return true;
}

void DiscordRpc::Clear() { SetActivity("", "", 0); }

void DiscordRpc::Close() {
    if (m_pipe) {
        const std::string f = EncodeFrame(2, "{}");
        DWORD n = 0;
        WriteFile(static_cast<HANDLE>(m_pipe), f.data(), static_cast<DWORD>(f.size()), &n, nullptr);
        CloseHandle(static_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
}

}  // namespace launcher
