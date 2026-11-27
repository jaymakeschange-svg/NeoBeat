// IDM Jack's realtime core.
//
// Spec: docs/PROPOSED-FEATURES.md, "IDM Jack" - the user's - and docs/DECISIONS.md. Where the two
// disagree, the spec wins.
//
// The spec's central rule, quoted because it decides the whole architecture: parameter chaos
// "must be an overlay applied at playback - per-hit offsets layered over the stored values, never
// written back to them. Written back, releasing the control would leave every sound in the kit
// scrambled." So nothing here touches a stored parameter. It decides what happens on each lattice
// step and hands back offsets; the plugin applies them to a COPY of a hit's settings at the moment
// the hit is triggered. Releasing the control is then the return to the original by construction -
// there is nothing to restore, because nothing was changed.
//
// nsr::Chaos (Chaos.h) is the other half of the family: the global randomiser, which does write
// values, because it is a destination rather than a visit. It also carried an earlier, write-back
// version of Jack that the spec rules out; that version is not used by this.
//
// Realtime: fixed arrays, no allocation, no std::function. Everything here may run in processBlock.

#pragma once

#include "../gen/Rng.h"
#include "Chaos.h"          // ChaosAxis and its names: one definition of the axes for the family

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace nsr
{

// What one hit's parameters are pushed by. Normalised to -1..+1 and already scaled by extremity;
// the plugin decides what a full push means for each of its own parameters.
enum JackOffset { JoTune = 0, JoDecay, JoTone, JoDrive, JoHollow, JoEvolve, JoPan, JoWidth, JoLevel, JoEffect, JoCount };

// The settings, as a plain value. Published from the message thread, copied on the audio thread.
struct JackSettings
{
    // ---------------------------------------------------------------------------- the lattice
    // Events can only land on it; randomness decides whether and how much, never where. 6 per beat
    // is sixteenth triplets, the default the spec names.
    int division = 6;
    float swing = 0.58f;            // 0.5 is straight

    float density = 0.45f;          // the chance a lattice step fires a burst at all
    float extremity = 0.6f;         // how far each seizure goes

    // One seed per window, with a probability the next window reuses it: bar-level coherence.
    float repeatProb = 0.45f;
    int windowSteps = 12;

    // The figure buffer. The spec: "Motifs need a memory, not just a seed. Per-position randomness
    // produces texture, not motifs." A fired burst is stored - which lanes, where in the step, how
    // many retriggers and every parameter move - and with this probability a later burst replays
    // a stored one instead of making a new one. Few slots give recurring shapes; many approach
    // texture.
    float recallProb = 0.4f;
    int memory = 4;                 // 1..kMaxFigures

    bool axis[(int) ChaosAxis::Count] = { true, true, true, true, true };
    int maxRetriggers = 4;          // a burst is 1 + up to this many hits subdividing its step
    float timingMinMs = 1.0f, timingMaxMs = 100.0f;

    // Overlay keeps the written pattern and glitches over it; replace clears it and runs on the
    // kit alone. "Both, as a toggle" - the spec.
    bool replace = false;

    // How it lands back on release. 0 is a hard cut - the spec's "instant return". Above that the
    // offsets fade out over this many bars, and no new bursts are started.
    float recoveryBars = 0.0f;

    // Roll its own settings on switch-on, at this depth. Never reaches this control itself.
    float selfRandomise = 0.0f;

    uint64_t seed = 1;
};

struct JackHit
{
    float at = 0.0f;                // where in its lattice step, 0..1
    int pad = 0;
    float velocity = 0.8f;
    float timingMs = 0.0f;          // late by this much
    float off[JoCount] {};
};

static constexpr int kMaxBurst = 8;
static constexpr int kMaxFigures = 8;

struct JackFigure
{
    int count = 0;
    bool recalled = false;          // replayed from the buffer rather than made fresh
    JackHit hits[kMaxBurst];
};

// Its own settings, rolled at switch-on. Deliberately narrow, and never selfRandomise itself: a
// control that randomises how much it randomises stops being a control. At least one axis always
// survives, so switching on always does something.
inline void jackSelfRandomise (JackSettings& s, gen::Rng& rng) noexcept
{
    const float d = std::max (0.0f, std::min (1.0f, s.selfRandomise));
    if (d <= 0.001f) return;
    auto nudge = [&] (float v, float lo, float hi)
    {
        const float x = v + rng.unif (-(hi - lo) * d, (hi - lo) * d);
        return x < lo ? lo : (x > hi ? hi : x);
    };
    s.density    = nudge (s.density, 0.05f, 1.0f);
    s.extremity  = nudge (s.extremity, 0.1f, 1.0f);
    s.swing      = nudge (s.swing, 0.5f, 0.72f);
    s.repeatProb = nudge (s.repeatProb, 0.0f, 0.95f);
    s.recallProb = nudge (s.recallProb, 0.0f, 0.95f);
    const int divs[] = { 4, 6, 8, 12 };
    if (rng.chance ((double) d)) s.division = divs[rng.below (4)];
    for (int i = 0; i < (int) ChaosAxis::Count; ++i)
        if (rng.chance ((double) d * 0.5)) s.axis[i] = ! s.axis[i];
    bool any = false;
    for (int i = 0; i < (int) ChaosAxis::Count; ++i) any = any || s.axis[i];
    if (! any) s.axis[(int) ChaosAxis::Parameter] = true;
}

// Settings cross from the message thread to the audio thread through this. A triple buffer: the
// writer and the reader each own a slot and swap through a third, so neither ever waits and the
// reader never sees a half-written value. Small and dull on purpose.
template <typename T>
class TripleBuffer
{
public:
    void write (const T& v) noexcept
    {
        slots[(size_t) back] = v;
        back = middle.exchange (back | kDirty, std::memory_order_acq_rel) & kIndex;
    }

    // true when something new arrived since the last read; `out` is then the newest value
    bool read (T& out) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kDirty) == 0) return false;
        front = middle.exchange (front, std::memory_order_acq_rel) & kIndex;
        out = slots[(size_t) front];
        return true;
    }

