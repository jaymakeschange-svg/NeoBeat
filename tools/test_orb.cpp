// The Orb: that parking on a corner gives that corner exactly, that the blend between two is a blend, that
// effects do not cross-fade into each other when the corners hold different ones, and that the material
// migrates a step at a time rather than smearing.
#include "../Source/core/Orb.h"
#include <cstdio>
#include <map>
#include <vector>

using namespace mjq;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static bool near (float a, float b, float tol = 1.0e-4f) { return std::fabs (a - b) <= tol; }

int main()
{
    std::printf ("\nthe ring\n");
    {
        Orb orb;
        for (int i = 0; i < kOrbCorners; ++i)
        {
            float cx = 0.0f, cy = 0.0f;
            orbCornerAt (i, cx, cy);
            CHECK (near (std::sqrt (cx * cx + cy * cy), 1.0f), "corner %d sits on the ring", i);
        }
        float cx = 0.0f, cy = 0.0f;
        orbCornerAt (0, cx, cy);
        CHECK (near (cx, 0.0f) && near (cy, -1.0f), "corner 1 is at the top");
        orbCornerAt (2, cx, cy);
        CHECK (near (cx, 1.0f) && near (cy, 0.0f), "corner 3 is to the right");
        orbCornerAt (8, cx, cy);
        CHECK (near (cx, 0.0f) && near (cy, -1.0f), "it wraps round");
    }

    std::printf ("\nan empty Orb\n");
    {
        Orb orb;
        CHECK (! orb.anySet(), "starts with nothing in it");
        float w[kOrbCorners];
        orbWeights (orb, 0.0f, 0.0f, w);
        float total = 0.0f;
        for (float v : w) total += v;
        CHECK (near (total, 0.0f), "and no corner has any say");
    }

    std::printf ("\nparking on a corner\n");
    {
        Orb orb;
        for (int i = 0; i < kOrbCorners; ++i)
        {
            auto& c = orb.corners[(size_t) i];
            c.set = true;
            c.dialect = 0.1f * (float) i;
            c.traffic = 1.0f - 0.1f * (float) i;
            c.gate = 0.5f + 0.05f * (float) i;
            c.transpose = i;
        }

        for (int i = 0; i < kOrbCorners; ++i)
        {
            float cx = 0.0f, cy = 0.0f;
            orbCornerAt (i, cx, cy);
            const auto b = orbRead (orb, cx, cy);
            CHECK (b.nearest == i, "the joystick on corner %d reads corner %d", i, b.nearest);
            CHECK (b.weight[(size_t) i] > 0.5f, "and that corner has most of the say (%.2f)", b.weight[(size_t) i]);
        }

        // the weights always add up, wherever it is put
        for (float t = 0.0f; t < 6.28f; t += 0.37f)
        {
            float w[kOrbCorners];
            orbWeights (orb, 0.8f * std::sin (t), 0.8f * std::cos (t), w);
            float total = 0.0f;
            for (float v : w) total += v;
            if (! near (total, 1.0f)) { CHECK (false, "the weights add up to one at angle %.2f (got %.4f)", t, total); break; }
        }
        CHECK (true, "the weights add up to one all the way round");

        // the middle is a blend of everything, not one corner
        const auto middle = orbRead (orb, 0.0f, 0.0f);
        int heard = 0;
        for (float v : middle.weight) if (v > 0.01f) ++heard;
        CHECK (heard == kOrbCorners, "in the middle every corner is heard (%d)", heard);
    }

    std::printf ("\nblending the controls\n");
    {
        Orb orb;
        auto& a = orb.corners[0];
        auto& b = orb.corners[4];           // the opposite corner
        a.set = b.set = true;
        a.dialect = 0.0f; a.traffic = 0.0f; a.gate = 0.25f; a.transpose = -12;
        b.dialect = 1.0f; b.traffic = 1.0f; b.gate = 1.75f; b.transpose = 12;

        const auto onA = orbRead (orb, 0.0f, -1.0f);
        CHECK (near (onA.dialect, 0.0f, 0.01f) && near (onA.gate, 0.25f, 0.01f), "on one corner it reads that corner (%.2f)", onA.dialect);

        const auto onB = orbRead (orb, 0.0f, 1.0f);
        CHECK (near (onB.dialect, 1.0f, 0.01f) && near (onB.gate, 1.75f, 0.01f), "on the other it reads the other (%.2f)", onB.dialect);

        const auto mid = orbRead (orb, 0.0f, 0.0f);
        CHECK (near (mid.dialect, 0.5f, 0.02f), "halfway between is halfway (%.3f)", mid.dialect);
        CHECK (near (mid.gate, 1.0f, 0.02f), "and so is the gate (%.3f)", mid.gate);
        CHECK (near (mid.transpose, 0.0f, 0.1f), "and the transpose (%.3f)", mid.transpose);

        // nothing jumps on the way across
        float last = orbRead (orb, 0.0f, -1.0f).dialect, biggest = 0.0f;
        for (float y = -1.0f; y <= 1.0f; y += 0.02f)
        {
            const float d = orbRead (orb, 0.0f, y).dialect;
            biggest = std::max (biggest, std::fabs (d - last));
            last = d;
        }
        CHECK (biggest < 0.06f, "and it travels smoothly, never jumping more than %.3f at a step", biggest);
    }

    std::printf ("\nthe effect stack\n");
    {
        Orb orb;
        auto& a = orb.corners[0];
        auto& b = orb.corners[4];
        a.set = b.set = true;

        // both corners hold the same effect in slot 0, with different settings
        a.stack[0].fx = 3; a.stack[0].mix = 1.0f; a.stack[0].p[0] = 0.0f;
        b.stack[0].fx = 3; b.stack[0].mix = 1.0f; b.stack[0].p[0] = 1.0f;
        // they hold different effects in slot 1
        a.stack[1].fx = 7; a.stack[1].mix = 1.0f; a.stack[1].p[0] = 0.2f;
        b.stack[1].fx = 11; b.stack[1].mix = 1.0f; b.stack[1].p[0] = 0.9f;

        const auto mid = orbRead (orb, 0.0f, 0.0f);
        CHECK (mid.stack[0].fx == 3, "an effect both corners hold stays itself across the blend");
        CHECK (near (mid.stack[0].p[0], 0.5f, 0.02f), "and its settings blend (%.3f)", mid.stack[0].p[0]);

        CHECK (mid.stack[1].fx == 7 || mid.stack[1].fx == 11, "where the corners hold different effects, one of them wins");
        CHECK (near (mid.stack[1].p[0], mid.stack[1].fx == 7 ? 0.2f : 0.9f, 0.02f),
               "and it keeps its own settings rather than averaging with the other (%.3f)", mid.stack[1].p[0]);
        CHECK (mid.stack[1].mix < 0.75f, "its mix fades with how much say it has, so it arrives rather than jumps (%.2f)", mid.stack[1].mix);

        CHECK (mid.stack[2].fx < 0 && ! mid.stack[2].on(), "an empty slot stays empty");

        // an effect only one corner holds comes in as the joystick approaches
        Orb one;
        one.corners[0].set = one.corners[4].set = true;
        one.corners[0].stack[0].fx = 5; one.corners[0].stack[0].mix = 1.0f;
        const float farMix = orbRead (one, 0.0f, 0.6f).stack[0].mix;
        const float nearMix = orbRead (one, 0.0f, -0.6f).stack[0].mix;
        CHECK (nearMix > farMix, "it is stronger near its own corner (%.2f against %.2f)", nearMix, farMix);
        CHECK (near (orbRead (one, 0.0f, -1.0f).stack[0].mix, 1.0f, 0.02f), "and full on the corner itself");
    }

    std::printf ("\nthe material changes hands\n");
    {
        Orb orb;
        for (int i = 0; i < kOrbCorners; ++i) orb.corners[(size_t) i].set = true;
        const int steps = 64;

        auto ownersAt = [&] (float x, float y)
        {
            float w[kOrbCorners];
            orbWeights (orb, x, y, w);
            std::vector<int> owners;
            owners.reserve (steps);
            for (int s = 0; s < steps; ++s) owners.push_back (orbCornerForStep (w, s, orb.salt));
            return owners;
        };

        // parked on a corner, every step is that corner's
        for (int i = 0; i < kOrbCorners; ++i)
        {
            float cx = 0.0f, cy = 0.0f;
            orbCornerAt (i, cx, cy);
            float w[kOrbCorners] {};
            for (int k = 0; k < kOrbCorners; ++k) w[k] = k == i ? 1.0f : 0.0f;
            bool all = true;
            for (int s = 0; s < steps; ++s) all = all && orbCornerForStep (w, s, orb.salt) == i;
            if (! all) { CHECK (false, "every step belongs to corner %d when it has all the say", i); break; }
        }
        CHECK (true, "with one corner holding all the say, every step is its own");

        // the same place always gives the same answer
        CHECK (ownersAt (0.3f, -0.4f) == ownersAt (0.3f, -0.4f), "the same spot always divides the steps the same way");

        // halfway between two corners, the steps are shared
        Orb two;
        two.corners[0].set = two.corners[4].set = true;
        float w[kOrbCorners];
        orbWeights (two, 0.0f, 0.0f, w);
        int fromA = 0, fromB = 0;
        for (int s = 0; s < steps; ++s) (orbCornerForStep (w, s, two.salt) == 0 ? fromA : fromB)++;
        CHECK (fromA > steps / 4 && fromB > steps / 4, "halfway between two corners the steps are shared (%d and %d)", fromA, fromB);

        // moving a little changes a few steps, not all of them: the figure migrates
        auto before = ownersAt (0.0f, -0.9f);
        int biggestChange = 0;
        for (float y = -0.9f; y <= 0.9f; y += 0.05f)
        {
            auto now = ownersAt (0.0f, y);
            int changed = 0;
            for (int s = 0; s < steps; ++s) if (now[(size_t) s] != before[(size_t) s]) ++changed;
            biggestChange = std::max (biggestChange, changed);
            before = now;
        }
        CHECK (biggestChange <= steps / 4, "a small move changes at most %d of %d steps, so the figure migrates", biggestChange, steps);

        // and travelling the whole way does change it
        auto start = ownersAt (0.0f, -1.0f);
        auto end = ownersAt (0.0f, 1.0f);
        int differ = 0;
        for (int s = 0; s < steps; ++s) if (start[(size_t) s] != end[(size_t) s]) ++differ;
        CHECK (differ > steps / 2, "travelling across changes most of them (%d of %d)", differ, steps);
    }

    std::printf ("\nthe line it reads out\n");
    {
        Orb orb;
        orb.corners[0].set = true; orb.corners[0].seed = 11; orb.corners[0].dialect = 0.0f; orb.corners[0].gate = 0.3f;
        orb.corners[4].set = true; orb.corners[4].seed = 99; orb.corners[4].dialect = 1.0f; orb.corners[4].gate = 1.8f;

        Line shape;
        shape.numSteps = 32;
        generate (shape, 1234);

        Line a, b, mid;
        orbLine (orb, 0.0f, -1.0f, shape, a);
        orbLine (orb, 0.0f, 1.0f, shape, b);
        orbLine (orb, 0.0f, 0.0f, shape, mid);

        CHECK (near (a.gate, 0.3f, 0.02f) && near (b.gate, 1.8f, 0.02f), "each corner reads out its own controls");
        CHECK (near (mid.gate, 1.05f, 0.05f), "and halfway is halfway (%.2f)", mid.gate);

        // the material of the two corners really is different
        int differ = 0;
        for (int s = 0; s < shape.numSteps; ++s) if (a.steps[(size_t) s].freeDegree != b.steps[(size_t) s].freeDegree) ++differ;
        CHECK (differ > shape.numSteps / 3, "the two corners write different material (%d of %d steps)", differ, shape.numSteps);

        // in the middle, the steps come from both
        int fromA = 0, fromB = 0, fromNeither = 0;
        for (int s = 0; s < shape.numSteps; ++s)
        {
            const bool isA = mid.steps[(size_t) s].freeDegree == a.steps[(size_t) s].freeDegree;
            const bool isB = mid.steps[(size_t) s].freeDegree == b.steps[(size_t) s].freeDegree;
            if (isA && ! isB) ++fromA; else if (isB && ! isA) ++fromB; else if (! isA && ! isB) ++fromNeither;
        }
        CHECK (fromNeither == 0, "every step in the blend comes from a real corner, none is invented");
        CHECK (fromA > 0 && fromB > 0, "and in the middle both corners are playing (%d and %d)", fromA, fromB);

        // reading the same place twice gives the same line
        Line again;
        orbLine (orb, 0.2f, -0.3f, shape, again);
        Line onceMore;
        orbLine (orb, 0.2f, -0.3f, shape, onceMore);
        bool same = true;
        for (int s = 0; s < shape.numSteps; ++s) same = same && again.steps[(size_t) s].freeDegree == onceMore.steps[(size_t) s].freeDegree;
        CHECK (same, "the same place reads out the same line every time");

        // what the line is in is the line's, not the Orb's
        CHECK (again.numSteps == shape.numSteps && again.root == shape.root && again.scaleIndex == shape.scaleIndex,
               "the key, the scale and the length stay the line's own");
    }

    std::printf ("\n%s\n", failures == 0 ? "all ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
