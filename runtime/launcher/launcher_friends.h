// shadNet for the launcher, the pure parts: where a server's shadNet is (the same rule as the
// game's, net/shadnet_service.cpp), the friends list as notifications and replies change it, and
// the errors in plain words. The connection itself is launcher_net (the runtime's shadNet client).
// Protocol facts: shadPS4's client (runtime/src/net/shadnet) and LBPPC's scripts/online_accounts.py
// ("a request to someone who asked you is an accept; removing someone also declines their request
// or cancels yours").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

struct ShadnetEndpoint {
    std::string host;
    std::uint16_t port = 0;
    bool tls = false;
    bool ok() const { return !host.empty() && port != 0; }
};
// "play.lbppc.com" (or https:// / tls:// of it) -> shadnet.lbppc.com:31313 over TLS (LBPPC's);
// "tls://host:port", "tcp://host:port", "host:port", "host" (port 31313).
ShadnetEndpoint ParseShadnetServer(const std::string& server);

struct Friend {
    std::string name;
    bool online = false;
};
struct FriendsList {
    std::vector<Friend> friends;
    std::vector<std::string> sent, received, blocked;  // requests sent, requests received
};
// What changes the list.
void FriendCameOnline(FriendsList& l, const std::string& name, bool online);  // a status notification
void FriendAdded(FriendsList& l, const std::string& name, bool online);       // FriendNew: now friends
void FriendLost(FriendsList& l, const std::string& name);                     // FriendLost
void RequestReceived(FriendsList& l, const std::string& name);                // FriendQuery
// After a successful AddFriend: an accept if they had asked, else a request sent.
void AddFriendDone(FriendsList& l, const std::string& name);
// After a successful RemoveFriend: gone from friends, received (declined) and sent (cancelled).
void RemoveFriendDone(FriendsList& l, const std::string& name);
// Friends online first, then by name (case-insensitive).
void SortFriends(FriendsList& l);

// shadNet's ErrorType of a friend command's reply, in plain words.
std::string ExplainFriendError(int code);
// The client's ShadNetState (as its integer) after a failed sign-in, in plain words.
std::string ExplainShadnetState(int state);

}  // namespace launcher