private:
    static constexpr int kDirty = 4, kIndex = 3;
    std::array<T, 3> slots {};
    std::atomic<int> middle { 1 };
    int back = 0, front = 2;
};

class JackCore
{
public:
    void setSettings (const JackSettings& s) noexcept
    {
        st = s;
        st.division = std::max (1, std::min (24, st.division));
        st.windowSteps = std::max (1, st.windowSteps);
        st.memory = std::max (1, std::min (kMaxFigures, st.memory));
        st.maxRetriggers = std::max (0, std::min (kMaxBurst - 1, st.maxRetriggers));
    }
    const JackSettings& settings() const noexcept { return st; }

    // --------------------------------------------------------------------------------- the lattice
    //
    // Steps go in pairs. The even one sits on its own division; the odd one sits `swing` of the
    // way through the pair rather than halfway, which is the shuffle. The swing has to live here,
    // not in a beats-to-steps conversion, because with swing on that mapping is not linear.
    double beatOfStep (int64_t step) const noexcept
    {
        const int64_t pair = step >= 0 ? step / 2 : (step - 1) / 2;
        const bool off = step - pair * 2 != 0;
        return ((double) (pair * 2) + (off ? (double) st.swing * 2.0 : 0.0)) / (double) st.division;
    }

    int64_t firstStepAtOrAfter (double beats) const noexcept
    {
        int64_t s = (int64_t) std::floor (beats * (double) st.division) - 2;
        while (beatOfStep (s) < beats - 1.0e-12) ++s;
        return s;
    }

    double stepBeats (int64_t step) const noexcept { return beatOfStep (step + 1) - beatOfStep (step); }

    // ------------------------------------------------------------------------------ held, released
    void engage (double beats) noexcept
    {
        held = true; recovering = false;
        nextStep = firstStepAtOrAfter (beats);
        window = INT64_MIN; windowSeed = st.seed;
        stored = 0; writeAt = 0;            // a fresh pass starts with an empty memory
    }

    void release (double beats) noexcept
    {
        if (! held) return;
        held = false;
        recovering = st.recoveryBars > 0.001f;
        releasedAt = beats;
    }

    bool isHeld() const noexcept { return held; }
    bool isRecovering() const noexcept { return recovering; }
    bool active() const noexcept { return held || recovering; }

    // How hard offsets apply now: 1 while held, falling to 0 over the recovery after release.
    float scaleAt (double beats) noexcept
    {
        if (held) return 1.0f;
        if (! recovering) return 0.0f;
        const double t = (beats - releasedAt) / (double) std::max (0.001f, st.recoveryBars * 4.0f);
        if (t >= 1.0) { recovering = false; return 0.0f; }
        return (float) (1.0 - t);
    }

    // The next lattice step not yet planned. The plugin walks from here to the end of its block and
    // hands back where it stopped; after a transport jump it re-aims with firstStepAtOrAfter.
    int64_t pendingStep() const noexcept { return nextStep; }
    void setPendingStep (int64_t s) noexcept { nextStep = s; }

    // A recovery needs the transport to run out; when it stops instead, the plugin cuts.
    void cut() noexcept { recovering = false; }

