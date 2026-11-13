// Online co-op's peer connections (shadNet phase (c), best/shadnet_design.md): one UDP socket per
// player that carries the connection handshake, keepalives and the game's own P2P datagrams to the
// other players, directly or through shadNet's relay. Ours (GPL-2.0-or-later like the project).
//
// The server side is shadNet's UDP service (31314 by default):
//   - a STUN ping, every 5 s: [FF 83 FF FF FF FF][01][online ID, 16][LAN IPv4]; the reply carries
//     our address as the server sees it. The server keeps it for RequestSignalingInfos (the other
//     player asks it where we are) and for its relay.
//   - the relay (LBPPC's server patch): "SMRL" 02 [IPv4][port] packet goes to that endpoint as
//     "SMRL" 03 [sender's IPv4][port] packet; only between endpoints that have sent STUN pings.
// Between players (directly or relayed), our own packets: "L3P1", a type, then
//   HELLO 1 / ACK 2: from[16] to[16] ext IPv4[4] ext port[2] LAN IPv4[4] LAN port[2], then (since
//            v0.1.1; older links ignore it) capabilities[1] (1: DATA3) and the connection's epoch[4]
//            (new each time the sender sets up the connection: a new one restarts DATA3's counting)
//   DATA 3:  src vport[2] dst vport[2] sender[4] payload (the game's datagram; sender: a hash of
//            the sender's online ID, so it's recognised whatever address its packets come from)
//   PING 4 / PONG 5: from[16] time[8]
//   BYE 6:   from[16] to[16]
//   DATA2 7: as DATA, then seq[4] send time[8] (QueryPerformanceCounter), then the payload: only with
//            Config::diag (LBP3_P2P_DIAG=1 on both players; older builds drop it)
//   DATA3 8: sender[4] seq[4] count[1], then `count` entries for seq, seq-1, ...: src vport[2]
//            dst vport[2] length[2] payload. Used when both links have it (HELLO capabilities).
//   NACK 9:  from[16] first seq[4] count[2]: please send these DATA3 again
// (all big-endian / network order).
//
// A connection: Activate(peer) asks the server where the peer is (the resolve callback), then sends
// HELLO to every address it knows for them (the server's, then the LAN address the peer reports),
// and after 4 s through the relay as well, until an ACK comes. A HELLO from a player we didn't
// activate is answered only if the join check (mayConnect: friends) allows it; otherwise it's
// dropped and logged. Traffic goes directly once a packet has come directly from the peer, else
// through the relay. No packet for 15 s: the connection is dead. When both players are behind one
// public address, the LAN address goes first (no hairpin through the router): Config::path.
//
// To the game, a peer is identified by its "game address": the address the server sees for it
// (as the peer reports it in HELLO), or its LAN address when it has none.
//
// Gap-free delivery (DATA3): LBP3 can't survive some lost datagrams. After one, the host stops
// accepting the other player's traffic and ends the connection 10 s later (eboot+0x37b3f0), although
// that traffic arrives perfectly. So when both links have it, game datagrams are numbered and reach the
// game strictly in order with none missing: each DATA3 also carries the previous two payloads (an
// isolated loss is filled by the next datagram), a gap still open asks the sender again (NACK, from
// its last 256), and later datagrams wait until it's filled. A gap still open after 2 s is skipped
// and logged. When nothing new has gone out for 100 ms the last DATA3 goes once more (a lost last
// datagram has no later one to fill it in).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace net::p2p {

