// Exact math for online co-op (core/exact_math.h). The stub generator is ported from LBPPC (emurphy04,
// github.com/emurphy04/LBPPC), GPL-3.0-or-later, commit 951fb8d, src/probe.c: vex_op, exact_dpps and
// exact_math_install; the site list is their src/exact_math_sites.inc.
#include "core/exact_math.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <Zydis/Zydis.h>

#include "core/module.h"
#include "hle/online.h"

namespace core::exactmath {
namespace {

const Site kSites[] = {
#include "core/exact_math_sites.inc"
};

std::atomic<int> g_ebootDone{0}, g_spuDone{0};  // 0: not yet, 1: applied, -1: not applied
std::atomic<bool> g_spuHasSites{false};

bool EnvOn(const char* name, bool* value) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    *value = v[0] == '1';
    return true;
}

// Executable memory within reach of a rel32 jump from `anchor` (as res_patches.cpp's AllocNear).
uint8_t* AllocNear(const uint8_t* anchor, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t start = reinterpret_cast<uintptr_t>(anchor) & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000u; d += gran) {
        for (const uintptr_t at : {start - d, start + d}) {
            if (at > start + d || at + d < start) continue;  // wrapped around
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

bool Rel32(const uint8_t* end, const void* to, int32_t* out) {
    const int64_t d = static_cast<const uint8_t*>(to) - end;
    if (d != int64_t(int32_t(d))) return false;
    *out = int32_t(d);
    return true;
}

// A 3-byte VEX instruction on registers: map 1 (0F) or 3 (0F3A), prefix pp, 128 bits. (LBPPC vex_op)
uint8_t* VexOp(uint8_t* o, unsigned map, unsigned pp, unsigned opcode, unsigned reg, unsigned first, unsigned rm) {
    *o++ = 0xc4;
    *o++ = uint8_t((reg < 8 ? 0x80 : 0) | 0x40 | (rm < 8 ? 0x20 : 0) | map);
    *o++ = uint8_t(((~first & 15) << 3) | pp);
    *o++ = uint8_t(opcode);
    *o++ = uint8_t(0xc0 | ((reg & 7) << 3) | (rm & 7));
    return o;
}

// vdpps as Intel documents it: (x0*y0+x1*y1)+(x2*y2+x3*y3), the elements not in the mask +0.
// (LBPPC exact_dpps)
uint8_t* ExactDpps(uint8_t* o, const uint8_t* site) {
    const unsigned dst = ((site[4] >> 3) & 7) | (site[1] & 0x80 ? 0 : 8), rm = (site[4] & 7) | (site[1] & 0x20 ? 0 : 8);
    const unsigned first = (~(site[2] >> 3)) & 15, mask = site[5];
    unsigned t = 0, z;
    while (t == dst || t == first || t == rm) ++t;
    z = t + 1;
    while (z == dst || z == first || z == rm) ++z;
    static const uint8_t below[] = {0x48, 0x8d, 0xa4, 0x24, 0x40, 0xff, 0xff, 0xff};  // lea rsp,[rsp-192]: past the red zone
    static const uint8_t above[] = {0x48, 0x8d, 0xa4, 0x24, 0xc0, 0x00, 0x00, 0x00};  // lea rsp,[rsp+192]
    std::memcpy(o, below, 8);
    o += 8;
    *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x11; *o++ = uint8_t(0x04 | (t << 3)); *o++ = 0x24;               // vmovups [rsp],ymmT
    *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x11; *o++ = uint8_t(0x44 | (z << 3)); *o++ = 0x24; *o++ = 0x20;  // vmovups [rsp+32],ymmZ
    o = VexOp(o, 1, 0, 0x59, t, first, rm);                                                           // vmulps t,first,rm
    o = VexOp(o, 1, 0, 0x57, z, z, z);                                                                // vxorps z,z,z
    o = VexOp(o, 3, 1, 0x0c, t, z, t); *o++ = uint8_t(mask >> 4);                                     // vblendps t,z,t,mask>>4
    o = VexOp(o, 1, 3, 0x7c, t, t, t);                                                                // vhaddps t,t,t
    o = VexOp(o, 1, 3, 0x7c, t, t, t);                                                                // vhaddps t,t,t
    o = VexOp(o, 1, 0, 0xc6, t, t, t); *o++ = 0;                                                      // vshufps t,t,t,0
    o = VexOp(o, 3, 1, 0x0c, dst, z, t); *o++ = uint8_t(mask & 15);                                   // vblendps dst,z,t,mask&15
    *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x10; *o++ = uint8_t(0x44 | (z << 3)); *o++ = 0x24; *o++ = 0x20;  // vmovups ymmZ,[rsp+32]
    *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x10; *o++ = uint8_t(0x04 | (t << 3)); *o++ = 0x24;               // vmovups ymmT,[rsp]
    std::memcpy(o, above, 8);
    o += 8;
    return o;
}

// Copies site bytes [from, to) to `o` and re-aims the RIP displacement if it lies in them.
bool CopyRelocated(uint8_t* o, const uint8_t* site, const Site& s, unsigned from, unsigned to) {
    std::memcpy(o, site + from, to - from);
    if (s.ripAt && s.ripAt >= from && s.ripAt < to) {
        int32_t d;
        std::memcpy(&d, site + s.ripAt, 4);
        const uint8_t* target = site + s.ripNext + d;
        if (!Rel32(o + (s.ripNext - from), target, &d)) return false;
        std::memcpy(o + (s.ripAt - from), &d, 4);
    }
    return true;
}

bool UsesStack(const ZydisDecodedInstruction& in, const ZydisDecodedOperand* ops) {
    for (unsigned i = 0; i < in.operand_count; ++i) {
        const ZydisDecodedOperand& op = ops[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            (op.reg.value == ZYDIS_REGISTER_RSP || op.reg.value == ZYDIS_REGISTER_ESP || op.reg.value == ZYDIS_REGISTER_SP))
            return true;
        if (op.type == ZYDIS_OPERAND_TYPE_MEMORY &&
            (op.mem.base == ZYDIS_REGISTER_RSP || op.mem.base == ZYDIS_REGISTER_ESP || op.mem.index == ZYDIS_REGISTER_RSP))
            return true;
    }
    return false;
}

int RipOperands(const ZydisDecodedInstruction& in, const ZydisDecodedOperand* ops) {
    int n = 0;
    for (unsigned i = 0; i < in.operand_count; ++i)
        if (ops[i].type == ZYDIS_OPERAND_TYPE_MEMORY && ops[i].mem.base == ZYDIS_REGISTER_RIP) ++n;
    return n;
}

}  // namespace

std::string Check(const uint8_t* code, const Site& s) {
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction in;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (s.moved < 5 || s.moved < s.length) return "too short for the jump";
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code, 15, &in, ops))) return "not an instruction";
    if (in.length != s.length) return "the instruction's length differs from the list's";
    const bool dpps = in.mnemonic == ZYDIS_MNEMONIC_VDPPS;
    if (in.mnemonic != ZYDIS_MNEMONIC_VRCPPS && in.mnemonic != ZYDIS_MNEMONIC_VRSQRTPS && !dpps)
        return "not vrcpps, vrsqrtps or vdpps";
    if (in.encoding != ZYDIS_INSTRUCTION_ENCODING_VEX || (code[0] != 0xc4 && code[0] != 0xc5)) return "not VEX-encoded";
    if (in.avx.vector_length != 128) return "256-bit (the stub's 1.0 is 128-bit)";
    if (UsesStack(in, ops)) return "a stack-relative operand (the stub moves rsp)";
    if (dpps) {
        // LBPPC's form: c4, map 3, opcode 40, register operands, 6 bytes.
        if (s.length != 6 || (code[1] & 0x1f) != 3 || code[3] != 0x40 || (code[4] >> 6) != 3) return "vdpps form";
    } else {
        const unsigned opAt = code[0] == 0xc5 ? 2 : 3;
        if (code[0] == 0xc4 && (code[1] & 0x1f) != 1) return "not map 0F";
        if (code[opAt] != 0x52 && code[opAt] != 0x53) return "opcode";
    }
    int rip = RipOperands(in, ops);
    unsigned ripAt = rip ? in.raw.disp.offset : 0, ripNext = s.length;
    if (rip && in.raw.disp.size != 32) return "RIP displacement size";
    if (s.moved > s.length) {
        ZydisDecodedInstruction mv;
        ZydisDecodedOperand mops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + s.length, 15, &mv, mops)))
            return "the moved instruction isn't one";
        if (mv.length != s.moved - s.length) return "the moved bytes aren't one instruction";
        switch (mv.meta.category) {
        case ZYDIS_CATEGORY_COND_BR:
        case ZYDIS_CATEGORY_UNCOND_BR:
        case ZYDIS_CATEGORY_CALL:
        case ZYDIS_CATEGORY_RET:
        case ZYDIS_CATEGORY_SYSTEM:
        case ZYDIS_CATEGORY_INTERRUPT: return "the moved instruction branches";
        default: break;
        }
        if (mv.mnemonic == ZYDIS_MNEMONIC_VRCPPS || mv.mnemonic == ZYDIS_MNEMONIC_VRSQRTPS ||
            mv.mnemonic == ZYDIS_MNEMONIC_VDPPS)
            return "the moved instruction is a site too";
        if (UsesStack(mv, mops)) return "the moved instruction uses the stack";
        for (unsigned i = 0; i < mv.operand_count; ++i)
            if (mops[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                (mops[i].mem.segment == ZYDIS_REGISTER_FS || mops[i].mem.segment == ZYDIS_REGISTER_GS))
                return "the moved instruction uses fs or gs";
        if (const int r = RipOperands(mv, mops)) {
            if (rip || r > 1) return "two RIP-relative operands";
            if (mv.raw.disp.size != 32) return "RIP displacement size";
            rip = r;
            ripAt = s.length + mv.raw.disp.offset;
            ripNext = s.moved;
        }
    }
    if (rip ? (s.ripAt != ripAt || s.ripNext != ripNext) : s.ripAt != 0) return "the RIP field differs from the list's";
    return {};
}

