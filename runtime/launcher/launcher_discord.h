// Discord Rich Presence, our own client for Discord's local IPC (no SDK): a named pipe
// \\.\pipe\discord-ipc-N (N = 0..9), frames of [opcode u32][length u32][JSON], little-endian.
// Opcodes: 0 handshake {"v":1,"client_id":...}, 1 frame (commands such as SET_ACTIVITY), 2 close.
// Opt-in, and only with the player's own application ID; nothing connects unless they turn it on.
#pragma once

#include <cstdint>
#include <string>

namespace launcher {

// One frame, encoded and decoded (exposed for the tests).
std::string EncodeFrame(std::uint32_t opcode, const std::string& json);
bool DecodeFrameHeader(const std::string& header8, std::uint32_t* opcode, std::uint32_t* length);

// The SET_ACTIVITY command's JSON.
std::string ActivityJson(std::uint32_t pid, const std::string& details, const std::string& state,
                         std::int64_t start_unix, const std::string& nonce);

class DiscordRpc {
public:
    ~DiscordRpc() { Close(); }
    // Finds Discord's pipe (or <pipe_prefix>N for tests: LBP3_LAUNCHER_DISCORD_PIPE overrides the
    // prefix) and does the handshake. False with why() set if Discord isn't running or refused.
    bool Connect(const std::string& app_id);
    bool Connected() const { return m_pipe != nullptr; }
    // Shows "details / state" with an elapsed timer from start_unix. Empty details clears it.
    bool SetActivity(const std::string& details, const std::string& state, std::int64_t start_unix);
    void Clear();
    void Close();
    const std::string& why() const { return m_why; }

private:
    bool Send(std::uint32_t op, const std::string& json);
    bool Receive(std::uint32_t* op, std::string* json);
    void* m_pipe = nullptr;
    std::string m_why;
    int m_nonce = 0;
};

}  // namespace launcher
