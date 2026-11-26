// The stereo room, listened to rather than inspected.
//
// Each of these asks a question that a mix would answer badly if we got it wrong: does moving
// something change how loud it is, does width survive being summed to mono, does a locked element
// actually refuse to move, does distance sound like distance rather than like turning it down.

#include "../Source/shared/StereoRoom.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr float kSr = 48000.0f;
static constexpr int kBlock = 64;

struct Out
{
    std::vector<float> l, r;
    double rms() const
    {
        double s = 0.0;
        for (size_t i = 0; i < l.size(); ++i) s += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
        return std::sqrt (s / std::max<size_t> (1, l.size()));
    }
    // what a listener on one speaker hears
    double monoRms() const
    {
        double s = 0.0;
        for (size_t i = 0; i < l.size(); ++i) { const double m = 0.5 * ((double) l[i] + r[i]); s += m * m; }
        return std::sqrt (s / std::max<size_t> (1, l.size()));
    }
    double balanceDb() const
    {
        double el = 0.0, er = 0.0;
        for (size_t i = 0; i < l.size(); ++i) { el += (double) l[i] * l[i]; er += (double) r[i] * r[i]; }
        return 20.0 * std::log10 (std::sqrt (std::max (1e-12, el)) / std::sqrt (std::max (1e-12, er)));
    }
};

static double db (double a, double b) { return 20.0 * std::log10 (std::max (1e-9, a) / std::max (1e-9, b)); }

// a steady tone, with a little low end so the bass-mono net has something to work on
static void source (std::vector<float>& l, std::vector<float>& r, int n, bool stereo)
{
    l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        // there has to be real top end here, or the dulling that carries distance has nothing to act on
        const float hi = (float) (0.3 * std::sin (6.283185307179586 * 700.0 * t)
                                + 0.3 * std::sin (6.283185307179586 * 6500.0 * t));
        const float lo = (float) (0.4 * std::sin (6.283185307179586 * 60.0 * t));
        l[(size_t) i] = hi + lo;
        // the right differs, so there is real side content to widen or fold
        r[(size_t) i] = stereo ? (float) (0.3 * std::sin (6.283185307179586 * 700.0 * t + 1.1)
                                        + 0.3 * std::sin (6.283185307179586 * 6500.0 * t + 2.2) + lo) : l[(size_t) i];
    }
}

static Out run (const nsr::Place& p, double seconds, bool stereoSource = true,
                float monoBelowHz = 0.0f, double startBeat = 0.0, double bpm = 120.0)
{
    const int n = (int) (seconds * kSr);
    std::vector<float> inL, inR;
    source (inL, inR, n, stereoSource);

    nsr::Strip strip; strip.prepare (kSr, kBlock);
    nsr::BassMono net; net.prepare (kSr);

    Out o; o.l.assign ((size_t) n, 0.0f); o.r.assign ((size_t) n, 0.0f);
    std::vector<float> sl ((size_t) n, 0.0f), sr ((size_t) n, 0.0f);

    double beat = startBeat;
    for (int at = 0; at < n; at += kBlock)
    {
        const int len = std::min (kBlock, n - at);
        if (! p.on)
        {
            // off is a real bypass: the element goes straight out, exactly as it arrived
            for (int i = 0; i < len; ++i) { o.l[(size_t) (at + i)] = inL[(size_t) (at + i)]; o.r[(size_t) (at + i)] = inR[(size_t) (at + i)]; }
        }
        else
        {
            float x = 0.0f, y = 0.0f;
            nsr::placeAt (p, beat, x, y);
            strip.aim (nsr::placementFor (x, y, p.width, p.locked));
            strip.process (inL.data() + at, inR.data() + at, o.l.data() + at, o.r.data() + at,
                           sl.data() + at, sr.data() + at, len);
        }
        beat += (double) len * bpm / (60.0 * kSr);
    }
    if (monoBelowHz > 0.0f) net.process (o.l.data(), o.r.data(), n, monoBelowHz);
    return o;
}

