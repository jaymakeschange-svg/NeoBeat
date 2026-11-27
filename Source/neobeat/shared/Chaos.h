// The chaos engine: one spine under two features.
//
// Spec: docs/DECISIONS.md, "Chaos and randomisation — the settled structure".
//
// The global randomiser and IDM Jack were separate ideas that turned out to share every mechanism
// but one. The randomiser presses once and rolls every parameter in the plugin, for auditioning:
// press, listen, press again. IDM Jack is switched on and holds, seizing parameters continuously
// on a rhythmic lattice until it is released, whereupon the original comes back untouched. Speed,
// swing, extremity, cycling rhythmically and landing back on the original describe both; only the
// trigger differs.
//
// So this is one engine with two entry points: rollOnce() and engage()/release().
//
// What it may touch is a registered list, not a hard-coded one. Each plugin registers its own
// parameters with a range, a reach and a never-flag, which is the only way one engine can seize
// "every imaginable parameter and feature" in programs that have entirely different parameters.
//
// Framework-free, so it can be tested and rendered without a plugin, a host or a window.

#pragma once

#include "../gen/Rng.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nsr
{

// ---------------------------------------------------------------------------------- what it seizes
//
// A live parameter the engine may move. The pointer is the plugin's own float, so a seizure is felt
// without the plugin having to poll anything.
struct ChaosTarget
{
    std::string room;           // "KIT", "BANDS", "MOD" - for the interface to group by
    std::string name;           // "drive", "chop pitch"
    float* value = nullptr;     // the plugin's own parameter
    float lo = 0.0f, hi = 1.0f;

    // How far this parameter may be moved when it is seized, 0..1. Not every parameter wants the
    // same licence: chop order can go anywhere, an LFO rate wants a nudge.
    float reach = 1.0f;

    bool on = true;             // included in the roll
    bool never = false;         // excluded always, whatever the interface says

    // Where it was before the engine took hold. Captured on engage, written back on release, so
    // "landing back on the original" is exact rather than approximate.
    float original = 0.0f;

    // ------------------------------------------------------------------ the escape hatch
    //
    // Not everything worth seizing is a float. A chop order is a permutation, a map mark is an
    // integer row, a place is a struct, an orb is a pair. For those, leave `value` null and give
    // these instead: `apply` is handed how hard this seizure hit and a seeded generator, `keep`
    // is called on engage to save whatever the plugin needs, and `giveBack` restores it.
    //
    // The float path stays for the simple majority, because a std::function per parameter per
    // lattice step is a lot of indirection for something that is usually one multiply.
    std::function<void (float amount, gen::Rng&)> apply;
    std::function<void()> keep;
    std::function<void()> giveBack;

    bool isAction() const noexcept { return value == nullptr && apply != nullptr; }
    bool usable() const noexcept { return value != nullptr || apply != nullptr; }
};

// The three axes from the brief, plus the two the lattice adds. Each can be switched off, so a
// pass can be made to seize parameters without retriggering anything, or the reverse.
enum class ChaosAxis { Pitch = 0, Parameter, Effect, Retrigger, Timing, Count };

inline const char* chaosAxisName (ChaosAxis a) noexcept
{
    switch (a)
    {
        case ChaosAxis::Pitch:     return "PITCH";
        case ChaosAxis::Parameter: return "PARAM";
        case ChaosAxis::Effect:    return "EFFECT";
        case ChaosAxis::Retrigger: return "RETRIG";
        case ChaosAxis::Timing:    return "TIMING";
        default:                   return "?";
    }
}

// What a fired lattice step asks the plugin to do, beyond moving parameters. The engine cannot
// retrigger a voice or nudge a note itself - it does not know what a voice is - so it reports the
// event and the plugin acts on it.
struct ChaosEvent
{
    double beats = 0.0;         // when, snapped to the lattice
    int retriggers = 0;         // 0 when the Retrigger axis is off
    float timingMs = 0.0f;      // micro-timing offset, within the 1-100 ms window
    float semitones = 0.0f;     // pitch seizure
    float amount = 0.0f;        // how hard this particular step hit, 0..1
};

struct ChaosParams
{
    // ------------------------------------------------------------------ the lattice
    //
    // The whole mechanism. Events can only land on the lattice; randomness decides whether and how
    // much, never where. That is what keeps it rhythmic rather than broken, and it is why the grid
    // is not drawn and not editable: it is a constraint, not a canvas.
    int division = 6;           // steps per beat. 6 = sixteenth triplets, the default
    float swing = 0.58f;        // 0.5 is straight

    float density = 0.5f;       // chance a lattice step fires at all
    float extremity = 0.6f;     // how far a seizure goes when it does

    // Seeding. One seed per window, with a probability that the next window reuses it. A high
    // repeat probability is what makes a figure persist long enough to register as an idea before
    // it mutates - without it, the output is undifferentiated noise rather than motifs.
    uint64_t seed = 1u;
    float repeatProb = 0.45f;
    int windowSteps = 8;        // lattice steps per window

    bool axis[(int) ChaosAxis::Count] = { true, true, true, true, true };

    int maxRetriggers = 4;
    float timingMinMs = 1.0f, timingMaxMs = 100.0f;
    float pitchRange = 12.0f;   // semitones either way

    // Its own settings rolled on switch-on, at this depth. 0 is off. A parameter that randomises
    // its own randomisation needs a limit or it immediately settles wherever the first roll put it
    // and stops being controllable at all - so this never reaches itself.
    float selfRandomise = 0.0f;

    // How it lands back on release. 0 means a hard cut: the original returns on the same sample.
    // Above that, the parameters glide home over this many bars.
    float recoveryBars = 0.0f;
};

class Chaos
{
public:
    // ------------------------------------------------------------------------------- the targets
    void clearTargets() { targets.clear(); }
    void addTarget (ChaosTarget t) { targets.push_back (std::move (t)); }
    std::vector<ChaosTarget>& allTargets() noexcept { return targets; }
    const std::vector<ChaosTarget>& allTargets() const noexcept { return targets; }
    int targetCount() const noexcept { return (int) targets.size(); }

    void setParams (const ChaosParams& p) noexcept { params = p; }
    const ChaosParams& getParams() const noexcept { return params; }
    ChaosParams& editParams() noexcept { return params; }

    // ------------------------------------------------- the global randomiser: one press, once over
    //
    // Every target that is on and not never, moved once, within its own range and its own reach.
    // Nothing is captured and nothing is restored: this is a destination, not a visit.
    void rollOnce()
    {
        gen::Rng rng (params.seed ^ 0x9e3779b9u);
        for (auto& t : targets)
        {
            if (! t.on || t.never || ! t.usable()) continue;
            if (t.isAction()) t.apply (t.reach, rng);
            else              *t.value = visited (rng, t);
        }
        ++params.seed;          // so the next press is a different sound, as auditioning requires
    }

    // ---------------------------------------------------------------------- IDM Jack: switched on
    void engage (double beats)
    {
        if (held) return;
        for (auto& t : targets)
        {
            if (t.value != nullptr) t.original = *t.value;
            if (t.keep) t.keep();
        }

        if (params.selfRandomise > 0.001f) rollOwnSettings();

        held = true;
        recovering = false;
        nextStep = firstStepAtOrAfter (beats);
        window = 0;
        windowSeed = params.seed;
    }

    void release (double beats)
    {
        if (! held) return;
        held = false;
        if (params.recoveryBars > 0.001f)
        {
            recovering = true;
            recoveryFrom = beats;
        }
        else
        {
            restore();              // hard cut: the original is back on this sample
        }
    }

    bool engaged() const noexcept { return held; }
    bool isRecovering() const noexcept { return recovering; }

    // Called once per block with the transport position at its start. Returns the events that fired
    // inside it, for the plugin to act on; parameter seizures have already been applied.
    const std::vector<ChaosEvent>& advance (double beats, double lengthBeats)
    {
        fired.clear();

        if (recovering)
        {
            const double done = (beats - recoveryFrom) / (double) (params.recoveryBars * 4.0);
            if (done >= 1.0) { restore(); recovering = false; }
            else
            {
                // glide home, so a release under a long tail does not snap
                const float k = (float) done;
                for (auto& t : targets)
                    if (t.value != nullptr && ! t.never)
                        *t.value = *t.value + (t.original - *t.value) * k;
            }
            return fired;
        }

        if (! held) return fired;

        const double blockEnd = beats + lengthBeats;
        while (beatOf (nextStep) < blockEnd)
        {
            const double at = beatOf (nextStep);
            if (at >= beats)
            {
                // a new window may begin here, which is where the seed is decided
                const int w = (int) std::floor (nextStep / (double) std::max (1, params.windowSteps));
                if (w != window)
                {
                    window = w;
                    gen::Rng pick (gen::Rng::mix (params.seed, (uint64_t) w));
                    if (! pick.chance ((double) params.repeatProb))
                        windowSeed = gen::Rng::mix (params.seed, (uint64_t) w);
                    // on a repeat, windowSeed is left as it was - the figure plays again
                }

                // Mixed with the step's position WITHIN the window, not its absolute index.
                // With the absolute index, two windows sharing a seed still produced different
                // figures, and the repeat probability did nothing at all — which is the whole
                // mechanism for a figure persisting long enough to register as an idea.
                const int span = std::max (1, params.windowSteps);
                const int64_t inWindow = (int64_t) nextStep - (int64_t) window * span;
                gen::Rng rng (gen::Rng::mix (windowSeed, (uint64_t) inWindow));
                if (rng.chance ((double) params.density))
                    fireStep (rng, at);
            }
            nextStep += 1.0;
        }
        return fired;
    }

    // Put everything back exactly as it was. Public because a plugin may need it on a project
    // change or a panic.
    void restore()
    {
        for (auto& t : targets)
        {
            if (t.never) continue;
            if (t.value != nullptr) *t.value = t.original;
            if (t.giveBack) t.giveBack();
        }
        recovering = false;
    }

private:
    // Where a lattice step falls, in beats.
    //
    // Steps go in pairs. The even one sits on its own division; the odd one sits `swing` of the way
    // through the pair rather than halfway, which is what makes the grid shuffle. At swing 0.5 it
    // lands exactly halfway and the lattice is straight.
    //
    // The swing has to live here rather than in a conversion from beats to steps, because with
    // swing on, that mapping is not linear and cannot be inverted by multiplying.
    double beatOf (double step) const noexcept
    {
        const int div = std::max (1, params.division);
        const int64_t s = (int64_t) std::floor (step);
        const double pairStart = (double) ((s >= 0 ? s / 2 : (s - 1) / 2) * 2);
        const bool off = (s - (int64_t) pairStart) != 0;
        const double within = off ? (double) params.swing * 2.0 : 0.0;
        return (pairStart + within) / (double) div;
    }

    // The first step at or after a given beat. Found by walking rather than by arithmetic, for the
    // same reason: the swing makes the mapping one-way.
    double firstStepAtOrAfter (double beats) const noexcept
    {
        const int div = std::max (1, params.division);
        double s = std::floor (beats * (double) div) - 2.0;      // start safely behind
        while (beatOf (s) < beats) s += 1.0;
        return s;
    }

    // A seizure: moved relative to where it is, so it reads as a disturbance of the sound you
    // had rather than a jump to a different one. Right for IDM Jack, wrong for the randomiser.
    float rolled (gen::Rng& rng, const ChaosTarget& t, float scale) const noexcept
    {
        const float span = (t.hi - t.lo) * t.reach * scale;
        const float from = t.value != nullptr ? *t.value : t.lo;
        const float v = from + rng.unif (-span, span);
        return v < t.lo ? t.lo : (v > t.hi ? t.hi : v);
    }

    // A visit: somewhere in the range, chosen outright. This is what the randomiser needs, and
    // the relative walk above is actively wrong for it — a relative move with full reach lands
    // on a rail, the next press clamps to the same rail, and pressing again stops changing
    // anything. The randomiser is a destination; IDM Jack is a disturbance.
    //
    // Reach still narrows it, as a window around where the parameter already sits, so a parameter
    // that only wants a nudge gets one even from a press rather than a seizure.
    float visited (gen::Rng& rng, const ChaosTarget& t) const noexcept
    {
        if (t.reach >= 0.999f) return rng.unif (t.lo, t.hi);
        const float half = (t.hi - t.lo) * t.reach * 0.5f;
        const float from = t.value != nullptr ? *t.value : t.lo;
        const float lo = std::max (t.lo, from - half);
        const float hi = std::min (t.hi, from + half);
        return hi > lo ? rng.unif (lo, hi) : lo;
    }

    void fireStep (gen::Rng& rng, double at)
    {
        const float amount = params.extremity * rng.unif (0.35f, 1.0f);

        if (params.axis[(int) ChaosAxis::Parameter] || params.axis[(int) ChaosAxis::Effect])
            for (auto& t : targets)
            {
                if (! t.on || t.never || ! t.usable()) continue;
                if (! rng.chance (0.5)) continue;
                if (t.isAction()) t.apply (amount * t.reach, rng);
                else              *t.value = rolled (rng, t, amount);
            }

        ChaosEvent e;
        e.beats = at;
        e.amount = amount;
        if (params.axis[(int) ChaosAxis::Retrigger])
            e.retriggers = rng.range (0, std::max (0, params.maxRetriggers));
        if (params.axis[(int) ChaosAxis::Timing])
            e.timingMs = rng.unif (params.timingMinMs, params.timingMaxMs) * amount;
        if (params.axis[(int) ChaosAxis::Pitch])
            e.semitones = rng.unif (-params.pitchRange, params.pitchRange) * amount;
        fired.push_back (e);
    }

    // Its own settings, rolled on switch-on. Deliberately narrow: density, extremity, swing,
    // division, repeat probability and the axes. It never touches selfRandomise itself, because a
    // control that randomises how much it randomises itself stops being a control.
    void rollOwnSettings()
    {
        gen::Rng rng (gen::Rng::mix (params.seed, 0xC0FFEEull));
        const float d = params.selfRandomise;
        auto nudge = [&] (float v, float lo, float hi)
        {
            const float span = (hi - lo) * d;
            const float x = v + rng.unif (-span, span);
            return x < lo ? lo : (x > hi ? hi : x);
        };
        params.density    = nudge (params.density, 0.05f, 1.0f);
        params.extremity  = nudge (params.extremity, 0.1f, 1.0f);
        params.swing      = nudge (params.swing, 0.5f, 0.72f);
        params.repeatProb = nudge (params.repeatProb, 0.0f, 0.95f);

        const int divs[] = { 4, 6, 8, 12 };
        if (rng.chance ((double) d)) params.division = divs[rng.below (4)];

        // at least one axis stays on, or switching it on does nothing at all
        for (int i = 0; i < (int) ChaosAxis::Count; ++i)
            if (rng.chance ((double) d * 0.5)) params.axis[i] = ! params.axis[i];
        bool any = false;
        for (int i = 0; i < (int) ChaosAxis::Count; ++i) any = any || params.axis[i];
        if (! any) params.axis[(int) ChaosAxis::Parameter] = true;
    }

    std::vector<ChaosTarget> targets;
    ChaosParams params;
    std::vector<ChaosEvent> fired;

    bool held = false, recovering = false;
    double nextStep = 0.0, recoveryFrom = 0.0;
    int window = -1;
    uint64_t windowSeed = 1u;
};

} // namespace nsr
