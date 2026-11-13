// shadNet for the launcher, the connected parts, over the runtime's own shadNet client
// (runtime/src/net/shadnet, GPL-2.0-or-later, see its NOTICE.md):
// - the Friends page's session: signed in only while that page is open and the game isn't running
//   (an account can't be signed in twice), signed out before Play;
// - "Test sign-in": one login, then out;
// - creating an account: shadNet's Create command (as tools/create_online_account.py and LBPPC's
//   scripts/online_accounts.py do it), then the Refresh website account.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "launcher_account.h"
#include "launcher_friends.h"

namespace ShadNet {
class ShadNetClient;
}

namespace launcher {

class FriendsSession {
public:
    enum class Status { Off, SigningIn, SignedIn, Failed };
    struct View {
        Status status = Status::Off;
        std::string name;    // signed in as
        std::string error;   // why it failed (plain words)
        std::string note;    // the last friend command's result
        FriendsList list;
    };
    FriendsSession();
    ~FriendsSession();  // signs out
    void Start(const ShadnetEndpoint& at, const std::string& name, const std::string& password);
    // Signs out and waits for the client to stop (its threads end within a few seconds).
    void Stop();
    bool Active() const;  // signing in or signed in
    View Snapshot() const;
    // A friend request (or an accept, if they asked), and remove (or decline, or cancel).
    void Add(const std::string& name);
    void Remove(const std::string& name);

private:
    mutable std::mutex m_mutex;
    std::unique_ptr<ShadNet::ShadNetClient> m_client;
    std::thread m_waiter;
    View m_view;
    std::vector<std::pair<unsigned long long, std::pair<int, std::string>>> m_pending;  // id -> (cmd, name)
};

// One shadNet login (then signed out): empty when it worked, else why not (plain words).
std::string TestShadnetSignIn(const ShadnetEndpoint& at, const std::string& name, const std::string& password);

// shadNet's Create command: 0 when the account was made, else shadNet's error code (and *error
// in plain words); -1 when the server couldn't be reached.
int CreateShadnetAccount(const ShadnetEndpoint& at, const std::string& name, const std::string& password,
                         const std::string& email, std::string* error);

// "Create account" for a friend: the account file first (never overwriting one; online_account.txt
// if there is no active account yet), then shadNet, then the website (Refresh register, then game
// logins allowed). log gets one line per step (never the password).
struct CreateResult {
    bool ok = false;
    std::string message;
    std::string file;  // where the account was saved
};
CreateResult CreateOnlineAccounts(const std::string& config_dir, const std::string& refresh_server, const ShadnetEndpoint& shadnet,
                                  const std::string& name, const std::string& password,
                                  const std::function<void(const std::string&)>& log);

}  // namespace launcher
