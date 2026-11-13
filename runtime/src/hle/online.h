// Online play on a community LittleBigPlanet server (Refresh-compatible), opt-in.
//
// Off unless LBP3_ONLINE=1 and LBP3_ONLINE_SERVER names a server ("http://host:port" or
// "https://host"); there is no default server. When on: PSN is signed in locally under
// LBP3_ONLINE_NAME (or LBP3_PLAYER_NAME / the Windows user name), the LAN is up, the game's
// HTTP requests go to that server (hle/http_online.cpp), its request-signing key becomes the
// community servers' "CustomServerDigest" (core/patches.cpp), and the login carries a local
// PS3-style ticket. When off, everything stays as on a console without a network (the
// *_offline.cpp files), and nothing here runs.
//
// The approach follows LBPPC (emurphy04, https://github.com/emurphy04/LBPPC at aad6a91,
// docs/online.md); see hle/http_online.cpp for the parts adapted from its GPL-2.0-or-later
// gpu/bbnet/vendor/shadps4/core/libraries/network/http.cpp.
#pragma once
#include <cstdint>
#include <string>

namespace hle::online {

struct Server {
    std::string scheme;  // "http" or "https"
    std::string host;
    uint16_t port = 0;
    bool secure = false;
};

bool Enabled();
const Server& GetServer();
// The player's online name when online (empty when off).
const std::string& Name();
// The server's IPv4 address in network byte order (0 if it can't be resolved); the game's own
// server name (*.scee.com) resolves to it, since its HTTP requests are redirected there anyway.
uint32_t ServerIPv4();
// A host name's IPv4 address through DNS, network byte order; 0 if it doesn't resolve.
uint32_t ResolveIPv4(const std::string& host);

}  // namespace hle::online