int main()
{
    std::printf ("moving something must not change how loud it is\n");
    {
        nsr::Place centre; centre.on = true; centre.x = 0.0f;
        nsr::Place left = centre; left.x = -1.0f;
        nsr::Place right = centre; right.x = 1.0f;
        nsr::Place half = centre; half.x = -0.5f;

        const auto c = run (centre, 1.0), l = run (left, 1.0), r = run (right, 1.0), h = run (half, 1.0);
        std::printf ("     centre %.1f dB, hard left %+.1f, hard right %+.1f, half left %+.1f\n",
                     20.0 * std::log10 (c.rms()), db (l.rms(), c.rms()), db (r.rms(), c.rms()), db (h.rms(), c.rms()));
        CHECK (std::fabs (db (l.rms(), c.rms())) < 1.0, "hard left is as loud as the middle (%+.2f dB)", db (l.rms(), c.rms()));
        CHECK (std::fabs (db (r.rms(), c.rms())) < 1.0, "hard right is as loud as the middle (%+.2f dB)", db (r.rms(), c.rms()));
        CHECK (std::fabs (db (h.rms(), c.rms())) < 1.0, "and so is half way (%+.2f dB)", db (h.rms(), c.rms()));

        CHECK (l.balanceDb() > 20.0, "hard left really is on the left (%+.0f dB)", l.balanceDb());
        CHECK (r.balanceDb() < -20.0, "hard right really is on the right (%+.0f dB)", r.balanceDb());
        CHECK (std::fabs (c.balanceDb()) < 3.0, "the middle is in the middle (%+.1f dB)", c.balanceDb());
    }

    std::printf ("\nswitching the room on changes nothing until something is moved\n");
    {
        nsr::Place off; off.on = false;
        nsr::Place front; front.on = true; front.x = 0.0f; front.y = 0.0f; front.width = 0.5f;
        const auto a = run (off, 1.0), b = run (front, 1.0);
        CHECK (std::fabs (db (b.rms(), a.rms())) < 1.0,
               "at the front of the stage it is the level it was (%+.2f dB)", db (b.rms(), a.rms()));
    }

    std::printf ("\nwidth survives being summed to mono\n");
    {
        nsr::Place narrow; narrow.on = true; narrow.width = 0.5f;
        nsr::Place wide = narrow; wide.width = 1.0f;
        nsr::Place point = narrow; point.width = 0.0f;

        const auto n = run (narrow, 1.0), w = run (wide, 1.0), p = run (point, 1.0);
        std::printf ("     in mono: as recorded %.4f, wide %.4f, a point %.4f\n", n.monoRms(), w.monoRms(), p.monoRms());
        CHECK (std::fabs (db (w.monoRms(), n.monoRms())) < 0.5,
               "widening does not change what a mono listener hears (%+.2f dB)", db (w.monoRms(), n.monoRms()));
        CHECK (std::fabs (db (p.monoRms(), n.monoRms())) < 0.5,
               "nor does narrowing it to a point (%+.2f dB)", db (p.monoRms(), n.monoRms()));
        // and in stereo it genuinely is wider
        CHECK (w.rms() > p.rms() * 1.02, "but in stereo wide is wider than a point (%.4f against %.4f)", w.rms(), p.rms());
    }

    std::printf ("\na locked element will not move\n");
    {
        nsr::Place loose; loose.on = true; loose.travel = nsr::Travel::Sway; loose.depth = 1.0f; loose.bars = 1.0f; loose.width = 1.0f;
        nsr::Place held = loose; held.locked = true;

        // sample the balance at four points in the lap
        auto balanceSpread = [] (const nsr::Place& p)
        {
            double lo = 1e9, hi = -1e9;
            for (int q = 0; q < 4; ++q)
            {
                const auto o = run (p, 0.1, true, 0.0f, (double) q * 1.0);
                lo = std::min (lo, o.balanceDb()); hi = std::max (hi, o.balanceDb());
            }
            return hi - lo;
        };
        const double loose_spread = balanceSpread (loose), held_spread = balanceSpread (held);
        std::printf ("     across a lap the loose one swings %.0f dB, the locked one %.2f dB\n", loose_spread, held_spread);
        CHECK (loose_spread > 10.0, "an element set to sway really does travel (%.0f dB)", loose_spread);
        CHECK (held_spread < 0.5, "a locked one stays put (%.2f dB)", held_spread);

        const auto h = run (held, 0.5);
        CHECK (std::fabs (h.balanceDb()) < 0.5, "and stays in the middle (%+.2f dB)", h.balanceDb());
        // locked means mono, so the stereo and mono levels agree
        CHECK (std::fabs (db (h.monoRms(), h.rms())) < 0.5, "and is mono (%+.2f dB between stereo and mono)", db (h.monoRms(), h.rms()));
    }

    std::printf ("\ndistance sounds like distance, not like turning it down\n");
    {
        nsr::Place near_; near_.on = true; near_.y = 0.0f;
        nsr::Place far_ = near_; far_.y = 1.0f;

        const auto a = run (near_, 1.0), b = run (far_, 1.0);
        const double drop = db (b.rms(), a.rms());
        std::printf ("     the back of the stage is %+.1f dB\n", drop);
        CHECK (drop < -1.0 && drop > -8.0, "far is a little quieter, not merely turned down (%+.1f dB)", drop);

        // and duller, which is what actually reads as distance
        auto top = [] (const Out& o)
        {
            double s = 0.0, prev = 0.0;
            for (size_t i = 0; i < o.l.size(); ++i) { const double v = 0.5 * (o.l[i] + o.r[i]); const double d = v - prev; prev = v; s += d * d; }
            return std::sqrt (s / std::max<size_t> (1, o.l.size()));
        };
        const double dull = db (top (b) / std::max (1e-9, b.rms()), top (a) / std::max (1e-9, a.rms()));
        CHECK (dull < -3.0, "and markedly duller (%+.1f dB of top end, level for level)", dull);

        const auto pl = nsr::placementFor (0.0f, 1.0f, 0.0f, false), pn = nsr::placementFor (0.0f, 0.0f, 0.0f, false);
        CHECK (pl.send > pn.send * 2.0, "and sends far more to the room (%.2f against %.2f)", pl.send, pn.send);
    }

    std::printf ("\nthe bottom of the mix stays in the middle\n");
    {
        nsr::Place wide; wide.on = true; wide.width = 1.0f; wide.x = -0.8f;
        const auto without = run (wide, 1.0, true, 0.0f);
        const auto with = run (wide, 1.0, true, 120.0f);

        // the 60 Hz tone is in both channels; after the net, the low end should sum rather than cancel
        auto lowMono = [] (const Out& o)
        {
            // crude: the mono sum's energy, which is what a single speaker gets
            return o.monoRms();
        };
        std::printf ("     mono sum without the net %.4f, with it %.4f\n", lowMono (without), lowMono (with));
        CHECK (lowMono (with) >= lowMono (without) * 0.98, "the net does not cost the mono listener anything");
        CHECK (std::fabs (db (with.rms(), without.rms())) < 1.5, "and barely changes the stereo level (%+.2f dB)", db (with.rms(), without.rms()));
    }

    std::printf ("\ntwo elements can be told apart on the stage\n");
    {
        nsr::Place a; a.on = true; a.x = -0.7f;
        nsr::Place b; b.on = true; b.x = 0.7f;
        const auto oa = run (a, 0.5), ob = run (b, 0.5);
        CHECK (oa.balanceDb() > 6.0 && ob.balanceDb() < -6.0,
               "one sits left and one sits right (%+.0f dB, %+.0f dB)", oa.balanceDb(), ob.balanceDb());
    }

    std::printf ("\nphase keeps two travelling elements apart\n");
    {
        nsr::Place one; one.on = true; one.travel = nsr::Travel::Sway; one.depth = 1.0f; one.bars = 2.0f; one.phase = 0.0f;
        nsr::Place two = one; two.phase = 0.5f;
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        nsr::placeAt (one, 2.0, x1, y1);
        nsr::placeAt (two, 2.0, x2, y2);
        CHECK (std::fabs (x1 - x2) > 1.0, "half a lap apart, they are on opposite sides (%.2f against %.2f)", x1, x2);
    }

    std::printf ("\nevery path stays on the stage\n");
    {
        bool inside = true;
        for (int t = 1; t < (int) nsr::Travel::Count; ++t)
        {
            nsr::Place p; p.on = true; p.travel = (nsr::Travel) t; p.depth = 1.0f; p.bars = 1.0f; p.x = 0.9f; p.y = 0.9f;
            for (int i = 0; i < 400; ++i)
            {
                float x = 0, y = 0;
                nsr::placeAt (p, (double) i * 0.05, x, y);
                inside = inside && x >= -1.0001f && x <= 1.0001f && y >= -0.0001f && y <= 1.0001f && std::isfinite (x) && std::isfinite (y);
            }
        }
        CHECK (inside, "no path walks off the edge, even from a corner");
    }

    std::printf ("\nnothing clicks when a control is moved while it plays\n");
    {
        // swept hard left to hard right across one second, looking for a jump between samples
        const int n = (int) kSr;
        std::vector<float> inL, inR;
        source (inL, inR, n, true);
        nsr::Strip strip; strip.prepare (kSr, kBlock);
        std::vector<float> ol ((size_t) n, 0.0f), orr ((size_t) n, 0.0f);

        for (int at = 0; at < n; at += kBlock)
        {
            const int len = std::min (kBlock, n - at);
            const float x = -1.0f + 2.0f * (float) at / (float) n;
            strip.aim (nsr::placementFor (x, 0.0f, 0.5f, false));
            strip.process (inL.data() + at, inR.data() + at, ol.data() + at, orr.data() + at, nullptr, nullptr, len);
        }

        // A click from a parameter update lands exactly on a block boundary, because that is where
        // the value changes. So compare the steps that straddle a boundary with the steps that do
        // not. Any global measure is fooled here: as the sweep moves everything to one side, the
        // fading channel's typical step shrinks while its biggest step is left over from when it
        // was loud, which looks like a spike and is not one.
        auto boundaryRatio = [] (const std::vector<float>& v, int block)
        {
            double onEdge = 0.0, inside = 0.0;
            long nEdge = 0, nIn = 0;
            for (size_t i = 1; i < v.size(); ++i)
            {
                const double d = (double) (v[i] - v[i - 1]) * (v[i] - v[i - 1]);
                if ((int) (i % (size_t) block) == 0) { onEdge += d; ++nEdge; }
                else                                 { inside += d; ++nIn; }
            }
            if (nEdge < 4 || nIn < 4) return 1.0;
            const double e = std::sqrt (onEdge / (double) nEdge), q = std::sqrt (inside / (double) nIn);
            return q > 1.0e-12 ? e / q : 1.0;
        };

        const double sweptEdge = std::max (boundaryRatio (ol, kBlock), boundaryRatio (orr, kBlock));
        std::printf ("     steps on a block boundary against steps inside, while sweeping: %.2fx\n", sweptEdge);
        CHECK (sweptEdge < 1.3, "updating the position every block puts no step on the boundary (%.2fx)", sweptEdge);

        // and the hard case: the control slammed from side to side every single block
        {
            std::vector<float> jl ((size_t) n, 0.0f), jr ((size_t) n, 0.0f);
            nsr::Strip s2; s2.prepare (kSr, kBlock);
            for (int at = 0; at < n; at += kBlock)
            {
                const int len = std::min (kBlock, n - at);
                const float x = ((at / kBlock) % 2 == 0) ? -1.0f : 1.0f;
                s2.aim (nsr::placementFor (x, 0.0f, 0.5f, false));
                s2.process (inL.data() + at, inR.data() + at, jl.data() + at, jr.data() + at, nullptr, nullptr, len);
            }
            const double jumpedEdge = std::max (boundaryRatio (jl, kBlock), boundaryRatio (jr, kBlock));
            std::printf ("     the same, slammed hard left to hard right every block: %.2fx\n", jumpedEdge);
            CHECK (jumpedEdge < 2.0, "even slamming it side to side every block does not click (%.2fx)", jumpedEdge);
        }
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
