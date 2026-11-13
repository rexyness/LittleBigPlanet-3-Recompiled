#include "hle/online.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace hle::online {
namespace {

struct Config {
    bool enabled = false;
    Server server;
    std::string name;
};

// "http://host:port", "https://host", "host:port" (http) or "host" (http, port 80).
bool ParseServer(std::string s, Server& out) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    out.scheme = "http";
    if (const auto at = s.find("://"); at != std::string::npos) {
        out.scheme = s.substr(0, at);
        for (char& c : out.scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s = s.substr(at + 3);
    }
    if (out.scheme != "http" && out.scheme != "https") return false;
    if (const auto slash = s.find('/'); slash != std::string::npos) s = s.substr(0, slash);
    out.secure = out.scheme == "https";
    out.port = out.secure ? 443 : 80;
    if (const auto colon = s.rfind(':'); colon != std::string::npos) {
        const int p = std::atoi(s.c_str() + colon + 1);
        if (p <= 0 || p > 65535) return false;
        out.port = static_cast<uint16_t>(p);
        s = s.substr(0, colon);
    }
    out.host = s;
    return !out.host.empty();
}

const Config& Get() {
    static const Config cfg = [] {
        Config c;
        const char* on = std::getenv("LBP3_ONLINE");
        if (!on || std::strcmp(on, "1") != 0) return c;
        const char* server = std::getenv("LBP3_ONLINE_SERVER");
        if (!server || !*server) {
            std::fprintf(stderr, "[online] LBP3_ONLINE=1 but no LBP3_ONLINE_SERVER: staying offline\n");
            return c;
        }
        if (!ParseServer(server, c.server)) {
            std::fprintf(stderr, "[online] LBP3_ONLINE_SERVER '%s' isn't a usable address: staying offline\n",
                         server);
            return c;
        }
        // PSN IDs: 3-16 characters of letters, digits, '-' and '_'.
        std::string raw;
        if (const char* n = std::getenv("LBP3_ONLINE_NAME"); n && *n) raw = n;
        else if (const char* p = std::getenv("LBP3_PLAYER_NAME"); p && *p) raw = p;
        else {
            char buf[64] = {};
            DWORD len = sizeof(buf);
            if (GetUserNameA(buf, &len)) raw = buf;
        }
        for (char ch : raw)
            if (c.name.size() < 16 && (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_'))
                c.name += ch;
        if (c.name.size() < 3) c.name = "Sackboy";
        c.enabled = true;
        std::fprintf(stderr, "[online] on: server %s://%s:%u, name '%s'\n", c.server.scheme.c_str(),
                     c.server.host.c_str(), c.server.port, c.name.c_str());
        return c;
    }();
    return cfg;
}

}  // namespace

uint32_t ResolveIPv4(const std::string& host) {
    static const bool wsa = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!wsa) return 0;
    uint32_t out = 0;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
        out = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(res);
    }
    return out;
}

uint32_t ServerIPv4() {
    static const uint32_t ip = ResolveIPv4(Get().server.host);
    return ip;
}

bool Enabled() { return Get().enabled; }
const Server& GetServer() { return Get().server; }
const std::string& Name() { return Get().name; }

}  // namespace hle::online