size_t BuildStub(const uint8_t* site, const Site& s, uint8_t* out, const float* one) {
    uint8_t* o = out;
    int32_t d;
    if (site[0] == 0xc4 && (site[1] & 0x1f) == 3) {
        o = ExactDpps(o, site);
    } else {
        const bool vex2 = site[0] == 0xc5;
        const unsigned opAt = vex2 ? 2 : 3, modrm = site[opAt + 1];
        const bool sqrt = site[opAt] == 0x52;
        const unsigned dst = ((modrm >> 3) & 7) | (site[1] & 0x80 ? 0 : 8);
        const unsigned src = (modrm >> 6) == 3 ? ((modrm & 7) | (!vex2 && !(site[1] & 0x20) ? 8 : 0)) : 16;
        unsigned t = 0;
        while (t == dst || t == src) ++t;
        static const uint8_t below[] = {0x48, 0x8d, 0xa4, 0x24, 0x60, 0xff, 0xff, 0xff};  // lea rsp,[rsp-160]: past the red zone
        static const uint8_t above[] = {0x48, 0x8d, 0xa4, 0x24, 0xa0, 0x00, 0x00, 0x00};  // lea rsp,[rsp+160]
        std::memcpy(o, below, 8);
        o += 8;
        *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x11; *o++ = uint8_t(0x04 | (t << 3)); *o++ = 0x24;  // vmovups [rsp],ymmT
        *o++ = 0xc4; *o++ = 0xe2; *o++ = 0x79; *o++ = 0x18; *o++ = uint8_t(0x05 | (t << 3));  // vbroadcastss xmmT,[rip+one]
        if (!Rel32(o + 4, one, &d)) return 0;
        std::memcpy(o, &d, 4);
        o += 4;
        if (!CopyRelocated(o, site, s, 0, s.length)) return 0;  // LBPPC re-aims the rcpps form only
        if (sqrt) {
            o[opAt] = 0x51;  // vsqrtps dst,src
            o += s.length;
            *o++ = 0xc4; *o++ = uint8_t(0x41 | (dst < 8 ? 0xa0 : 0));  // vdivps dst,xmmT,dst
            *o++ = uint8_t((~t & 15) << 3); *o++ = 0x5e; *o++ = uint8_t(0xc0 | ((dst & 7) << 3) | (dst & 7));
        } else {
            // The site itself as vdivps dst,xmmT,src: the same operands, another opcode and first source.
            o[opAt] = 0x5e;
            o[opAt - 1] = uint8_t((o[opAt - 1] & 0x87) | ((~t & 15) << 3));
            o += s.length;
        }
        *o++ = 0xc5; *o++ = 0xfc; *o++ = 0x10; *o++ = uint8_t(0x04 | (t << 3)); *o++ = 0x24;  // vmovups ymmT,[rsp]
        std::memcpy(o, above, 8);
        o += 8;
    }
    if (s.moved > s.length) {
        if (!CopyRelocated(o, site, s, s.length, s.moved)) return 0;
        o += s.moved - s.length;
    }
    *o++ = 0xe9;  // jmp site+moved
    if (!Rel32(o + 4, site + s.moved, &d)) return 0;
    std::memcpy(o, &d, 4);
    o += 4;
    return size_t(o - out);
}

