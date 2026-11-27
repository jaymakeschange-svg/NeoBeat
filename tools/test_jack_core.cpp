// IDM Jack's realtime core, tested against the spec's own claims.

#include "JackCore.h"

#include <cstdio>
#include <cmath>
#include <set>
#include <string>
#include <vector>

static int failures = 0;
static void ok (bool c, const std::string& w) { std::printf ("  %s  %s\n", c ? "ok: " : "BAD:", w.c_str()); if (! c) ++failures; }
static void heading (const std::string& h) { std::printf ("\n%s\n", h.c_str()); }

static bool sameHit (const nsr::JackHit& a, const nsr::JackHit& b)
{
    if (a.pad != b.pad || a.at != b.at || a.velocity != b.velocity || a.timingMs != b.timingMs) return false;
    for (int k = 0; k < nsr::JoCount; ++k) if (a.off[k] != b.off[k]) return false;
    return true;
}
static bool sameFigure (const nsr::JackFigure& a, const nsr::JackFigure& b)
{
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i) if (! sameHit (a.hits[i], b.hits[i])) return false;
    return true;
}

int main()
{
    std::printf ("IDM Jack core\n");
    const uint32_t all = 0xffffu;

    {
        heading ("the lattice");
        nsr::JackCore j;
        nsr::JackSettings s; s.division = 6; s.swing = 0.5f;
        j.setSettings (s);
        bool straight = true;
        for (int64_t k = 0; k < 48; ++k)
            if (std::abs (j.beatOfStep (k) * 6.0 - (double) k) > 1e-9) straight = false;
        ok (straight, "at swing 0.5, step k sits exactly on k sixths of a beat");

        s.swing = 0.62f; j.setSettings (s);
        bool evens = true, odds = true;
        for (int64_t k = 0; k < 48; k += 2) if (std::abs (j.beatOfStep (k) * 6.0 - (double) k) > 1e-9) evens = false;
        // swing is a float: 0.62f widens to 0.6200000048, so the comparison allows for that rather
        // than for an exact 1.24
        for (int64_t k = 1; k < 48; k += 2)
            if (std::abs (j.beatOfStep (k) * 6.0 - ((double) (k - 1) + 1.24)) > 1e-6) odds = false;
        ok (evens, "with swing, the downbeat steps stay put");
        ok (odds, "and the offbeat steps sit late by the swing, not halfway");

        bool round = true;
        for (double b : { 0.0, 0.1, 0.5, 1.03, 3.999, 7.25 })
        {
            const auto k = j.firstStepAtOrAfter (b);
            if (j.beatOfStep (k) < b - 1e-9 || j.beatOfStep (k - 1) >= b - 1e-12) round = false;
        }
        ok (round, "the first step at or after any beat is found exactly, swing and all");
    }

    {
        heading ("bursts");
        nsr::JackCore j;
        nsr::JackSettings s; s.density = 1.0f; s.recallProb = 0.0f; s.maxRetriggers = 4;
        j.setSettings (s);
        j.engage (0.0);

        bool countsOk = true, subdiv = true, ownOffsets = true;
        int multi = 0;
        for (int64_t k = 0; k < 96; ++k)
        {
            nsr::JackFigure f;
            if (! j.planStep (k, all, 16, f)) continue;
            if (f.count < 1 || f.count > 5) countsOk = false;
            for (int i = 0; i < f.count; ++i)
                if (std::abs (f.hits[i].at - (float) i / (float) f.count) > 1e-6f) subdiv = false;
            if (f.count > 1)
            {
                ++multi;
                if (sameHit (f.hits[0], f.hits[1])) ownOffsets = false;
            }
        }
        ok (countsOk, "a burst is one hit plus up to the retrigger limit");
        ok (subdiv, "its hits subdivide the step evenly");
        ok (multi > 10, "retriggers actually happen: " + std::to_string (multi) + " multi-hit bursts in 96 steps");
        ok (ownOffsets, "each hit in a burst has its own parameter offsets - the spec's line between this and a stutter");

        // retrigger axis off: one hit per burst
        s.axis[(int) nsr::ChaosAxis::Retrigger] = false;
        j.setSettings (s); j.engage (0.0);
        bool single = true;
        for (int64_t k = 0; k < 48; ++k) { nsr::JackFigure f; if (j.planStep (k, all, 16, f) && f.count != 1) single = false; }
        ok (single, "with the retrigger axis off, every burst is a single hit");
    }

    {
        heading ("which lanes it may grab");
        nsr::JackCore j;
        nsr::JackSettings s; s.density = 1.0f; s.recallProb = 0.0f;
        j.setSettings (s); j.engage (0.0);
        const uint32_t kickAndHat = (1u << 0) | (1u << 6);
        std::set<int> seen;
        for (int64_t k = 0; k < 200; ++k)
        {
            nsr::JackFigure f;
            if (j.planStep (k, kickAndHat, 16, f)) for (int i = 0; i < f.count; ++i) seen.insert (f.hits[i].pad);
        }
        ok (seen == std::set<int> { 0, 6 }, "only the included lanes are ever grabbed, and both of them are");

        nsr::JackFigure f;
        ok (! j.planStep (500, 0u, 16, f), "with nothing included, nothing fires");
    }

    {
        heading ("the figure buffer: motifs need a memory");
        nsr::JackCore j;
        nsr::JackSettings s; s.density = 1.0f; s.recallProb = 1.0f; s.memory = 1;
        j.setSettings (s); j.engage (0.0);

        nsr::JackFigure first;
        j.planStep (0, all, 16, first);
        ok (! first.recalled, "the first burst is made fresh - there is nothing to recall yet");

        bool identical = true; int recalled = 0;
        for (int64_t k = 1; k < 40; ++k)
        {
            nsr::JackFigure f;
            if (! j.planStep (k, all, 16, f)) continue;
            if (f.recalled) { ++recalled; if (! sameFigure (f, first)) identical = false; }
        }
        ok (recalled > 30, "with recall at 1 and one slot, nearly every later burst is a recall (" + std::to_string (recalled) + ")");
        ok (identical, "and it is the same figure exactly - lanes, positions, retriggers and parameter moves - "
                       "so it is recognisable as the same motif");

        // recall at 0: never recalled
        s.recallProb = 0.0f; j.setSettings (s); j.engage (0.0);
        int r0 = 0;
        for (int64_t k = 0; k < 60; ++k) { nsr::JackFigure f; if (j.planStep (k, all, 16, f) && f.recalled) ++r0; }
        ok (r0 == 0, "with recall at 0, nothing is recalled");

        // a recalled figure drops a lane that has been taken out since
        s.recallProb = 1.0f; s.memory = 1; j.setSettings (s); j.engage (0.0);
        nsr::JackFigure seed;
        int64_t k = 0;
        while (! j.planStep (k, (1u << 3) | (1u << 9), 16, seed)) ++k;
        bool dropped = true;
        for (int64_t m = k + 1; m < k + 30; ++m)
        {
            nsr::JackFigure f;
            if (j.planStep (m, 1u << 3, 16, f)) for (int i = 0; i < f.count; ++i) if (f.hits[i].pad != 3) dropped = false;
        }
        ok (dropped, "a recalled figure never plays a lane that has since been excluded");
    }

    {
        heading ("axes, and what switching one off does to the others");
        nsr::JackCore j;
        nsr::JackSettings s;
        j.setSettings (s); j.engage (0.0);
        float a[nsr::JoCount], b[nsr::JoCount];
        j.hitOffsets (12345, a);
        s.axis[(int) nsr::ChaosAxis::Pitch] = false;
        j.setSettings (s);
        j.hitOffsets (12345, b);
        bool rest = true;
        for (int k = nsr::JoDecay; k <= nsr::JoLevel; ++k) if (a[k] != b[k]) rest = false;
        ok (b[nsr::JoTune] == 0.0f, "pitch axis off: no pitch offset");
        ok (rest, "and every other offset is exactly what it was - turning one axis off does not reshuffle the rest");

        float c[nsr::JoCount];
        j.hitOffsets (12346, c);
        ok (c[nsr::JoDecay] != b[nsr::JoDecay], "two different hits get different offsets");

        bool bounded = true;
        for (uint64_t key = 0; key < 2000; ++key)
        {
            float o[nsr::JoCount]; j.hitOffsets (key, o);
            for (float v : o) if (std::abs (v) > s.extremity + 1e-6f) bounded = false;
        }
        ok (bounded, "no offset is ever larger than the extremity");
    }

    {
        heading ("reproducible, and the window seed");
        auto run = [] (uint64_t seed)
        {
            nsr::JackCore j; nsr::JackSettings s; s.seed = seed; s.density = 0.7f;
            j.setSettings (s); j.engage (0.0);
            std::vector<float> v;
            for (int64_t k = 0; k < 120; ++k)
            {
                nsr::JackFigure f;
                if (j.planStep (k, 0xffffu, 16, f)) for (int i = 0; i < f.count; ++i) { v.push_back ((float) f.hits[i].pad); v.push_back (f.hits[i].off[nsr::JoDrive]); }
                else v.push_back (-1.0f);
            }
            return v;
        };
        ok (run (7) == run (7), "the same seed gives the same pass");
        ok (run (7) != run (8), "a different seed gives a different one");
    }

    {
        heading ("release");
        nsr::JackCore j; nsr::JackSettings s; s.density = 1.0f;
        j.setSettings (s); j.engage (0.0);
        nsr::JackFigure f;
        ok (j.planStep (0, 0xffffu, 16, f), "held: it fires");
        j.release (2.0);
        ok (! j.planStep (1, 0xffffu, 16, f), "released: it stops at once");
        ok (j.scaleAt (2.0) == 0.0f && ! j.active(), "with no recovery the offsets are gone on the same beat - the spec's instant return");

        s.recoveryBars = 1.0f; j.setSettings (s); j.engage (0.0); j.release (2.0);
        const float early = j.scaleAt (2.5), late = j.scaleAt (5.5);
        ok (early > late && late > 0.0f, "with recovery, the offsets fade rather than cut");
        ok (j.scaleAt (6.01) == 0.0f && ! j.active(), "and are gone after the recovery");
    }

    {
        heading ("self-randomise");
        nsr::JackSettings s; s.selfRandomise = 0.9f;
        const auto was = s;
        gen::Rng rng (3);
        nsr::jackSelfRandomise (s, rng);
        ok (s.density != was.density || s.extremity != was.extremity, "it moves its own settings");
        ok (s.selfRandomise == was.selfRandomise, "but never its own depth");
        bool any = false; for (bool b : s.axis) any = any || b;
        ok (any, "and at least one axis survives");
        nsr::JackSettings off; off.selfRandomise = 0.0f; const auto offWas = off;
        nsr::jackSelfRandomise (off, rng);
        ok (off.density == offWas.density, "off means off");
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
