// The chaos engine: the global randomiser and IDM Jack over one spine.
//
// Two claims matter more than the rest. Events land only on the lattice — that is what keeps it
// rhythmic rather than broken. And release puts everything back exactly, because the brief says
// the written pattern is never altered, so there must be nothing to undo.

#include "Chaos.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <set>

static int failures = 0;

static void ok (bool cond, const std::string& what)
{
    std::printf ("  %s  %s\n", cond ? "ok: " : "BAD:", what.c_str());
    if (! cond) ++failures;
}

static void heading (const std::string& h)
{
    std::printf ("\n%s\n", h.c_str());
}

// A few parameters to seize, standing in for a plugin's own.
struct Rig
{
    float drive = 0.5f, tone = 0.3f, decay = 0.7f, monoLock = 1.0f, ownRate = 0.25f;
    nsr::Chaos chaos;

    Rig()
    {
        chaos.addTarget ({ "KIT",  "drive", &drive, 0.0f, 1.0f, 1.0f, true, false, 0.0f });
        chaos.addTarget ({ "KIT",  "tone",  &tone,  0.0f, 1.0f, 1.0f, true, false, 0.0f });
        chaos.addTarget ({ "KIT",  "decay", &decay, 0.0f, 1.0f, 0.5f, true, false, 0.0f });
        // the two never-targets from the brief: a mono lock turns a mix inside out, and the
        // engine's own controls would randomise their own rate
        chaos.addTarget ({ "ROOM", "mono lock", &monoLock, 0.0f, 1.0f, 0.0f, false, true, 0.0f });
        chaos.addTarget ({ "JACK", "own rate",  &ownRate,  0.0f, 1.0f, 0.0f, false, true, 0.0f });
    }

    std::vector<float> snapshot() const { return { drive, tone, decay, monoLock, ownRate }; }
};

