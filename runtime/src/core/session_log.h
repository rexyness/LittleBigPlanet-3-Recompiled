// The session log's game side (sync_probe.cpp): cheap lines that are always on while online, so a
// session can be measured from its own log. "[ses]" lines on a change (player slots, a join's block,
// the catch-up loading screen), "[ses-win]" and "[ses-conn]" lines every 5 s. The link's side is the
// "[p2p-win]" and "[p2p-path]" lines of net/p2p_link.cpp. LBP3_SESSION_LOG=0 turns it off. Ours.
#pragma once
#include <string>

namespace core::seslog {

bool Enabled();  // online (LBP3_ONLINE with a server, or LBP3_SHADNET=1) and not turned off
void NoteFlip();        // the game's frame thread, at each flip; does nothing unless Enabled()
void NoteSocketDrop();  // a datagram didn't fit a socket's queue (hle/net_offline.cpp)
// The game's own slot for a player, as text ("slot 1 in state 11", "no slot").
std::string PlayerState(const std::string& name);
// The link's RTT to a player in microseconds, or -1; printed next to the game's own.
using LinkRtt = int (*)(const char* name);
void SetLinkRtt(LinkRtt fn);

}  // namespace core::seslog