bool Wanted() {
    static const bool wanted = [] {
        bool v = false;
        if (EnvOn("LBP3_EXACT_MATH", &v)) return v;
        const char* sn = std::getenv("LBP3_SHADNET");
        return hle::online::Enabled() || (sn && sn[0] == '1');
    }();
    return wanted;
}

bool Active() { return g_ebootDone.load() == 1 && (!g_spuHasSites.load() || g_spuDone.load() == 1); }

void Apply(Module& m) {
    const bool eboot = m.fileName == "eboot.bin", spu = m.fileName == "spu.prx";
    if ((!eboot && !spu) || !Wanted()) return;
    std::vector<Site> sites;
    for (const Site& s : kSites) {
        if (eboot && s.offset < kSpuBase) sites.push_back(s);
        if (spu && s.offset >= kSpuBase) sites.push_back({s.offset - kSpuBase, s.length, s.moved, s.ripAt, s.ripNext});
    }
    if (spu) g_spuHasSites = !sites.empty();
    std::atomic<int>& done = eboot ? g_ebootDone : g_spuDone;

    // Every site first: the bytes the list expects (LBPPC: else nothing changes), then the checks.
    std::vector<const Site*> ok;
    int skipped = 0;
    for (const Site& s : sites) {
        if (uint64_t(s.offset) + 15 > m.size) {
            std::fprintf(stderr, "[exactmath] %s+0x%x: outside the module, NOT applied (online co-op may diverge)\n",
                         m.fileName.c_str(), s.offset);
            done = -1;
            return;
        }
        const uint8_t* code = m.Addr(s.offset);
        const uint8_t op = code[0] == 0xc5 ? code[2] : code[3];
        const bool dpps = code[0] == 0xc4 && (code[1] & 0x1f) == 3 && op == 0x40;
        if ((code[0] != 0xc5 && code[0] != 0xc4) || (op != 0x52 && op != 0x53 && !dpps)) {
            std::fprintf(stderr,
                         "[exactmath] %s+0x%x: unexpected game code, NOT applied (online co-op may diverge)\n",
                         m.fileName.c_str(), s.offset);
            done = -1;
            return;
        }
        if (const std::string why = Check(code, s); !why.empty()) {
            if (++skipped <= 20)
                std::fprintf(stderr, "[exactmath] %s+0x%x left as it is: %s\n", m.fileName.c_str(), s.offset, why.c_str());
            continue;
        }
        ok.push_back(&s);
    }

    // The stubs, in 64 KB blocks near the code, each starting with the 1.0 they load. All are built
    // before any site changes, so a displacement that doesn't reach leaves the module untouched.
    struct Planned {
        uint8_t* site;
        const Site* s;
        uint8_t* stub;
    };
    std::vector<Planned> plan;
    uint8_t* block = nullptr;
    size_t used = 0, blocks = 0;
    for (const Site* s : ok) {
        uint8_t* site = m.Addr(s->offset);
        if (!block || used + kMaxStub > 65536) {
            block = AllocNear(site, 65536);
            if (!block) {
                std::fprintf(stderr, "[exactmath] %s: no memory near the code, NOT applied\n", m.fileName.c_str());
                done = -1;
                return;
            }
            const float one = 1.0f;
            std::memcpy(block, &one, 4);
            used = 16;
            ++blocks;
        }
        const size_t n = BuildStub(site, *s, block + used, reinterpret_cast<const float*>(block));
        int32_t rel;
        if (!n || n > kMaxStub || !Rel32(site + 5, block + used, &rel)) {
            std::fprintf(stderr, "[exactmath] %s+0x%x: the stub is out of reach, NOT applied\n", m.fileName.c_str(),
                         s->offset);
            done = -1;
            return;
        }
        plan.push_back({site, s, block + used});
        used += (n + 15) & ~size_t(15);
    }
    for (const Planned& p : plan) {
        int32_t rel;
        Rel32(p.site + 5, p.stub, &rel);
        p.site[0] = 0xe9;  // jmp stub, then nops over the rest of the moved bytes
        std::memcpy(p.site + 1, &rel, 4);
        for (unsigned k = 5; k < p.s->moved; ++k) p.site[k] = 0x90;
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    // A site the checks left out is left out the same way on every PC with this version, so the
    // co-op link still says "on" (its partners patched the same sites).
    done = 1;
    std::fprintf(stderr, "[exactmath] %s: exact math at %zu of %zu sites (%zu blocks of stubs)%s\n", m.fileName.c_str(),
                 plan.size(), sites.size(), blocks, skipped ? "; some left as they are (online co-op may diverge)" : "");
}

}  // namespace core::exactmath
