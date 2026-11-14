// Exact math (core/exact_math.h): the stubs compute exactly what they replace, and nothing else changes.
// Synthetic sites of every form the game's list has (vrcpps, vrsqrtps and vdpps; low and high registers;
// RIP-relative operands; a moved instruction, with and without its own RIP operand) are run twice from
// the same random register state through an assembly harness: the original code, and through its stub.
// Expected: the stub's run equals the original's except the destination, which holds the exact result
// (1/x, 1/sqrt(x), the dot product in (x0*y0+x1*y1)+(x2*y2+x3*y3) order) with the upper half zeroed;
// every other ymm register (full width) and the flags as the original left them; the 128 bytes below
// the stack pointer (the game's red zone) untouched. Under both the Windows MXCSR and the game's
// (0x9fc0). Also: the checks refuse what the stubs can't do. Offline; no game.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <immintrin.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/exact_math.h"

namespace {
int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(c)) {                                                                                \
            ++g_failed;                                                                            \
            if (g_failed <= 40) std::printf("  FAIL (line %d): %s\n", __LINE__, #c);               \
        }                                                                                          \
    } while (0)

struct alignas(32) State {
    uint8_t ymm[16][32];
    uint64_t flags;
    uint64_t canaryBad;
    uint64_t pad[2];
};
static_assert(offsetof(State, flags) == 512 && offsetof(State, canaryBad) == 520);

}  // namespace

// exact_harness(in, out, fn): loads ymm0-15 and the arithmetic flags from `in`, fills the 128 bytes
// below the callee's stack pointer with 0xA5, calls fn, then stores ymm0-15, the flags and how many
// of those 128 bytes changed to `out`. Keeps the Windows callee-saved registers.
extern "C" void exact_harness(const State* in, State* out, const void* fn);
__asm__(R"(
    .text
    .globl exact_harness
    .def exact_harness; .scl 2; .type 32; .endef
exact_harness:
    .intel_syntax noprefix
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 168
    movdqu [rsp+0], xmm6
    movdqu [rsp+16], xmm7
    movdqu [rsp+32], xmm8
    movdqu [rsp+48], xmm9
    movdqu [rsp+64], xmm10
    movdqu [rsp+80], xmm11
    movdqu [rsp+96], xmm12
    movdqu [rsp+112], xmm13
    movdqu [rsp+128], xmm14
    movdqu [rsp+144], xmm15
    mov r12, rdx
    mov r13, r8
    mov r14, rcx
    lea rdi, [rsp-136]
    mov rcx, 128
    mov al, 0xA5
    rep stosb
    vmovdqu ymm0, [r14+0]
    vmovdqu ymm1, [r14+32]
    vmovdqu ymm2, [r14+64]
    vmovdqu ymm3, [r14+96]
    vmovdqu ymm4, [r14+128]
    vmovdqu ymm5, [r14+160]
    vmovdqu ymm6, [r14+192]
    vmovdqu ymm7, [r14+224]
    vmovdqu ymm8, [r14+256]
    vmovdqu ymm9, [r14+288]
    vmovdqu ymm10, [r14+320]
    vmovdqu ymm11, [r14+352]
    vmovdqu ymm12, [r14+384]
    vmovdqu ymm13, [r14+416]
    vmovdqu ymm14, [r14+448]
    vmovdqu ymm15, [r14+480]
    push qword ptr [r14+512]
    popfq
    call r13
    pushfq
    pop qword ptr [r12+512]
    vmovdqu [r12+0], ymm0
    vmovdqu [r12+32], ymm1
    vmovdqu [r12+64], ymm2
    vmovdqu [r12+96], ymm3
    vmovdqu [r12+128], ymm4
    vmovdqu [r12+160], ymm5
    vmovdqu [r12+192], ymm6
    vmovdqu [r12+224], ymm7
    vmovdqu [r12+256], ymm8
    vmovdqu [r12+288], ymm9
    vmovdqu [r12+320], ymm10
    vmovdqu [r12+352], ymm11
    vmovdqu [r12+384], ymm12
    vmovdqu [r12+416], ymm13
    vmovdqu [r12+448], ymm14
    vmovdqu [r12+480], ymm15
    lea rsi, [rsp-136]
    xor eax, eax
    mov rcx, 128
.Lexact_canary_loop:
    cmp byte ptr [rsi], 0xA5
    je .Lexact_canary_next
    inc rax
.Lexact_canary_next:
    inc rsi
    dec rcx
    jnz .Lexact_canary_loop
    mov [r12+520], rax
    vzeroupper
    movdqu xmm6, [rsp+0]
    movdqu xmm7, [rsp+16]
    movdqu xmm8, [rsp+32]
    movdqu xmm9, [rsp+48]
    movdqu xmm10, [rsp+64]
    movdqu xmm11, [rsp+80]
    movdqu xmm12, [rsp+96]
    movdqu xmm13, [rsp+112]
    movdqu xmm14, [rsp+128]
    movdqu xmm15, [rsp+144]
    add rsp, 168
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
    .att_syntax
)");

