// shadNet for the launcher, the pure parts (launcher_friends.h).
#include "launcher_friends.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace launcher {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Same(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }
void Drop(std::vector<std::string>& v, const std::string& name) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& s) { return Same(s, name); }), v.end());
}
bool Has(const std::vector<std::string>& v, const std::string& name) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return Same(s, name); });
}

}  // namespace

ShadnetEndpoint ParseShadnetServer(const std::string& typed) {
    ShadnetEndpoint e;
    std::string server = typed;
    while (!server.empty() && std::isspace(static_cast<unsigned char>(server.back()))) server.pop_back();
    while (!server.empty() && std::isspace(static_cast<unsigned char>(server.front()))) server.erase(server.begin());
    std::string lowered = Lower(server);
    while (!lowered.empty() && lowered.back() == '/') lowered.pop_back(), server.pop_back();
    if (lowered == "play.lbppc.com" || lowered == "https://play.lbppc.com" || lowered == "tls://play.lbppc.com") {
        e.host = "shadnet.lbppc.com";
        e.port = 31313;
        e.tls = true;
        return e;
    }
    if (server.empty()) return e;
    if (lowered.rfind("tls://", 0) == 0) e.tls = true, server = server.substr(6);
    else if (lowered.rfind("tcp://", 0) == 0) server = server.substr(6);
    else if (lowered.rfind("https://", 0) == 0) server = server.substr(8);
    else if (lowered.rfind("http://", 0) == 0) server = server.substr(7);
    const size_t colon = server.rfind(':');
    e.host = colon == std::string::npos ? server : server.substr(0, colon);
    int port = 31313;
    if (colon != std::string::npos) port = std::atoi(server.c_str() + colon + 1);
    e.port = port > 0 && port < 65536 ? static_cast<std::uint16_t>(port) : 0;
    return e;
}

void FriendCameOnline(FriendsList& l, const std::string& name, bool online) {
    for (auto& f : l.friends)
        if (Same(f.name, name)) f.online = online;
}

void FriendAdded(FriendsList& l, const std::string& name, bool online) {
    Drop(l.sent, name);
    Drop(l.received, name);
    for (auto& f : l.friends)
        if (Same(f.name, name)) {
            f.online = online;
            return;
        }
    l.friends.push_back({name, online});
}

void FriendLost(FriendsList& l, const std::string& name) {
    l.friends.erase(std::remove_if(l.friends.begin(), l.friends.end(), [&](const Friend& f) { return Same(f.name, name); }),
                    l.friends.end());
}

void RequestReceived(FriendsList& l, const std::string& name) {
    if (!Has(l.received, name)) l.received.push_back(name);
}

void AddFriendDone(FriendsList& l, const std::string& name) {
    if (Has(l.received, name)) FriendAdded(l, name, false);  // accepted; their status comes next
    else if (!Has(l.sent, name)) l.sent.push_back(name);
}

void RemoveFriendDone(FriendsList& l, const std::string& name) {
    FriendLost(l, name);
    Drop(l.received, name);
    Drop(l.sent, name);
}

void SortFriends(FriendsList& l) {
    std::sort(l.friends.begin(), l.friends.end(), [](const Friend& a, const Friend& b) {
        if (a.online != b.online) return a.online;
        return Lower(a.name) < Lower(b.name);
    });
}

std::string ExplainFriendError(int code) {
    switch (code) {
    case 0: return "done";
    case 5: return "the sign-in failed";
    case 6: return "this account is signed in somewhere else already (close the game first)";
    case 23: return "not allowed (signed out?)";
    case 26: return "no player with that name";
    case 27: return "that player is blocked";
    case 28: return "you are friends already";
    case 3: return "that isn't a valid online name";
    case 4: return "too many requests; wait a moment";
    default: return "the server said no (error " + std::to_string(code) + ")";
    }
}

std::string ExplainShadnetState(int state) {
    // ShadNetState: Ok, FailureInput, FailureResolve, FailureConnect, FailureServerInfo, FailureAuth,
    // FailureAlreadyIn, FailureUsername, FailurePassword, FailureToken, FailureProtocol, FailureOther.
    switch (state) {
    case 0: return "signed in";
    case 1: return "the name or password is missing";
    case 2: return "the shadNet server's address couldn't be found (offline?)";
    case 3: return "couldn't connect to the shadNet server (offline, or it is down)";
    case 4: return "the shadNet server didn't introduce itself (a proxy or firewall in the way?)";
    case 5: return "the shadNet server didn't let you in";
    case 6: return "this account is signed in already (is the game running, or another PC?)";
    case 7: return "no shadNet account with that name";
    case 8: return "wrong password for shadNet (fix it under Accounts)";
    case 9: return "the sign-in token was refused";
    case 10: return "the shadNet server speaks a version this build doesn't";
    default: return "the shadNet sign-in failed";
    }
}

}  // namespace launcher
