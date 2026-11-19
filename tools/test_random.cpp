// The randomiser: that it jumps in time rather than drifting, that it comes home when switched off, that
// extremity really bounds it, and that the same bar always gives the same jumps.
#include "../Source/core/Randomise.h"
#include <cstdio>
#include <set>
#include <vector>

using namespace mjq;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static bool near (float a, float b, float tol = 1.0e-5f) { return std::fabs (a - b) <= tol; }

int main()
{
    std::printf ("\nswitched off\n");
    {
        Cycle c;
        c.home = 0.3f;
        CHECK (! c.on, "a knob does not cycle until it is told to");
        for (double b = 0.0; b < 8.0; b += 0.37)
            if (! near (cycleValueAt (c, Cycled::Dialect, b), 0.3f)) { CHECK (false, "it stays where it was left"); break; }
        CHECK (true, "and stays exactly where it was left, all the way through");

        // switching it on and off again lands back home, not near home
        c.on = true; c.extremity = 1.0f;
        const float away = cycleValueAt (c, Cycled::Dialect, 3.0);
        c.on = false;
        CHECK (! near (away, 0.3f, 0.01f), "switched on it goes somewhere else (%.3f)", away);
        CHECK (near (cycleValueAt (c, Cycled::Dialect, 3.0), 0.3f), "and switched off it lands back exactly home");

        // extremity zero is the same as off
        c.on = true; c.extremity = 0.0f;
        CHECK (near (cycleValueAt (c, Cycled::Dialect, 3.0), 0.3f), "extremity at nothing is the same as off");
    }

    std::printf ("\njumping in time\n");
    {
        Cycle c;
        c.on = true; c.home = 0.5f; c.extremity = 1.0f; c.rate = 1.0f; c.seed = 7;

        // it holds a value for a whole beat, then jumps
        const float atQuarter = cycleValueAt (c, Cycled::Dialect, 0.25);
        const float atThreeQuarters = cycleValueAt (c, Cycled::Dialect, 0.75);
        CHECK (near (atQuarter, atThreeQuarters), "it holds its value through the beat");
        CHECK (! near (atQuarter, cycleValueAt (c, Cycled::Dialect, 1.25), 1.0e-4f), "and jumps on the next one");

        // four a beat jumps four times as often
        c.rate = 4.0f;
        std::set<int64_t> steps;
        for (double b = 0.0; b < 1.0; b += 0.01) steps.insert (cycleStepAt (c, b));
        CHECK ((int) steps.size() == 4, "at four a beat there are four jumps in a beat (%d)", (int) steps.size());

        c.rate = 0.25f;
        steps.clear();
        for (double b = 0.0; b < 4.0; b += 0.01) steps.insert (cycleStepAt (c, b));
        CHECK ((int) steps.size() == 1, "at a quarter a beat, one value lasts four beats (%d)", (int) steps.size());
    }

    std::printf ("\nswing\n");
    {
        Cycle straight, swung;
        straight.on = swung.on = true;
        straight.rate = swung.rate = 2.0f;
        straight.swing = 0.5f;
        swung.swing = 0.7f;

        // the first of each pair lasts longer when it is swung
        auto firstJumpAfter = [] (const Cycle& c)
        {
            const int64_t at0 = cycleStepAt (c, 0.0);
            for (double b = 0.0; b < 4.0; b += 0.001) if (cycleStepAt (c, b) != at0) return b;
            return 4.0;
        };
        const double plain = firstJumpAfter (straight), late = firstJumpAfter (swung);
        CHECK (late > plain + 0.01, "swung, the first of each pair holds on longer (%.3f against %.3f)", late, plain);

        // but a pair still takes the same time, so it does not drift out of the bar
        CHECK (cycleStepAt (straight, 4.0) == cycleStepAt (swung, 4.0), "and a pair still takes the same time, so it stays with the bar");
    }

    std::printf ("\nextremity\n");
    {
        Cycle c;
        c.on = true; c.home = 0.5f; c.rate = 4.0f; c.seed = 3;

        for (float e : { 0.1f, 0.25f, 0.5f, 1.0f })
        {
            c.extremity = e;
            float lowest = 9.0f, highest = -9.0f;
            for (double b = 0.0; b < 400.0; b += 0.25)
            {
                const float v = cycleValueAt (c, Cycled::Dialect, b);
                lowest = std::min (lowest, v); highest = std::max (highest, v);
            }
            const bool bounded = lowest >= 0.5f - e - 1.0e-4f && highest <= 0.5f + e + 1.0e-4f;
            if (! bounded) { CHECK (false, "extremity %.2f keeps it within reach (%.3f to %.3f)", e, lowest, highest); break; }
        }
        CHECK (true, "extremity keeps it within reach of home, whatever it is set to");

        // a wider extremity really does go further
        c.extremity = 0.1f;
        float narrow = 0.0f;
        for (double b = 0.0; b < 200.0; b += 0.25) narrow = std::max (narrow, std::fabs (cycleValueAt (c, Cycled::Dialect, b) - 0.5f));
        c.extremity = 0.9f;
        float wide = 0.0f;
        for (double b = 0.0; b < 200.0; b += 0.25) wide = std::max (wide, std::fabs (cycleValueAt (c, Cycled::Dialect, b) - 0.5f));
        CHECK (wide > narrow * 3.0f, "and a wide one strays much further than a narrow one (%.2f against %.2f)", wide, narrow);
    }

    std::printf ("\nstaying inside the control\n");
    {
        // a knob left near the top of its range must stray downwards, not pile up against the ceiling
        Cycle c;
        c.on = true; c.extremity = 1.0f; c.rate = 4.0f; c.seed = 11;
        c.home = 0.97f;
        bool inside = true;
        int below = 0, total = 0;
        for (double b = 0.0; b < 200.0; b += 0.25)
        {
            const float v = cycleValueAt (c, Cycled::Dialect, b);
            inside = inside && v >= 0.0f && v <= 1.0f;
            if (v < 0.97f) ++below;
            ++total;
        }
        CHECK (inside, "a knob near the top never goes past it");
        CHECK (below > total / 2, "and strays downwards instead of piling up against it (%d of %d)", below, total);

        // GATE and SWING have ranges of their own
        Cycle g;
        g.on = true; g.extremity = 1.0f; g.rate = 4.0f; g.home = 1.0f;
        bool inGate = true;
        for (double b = 0.0; b < 100.0; b += 0.25)
        {
            const float v = cycleValueAt (g, Cycled::Gate, b);
            inGate = inGate && v >= 0.1f && v <= 2.0f;
        }
        CHECK (inGate, "GATE cycles inside its own range, not 0 to 1");

        Cycle sw;
        sw.on = true; sw.extremity = 1.0f; sw.rate = 4.0f; sw.home = 0.6f;
        bool inSwing = true;
        for (double b = 0.0; b < 100.0; b += 0.25)
        {
            const float v = cycleValueAt (sw, Cycled::Swing, b);
            inSwing = inSwing && v >= 0.5f && v <= 0.75f;
        }
        CHECK (inSwing, "and SWING inside its own");
    }

    std::printf ("\nthe same take twice\n");
    {
        Cycle c;
        c.on = true; c.home = 0.5f; c.extremity = 0.8f; c.rate = 2.0f; c.seed = 42;

        std::vector<float> first, second;
        for (double b = 0.0; b < 16.0; b += 0.1) first.push_back (cycleValueAt (c, Cycled::Dialect, b));
        for (double b = 0.0; b < 16.0; b += 0.1) second.push_back (cycleValueAt (c, Cycled::Dialect, b));
        CHECK (first == second, "the same bar gives the same jumps every time");

        // and it comes from the beat, so jumping the transport about does not shift it
        CHECK (near (cycleValueAt (c, Cycled::Dialect, 9.3), first[93], 1.0e-4f), "reading one beat on its own gives the same answer");

        // a different seed gives a different take
        Cycle other = c;
        other.seed = 43;
        bool differs = false;
        for (double b = 0.0; b < 16.0; b += 0.5)
            differs = differs || ! near (cycleValueAt (c, Cycled::Dialect, b), cycleValueAt (other, Cycled::Dialect, b), 1.0e-4f);
        CHECK (differs, "another seed gives another take");
    }

    std::printf ("\nseveral at once\n");
    {
        Randomiser r;
        CHECK (! r.any(), "nothing cycles to begin with");

        r.cycles[(size_t) Cycled::Traffic].on = true;
        r.cycles[(size_t) Cycled::Traffic].home = 0.6f;
        r.cycles[(size_t) Cycled::Traffic].extremity = 0.5f;
        r.cycles[(size_t) Cycled::Gate].home = 1.4f;        // left alone, not cycling
        CHECK (r.any(), "one is enough to count");

        const auto read = cycleRead (r, 5.5);
        CHECK (read.live[(size_t) Cycled::Traffic], "the one that is cycling reads as live");
        CHECK (! read.live[(size_t) Cycled::Gate], "the one that is not, does not");
        CHECK (near (read.value[(size_t) Cycled::Gate], 1.4f), "and reads out exactly where it was left (%.2f)", read.value[(size_t) Cycled::Gate]);
        CHECK (std::fabs (read.value[(size_t) Cycled::Traffic] - 0.6f) <= 0.5f + 1.0e-4f, "while the cycling one stays within reach of home");
    }

    std::printf ("\n%s\n", failures == 0 ? "all ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