struct Endpoint {
    uint32_t ip = 0;    // network order
    uint16_t port = 0;  // host order
    bool Valid() const { return ip != 0 && port != 0; }
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
    bool operator<(const Endpoint& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
};
std::string ToString(const Endpoint& e);
Endpoint MakeEndpoint(const char* dottedIp, uint16_t port);

struct Config {
    std::string self;      // our online ID
    Endpoint server;       // shadNet's UDP service; invalid = no server (no STUN, no relay)
    uint16_t localPort = 0;  // 0 = any free port
    uint32_t lanIp = 0;    // our LAN address to report (network order); 0 = found from the route to the server
    bool reportLan = true;  // false: report no LAN address (tests: only the server's address and the relay)
    // The join check: may this player connect to us although we didn't ask for them?
    std::function<bool(const std::string& peer)> mayConnect;
    // Where the server says the peer is: calls done(ok, endpoint) once, from any thread.
    std::function<void(const std::string& peer, std::function<void(bool, Endpoint)> done)> resolve;
    int resendMs = 500, relayAfterMs = 4000, timeoutMs = 30000, keepaliveMs = 1000, deadAfterMs = 15000,
        stunMs = 5000;
    // Which way to send directly. Auto: the peer's LAN address when it is behind our public address
    // (same network), else wherever its packets come from. Lan / Public: that address only once it
    // answers. Relay: always through the server (tests). Any: wherever the peer's last packet came
    // from, as before (tests).
    enum class Path { Auto, Lan, Public, Relay, Any } path = Path::Auto;
    // Diagnostics: DATA2 (a sequence number and the send time) instead of DATA; the receiver logs
    // gaps, late packets, sender pauses and one-way delays, and the last 20 s of game datagrams are
    // kept for DumpRecent. Both players need it (an older build drops DATA2).
    bool diag = false;
    // Gap-free delivery (DATA3) with players whose links have it (LBP3_P2P_DATA3=0 turns it off).
    bool data3 = true;
    // Tests, on incoming game datagrams: drop `testLossBurst` out of every `testLossEvery`; hold every
    // `testLateEvery`th back 50 ms (its successors overtake it); handle every `testDupEvery`th twice.
    int testLossEvery = 0, testLossBurst = 1, testLateEvery = 0, testDupEvery = 0;
    bool testNoBye = false;  // tests: leave without BYE (as if it were lost)
};

enum class State { None, Pending, Active };

struct Event {
    enum Kind { PeerActivated, Established, Mutual, Dead } kind;  // Mutual: both sides activated it
    std::string peer;
    int error = 0;  // Dead: kErrorTimeout, kErrorPeerGone, kErrorUnreachable
};
constexpr int kErrorTimeout = 1, kErrorPeerGone = 2, kErrorUnreachable = 3;

struct PeerInfo {
    State state = State::None;
    Endpoint gameAddr;
    bool relayed = false;
    int rttUs = 0;
    bool locallyActivated = false, peerActivated = false;
};

class Link {
public:
    Link();
    ~Link();
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    bool Start(const Config& config);  // binds and starts the thread; false if the socket fails
    void Stop();
    bool Running() const;
    uint16_t LocalPort() const;
    Endpoint External() const;  // our address as the server sees it (invalid until a STUN reply)

    void Activate(const std::string& peer);
    void Terminate(const std::string& peer);
    PeerInfo Info(const std::string& peer) const;
    std::string PeerAt(const Endpoint& gameAddr) const;  // "" = none
    std::vector<Event> TakeEvents();

    // A game datagram to a peer's game address (or, when only one peer has that IP, any port of
    // it). False when no active peer has it.
    bool SendData(const Endpoint& gameAddr, uint16_t srcVport, uint16_t dstVport, const void* data, size_t len);
    using DataSink = std::function<void(const Endpoint& gameAddr, uint16_t srcVport, uint16_t dstVport,
                                        const uint8_t* data, size_t len)>;
    void SetDataSink(DataSink sink);  // called on the link's thread
    // Diagnostics: logs the game datagrams of the last 20 s (Config::diag), then forgets them.
    void DumpRecent(const char* why);

    struct Stats {
        uint64_t sentDirect = 0, sentRelayed = 0, received = 0, refused = 0;
        uint64_t dataSent = 0, dataReceived = 0, dataUnknown = 0, dataNoPeer = 0;
        // DATA3: datagrams filled in from a later one's copies, sent again on request, requests sent,
        // given up on; and incoming ones dropped by the test setting.
        uint64_t recovered = 0, resent = 0, nacks = 0, skipped = 0, testDropped = 0, testLate = 0, testDuplicated = 0;
    };
    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace net::p2p