    // --------------------------------------------------------------------------- what a step does
    //
    // Deterministic for a given seed and history, so a pass can be recovered. Steps must be planned
    // in order, which they are - the plugin walks the lattice forwards.
    bool planStep (int64_t step, uint32_t includeMask, int numPads, JackFigure& out) noexcept
    {
        out.count = 0; out.recalled = false;
        if (! held || includeMask == 0 || numPads <= 0) return false;

        // a new window decides its seed: the same as the last one, or a new one
        const int64_t w = floorDiv (step, st.windowSteps);
        if (w != window)
        {
            gen::Rng pick (gen::Rng::mix (st.seed, (uint64_t) (w + 0x40000000LL)));
            if (window == INT64_MIN || ! pick.chance ((double) st.repeatProb))
                windowSeed = gen::Rng::mix (st.seed, (uint64_t) w);
            window = w;
        }

        // mixed with the step's place WITHIN the window, so a repeated window plays the same figure
        const int64_t within = step - w * st.windowSteps;
        gen::Rng rng (gen::Rng::mix (windowSeed, (uint64_t) within));
        if (! rng.chance ((double) st.density)) return false;

        // a burst: recalled from memory, or made fresh and remembered
        if (stored > 0 && rng.chance ((double) st.recallProb))
        {
            out = figures[(size_t) rng.below (stored)];
            out.recalled = true;
            // a recalled figure can name a lane that has since been taken out of the include list
            int k = 0;
            for (int i = 0; i < out.count; ++i)
                if ((includeMask >> out.hits[i].pad) & 1u) out.hits[k++] = out.hits[i];
            out.count = k;
            return out.count > 0;
        }

        const float amount = st.extremity * rng.unif (0.35f, 1.0f);
        const bool retrig = st.axis[(int) ChaosAxis::Retrigger];
        out.count = retrig ? 1 + rng.range (0, st.maxRetriggers) : 1;
        for (int i = 0; i < out.count; ++i)
        {
            auto& h = out.hits[i];
            h.at = (float) i / (float) out.count;           // subdividing the step
            h.pad = pickPad (rng, includeMask, numPads);
            h.velocity = rng.unif (0.55f, 1.0f) * (1.0f - 0.07f * (float) i);
            h.timingMs = st.axis[(int) ChaosAxis::Timing]
                       ? rng.unif (st.timingMinMs, st.timingMaxMs) * amount * 0.25f : 0.0f;
            fillOffsets (rng, amount, h.off);
        }

        figures[(size_t) writeAt] = out;
        writeAt = (writeAt + 1) % st.memory;
        stored = std::min (stored + 1, st.memory);
        return true;
    }

    // Offsets for a hit from the written pattern while Jack holds (overlay mode). Each hit gets
    // its own: "each repeat in a burst gets its own parameter offsets" - that is what separates
    // this from a stutter. `key` should identify the hit (step, pad) so a pass can be reproduced.
    //
    // Keyed on the pass seed and the hit's own position, not on the window: an effect that needs a
    // hit a frame early starts on a pre-hit, and the pre-hit and the hit must get the same offsets
    // even when a window boundary falls between them.
    void hitOffsets (uint64_t key, float out[JoCount]) const noexcept
    {
        gen::Rng rng (gen::Rng::mix (st.seed ^ 0x5eedull, key));
        fillOffsets (rng, st.extremity * rng.unif (0.35f, 1.0f), out);
    }

    int figuresStored() const noexcept { return stored; }

private:
    static int64_t floorDiv (int64_t a, int64_t b) noexcept
    {
        int64_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    static int pickPad (gen::Rng& rng, uint32_t mask, int numPads) noexcept
    {
        int n = 0;
        for (int p = 0; p < numPads && p < 32; ++p) n += (mask >> p) & 1u;
        if (n == 0) return 0;
        int k = rng.below (n);
        for (int p = 0; p < numPads && p < 32; ++p)
            if ((mask >> p) & 1u) { if (k == 0) return p; --k; }
        return 0;
    }

    void fillOffsets (gen::Rng& rng, float amount, float out[JoCount]) const noexcept
    {
        const bool pitch = st.axis[(int) ChaosAxis::Pitch];
        const bool param = st.axis[(int) ChaosAxis::Parameter];
        const bool fx    = st.axis[(int) ChaosAxis::Effect];
        for (int k = 0; k < JoCount; ++k) out[k] = 0.0f;
        // every draw happens whether or not its axis is on, so switching an axis off does not
        // reshuffle what the others do
        const float t = rng.unif (-1.0f, 1.0f);
        float p[JoLevel - JoDecay + 1];
        for (auto& v : p) v = rng.unif (-1.0f, 1.0f);
        const float e = rng.unif (-1.0f, 1.0f);
        if (pitch) out[JoTune] = t * amount;
        if (param) for (int k = JoDecay; k <= JoLevel; ++k) out[k] = p[k - JoDecay] * amount;
        if (fx) out[JoEffect] = e * amount;
    }

    JackSettings st;
    bool held = false, recovering = false;
    double releasedAt = 0.0;
    int64_t nextStep = 0, window = INT64_MIN;
    uint64_t windowSeed = 1;
    std::array<JackFigure, kMaxFigures> figures {};
    int stored = 0, writeAt = 0;
};

} // namespace nsr
