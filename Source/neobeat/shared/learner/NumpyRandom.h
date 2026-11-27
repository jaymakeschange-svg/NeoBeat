// numpy's default random generator - np.random.default_rng(seed) - for uniform draws, in C++.
//
// Part of the learner port (Body2.h). The learner's analysis draws random phases from
// default_rng(3) and default_rng(5); producing the same numbers makes the port's starting body the
// learner's starting body, so a whole fit can be compared with the learner's. Only what the
// analysis uses: SeedSequence from one integer, PCG64 (XSL-RR 128/64), and uniform doubles.
// Written from the published algorithms (O'Neill's PCG; numpy's SeedSequence, NEP 19).
#pragma once

#include <cstdint>

namespace ml
{
class NumpyRandom
{
public:
    explicit NumpyRandom (uint32_t seed)
    {
        // ---- SeedSequence(seed).generate_state(4, uint64)
        uint32_t pool[4];
        uint32_t hashConst = kInitA;
        auto hashmix = [&] (uint32_t v)
        {
            v ^= hashConst;
            hashConst *= kMultA;
            v *= hashConst;
            v ^= v >> kXShift;
            return v;
        };
        auto mix = [] (uint32_t x, uint32_t y)
        {
            uint32_t r = kMixMultL * x - kMixMultR * y;
            r ^= r >> kXShift;
            return r;
        };
        for (int i = 0; i < 4; ++i) pool[i] = hashmix (i == 0 ? seed : 0u);
        for (int s = 0; s < 4; ++s)
            for (int d = 0; d < 4; ++d)
                if (s != d) pool[d] = mix (pool[d], hashmix (pool[s]));
        uint32_t words[8];
        uint32_t hc = kInitB;
        for (int i = 0; i < 8; ++i)
        {
            uint32_t v = pool[i % 4];
            v ^= hc;
            hc *= kMultB;
            v *= hc;
            v ^= v >> kXShift;
            words[i] = v;
        }
        uint64_t st[4];
        for (int i = 0; i < 4; ++i) st[i] = (uint64_t) words[2 * i] | ((uint64_t) words[2 * i + 1] << 32);

        // ---- PCG64: initstate (st0:st1), initseq (st2:st3)
        const U128 initState { st[0], st[1] }, initSeq { st[2], st[3] };
        inc = shl1 (initSeq); inc.lo |= 1u;
        state = { 0, 0 };
        step();
        state = add (state, initState);
        step();
    }

    uint64_t next64() noexcept
    {
        step();
        const uint64_t v = state.hi ^ state.lo;
        const unsigned rot = (unsigned) (state.hi >> 58);
        return (v >> rot) | (v << ((64u - rot) & 63u));
    }

    double uniform() noexcept { return (double) (next64() >> 11) * (1.0 / 9007199254740992.0); }
    double uniform (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }

private:
    struct U128 { uint64_t hi = 0, lo = 0; };
    static U128 add (U128 a, U128 b) noexcept { U128 r { a.hi + b.hi, a.lo + b.lo }; if (r.lo < a.lo) ++r.hi; return r; }
    static U128 shl1 (U128 a) noexcept { return { (a.hi << 1) | (a.lo >> 63), a.lo << 1 }; }
    static U128 mul (U128 a, U128 b) noexcept
    {
        // low 128 bits of a * b
        const uint64_t a0 = a.lo & 0xffffffffu, a1 = a.lo >> 32, b0 = b.lo & 0xffffffffu, b1 = b.lo >> 32;
        const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        const uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
        U128 r;
        r.lo = (p00 & 0xffffffffu) | (mid << 32);
        r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32) + a.hi * b.lo + a.lo * b.hi;
        return r;
    }
    void step() noexcept { state = add (mul (state, kMult), inc); }

    static constexpr uint32_t kInitA = 0x43b0d7e5u, kMultA = 0x931e8875u, kInitB = 0x8b51f9ddu, kMultB = 0x58f38dedu;
    static constexpr uint32_t kMixMultL = 0xca01f9ddu, kMixMultR = 0x4973f715u;
    static constexpr int kXShift = 16;
    static constexpr U128 kMult { 0x2360ed051fc65da4ull, 0x4385df649fccf645ull };
    U128 state, inc;
};

} // namespace ml
