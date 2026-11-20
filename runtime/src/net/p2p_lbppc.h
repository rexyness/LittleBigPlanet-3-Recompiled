// LBPPC's peer protocol, so a player on this port can play online co-op with one on LBPPC
// (github.com/emurphy04/LBPPC), whose network layer is shadPS4's (both GPL-2.0-or-later). Ours: a
// re-implementation of the wire format from reading their code (LBPPC cb4cbe7, v0.1.5+), not their code.
// Used only in the LBPPC co-op mode (Config::lbppc, LBP3_LBPPC_COOP=1); docs/research/lbppc-coop.md.
//
// Every packet between two LBPPC players goes over one UDP socket (directly, or inside the server's
// "SMRL" relay frames, as ours), framed as a PS4 P2P datagram (core/net/p2p_codec.h there):
//   [FF][83][src vport, BE16][dst vport, BE16][payload]
// 0x83 = valid (0x80) | plain UDP datagram (3): no broadcast, no digest, no crypto. LBP3 never
// installs P2P keys or a communication ID (it imports no sceNetIoctl), so there are none.
//   - the game's datagrams: its own vports (3658 both ways) and payload, nothing else; the receiver
//     knows the sender by the packet's source address (or the relay's sender field);
//   - signaling: vports FFFF and FFFF, then one of LBPPC's packets (np_signaling_state.h there),
//     packed and LITTLE-endian, told apart by "SHAD", a type byte and the exact size:
//       echo ping 05 / pong 06 (17 bytes): conn id[4] time us[8] (a pong returns the ping's time);
//       handshake 07 (62 bytes): kind[1] from id[16] to id[16] from conn[4] to conn[4]
//                                mapped addr[4, network order] mapped port[2] reserved[2] nonce[8];
//         kinds: Offer 1, Accept 2, Check 3, CheckAck 4;
//       control 10 (50 bytes): kind[1] from id[16] to id[16] from conn[4] mapped addr[4]
//                              mapped port[2] reason[2];
//         kinds: ActivationRequest 1, ActivationAck 2, Close 3 (reason 0 terminate, 1 deactivate),
//                Established 4.
//   Our shadNet STUN ping already is such a frame (vports FFFF, then 01 and the online ID).
//
// A connection, as LBPPC runs it (signaling_handler.cpp there): only the joining player starts. It
// sends one ActivationRequest (again when it turns to the relay), then an Offer every 500 ms. The
// host checks the join (friends), answers ActivationAck, and from then on each side answers the
// other's handshake: Offer -> Accept, Accept -> Check (nonce = its clock), Check -> CheckAck (the
// same nonce). A side whose Check is answered is connected and sends Established once. Each side
// sends the handshake again every 500 ms until it is connected, through the relay once the peer has
// been silent 4 s, and gives up after 30 s. Connected, each side sends an echo ping every second
// (only for the RTT: nothing ends a connection but Close). Once anything has come through the relay
// from an IP, LBPPC sends everything to that IP through the relay.
//
// Our ActivationRequest carries kOurMarker in `reason`, which LBPPC never reads in one: a host of
// ours in the mode knows the joiner is ours and waits for its HELLO (two of our players always use
// our own protocol, whichever opening arrives first).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace net::p2p::lbppc {

constexpr uint8_t kMarker = 0xFF, kPlainDatagram = 0x83;
constexpr uint16_t kSignalingVport = 0xFFFF;
constexpr uint8_t kShad[4] = {'S', 'H', 'A', 'D'};
enum : uint8_t { kEchoPing = 0x05, kEchoPong = 0x06, kHandshake = 0x07, kControl = 0x10 };
enum : uint8_t { kOffer = 1, kAccept = 2, kCheck = 3, kCheckAck = 4 };
enum : uint8_t { kActivationRequest = 1, kActivationAck = 2, kClose = 3, kEstablished = 4 };
constexpr size_t kEchoLen = 17, kHandshakeLen = 62, kControlLen = 50, kIdLen = 16;
constexpr uint16_t kOurMarker = 0x334C;  // "L3" as LBPPC's little-endian u16 would store it

// A frame of either kind: the game's datagram (its vports) or a signaling packet (FFFF, FFFF).
inline std::vector<uint8_t> Frame(uint16_t srcVport, uint16_t dstVport, const uint8_t* body, size_t len) {
    std::vector<uint8_t> b{kMarker, kPlainDatagram, uint8_t(srcVport >> 8), uint8_t(srcVport), uint8_t(dstVport >> 8),
                           uint8_t(dstVport)};
    b.insert(b.end(), body, body + len);
    return b;
}
inline std::vector<uint8_t> Signaling(const std::vector<uint8_t>& body) {
    return Frame(kSignalingVport, kSignalingVport, body.data(), body.size());
}

// What a packet from a peer is, if it is LBPPC's at all.
struct Parsed {
    enum Kind { None, Data, Signaling } kind = None;
    uint16_t srcVport = 0, dstVport = 0;
    const uint8_t* body = nullptr;
    size_t len = 0;
};
inline Parsed Parse(const uint8_t* d, size_t n) {
    Parsed p;
    // Only plain datagrams (their codec also knows streams, broadcasts, digests and crypto, which
    // LBP3's traffic never uses).
    if (n < 6 || d[0] != kMarker || d[1] != kPlainDatagram) return p;
    p.srcVport = uint16_t(d[2] << 8 | d[3]);
    p.dstVport = uint16_t(d[4] << 8 | d[5]);
    p.body = d + 6;
    p.len = n - 6;
    p.kind = p.srcVport == kSignalingVport && p.dstVport == kSignalingVport ? Parsed::Signaling : Parsed::Data;
    return p;
}

