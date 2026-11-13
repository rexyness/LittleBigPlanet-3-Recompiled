// shadNet: the PSN stand-in LBPPC's community server runs (sign-in, friends, presence; later invites
// and the peer connection for online co-op). Opt-in; best/shadnet_design.md.
//
// Off unless LBP3_SHADNET=1 and a server, an online name and a password are all set. Off means
// nothing here runs: no client object, no thread, no socket (shadnettest checks that).
//   LBP3_SHADNET_SERVER   "host:port" (plain TCP), "tls://host:port" (a server behind a TLS proxy),
//                         or "play.lbppc.com" (LBPPC's server: tls://shadnet.lbppc.com:31313).
//                         No default server.
//   LBP3_SHADNET_NAME     the online name (3-16 letters, digits, '-', '_'); else LBP3_ONLINE_NAME;
//                         else the account file below.
//   LBP3_SHADNET_PASSWORD the account's password; else the account file below.
//   LBP3_SHADNET_WEBAPI   the server's Web API (friends, presence, sessions, invitations), where
//                         the game's PSN Web API requests go: "http(s)://host[:port]", or "local" to
//                         answer them here. Default: https://api.lbppc.com for LBPPC's server, else
//                         http://<host>:31315 (shadNet's own Web API port).
//   user\config\online_account.txt  lines "name=..." / "password=..." (also "key: value").
//
// The client is shadPS4's (rpcs3/rpcsn lineage), GPL-2.0-or-later, as vendored by LBPPC (emurphy04,
// aad6a91) with their TLS support: net/shadnet/ (NOTICE.md there). This file is ours.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace net::shadnet {

struct Config {
    bool enabled = false;
    std::string host;
    uint16_t port = 0;
    bool tls = false;
    std::string name;
    std::string password;
    std::string webApi;  // the Web API's base URL, no trailing slash ("" = answer locally)
    // shadNet's UDP service (STUN pings, the relay; phase (c)): the server's host, its port + 1, or
    // LBP3_SHADNET_UDP ("host:port", or "off"). Empty host = none.
    std::string udpHost;
    uint16_t udpPort = 0;
    std::string why;  // when not enabled although LBP3_SHADNET=1: what's missing (for the log)
};

// Pure: the settings from an environment lookup and the account file's text (tests feed both).
Config ParseConfig(const std::function<const char*(const char*)>& getenv, const std::string& accountFile);
// The process's own settings (environment + user\config\online_account.txt), read once.
const Config& Settings();

enum class Status { Off, Connecting, SignedIn, Failed };
const char* StatusName(Status s);

struct Friend {
    std::string npid;
    bool online = false;
};

// Starts the client when the settings enable it (no-op otherwise). Idempotent.
void Start();
void Start(const Config& config);  // tests: explicit settings
void Stop();
bool Active();  // a client exists (only when enabled)
Status GetStatus();
// The online name the client signs in with ("" when off).
std::string Name();
// The account id the server gave at sign-in (its user id; 0 until signed in). Each player's is
// different, as on PSN; the game compares them between players.
uint64_t AccountId();
std::vector<Friend> Friends();
// Waits (up to ms) until the login has an answer; returns the status then.
Status WaitSettled(int ms);
// Called on every status change (from the client's threads). One listener (hle/np_offline.cpp).
void SetStatusListener(std::function<void(Status)> listener);
// Seconds since Start() (sign-in hold-back in sceNpCheckCallback); -1 when not started.
double SecondsSinceStart();

// The Web API base URL of the running client ("" when off or "local").
std::string WebApiBase();
// The session token the server gave after the login (GetToken), for the Web API's Authorization
// header; "" until then. Never logged.
std::string BearerToken();

// A single-use sign-in code for the game server's login (shadNet command 42, client id "lbppc-refresh",
// scope "login"; LBPPC 0.1.3). play.lbppc.com's game server redeems it with shadNet to know whose login
// it is; without one it falls back to an address check, which LBPPC will switch off. A fresh code for
// every login (single use, 300 s). Waits up to 5 s; must not be called from the client's own threads.
// Empty when off, not signed in, refused or timed out. expiresIn (seconds) is filled in when given.
std::optional<std::string> LoginCode(uint32_t* expiresIn = nullptr);

// A Web API push event from the server (notification 17): friend, presence, invitation and game
// custom data changes, for the game's push event callbacks (hle/np_shadnet.cpp).
struct PushEvent {
    std::string service;  // npServiceName ("" for the basic, non-service events)
    uint32_t label = 0;
    std::string dataType;  // e.g. "np:service:invitation"
    std::string data;      // the event's body (JSON)
    std::string from, to;  // online IDs ("" when not sent)
    std::vector<std::pair<std::string, std::string>> extd;
    uint64_t fromAccount = 0, toAccount = 0;
};
// The push events received since the last call, oldest first (at most 256 are kept).
std::vector<PushEvent> TakePushEvents();

// Where the server says another player is (RequestSignalingInfos: the address it has seen their
// STUN pings from): done(ok, "a.b.c.d", port) once, from the client's thread (it must not wait).
// Not signed in: done(false) at once.
void ResolvePeer(const std::string& npid, std::function<void(bool ok, const std::string& ip, uint16_t port)> done);
// The running client's UDP service, as configured ("" when off).
std::string UdpHost();
uint16_t UdpPort();

}  // namespace net::shadnet
