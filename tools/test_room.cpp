// The stereo room: that near and far is more than a volume control, that a voice swung across the picture
// keeps its weight, and that a drawn curve loops without a step in it.
#include "../Source/core/Room.h"
#include <cstdio>

using namespace mjq;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static bool near (float a, float b, float tol) { return std::fabs (a - b) <= tol; }
static float power (const RoomGain& g) { return g.l * g.l + g.r * g.r; }
static float db (float a) { return 20.0f * std::log10 (std::max (1.0e-6f, a)); }

int main()
{
    std::printf ("\nleft and right\n");
    {
        const auto mid = roomGain (0.0f, 0.0f, 0.0f);
        CHECK (near (mid.l, mid.r, 1.0e-4f), "in the middle both sides are equal");

        const auto left = roomGain (-1.0f, 0.0f, 0.0f);
        const auto right = roomGain (1.0f, 0.0f, 0.0f);
        CHECK (left.l > left.r * 20.0f, "hard left is on the left (%.3f against %.3f)", left.l, left.r);
        CHECK (right.r > right.l * 20.0f, "hard right is on the right");
        CHECK (near (left.l, right.r, 1.0e-4f), "and the two sides are mirror images");

        // constant power: swinging across must not make it louder in the middle or thinner at the edges
        float lowest = 1.0e9f, highest = 0.0f;
        for (float x = -1.0f; x <= 1.0f; x += 0.05f)
        {
            const float p = power (roomGain (x, 0.0f, 0.0f));
            lowest = std::min (lowest, p); highest = std::max (highest, p);
        }
        CHECK (db (std::sqrt (highest / lowest)) < 0.2f, "a voice swung across the picture keeps its weight (%.2f dB)", db (std::sqrt (highest / lowest)));
    }

    std::printf ("\nnear and far\n");
    {
        const auto close = roomGain (0.0f, 0.0f, 0.0f);
        const auto far = roomGain (0.0f, 1.0f, 0.0f);

        CHECK (power (far) < power (close), "the back of the room is quieter than the front");
        const float drop = db (std::sqrt (power (far) / power (close)));
        CHECK (drop < -12.0f && drop > -30.0f, "by a real amount, but not out of the mix (%.1f dB)", drop);

        CHECK (far.cutoff < close.cutoff * 0.3f, "and duller: the air takes the top off (%.0f Hz against %.0f)", far.cutoff, close.cutoff);
        CHECK (close.cutoff > 15000.0f, "close by, almost nothing is lost (%.0f Hz)", close.cutoff);

        // it gets quieter and duller all the way, never bouncing back
        float lastAmp = 1.0e9f, lastCut = 1.0e9f;
        bool monotonic = true;
        for (float y = 0.0f; y <= 1.0f; y += 0.02f)
        {
            const auto g = roomGain (0.0f, y, 0.0f);
            monotonic = monotonic && power (g) <= lastAmp + 1.0e-6f && g.cutoff <= lastCut + 1.0f;
            lastAmp = power (g); lastCut = g.cutoff;
        }
        CHECK (monotonic, "going back, it only ever gets quieter and duller");

        // the picture narrows with distance: a far voice is less hard left than a near one
        const auto nearLeft = roomGain (-1.0f, 0.0f, 0.0f);
        const auto farLeft = roomGain (-1.0f, 1.0f, 0.0f);
        const float nearRatio = nearLeft.l / std::max (1.0e-6f, nearLeft.r);
        const float farRatio = farLeft.l / std::max (1.0e-6f, farLeft.r);
        CHECK (farRatio < nearRatio, "a voice far off arrives from nearly one direction (%.1f against %.1f)", farRatio, nearRatio);
    }

    std::printf ("\nwidth\n");
    {
        const auto point = roomGain (-1.0f, 0.0f, 0.0f);
        const auto wide = roomGain (-1.0f, 0.0f, 1.0f);
        CHECK (wide.r > point.r, "a wide voice reaches the other side too (%.3f against %.3f)", wide.r, point.r);
        CHECK (db (std::sqrt (power (wide) / power (point))) < 1.5f, "without getting louder for it");
    }

    std::printf ("\nthe drawn curve\n");
    {
        Place p;
        CHECK (! p.moves(), "a voice stands still until it is put in the room");
        p.on = true;
        CHECK (! p.moves(), "and still until a curve is drawn");

        float x = 0.0f, y = 0.0f;
        p.x = 0.4f; p.y = 0.2f;
        roomPlaceAt (p, 3.7, 16, x, y);
        CHECK (near (x, 0.4f, 1.0e-5f) && near (y, 0.2f, 1.0e-5f), "and stays where it was put");

        // a curve that swings left and right over one bar
        p.move = RoomMove::LeftRight;
        p.depth = 1.0f;
        p.bars = 1;
        p.x = 0.0f;
        for (int i = 0; i < kRoomCurve; ++i)
            p.curve[(size_t) i] = std::sin (6.28318531f * (float) i / (float) kRoomCurve);

        CHECK (p.moves(), "with a curve drawn, it moves");
        {
            Place off = p; off.on = false;
            CHECK (! off.moves(), "taking it out of the room stops it moving, and keeps the curve");
        }

        float lowest = 9.0f, highest = -9.0f;
        for (double b = 0.0; b < 4.0; b += 0.01)
        {
            roomPlaceAt (p, b, 16, x, y);
            lowest = std::min (lowest, x); highest = std::max (highest, x);
        }
        CHECK (lowest < -0.8f && highest > 0.8f, "all the way across (%.2f to %.2f)", lowest, highest);

        // it loops: the end of the bar meets the start without a step
        float a1 = 0.0f, b1 = 0.0f, a2 = 0.0f, b2 = 0.0f;
        roomPlaceAt (p, 3.999, 16, a1, b1);
        roomPlaceAt (p, 4.001, 16, a2, b2);
        CHECK (near (a1, a2, 0.02f), "and loops without a step in it (%.3f to %.3f)", a1, a2);

        // bar two is bar one again
        float c1 = 0.0f, d1 = 0.0f, c2 = 0.0f, d2 = 0.0f;
        roomPlaceAt (p, 1.3, 16, c1, d1);
        roomPlaceAt (p, 5.3, 16, c2, d2);
        CHECK (near (c1, c2, 1.0e-4f), "every bar the same");

        // over two bars it takes twice as long
        p.bars = 2;
        float e1 = 0.0f, f1 = 0.0f, e2 = 0.0f, f2 = 0.0f;
        roomPlaceAt (p, 1.3, 16, e1, f1);
        roomPlaceAt (p, 9.3, 16, e2, f2);
        CHECK (near (e1, e2, 1.0e-4f), "a two-bar curve repeats every two bars");
        roomPlaceAt (p, 5.3, 16, e2, f2);
        CHECK (! near (e1, e2, 0.05f), "and is somewhere else in the second bar");

        // depth decides how far it goes
        p.bars = 1; p.depth = 0.25f;
        float g1 = 0.0f, h1 = 0.0f;
        lowest = 9.0f; highest = -9.0f;
        for (double b = 0.0; b < 4.0; b += 0.01)
        {
            roomPlaceAt (p, b, 16, g1, h1);
            lowest = std::min (lowest, g1); highest = std::max (highest, g1);
        }
        CHECK (highest < 0.3f && lowest > -0.3f, "a shallow curve moves it a little (%.2f to %.2f)", lowest, highest);

        // and it never leaves the room, however hard it is pushed
        p.depth = 1.0f; p.x = 0.9f;
        bool inside = true;
        for (double b = 0.0; b < 8.0; b += 0.01)
        {
            roomPlaceAt (p, b, 16, g1, h1);
            inside = inside && g1 >= -1.0f && g1 <= 1.0f && h1 >= 0.0f && h1 <= 1.0f;
        }
        CHECK (inside, "and a voice pushed past the wall stops at it");
    }

    std::printf ("\nthe air\n");
    {
        RoomAir air;
        air.prepare (48000.0);
        air.set (1000.0f);
        // a step into a low-pass comes up, it does not jump
        float l = 1.0f, r = 1.0f;
        air.process (l, r);
        CHECK (l < 0.3f && l > 0.0f, "a one-pole: the first sample of a step is well short of it (%.3f)", l);
        for (int i = 0; i < 2000; ++i) { l = 1.0f; r = 1.0f; air.process (l, r); }
        CHECK (near (l, 1.0f, 0.01f), "and it reaches it in the end (%.3f)", l);
    }

    std::printf ("\n%s\n", failures == 0 ? "all ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
