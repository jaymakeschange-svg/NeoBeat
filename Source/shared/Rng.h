// The generator library's random source. Seeded, small, and the same on every computer and compiler:
// a seed written into a project or a .NJQ file rebuilds the same loop or pattern on Windows and Linux
// alike (std::uniform_*_distribution and std::shuffle are free to differ between libraries; this is not).
//
// PCG32 (O'Neill 2014, pcg-random.org): 64-bit state, 32-bit output.
// Framework-free; shared by Batchcut (Kit room, LOOPS, KEYS) and MidiJaQ.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace gen
{
class Rng
{
public:
    explicit Rng (uint64_t seed = 1u, uint64_t stream = 0x5851f42d4c957f2dull) noexcept { reseed (seed, stream); }

    void reseed (uint64_t seed, uint64_t stream = 0x5851f42d4c957f2dull) noexcept
    {
        state = 0u; inc = (stream << 1u) | 1u;
        next(); state += seed; next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const uint32_t xs = (uint32_t) (((old >> 18u) ^ old) >> 27u), rot = (uint32_t) (old >> 59u);
        return (xs >> rot) | (xs << ((32u - rot) & 31u));
    }
    uint32_t operator()() noexcept { return next(); }

    // 0 <= x < n, without the bias of a plain modulo
    uint32_t below (uint32_t n) noexcept
    {
        if (n <= 1u) return 0u;
        const uint32_t limit = (uint32_t) (0x100000000ull - (0x100000000ull % n));
        uint32_t x;
        do x = next(); while (x >= limit && limit != 0u);
        return x % n;
    }
    int range (int lo, int hi) noexcept { return hi <= lo ? lo : lo + (int) below ((uint32_t) (hi - lo + 1)); }   // lo..hi inclusive
    double uni() noexcept { return (double) next() * (1.0 / 4294967296.0); }                                   // 0 <= x < 1
    float unif (float lo = 0.0f, float hi = 1.0f) noexcept { return lo + (hi - lo) * (float) uni(); }
    bool chance (double p) noexcept { return uni() < p; }

    template <typename T>
    void shuffle (std::vector<T>& v) noexcept
    {
        for (size_t i = v.size(); i > 1; --i) std::swap (v[i - 1], v[below ((uint32_t) i)]);
    }

    // a seed for a part of a whole (layer 3 of a loop, lane 5 of a kit): the same part, the same seed
    static uint64_t mix (uint64_t seed, uint64_t part) noexcept
    {
        uint64_t z = seed + 0x9e3779b97f4a7c15ull * (part + 1u);
        z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27u)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31u);
    }

private:
    uint64_t state = 0u, inc = 1u;
};
} // namespace gen
