// NeoBeat — the swung grid the effects land on (Friction Stretch's jumps, Bounce Delay's bounces).
//
// Both effects share one timing spec: a Tempo/ms switch, a step (a note value at the host tempo, or
// milliseconds), and a swing bias. Steps come in pairs; the second of each pair sits `swing` of the way
// through the pair (0.5 = straight, 0.66 = triplet feel, up to 0.75). In tempo mode the grid is the
// song's own (pairs start on the beat, like the sequencer); in ms mode it starts at the hit.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nb
{
struct RateSpec
{
    bool tempo = true;        // true: note values at the host tempo; false: milliseconds
    double beats = 0.25;      // tempo mode: the step in quarter notes (1/16 = 0.25; dotted x1.5; triplet x2/3)
    double ms = 125.0;        // ms mode: the step in milliseconds
    float swing = 0.62f;      // swing bias: 0.5 straight .. 0.75

    double stepSeconds (double bpm) const noexcept { return tempo ? beats * 60.0 / std::max (bpm, 1.0) : ms / 1000.0; }
};

struct SwingGrid
{
    double step = 0.125;      // seconds per step
    double origin = 0.0;      // seconds: an even (on-beat) grid point
    float swing = 0.5f;

    static SwingGrid make (const RateSpec& r, double bpm, double hitTime, double barStart = 0.0) noexcept
    {
        SwingGrid g;
        g.step = std::max (1.0e-3, r.stepSeconds (bpm));
        g.swing = std::min (0.75f, std::max (0.5f, r.swing));
        g.origin = r.tempo ? barStart : hitTime;
        return g;
    }
    double timeOf (int64_t k) const noexcept
    {
        const int64_t pair = k >= 0 ? k / 2 : -((-k + 1) / 2);
        const bool odd = (k - 2 * pair) != 0;
        return origin + (double) pair * 2.0 * step + (odd ? 2.0 * step * swing : 0.0);
    }
    // the grid point nearest to t
    int64_t nearest (double t) const noexcept
    {
        const int64_t pair = (int64_t) std::floor ((t - origin) / (2.0 * step));
        int64_t best = 2 * pair; double bd = 1.0e30;
        for (int64_t k = 2 * pair - 1; k <= 2 * pair + 2; ++k)
        {
            const double d = std::fabs (timeOf (k) - t);
            if (d < bd) { bd = d; best = k; }
        }
        return best;
    }
    // the first grid point at or after t
    int64_t atOrAfter (double t) const noexcept
    {
        int64_t k = nearest (t);
        while (timeOf (k) < t - 1.0e-9) ++k;
        while (timeOf (k - 1) >= t - 1.0e-9) --k;
        return k;
    }
};
} // namespace nb