namespace detail {
inline void PutLe(std::vector<uint8_t>& b, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
inline uint64_t GetLe(const uint8_t* p, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = v << 8 | p[i];
    return v;
}
inline void PutId(std::vector<uint8_t>& b, const std::string& id) {
    uint8_t buf[kIdLen] = {};
    std::memcpy(buf, id.data(), id.size() < kIdLen ? id.size() : kIdLen);
    b.insert(b.end(), buf, buf + kIdLen);
}
inline std::string GetId(const uint8_t* p) {
    size_t n = 0;
    while (n < kIdLen && p[n]) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}
inline std::vector<uint8_t> Head(uint8_t type) {
    std::vector<uint8_t> b(kShad, kShad + 4);
    b.push_back(type);
    return b;
}
}  // namespace detail

struct Echo {
    uint8_t type = kEchoPing;
    uint32_t connId = 0;
    uint64_t timeUs = 0;
};
struct Handshake {
    uint8_t kind = 0;
    std::string from, to;
    uint32_t fromConn = 0, toConn = 0;
    uint32_t mappedAddr = 0;  // network order (an in_addr's s_addr)
    uint16_t mappedPort = 0, reserved = 0;
    uint64_t nonce = 0;
};
struct Control {
    uint8_t kind = 0;
    std::string from, to;
    uint32_t fromConn = 0;
    uint32_t mappedAddr = 0;  // network order
    uint16_t mappedPort = 0, reason = 0;
};

inline std::vector<uint8_t> Encode(const Echo& e) {
    auto b = detail::Head(e.type);
    detail::PutLe(b, e.connId, 4);
    detail::PutLe(b, e.timeUs, 8);
    return b;
}
inline std::vector<uint8_t> Encode(const Handshake& h) {
    auto b = detail::Head(kHandshake);
    b.push_back(h.kind);
    detail::PutId(b, h.from);
    detail::PutId(b, h.to);
    detail::PutLe(b, h.fromConn, 4);
    detail::PutLe(b, h.toConn, 4);
    const auto* a = reinterpret_cast<const uint8_t*>(&h.mappedAddr);
    b.insert(b.end(), a, a + 4);
    detail::PutLe(b, h.mappedPort, 2);
    detail::PutLe(b, h.reserved, 2);
    detail::PutLe(b, h.nonce, 8);
    return b;
}
inline std::vector<uint8_t> Encode(const Control& c) {
    auto b = detail::Head(kControl);
    b.push_back(c.kind);
    detail::PutId(b, c.from);
    detail::PutId(b, c.to);
    detail::PutLe(b, c.fromConn, 4);
    const auto* a = reinterpret_cast<const uint8_t*>(&c.mappedAddr);
    b.insert(b.end(), a, a + 4);
    detail::PutLe(b, c.mappedPort, 2);
    detail::PutLe(b, c.reason, 2);
    return b;
}

// A signaling body's type, or 0 when it isn't one of LBPPC's (as their handler tells them apart:
// the magic, the type and the exact size).
inline uint8_t TypeOf(const uint8_t* body, size_t len) {
    if (len < 5 || std::memcmp(body, kShad, 4) != 0) return 0;
    const uint8_t t = body[4];
    if ((t == kEchoPing || t == kEchoPong) && len == kEchoLen) return t;
    if (t == kHandshake && len == kHandshakeLen) return t;
    if (t == kControl && len == kControlLen) return t;
    return 0;
}
inline Echo DecodeEcho(const uint8_t* b) {
    return {b[4], uint32_t(detail::GetLe(b + 5, 4)), detail::GetLe(b + 9, 8)};
}
inline Handshake DecodeHandshake(const uint8_t* b) {
    Handshake h;
    h.kind = b[5];
    h.from = detail::GetId(b + 6);
    h.to = detail::GetId(b + 22);
    h.fromConn = uint32_t(detail::GetLe(b + 38, 4));
    h.toConn = uint32_t(detail::GetLe(b + 42, 4));
    std::memcpy(&h.mappedAddr, b + 46, 4);
    h.mappedPort = uint16_t(detail::GetLe(b + 50, 2));
    h.reserved = uint16_t(detail::GetLe(b + 52, 2));
    h.nonce = detail::GetLe(b + 54, 8);
    return h;
}
inline Control DecodeControl(const uint8_t* b) {
    Control c;
    c.kind = b[5];
    c.from = detail::GetId(b + 6);
    c.to = detail::GetId(b + 22);
    c.fromConn = uint32_t(detail::GetLe(b + 38, 4));
    std::memcpy(&c.mappedAddr, b + 42, 4);
    c.mappedPort = uint16_t(detail::GetLe(b + 46, 2));
    c.reason = uint16_t(detail::GetLe(b + 48, 2));
    return c;
}

}  // namespace net::p2p::lbppc
