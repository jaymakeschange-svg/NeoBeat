// NeoBeat — the Matrixxxer's cell effects: what a One Shot cell does to the hit it sits on, or a Sample
// cell to its slice. A cell can hold any of the 24 library effects (MatrixFx.h), Friction Stretch or
// Bounce Delay, each with its own settings. Everything renders offline, when the cell changes.
#pragma once

#include "../dsp/MatrixFx.h"
#include "../dsp/FrictionStretch.h"
#include "../dsp/BounceDelay.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nb
{
struct CellFx
{
    enum Kind { None, Library, FrictionStretchFx, BounceDelayFx };
    Kind kind = None;
    MatrixFx fx = MatrixFx::Freezing;
    FxSettings settings;
    StretchParams stretch;
    BounceParams bounce;

    static CellFx library (MatrixFx f) { CellFx c; c.kind = Library; c.fx = f; c.settings = FxSettings::defaults (f); return c; }
    static CellFx friction (const StretchParams& p) { CellFx c; c.kind = FrictionStretchFx; c.stretch = p; return c; }
    static CellFx bouncing (const BounceParams& p) { CellFx c; c.kind = BounceDelayFx; c.bounce = p; return c; }
    // mod-matrix destinations: "<effect id>.<param name>"
    std::vector<std::string> destinations() const
    {
        std::vector<std::string> d;
        if (kind == Library) for (const auto& q : matrixFxInfo()[(size_t) fx].params) d.push_back (std::string (matrixFxInfo()[(size_t) fx].id) + "." + q.name);
        else if (kind == FrictionStretchFx) for (const char* n : { "stretch", "rate", "swing", "yank", "rip", "rasp", "glue", "roll", "strip", "freeze", "grit", "gate", "decay" }) d.push_back (std::string ("frictionstretch.") + n);
        else if (kind == BounceDelayFx) for (const char* n : { "rate", "grid", "swing", "bounciness", "distance start", "distance end", "speed", "pan random", "width", "room" }) d.push_back (std::string ("bouncedelay.") + n);
        return d;
    }
};

// ------------------------------------------------------------------ cell height -> main control
// In the Sample view a cell's height (0..1) sets its effect's main control: the one that makes the
// effect go further or last longer. Each effect names that control and the range the height sweeps.
//   curve: log for times, rates and counts with a wide range, lin otherwise.
//   invert: a higher cell lowers the value (Freezing's decay: higher cell = slower decay = longer hold).
//   keepSign: the height sets the size, the sign the user chose stays (a shift down stays down).
struct MainControl
{
    int index = 0;          // parameter index (library effects); -1 for Friction Stretch / Bounce Delay
    const char* name = "";
    float lo = 0, hi = 1;   // value at height 0 and at height 1 (before invert)
    bool log = false, invert = false, keepSign = false;

    float valueAt (float h) const
    {
        h = std::min (1.0f, std::max (0.0f, h));
        if (invert) h = 1.0f - h;
        if (log && lo > 0 && hi > 0) return lo * std::pow (hi / lo, h);
        return lo + (hi - lo) * h;
    }
    float heightOf (float v) const
    {
        v = std::fabs (v);
        float h = (log && lo > 0 && hi > 0) ? std::log (std::max (1e-9f, v) / lo) / std::log (hi / lo) : (v - lo) / (hi - lo);
        h = std::min (1.0f, std::max (0.0f, h));
        return invert ? 1.0f - h : h;
    }
};

inline MainControl mainControl (MatrixFx fx)
{
    switch (fx)
    {
        case MatrixFx::Averaging:      return { 0, "frames",      2,    64,   true };
        case MatrixFx::Blurring:       return { 0, "blur",        30,   5000, true };
        case MatrixFx::DroneMaker:     return { 0, "smoothing",   0.3f, 10,   true };
        case MatrixFx::Freezing:       return { 0, "decay",       0.5f, 40,   true, true };   // higher = holds longer
        case MatrixFx::GateHold:       return { 1, "hold",        40,   4000, true };
        case MatrixFx::Weave:          return { 0, "snapshot",    30,   1000, true };
        case MatrixFx::Shimmer:        return { 0, "jitter",      0,    100 };
        case MatrixFx::Shuffle:        return { 0, "amount",      0,    100 };
        case MatrixFx::Pulsing:        return { 0, "on",          10,   1000, true };
        case MatrixFx::Harmonizer:     return { 0, "shift 1",     0,    24,   false, false, true };
        case MatrixFx::BinShift:       return { 0, "shift",       0,    200,  false, false, true };
        case MatrixFx::PitchShift:     return { 0, "interval",    0,    24,   false, false, true };
        case MatrixFx::Stretch:        return { 0, "alpha",       0,    1,    false, false, true };
        case MatrixFx::Tracing:        return { 1, "keep",        2,    512,  true };
        case MatrixFx::Filterbank:     return { 2, "count",       1,    64 };
        case MatrixFx::GlidingFilters: return { 2, "glide",       0,    4 };
        case MatrixFx::Granulation:    return { 2, "delay",       0,    2000 };
        case MatrixFx::Emergence:      return { 2, "depth",       0,    1 };
        case MatrixFx::PartialGlide:   return { 3, "limit",       0,    36 };
        case MatrixFx::IdeeFixer:      return { 6, "length",      0.5f, 30,   true };
        case MatrixFx::Chorus:         return { 1, "depth",       0,    20 };
        case MatrixFx::CombFilterbank: return { 3, "feedback",    0.5f, 0.995f };
        case MatrixFx::GrainStreamer:  return { 1, "repeats",     1,    32 };
        case MatrixFx::SuperFilterbank:return { 3, "q",           2,    200,  true };
        default:                       return {};
    }
}

inline MainControl mainControl (const CellFx& c)
{
    if (c.kind == CellFx::FrictionStretchFx) return { -1, "stretch", 1, 64, true };
    if (c.kind == CellFx::BounceDelayFx)     return { -1, "bounciness", 0.3f, 0.95f };
    return mainControl (c.fx);
}

// Set the cell's main control from its height (0..1). Counts round to whole numbers.
inline void applyHeight (CellFx& c, float height)
{
    const MainControl m = mainControl (c);
    float v = m.valueAt (height);
    if (c.kind == CellFx::Library)
    {
        if (m.index < 0 || m.index >= (int) c.settings.p.size()) return;
        const auto& q = matrixFxInfo()[(size_t) c.fx].params[(size_t) m.index];
        if (q.unit[0] == '\0' && m.hi > 2 && std::string (m.name) != "amount") v = std::round (v);   // frames, count, repeats
        if (m.keepSign && c.settings.p[(size_t) m.index] < 0) v = -v;
        c.settings.p[(size_t) m.index] = std::min (q.hi, std::max (q.lo, v));
    }
    else if (c.kind == CellFx::FrictionStretchFx) c.stretch.stretch = v;
    else if (c.kind == CellFx::BounceDelayFx)     c.bounce.bounciness = v;
}

// The height that shows a cell's current main control (to draw the cell).
inline float heightOf (const CellFx& c)
{
    const MainControl m = mainControl (c);
    if (c.kind == CellFx::Library && m.index >= 0 && m.index < (int) c.settings.p.size()) return m.heightOf (c.settings.p[(size_t) m.index]);
    if (c.kind == CellFx::FrictionStretchFx) return m.heightOf (c.stretch.stretch);
    if (c.kind == CellFx::BounceDelayFx)     return m.heightOf (c.bounce.bounciness);
    return 0.0f;
}

// Render one cell's effect on a sound (stereo). hitTime/bpm put the tempo-synced effects on the song's grid.
// The result may be longer than the input (tails); for Bounce Delay it holds the dry sound plus the bounces.
inline void renderCell (const CellFx& c, const float* inL, const float* inR, int n, float fs, double bpm, double hitTime,
                        const FrictionMaterial* rubber, const Engine2Material* roll,
                        std::vector<float>& outL, std::vector<float>& outR)
{
    switch (c.kind)
    {
        case CellFx::None: outL.assign (inL, inL + n); outR.assign (inR, inR + n); return;
        case CellFx::Library: { MatrixFxEngine e; e.process (c.fx, c.settings, inL, inR, n, fs, outL, outR); return; }
        case CellFx::FrictionStretchFx:
        {
            if (rubber == nullptr) { outL.assign (inL, inL + n); outR.assign (inR, inR + n); return; }
            FrictionStretch f; f.render (c.stretch, *rubber, bpm, hitTime, 0.0, inL, inR, n, fs, outL, outR, roll); return;
        }
        case CellFx::BounceDelayFx:
        {
            BounceDelay b; const auto plan = b.plan (c.bounce, bpm, hitTime, 0.0);
            const int len = n + (int) (((plan.empty() ? 0.0 : plan.back().t - hitTime) + c.bounce.tail + 0.2) * fs);
            outL.assign ((size_t) len, 0.0f); outR.assign ((size_t) len, 0.0f);
            for (int i = 0; i < n; ++i) { outL[(size_t) i] = inL[i]; outR[(size_t) i] = inR[i]; }
            // plan times are on the song; render relative to the hit
            std::vector<Bounce> rel = plan; for (auto& x : rel) x.t -= hitTime;
            b.render (c.bounce, rel, 0.0, inL, inR, n, fs, outL.data(), outR.data(), nullptr, len);
            return;
        }
    }
}
} // namespace nb
