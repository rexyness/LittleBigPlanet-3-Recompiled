// LBP3Launcher's "Create account": registers an online name on a community server's Refresh web
// API, the two calls LBPPC's docs describe (POST /api/v3/register, then PATCH /api/v3/users/me to
// allow the PS3-style game logins), written here from those facts. Nothing contacts a server unless
// the player presses the button.
#pragma once

#include <string>

namespace launcher {

struct HttpReply {
    int status = 0;     // 0 = no answer (error says why)
    std::string body;
    std::string error;
};
// method "POST"/"PATCH"/"GET"; url http://host[:port]/path or https://...; body JSON (or empty).
HttpReply HttpJson(const std::string& method, const std::string& url, const std::string& body,
                   const std::string& authorization);

std::string Sha512Hex(const std::string& text);        // lower-case hex
std::string RandomPassword(size_t length = 20);        // letters and digits, from BCryptGenRandom
std::string JsonEscape(const std::string& s);
// The string value of "key" anywhere in a JSON text (the first one), unescaped; empty if absent.
std::string JsonString(const std::string& json, const std::string& key);
// "key": true anywhere in a JSON text.
bool JsonTrue(const std::string& json, const std::string& key);

struct AccountResult {
    bool ok = false;
    std::string message;   // for the player
    std::string password;  // the generated one (shown once, saved to online_account.txt)
};
// server: as NormalizeServer gives it. Generates the password itself.
AccountResult CreateRefreshAccount(const std::string& server, const std::string& name);

}  // namespace launcher
