// Exact math for online co-op. Online, LBP3 runs every player's world in lockstep and compares them
// ("The game diverged"). Its code uses vrcpps and vrsqrtps, approximations whose results differ
// between CPU makers (no problem on a PS4, where every machine is alike), and vdpps, whose order of
// additions isn't fixed. So an AMD player and an Intel player drift apart. Each of the 3,373 sites in
// eboot.bin and spu.prx jumps to a stub that computes the same thing exactly: 1/x as a division,
// 1/sqrt(x) as a square root and a division, the dot product as a fixed sequence of additions.
// Division and square root are correctly rounded, so every CPU gets the same bits.
//
// Ported from LBPPC (emurphy04, github.com/emurphy04/LBPPC), GPL-3.0-or-later, commit 951fb8d:
// src/probe.c (exact_math_install, exact_dpps, vex_op) and the site list src/exact_math_sites.inc.
// Changes here:
//   - every site and every instruction moved to a stub is checked with Zydis first (the instruction,
//     128-bit, no stack-relative operand, nothing moved that branches or uses the stack, the RIP field
//     where the list says). A site that fails is left as it is and logged.
//   - the vrsqrtps stub also fixes a RIP-relative operand of the site itself (LBPPC's doesn't).
//   - spu.prx's sites are patched in spu.prx wherever it's loaded (LBPPC's image has it at 0x16c0000).
//
// On when online (the levels server or shadNet), as in LBPPC. LBP3_EXACT_MATH=1 turns it on offline
// too, =0 off. The co-op link tells the other player whether it's on and refuses a connection when
// the two differ (net/p2p_link.h): such a session would drift apart.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace core {
class Module;
}

namespace core::exactmath {

// One site: its offset; the instruction's length; how many bytes go to the stub (at least 5, for the
// jump: a shorter site takes the next instruction along); where a RIP displacement sits in those bytes
// (0: none) and the end of the instruction it belongs to.
struct Site {
    uint32_t offset;
    uint8_t length, moved, ripAt, ripNext;
};

// LBPPC's image offset of spu.prx: the list's sites from here on are spu.prx's.
constexpr uint32_t kSpuBase = 0x16c0000;
// The longest stub BuildStub writes.
constexpr size_t kMaxStub = 96;

// LBP3_EXACT_MATH=1/0, else whether the game plays online.
bool Wanted();
// Every site of eboot.bin and spu.prx was sent to its stub (the co-op link's HELLO says so).
bool Active();

// eboot.bin and spu.prx: checks every site, then sends each one that passed to an exact stub. Call
// before the module's code is write-protected and before anything else patches it.
void Apply(Module& m);

// Why the site at `code` can't be sent to a stub (empty: it can). `code` holds the site and what
// follows it.
std::string Check(const uint8_t* code, const Site& s);

// Writes the stub for the site at `site` to `out`, which must be within a rel32 jump of the site and
// leave kMaxStub bytes; `one` points at 1.0f within a rel32 of `out`. The stub ends with a jump back
// to site + moved. Returns its length (0: a displacement doesn't reach). Doesn't touch the site.
size_t BuildStub(const uint8_t* site, const Site& s, uint8_t* out, const float* one);

}  // namespace core::exactmath