namespace {

using core::exactmath::Site;

enum class Kind { Rcp, Rsqrt, Dpps };

struct Case {
    const char* name;
    std::vector<uint8_t> code;  // the site and the moved instruction; RIP displacements patched in
    Site site;                  // offset unused
    Kind kind;
    int dst, src, src2;         // registers (src -1: the RIP data); src2 for vdpps
    int ripDispAt = -1;         // where to put the displacement to the data (in `code`), or -1
    int ripEnd = 0;             // the end of that instruction
    uint8_t mask = 0;           // vdpps
};

float Bits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t U(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// A float worth testing: mostly normal numbers over the whole range, and the special ones.
float RandomFloat(std::mt19937& rng) {
    const uint32_t r = rng();
    switch (r % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return INFINITY;
    case 3: return -INFINITY;
    case 4: return Bits(0x7fc00000u | (rng() & 0x3fffff));  // NaN
    case 5: return Bits((rng() & 0x807fffffu) | 1u);        // denormal
    case 6: return Bits(((rng() & 1) << 31) | (uint32_t(1 + rng() % 4) << 23) | (rng() & 0x7fffff));  // tiny
    default: return Bits((rng() & 0x80000000u) | (uint32_t(1 + rng() % 254) << 23) | (rng() & 0x7fffff));
    }
}

// The exact result of one site on four lanes (volatile: no folding, the runtime MXCSR decides).
void Reference(const Case& c, const float* a, const float* b, float* out) {
    if (c.kind == Kind::Rcp) {
        for (int i = 0; i < 4; ++i) {
            volatile float one = 1.0f, x = a[i];
            out[i] = one / x;
        }
    } else if (c.kind == Kind::Rsqrt) {
        for (int i = 0; i < 4; ++i) {
            volatile float one = 1.0f, x = a[i];
            const __m128 s = _mm_sqrt_ss(_mm_set_ss(x));
            volatile float r = _mm_cvtss_f32(s);
            out[i] = one / r;
        }
    } else {
        volatile float p[4];
        for (int i = 0; i < 4; ++i) {
            volatile float x = a[i], y = b[i];
            p[i] = (c.mask >> (4 + i)) & 1 ? x * y : 0.0f;
        }
        volatile float lo = p[0] + p[1], hi = p[2] + p[3];
        volatile float sum = lo + hi;
        for (int i = 0; i < 4; ++i) out[i] = (c.mask >> i) & 1 ? float(sum) : 0.0f;
    }
}

bool SameBits(const uint8_t* a, const uint8_t* b, size_t n) { return std::memcmp(a, b, n) == 0; }

void RunCase(const Case& c, uint8_t* region, std::mt19937& rng, unsigned csr) {
    uint8_t* site = region + 0x1000;
    float* data = reinterpret_cast<float*>(region + 0x2000);
    uint8_t* block = region + 0x10000;
    std::memset(site, 0xcc, 64);
    std::memcpy(site, c.code.data(), c.code.size());
    if (c.ripDispAt >= 0) {
        const int32_t d = int32_t(reinterpret_cast<uint8_t*>(data) - (site + c.ripEnd));
        std::memcpy(site + c.ripDispAt, &d, 4);
    }
    site[c.site.moved] = 0xc3;  // the stub jumps back here: ret
    const std::string why = core::exactmath::Check(site, c.site);
    CHECK(why.empty());
    if (!why.empty()) {
        std::printf("    %s: Check says %s\n", c.name, why.c_str());
        return;
    }
    const float one = 1.0f;
    std::memcpy(block, &one, 4);
    const size_t n = core::exactmath::BuildStub(site, c.site, block + 16, reinterpret_cast<const float*>(block));
    CHECK(n > 0 && n <= core::exactmath::kMaxStub);
    if (!n) return;
    FlushInstructionCache(GetCurrentProcess(), region, 0x20000);

    int mismatches = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        State in{}, native{}, stub{};
        for (int r = 0; r < 16; ++r)
            for (int l = 0; l < 8; ++l) {
                const float f = RandomFloat(rng);
                std::memcpy(&in.ymm[r][l * 4], &f, 4);
            }
        in.flags = 0x202 | (rng() & 0x8d5);
        for (int i = 0; i < 4; ++i) data[i] = RandomFloat(rng);

        _mm_setcsr(csr);
        exact_harness(&in, &native, site);
        exact_harness(&in, &stub, block + 16);
        // The exact result for the destination.
        float a[4], b[4], r[4];
        for (int i = 0; i < 4; ++i) {
            a[i] = c.src < 0 ? data[i] : Bits(U(*reinterpret_cast<const float*>(&in.ymm[c.src][i * 4])));
            b[i] = c.src2 >= 0 ? *reinterpret_cast<const float*>(&in.ymm[c.src2][i * 4]) : 0.0f;
        }
        Reference(c, a, b, r);
        _mm_setcsr(0x1f80);
        State expect = native;
        std::memset(expect.ymm[c.dst], 0, 32);
        std::memcpy(expect.ymm[c.dst], r, 16);
        bool ok = SameBits(&expect.ymm[0][0], &stub.ymm[0][0], 512) && (expect.flags & 0x8d5) == (stub.flags & 0x8d5) &&
                  stub.canaryBad == 0 && native.canaryBad == 0;
        if (!ok && ++mismatches <= 3) {
            std::printf("    %s (MXCSR %04x): mismatch at iteration %d\n", c.name, csr, iter);
            for (int reg = 0; reg < 16; ++reg)
                if (!SameBits(expect.ymm[reg], stub.ymm[reg], 32))
                    std::printf("      ymm%d lane0 expected %08x got %08x (input %08x)\n", reg, U(*reinterpret_cast<float*>(expect.ymm[reg])),
                                U(*reinterpret_cast<float*>(stub.ymm[reg])), U(a[0]));
            if ((expect.flags & 0x8d5) != (stub.flags & 0x8d5))
                std::printf("      flags %llx vs %llx\n", (unsigned long long)expect.flags, (unsigned long long)stub.flags);
            if (stub.canaryBad) std::printf("      %llu red-zone bytes changed\n", (unsigned long long)stub.canaryBad);
        }
        CHECK(ok);
    }
    std::printf("  %-44s MXCSR %04x: %s (stub %zu bytes)\n", c.name, csr, mismatches ? "MISMATCH" : "exact", n);
}

std::vector<uint8_t> Cat(std::vector<uint8_t> a, const std::vector<uint8_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

int main() {
    uint8_t* region = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!region) return 2;
    std::mt19937 rng(20261114);

    const std::vector<uint8_t> addRaxRbx = {0x48, 0x01, 0xd8};                       // add rax, rbx (flags)
    const std::vector<uint8_t> nop3 = {0x0f, 0x1f, 0x00};                            // nop dword [rax]
    const std::vector<uint8_t> movssRip = {0xf3, 0x0f, 0x10, 0x2d, 0, 0, 0, 0};      // movss xmm5, [rip+d]

    std::vector<Case> cases = {
        {"vrcpps xmm1,xmm2 + add rax,rbx", Cat({0xc5, 0xf8, 0x53, 0xca}, addRaxRbx), {0, 4, 7, 0, 4}, Kind::Rcp, 1, 2, -1},
        {"vrsqrtps xmm9,xmm12", {0xc4, 0x41, 0x78, 0x52, 0xcc}, {0, 5, 5, 0, 5}, Kind::Rsqrt, 9, 12, -1},
        {"vrsqrtps xmm14,xmm0", {0xc4, 0x61, 0x78, 0x52, 0xf0}, {0, 5, 5, 0, 5}, Kind::Rsqrt, 14, 0, -1},
        {"vrcpps xmm2,xmm2 + nop", Cat({0xc5, 0xf8, 0x53, 0xd2}, nop3), {0, 4, 7, 0, 4}, Kind::Rcp, 2, 2, -1},
        {"vrcpps xmm0,[rip]", {0xc5, 0xf8, 0x53, 0x05, 0, 0, 0, 0}, {0, 8, 8, 4, 8}, Kind::Rcp, 0, -1, -1, 4, 8},
        {"vrsqrtps xmm3,[rip] (LBPPC leaves it unaimed)", {0xc5, 0xf8, 0x52, 0x1d, 0, 0, 0, 0}, {0, 8, 8, 4, 8}, Kind::Rsqrt, 3, -1, -1, 4, 8},
        {"vrcpps xmm1,xmm2 + movss xmm5,[rip]", Cat({0xc5, 0xf8, 0x53, 0xca}, movssRip), {0, 4, 12, 8, 12}, Kind::Rcp, 1, 2, -1, 8, 12},
    };
    for (const uint8_t mask : {0xf1, 0x71, 0xff, 0x3c, 0xf8, 0x0f, 0x81}) {
        Case c{"", {0xc4, 0xe3, 0x71, 0x40, 0xc2, mask}, {0, 6, 6, 0, 6}, Kind::Dpps, 0, 1, 2};
        c.mask = mask;
        static std::vector<std::string> names;
        names.push_back("vdpps xmm0,xmm1,xmm2," + std::to_string(mask));
        c.name = names.back().c_str();
        cases.push_back(c);
    }
    {
        Case c{"vdpps xmm8,xmm9,xmm10,0xff", {0xc4, 0x43, 0x31, 0x40, 0xc2, 0xff}, {0, 6, 6, 0, 6}, Kind::Dpps, 8, 9, 10};
        c.mask = 0xff;
        cases.push_back(c);
    }

    std::printf("stubs against the original code and the exact result (3000 random states each):\n");
    for (const unsigned csr : {0x1f80u, 0x9fc0u})
        for (const Case& c : cases) RunCase(c, region, rng, csr);

    std::printf("the checks refuse what the stubs can't do:\n");
    struct Refuse {
        const char* name;
        std::vector<uint8_t> code;
        Site s;
        const char* expect;  // a word of the reason
    };
    const std::vector<Refuse> refuse = {
        {"vrcpps xmm1,[rsp+8]", {0xc5, 0xf8, 0x53, 0x4c, 0x24, 0x08}, {0, 6, 6, 0, 6}, "stack"},
        {"vrcpps ymm1,ymm2", Cat({0xc5, 0xfc, 0x53, 0xca}, addRaxRbx), {0, 4, 7, 0, 4}, "256-bit"},
        {"vrcpps + jmp", {0xc5, 0xf8, 0x53, 0xca, 0xeb, 0x00}, {0, 4, 6, 0, 4}, "branches"},
        {"vrcpps + push rax", {0xc5, 0xf8, 0x53, 0xca, 0x50}, {0, 4, 5, 0, 4}, "stack"},
        {"vrcpps xmm0,[rip] listed without its RIP field", {0xc5, 0xf8, 0x53, 0x05, 0, 0, 0, 0}, {0, 8, 8, 0, 8}, "RIP"},
        {"vrcpps listed 4 bytes moved", {0xc5, 0xf8, 0x53, 0xca, 0x90}, {0, 4, 4, 0, 4}, "short"},
        {"vsqrtps (not a site)", {0xc5, 0xf8, 0x51, 0xca, 0x90}, {0, 4, 5, 0, 4}, "not vrcpps"},
        {"vrcpps + another vrcpps", {0xc5, 0xf8, 0x53, 0xca, 0xc5, 0xf8, 0x53, 0xca}, {0, 4, 8, 0, 4}, "site too"},
    };
    for (const Refuse& r : refuse) {
        uint8_t buf[32];
        std::memset(buf, 0x90, sizeof buf);
        std::memcpy(buf, r.code.data(), r.code.size());
        const std::string why = core::exactmath::Check(buf, r.s);
        const bool ok = why.find(r.expect) != std::string::npos;
        std::printf("  %-48s %s (%s)\n", r.name, ok ? "refused" : "NOT refused as expected", why.empty() ? "accepted" : why.c_str());
        CHECK(ok);
    }

    std::printf("exactmathtest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
