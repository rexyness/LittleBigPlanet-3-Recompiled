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
//   user\config\online_account.txt  lines "name=..." / "password=..." (also "key: value").
//
// The client is shadPS4's (rpcs3/rpcsn lineage), GPL-2.0-or-later, as vendored by LBPPC (emurphy04,
// aad6a91) with their TLS support: net/shadnet/ (NOTICE.md there). This file is ours.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace net::shadnet {

struct Config {
    bool enabled = false;
    std::string host;
    uint16_t port = 0;
    bool tls = false;
    std::string name;
    std::string password;
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
std::vector<Friend> Friends();
// Waits (up to ms) until the login has an answer; returns the status then.
Status WaitSettled(int ms);
// Called on every status change (from the client's threads). One listener (hle/np_offline.cpp).
void SetStatusListener(std::function<void(Status)> listener);
// Seconds since Start() (sign-in hold-back in sceNpCheckCallback); -1 when not started.
double SecondsSinceStart();

}  // namespace net::shadnet
