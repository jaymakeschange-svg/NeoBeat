// The generator library: the shared ways of making rhythm and melody that Batchcut's LOOPS and MidiJaQ
// both use (MidiJaQ's names first, the modules they take their principle from after):
//
//   DejaVu   DRIFT's heart: random values with a memory. Deja vu 0: every value is new; 1: the last
//            `length` values loop for ever; in between, each step either replays the one from `length`
//            steps ago or rolls a fresh one (Mutable Instruments Marbles)
//   Habit    HABIT: a looping shift register that slowly mutates: a bit comes round and is flipped
//            with probability `change` (Music Thing Modular Turing Machine)
//   euclid   ORBIT: k hits spread as evenly as possible over n steps, rotated (Toussaint 2005)
//   Cue      CUES: when a hit plays: always, by chance, on the n-th of every m times round, the first
//            time only, not the first, on fills, not on fills, or as the cue before it did (Elektron
//            trig conditions)
//   swung    where a step lands in quarter notes on a swung grid: NeoBeat's own swing, so a loop from
//            Batchcut and a line from MidiJaQ sit in the same pocket as NeoBeat's drums
//
// Everything is seeded (Rng.h): the same seed and settings give the same result on every computer.
// Framework-free.
#pragma once

#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gen
{
// ------------------------------------------------------------------ DRIFT: deja vu
class DejaVu
{
public:
    void reset (uint64_t seed, int loopLength)
    {
        rng.reseed (seed, 0x1f123bb5u);
        length = std::max (1, std::min (loopLength, 64));
        memory.clear(); at = 0;
    }
    void setDejaVu (float d) noexcept { dejavu = std::max (0.0f, std::min (1.0f, d)); }
    float getDejaVu() const noexcept { return dejavu; }

    // the next value, 0 <= v < 1
    float next()
    {
        // the top of the knob is a locked loop; below it, the chance of replaying rises steadily
        const double replay = std::min (1.0, (double) dejavu * 1.12);
        float v;
        if ((int) memory.size() < length) { v = (float) rng.uni(); memory.push_back (v); }
        else if (rng.uni() < replay) v = memory[(size_t) at];
        else { v = (float) rng.uni(); memory[(size_t) at] = v; }
        at = (at + 1) % length;
        return v;
    }
    int loopLength() const noexcept { return length; }

private:
    Rng rng;
    std::vector<float> memory;
    int length = 16, at = 0;
    float dejavu = 0.0f;
};

// ------------------------------------------------------------------ HABIT: the looping shift register
class Habit
{
public:
    // bits: the loop's length in steps (2..16); change: 0 locked, 0.5 fully random, 1 locked but turned
    // over each time round (twice as long a loop), as the Turing Machine's big knob
    void reset (uint64_t seed, int bits)
    {
        rng.reseed (seed, 0x7ab1c3u);
        length = std::max (2, std::min (bits, 16));
        reg = 0; for (int i = 0; i < length; ++i) reg |= (rng.next() & 1u) << i;
    }
    void setChange (float c) noexcept { change = std::max (0.0f, std::min (1.0f, c)); }

    struct Out { bool bit; float value; };   // value: the register read as a number, 0..1
    Out next()
    {
        // the bit that comes round: kept, flipped by chance, or (past half way) flipped on purpose
        const uint32_t top = (reg >> (length - 1)) & 1u;
        const double flip = change <= 0.5f ? (double) change : 1.0 - (double) change;   // 0.5: half the bits change, as random as it gets
        uint32_t in = top;
        if (rng.uni() < flip) in ^= 1u;
        if (change > 0.98f) in = top ^ 1u;               // all the way: the loop turns over each time round
        reg = ((reg << 1) | in) & ((1u << length) - 1u);
        float v = 0.0f; for (int i = 0; i < std::min (length, 8); ++i) v += (float) ((reg >> i) & 1u) / (float) (2u << i);
        return { (reg & 1u) != 0u, std::min (0.9999f, v * 1.0039f) };
    }

private:
    Rng rng;
    uint32_t reg = 0;
    int length = 8;
    float change = 0.0f;
};

// ------------------------------------------------------------------ ORBIT: Euclidean rhythms
// hits as evenly as possible over steps, the first hit on step 0 before the rotation (k = 3, n = 8 gives
// x..x..x. , the tresillo); rotate moves the whole pattern later by that many steps
inline std::vector<bool> euclid (int hits, int steps, int rotate = 0)
{
    steps = std::max (1, std::min (steps, 256));
    hits = std::max (0, std::min (hits, steps));
    std::vector<bool> out ((size_t) steps, false);
    if (hits == 0) return out;
    for (int i = 0; i < steps; ++i)
    {
        // Bresenham: a hit wherever the running total of hits/steps crosses a whole number
        const bool on = ((i * hits) % steps) < hits;
        out[(size_t) (((i + rotate) % steps + steps) % steps)] = on;
    }
    return out;
}

// ------------------------------------------------------------------ CUES: when a hit plays
struct Cue
{
    enum Kind : uint8_t { Always, Chance, EveryNth, First, NotFirst, Fill, NotFill, Prev, NotPrev } kind = Always;
    uint8_t n = 1, of = 2;        // EveryNth: plays on round n of every `of` (1-based)
    float chance = 0.5f;          // Chance

    // round: how many times the loop has come round before this one (0 the first time)
    bool passes (int round, bool fill, bool prevPassed, Rng& rng) const
    {
        switch (kind)
        {
            case Always:   return true;
            case Chance:   return rng.uni() < (double) chance;
            case EveryNth: return of == 0 ? true : (round % std::max (1, (int) of)) == std::max (1, std::min ((int) n, (int) of)) - 1;
            case First:    return round == 0;
            case NotFirst: return round > 0;
            case Fill:     return fill;
            case NotFill:  return ! fill;
            case Prev:     return prevPassed;
            case NotPrev:  return ! prevPassed;
        }
        return true;
    }
};

// ------------------------------------------------------------------ the swung grid
// Where step s starts, in quarter notes, when a bar has `perBar` steps. Even grids swing in pairs, as
// NeoBeat does (0.5 straight, 0.66 a triplet feel, every second step later); triplet grids (12 or 24 a bar)
// are played as written: they are already a swing.
inline double swung (int s, int perBar, float swing) noexcept
{
    perBar = std::max (1, perBar);
    const double step = 4.0 / (double) perBar;
    if (perBar % 3 == 0 || perBar < 4) return s * step;
    const int pair = s >> 1;
    return 2.0 * step * pair + ((s & 1) ? 2.0 * step * (double) std::max (0.5f, std::min (0.75f, swing)) : 0.0);
}
} // namespace gen
