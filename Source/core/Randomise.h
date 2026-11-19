// The randomiser: a control that will not sit still.
//
// Right-click a knob and it starts cycling - jumping to a new value in time with the music rather than
// drifting like an LFO, because a knob that lands on the beat is a rhythm and a knob that wanders is a wash.
// Speed sets how often it jumps, swing pushes every other jump late the way the sequencer swings, and
// extremity is how far it is allowed to stray from where you left it. Turn it off and it lands back exactly
// where it started, so nothing is lost by trying it.
//
// The values come from the beat, not from a running counter, so the same bar always gives the same jumps:
// bounce the track twice and you get the same take, and the cycle stays with the music if the transport
// jumps around.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mjq
{
// What a cycle can be put on. These are the controls that are read while the line plays, so a cycle on any
// of them is heard immediately rather than waiting for the line to be written again.
enum class Cycled : uint8_t { Dialect = 0, Traffic, Accent, Gate, Swing, Count };

inline const char* cycledName (Cycled c) noexcept
{
    switch (c)
    {
        case Cycled::Dialect: return "DIALECT";
        case Cycled::Traffic: return "TRAFFIC";
        case Cycled::Accent:  return "ACCENT";
        case Cycled::Gate:    return "GATE";
        case Cycled::Swing:   return "SWING";
        default:              return "";
    }
}

// The range each control lives in, so extremity means the same thing on all of them.
inline void cycledRange (Cycled c, float& lo, float& hi) noexcept
{
    switch (c)
    {
        case Cycled::Gate:  lo = 0.1f; hi = 2.0f; break;
        case Cycled::Swing: lo = 0.5f; hi = 0.75f; break;
        default:            lo = 0.0f; hi = 1.0f; break;
    }
}

struct Cycle
{
    bool on = false;
    float rate = 1.0f;        // jumps a beat: 0.25 every four beats .. 4 four a beat
    float swing = 0.5f;       // 0.5 straight, up to 0.75 heavily swung
    float extremity = 0.5f;   // 0 never leaves home .. 1 the whole range
    float home = 0.5f;        // where it was left, and where it lands when switched off
    uint64_t seed = 1;

    bool operator== (const Cycle& o) const noexcept
    {
        return on == o.on && rate == o.rate && swing == o.swing && extremity == o.extremity
            && home == o.home && seed == o.seed;
    }
};

struct Randomiser
{
    std::array<Cycle, (size_t) Cycled::Count> cycles {};
    bool any() const noexcept { for (const auto& c : cycles) if (c.on) return true; return false; }
    bool operator== (const Randomiser& o) const noexcept { return cycles == o.cycles; }
};

// Which jump we are in at this point in the music, with swing applied. Swing pushes every other jump late,
// exactly as the sequencer swings its steps, so a cycling knob locks to the groove instead of fighting it.
inline int64_t cycleStepAt (const Cycle& c, double beats) noexcept
{
    const double rate = std::max (0.03125, (double) c.rate);
    const double raw = beats * rate;
    if (c.swing <= 0.5001f) return (int64_t) std::floor (raw);

    // a swung pair takes the same time, but the first of the two is longer
    const double pairIndex = std::floor (raw * 0.5);
    const double inPair = raw - pairIndex * 2.0;              // 0 .. 2
    const double split = 2.0 * std::min (0.9, std::max (0.5, (double) c.swing));
    return (int64_t) (pairIndex * 2.0) + (inPair < split ? 0 : 1);
}

// The value a cycle is reading out. Off, it is wherever it was left.
inline float cycleValueAt (const Cycle& c, Cycled what, double beats) noexcept
{
    float lo = 0.0f, hi = 1.0f;
    cycledRange (what, lo, hi);
    const float home = std::max (lo, std::min (hi, c.home));
    if (! c.on || c.extremity <= 0.0f) return home;

    const int64_t step = cycleStepAt (c, beats);
    uint64_t h = (uint64_t) step * 0x9E3779B97F4A7C15ull + c.seed * 0xD6E8FEB86659FD93ull;
    h ^= h >> 31; h *= 0xD6E8FEB86659FD93ull; h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    const float u = (float) ((h >> 11) & 0x1FFFFFull) / (float) 0x200000ull;    // 0 .. 1

    // extremity opens a window around home and keeps it inside the control's own range, so a knob left near
    // the top strays downwards rather than piling up against the ceiling
    const float reach = std::max (0.0f, std::min (1.0f, c.extremity)) * (hi - lo);
    const float from = std::max (lo, home - reach);
    const float to = std::min (hi, home + reach);
    return from + u * (to - from);
}

// Everything a cycling knob is doing at this moment, ready to be laid over a line.
struct CycleRead
{
    std::array<float, (size_t) Cycled::Count> value {};
    std::array<bool, (size_t) Cycled::Count> live {};
};

inline CycleRead cycleRead (const Randomiser& r, double beats) noexcept
{
    CycleRead out;
    for (int i = 0; i < (int) Cycled::Count; ++i)
    {
        const auto what = (Cycled) i;
        out.live[(size_t) i] = r.cycles[(size_t) i].on;
        out.value[(size_t) i] = cycleValueAt (r.cycles[(size_t) i], what, beats);
    }
    return out;
}
} // namespace mjq
