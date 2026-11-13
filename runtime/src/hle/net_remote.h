// Online co-op (shadNet phase (c), hle/np_shadnet.cpp): the game's P2P datagrams to and from other
// players' games, through net/p2p_link.h. The loopback path (the Pod's local session, whose tick the
// game host thread's epoll wait drives) is untouched: only a datagram that isn't for 127.x and that
// the remote side takes leaves the machine. Unset (the socket layer is the offline one) unless
// shadNet is on.
#pragma once
#include <cstddef>
#include <cstdint>

namespace hle::p2pnet {

// ip: network order; ports host order. True when it was taken (sent to a peer).
using RemoteSend = bool (*)(uint32_t ip, uint16_t port, uint16_t srcVport, uint16_t dstVport, const void* data,
                            size_t len);
void SetRemoteSend(RemoteSend send);

// A datagram from another player: queued on the socket bound to dstVport (the game's P2P socket),
// as from ip:port and srcVport, for recvfrom and epoll. False when no socket is bound to it.
bool DeliverRemote(uint32_t ip, uint16_t port, uint16_t srcVport, uint16_t dstVport, const uint8_t* data, size_t len);

}  // namespace hle::p2pnet