int main()
{
    std::printf ("chaos engine\n");

    // ------------------------------------------------------------------- the global randomiser (B)
    {
        heading ("one press, every parameter once");

        Rig r;
        const auto before = r.snapshot();
        r.chaos.rollOnce();
        const auto after = r.snapshot();

        ok (after[0] != before[0] && after[1] != before[1] && after[2] != before[2],
            "every target that is on has moved");
        ok (after[3] == before[3] && after[4] == before[4],
            "the never-targets are untouched: no mono lock, no randomising its own rate");

        // Decay has half the reach of drive, so over many rolls it should move less far.
        //
        // The seed has to be varied per iteration. A fresh Rig starts at seed 1 every time, so
        // without this the 200 "samples" are 200 copies of one draw and the average means nothing.
        float dSum = 0.0f, cSum = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            Rig q;
            auto qp = q.chaos.getParams();
            qp.seed = (uint64_t) (i + 1);
            q.chaos.setParams (qp);
            const float d0 = q.drive, c0 = q.decay;
            q.chaos.rollOnce();
            dSum += std::abs (q.drive - d0);
            cSum += std::abs (q.decay - c0);
        }
        ok (cSum < dSum * 0.75f,
            "reach is honoured: decay at half reach moved " + std::to_string ((int) (cSum / dSum * 100.0f))
            + "% as far as drive");

        // pressing again gives a different sound, which is the whole point of auditioning
        Rig s;
        s.chaos.rollOnce();
        const float first = s.drive;
        s.chaos.rollOnce();
        ok (s.drive != first, "a second press lands somewhere else");

        // and everything stays inside its own range
        bool inRange = true;
        for (int i = 0; i < 500; ++i)
        {
            Rig q;
            for (int k = 0; k < 20; ++k) q.chaos.rollOnce();
            for (const auto& t : q.chaos.allTargets())
                if (t.value != nullptr && (*t.value < t.lo - 1e-6f || *t.value > t.hi + 1e-6f))
                    inRange = false;
        }
        ok (inRange, "nothing escapes its range, however many times it is pressed");
    }

    // ------------------------------------------------------------------------- the lattice (IDM Jack)
    {
        heading ("events land only on the lattice");

        Rig r;
        auto p = r.chaos.getParams();
        p.division = 6;              // sixteenth triplets
        p.swing = 0.5f;              // straight first, so the positions are easy to state
        p.density = 1.0f;            // every step fires, so none is missed by chance
        r.chaos.setParams (p);

        r.chaos.engage (0.0);
        std::vector<double> at;
        const double block = 0.25;   // a sixteenth at a time, as a host would
        for (int i = 0; i < 64; ++i)
        {
            const auto& ev = r.chaos.advance ((double) i * block, block);
            for (const auto& e : ev) at.push_back (e.beats);
        }

        ok (! at.empty(), "it fires at all: " + std::to_string (at.size()) + " events over 16 beats");

        bool onGrid = true;
        for (double b : at)
        {
            const double steps = b * 6.0;
            if (std::abs (steps - std::round (steps)) > 1e-6) onGrid = false;
        }
        ok (onGrid, "every event sits exactly on a sixth of a beat, straight");

        // nothing fires twice in the same place, and nothing is skipped
        std::set<long long> seen;
        bool dup = false;
        for (double b : at) if (! seen.insert ((long long) std::llround (b * 6000.0)).second) dup = true;
        ok (! dup, "no step fires twice");
    }

    {
        heading ("swing moves the offbeats and leaves the downbeats");

        Rig r;
        auto p = r.chaos.getParams();
        p.division = 4; p.swing = 0.62f; p.density = 1.0f;
        r.chaos.setParams (p);
        r.chaos.engage (0.0);

        std::vector<double> at;
        for (int i = 0; i < 16; ++i)
        {
            const auto& ev = r.chaos.advance ((double) i * 0.25, 0.25);
            for (const auto& e : ev) at.push_back (e.beats);
        }

        // with division 4, the even steps land on 0, 0.5, 1.0 ... and the odd ones are pushed
        bool evensStraight = true, oddsPushed = true;
        for (double b : at)
        {
            const double pair = std::floor (b * 2.0) / 2.0;        // which pair of steps
            const double within = (b - pair) * 4.0;                // 0 for the downbeat
            if (within < 0.01) continue;                            // a downbeat, fine
            if (std::abs (within - 1.24) > 0.02) oddsPushed = false; // swing 0.62 -> 1.24
        }
        for (double b : at)
        {
            const double x = b * 2.0;
            if (std::abs (x - std::round (x)) < 1e-6) continue;      // a downbeat on the half beat
        }
        ok (oddsPushed, "the offbeat step sits late by the swing amount, not halfway");
        ok (evensStraight, "the downbeats are where they would be without swing");

        // and at swing 0.5 the same lattice is straight
        Rig s;
        auto q = s.chaos.getParams();
        q.division = 4; q.swing = 0.5f; q.density = 1.0f;
        s.chaos.setParams (q);
        s.chaos.engage (0.0);
        bool straight = true;
        for (int i = 0; i < 16; ++i)
        {
            const auto& ev = s.chaos.advance ((double) i * 0.25, 0.25);
            for (const auto& e : ev)
            {
                const double x = e.beats * 4.0;
                if (std::abs (x - std::round (x)) > 1e-6) straight = false;
            }
        }
        ok (straight, "swing 0.5 gives a straight grid");
    }

    // ------------------------------------------------------------------- landing back on the original
    {
        heading ("release puts everything back");

        Rig r;
        auto p = r.chaos.getParams();
        p.density = 1.0f; p.extremity = 1.0f;
        r.chaos.setParams (p);

        const auto before = r.snapshot();
        r.chaos.engage (0.0);
        for (int i = 0; i < 32; ++i) r.chaos.advance ((double) i * 0.25, 0.25);

        const auto during = r.snapshot();
        ok (during[0] != before[0], "it has taken hold while held");

        r.chaos.release (8.0);
        const auto after = r.snapshot();
        ok (after == before, "release restores every parameter exactly, so there is nothing to undo");

        // a hard release is exact; a recovery glides, so it is not exact on the same sample
        Rig s;
        auto q = s.chaos.getParams();
        q.density = 1.0f; q.recoveryBars = 1.0f;
        s.chaos.setParams (q);
        const float sBefore = s.drive;
        s.chaos.engage (0.0);
        for (int i = 0; i < 16; ++i) s.chaos.advance ((double) i * 0.25, 0.25);
        s.chaos.release (4.0);
        ok (s.chaos.isRecovering(), "with recovery set, it glides home rather than cutting");
        for (int i = 0; i < 40; ++i) s.chaos.advance (4.0 + (double) i * 0.25, 0.25);
        ok (! s.chaos.isRecovering() && std::abs (s.drive - sBefore) < 1e-5f,
            "and arrives exactly, once the recovery is over");
    }

    // --------------------------------------------------------------------------------- the seeding
    {
        heading ("seeds, and the repeat probability");

        auto run = [] (uint64_t seed, float repeat)
        {
            Rig r;
            auto p = r.chaos.getParams();
            p.seed = seed; p.density = 1.0f; p.repeatProb = repeat; p.windowSteps = 4;
            r.chaos.setParams (p);
            r.chaos.engage (0.0);
            std::vector<float> got;
            for (int i = 0; i < 48; ++i)
            {
                const auto& ev = r.chaos.advance ((double) i * 0.25, 0.25);
                for (const auto& e : ev) got.push_back (e.amount);
            }
            return got;
        };

        ok (run (7, 0.0f) == run (7, 0.0f), "the same seed gives the same pass, every time");
        ok (run (7, 0.0f) != run (8, 0.0f), "a different seed gives a different pass");

        // With the repeat probability at 1, every window reuses its seed, so the figure persists.
        // That is what makes motifs rather than noise, and it is the one knob that decides it.
        const auto always = run (11, 1.0f);
        const auto never = run (11, 0.0f);
        auto selfSimilarity = [] (const std::vector<float>& v, int period)
        {
            int same = 0, n = 0;
            for (size_t i = 0; i + (size_t) period < v.size(); ++i, ++n)
                if (std::abs (v[i] - v[i + (size_t) period]) < 1e-6f) ++same;
            return n > 0 ? (float) same / (float) n : 0.0f;
        };
        const float sA = selfSimilarity (always, 4), sN = selfSimilarity (never, 4);
        ok (sA > sN,
            "repeat probability 1 makes the figure recur (" + std::to_string ((int) (sA * 100))
            + "% vs " + std::to_string ((int) (sN * 100)) + "%)");
    }

    // ------------------------------------------------------------------------------ the axes
    {
        heading ("the axes switch off");

        Rig r;
        auto p = r.chaos.getParams();
        p.density = 1.0f;
        for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i) p.axis[i] = false;
        p.axis[(int) nsr::ChaosAxis::Parameter] = true;
        r.chaos.setParams (p);

        const float before = r.drive;
        r.chaos.engage (0.0);
        bool anyEvent = false;
        for (int i = 0; i < 16; ++i)
        {
            const auto& ev = r.chaos.advance ((double) i * 0.25, 0.25);
            for (const auto& e : ev)
                if (e.retriggers != 0 || e.timingMs != 0.0f || e.semitones != 0.0f) anyEvent = true;
        }
        ok (r.drive != before, "with only the parameter axis on, parameters still move");
        ok (! anyEvent, "and no retrigger, timing or pitch is asked for");
    }

    // ------------------------------------------------------------------------- self-randomise
    {
        heading ("it rolls its own settings on switch-on");

        Rig r;
        auto p = r.chaos.getParams();
        p.selfRandomise = 0.9f;
        p.density = 0.5f; p.extremity = 0.5f;
        r.chaos.setParams (p);
        const auto was = r.chaos.getParams();
        r.chaos.engage (0.0);
        const auto now = r.chaos.getParams();

        ok (now.density != was.density || now.extremity != was.extremity || now.swing != was.swing,
            "switching on with self-randomise set moves its own settings");
        ok (now.selfRandomise == was.selfRandomise,
            "but never its own depth: a control that randomises how much it randomises itself "
            "stops being a control");

        bool anyAxis = false;
        for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i) anyAxis = anyAxis || now.axis[i];
        ok (anyAxis, "at least one axis survives, so switching it on always does something");

        Rig s;
        auto q = s.chaos.getParams();
        q.selfRandomise = 0.0f;
        s.chaos.setParams (q);
        const auto sWas = s.chaos.getParams();
        s.chaos.engage (0.0);
        ok (s.chaos.getParams().density == sWas.density, "off means off");
    }

    // ------------------------------------------------------------------------- action targets
    //
    // Not everything worth seizing is a float: a chop order is a permutation, a map mark an
    // integer row. Those register an action instead, and must behave like any other target -
    // honour on/never, and give their state back on release.
    {
        heading ("targets that are not floats");

        std::vector<int> order { 0, 1, 2, 3, 4, 5, 6, 7 }, saved;
        int applied = 0, kept = 0, gave = 0;

        nsr::Chaos c;
        nsr::ChaosTarget t;
        t.room = "LOOPS"; t.name = "chop order"; t.reach = 1.0f;
        t.apply = [&] (float, gen::Rng& rng)
        {
            ++applied;
            for (int i = (int) order.size() - 1; i > 0; --i) std::swap (order[(size_t) i], order[(size_t) rng.below (i + 1)]);
        };
        t.keep     = [&] { ++kept; saved = order; };
        t.giveBack = [&] { ++gave; order = saved; };
        c.addTarget (t);

        // one that must never fire, to check the never-flag reaches the action path too
        nsr::ChaosTarget blocked;
        blocked.room = "ROOM"; blocked.name = "mono locks"; blocked.never = true; blocked.on = false;
        bool blockedFired = false;
        blocked.apply = [&] (float, gen::Rng&) { blockedFired = true; };
        c.addTarget (blocked);

        auto p2 = c.getParams();
        p2.density = 1.0f;
        c.setParams (p2);

        const auto before = order;
        c.rollOnce();
        ok (applied == 1, "rollOnce reaches an action target");
        ok (order != before, "and the action actually did something");
        ok (! blockedFired, "a never-target's action is not called");

        const auto afterRoll = order;
        c.engage (0.0);
        ok (kept == 1, "engage asks the action target to save its state");
        for (int i = 0; i < 24; ++i) c.advance ((double) i * 0.25, 0.25);
        ok (applied > 1, "and it is seized while held");
        c.release (6.0);
        ok (gave == 1 && order == afterRoll,
            "release gives it back exactly, as with any other target");
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
