# Does an inbound Windows Firewall rule on both PCs open a direct path to the friend?

Type: task
Status: open
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

Two findings meet here:

- [Why did no connection with the friend go direct?](02-why-no-direct-path.md): most likely the friend's NAT sends to the host from a port other than the one the server saw, and the host's side only lets in the exact port it has sent to.
- [Which ways to a direct path work with an online server we can't change?](04-direct-path-options.md): without an inbound rule for the game, Windows Firewall is exactly such a filter, whatever the router does. The host's PC has no rule and shows no prompt.

So the host's router may already let the friend's packets in, and Windows then drops them. This needs no build, only the human and the friend.

**Added on 2026-11-16:** [Is the host's PC behind an address its provider shares, and does it have IPv6?](11-host-address-sharing.md) found the host behind its provider's NAT, with working IPv6. So the rule alone can only help if the provider's NAT lets the friend's packets through, and the friend's answer on IPv6 (item 4) decides whether an IPv6 direct path is open to this pair.

**Checklist for each player:**

1. Open PowerShell as administrator and add the rule for your own `lbp3.exe` (the rule is per full path of the program):
   `New-NetFirewallRule -DisplayName "LBP3 online co-op" -Direction Inbound -Program "<full path>\lbp3.exe" -Protocol UDP -Action Allow`
2. Join once on today's release and play a few minutes.
3. Send the report right after the session (the launcher keeps only two launches).
4. The friend also notes: his router's WAN address against the address in his log's "the server sees us at" line, whether UPnP is on in his router, and whether his PC has IPv6.

**Then read** the five-second `[p2p]` summaries in both logs: "direct" or "relayed", and the RTT.

Done when the logs say which. If direct: the first cause in the ranking is confirmed with Windows Firewall as the filter, and the rule goes to the top of the path ladder. If still relayed: [Which NAT gives the link a mapping per destination: the friend's, the host's or both?](12-which-nat.md) takes over.

The rule opens the game's UDP socket to packets from addresses it hasn't sent to. The link answers a stranger's HELLO only for friends. To undo it: `Remove-NetFirewallRule -DisplayName "LBP3 online co-op"`.
