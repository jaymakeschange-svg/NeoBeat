// The spectral delay, listened to rather than inspected.
//
// Each of these asks something the ear would notice immediately and a glance at the code would
// not: does a band actually delay by the time it was given, does energy really climb the spectrum
// when the side feed is set, does the interaural split land inside the window that produces
// inside-the-head rather than a position, and does the thing stay finite when both feedbacks are
// pushed past what the maths allows.
//
// SpectralDelay.h leans on nothing but the standard library, so this compiles and runs in seconds.

#include "SpectralDelay.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void ok (bool cond, const std::string& what)
{
    std::printf ("  %s %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (! cond) ++failures;
}
static void heading (const char* s) { std::printf ("\n%s\n", s); }

static constexpr float SR = 48000.0f;

struct Buf
{
    std::vector<float> l, r;
    explicit Buf (int n) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
    int size() const { return (int) l.size(); }
};

// energy above ~1 kHz as a share of the whole, measured with one pole each way
static double brightness (const float* x, int from, int to)
{
    const float a = 1.0f - std::exp (-6.283185307179586f * 1000.0f / SR);
    float lp = 0.0f;
    double hi = 0.0, all = 0.0;
    for (int i = 0; i < to; ++i)
    {
        lp += a * (x[i] - lp);
        if (i < from) continue;
        const double h = (double) x[i] - lp;
        hi += h * h;
        all += (double) x[i] * x[i];
    }
    return all > 1e-12 ? hi / all : 0.0;
}

static double rms (const float* x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, to - from));
}

static bool finite (const Buf& b)
{
    for (int i = 0; i < b.size(); ++i)
        if (! std::isfinite (b.l[(size_t) i]) || ! std::isfinite (b.r[(size_t) i])) return false;
    return true;
}

// one click at the start, then silence
static void click (Buf& b, float amp = 1.0f)
{
    b.l[0] = b.r[0] = amp;
}

static void tone (Buf& b, float hz, int until)
{
    for (int i = 0; i < std::min (until, b.size()); ++i)
        b.l[(size_t) i] = b.r[(size_t) i] =
            0.5f * std::sin (6.283185307179586f * hz * (float) i / SR);
}

int main()
{
    //==============================================================================================
    heading ("silence in, silence out");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p; p.startFeedback = 0.7f; p.finishFeedback = 0.7f;
        d.setParams (p);
        Buf b (8192);
        d.process (b.l.data(), b.r.data(), b.size());
        ok (rms (b.l.data(), 0, b.size()) == 0.0, "nothing in produces nothing out");
    }

    //==============================================================================================
    heading ("the bands sit where they were asked to, log spaced");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p; p.startHz = 100.0f; p.finishHz = 8000.0f;
        d.setParams (p);
        ok (std::abs (d.bandHz (0) - 100.0f) < 1.0f, "the first band is at the start frequency");
        ok (std::abs (d.bandHz (15) - 8000.0f) < 10.0f, "the last is at the finish frequency");

        // log spacing means a constant ratio between neighbours
        bool constantRatio = true;
        const float r0 = d.bandHz (1) / d.bandHz (0);
        for (int b = 1; b < 15; ++b)
            if (std::abs (d.bandHz (b + 1) / d.bandHz (b) - r0) > 0.01f) constantRatio = false;
        ok (constantRatio, "the spacing is a constant ratio, not a constant number of hertz");
    }

    //==============================================================================================
    heading ("a band delays by the time it was given");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p;
        p.startTimeMs = p.finishTimeMs = 100.0f;      // every band the same, so timing is readable
        p.startFeedback = p.finishFeedback = 0.0f;
        p.startLfoMs = p.finishLfoMs = 0.0f;
        p.startLevel = p.finishLevel = 1.0f;
        p.dry = 0.0f; p.wet = 1.0f;
        d.setParams (p);

        Buf b (48000);
        click (b);
        d.process (b.l.data(), b.r.data(), b.size());

        int peak = 0; float best = 0.0f;
        for (int i = 1; i < b.size(); ++i)
            if (std::abs (b.l[(size_t) i]) > best) { best = std::abs (b.l[(size_t) i]); peak = i; }

        const float ms = 1000.0f * (float) peak / SR;
        std::printf ("     the one repeat lands at %.1f ms\n", ms);
        ok (std::abs (ms - 100.0f) < 6.0f, "it arrives at the time it was set to");
        ok (best > 1.0e-4f, "and it is actually audible");
    }

    //==============================================================================================
    heading ("the side feed makes energy climb the spectrum");
    {
        // A burst in the middle of the range, so energy has somewhere to go in both directions.
        // Feedback high enough that the tail still exists where it is measured - the first version
        // of this test measured at 0.6 to 1.6 seconds, by which point a 0.55 feedback at 60 ms has
        // been through ten repeats and there is nothing left but denormal dust. Three runs then
        // agreed to four decimal places and the test looked like it had found a dead control.
        auto run = [] (int offset, float sideFb)
        {
            nsr::SpectralDelay d;
            d.prepare (SR);
            nsr::SpectralDelayParams p;
            p.startTimeMs = p.finishTimeMs = 50.0f;
            p.startFeedback = p.finishFeedback = 0.80f;
            p.startLevel = p.finishLevel = 1.0f;
            p.startLfoMs = p.finishLfoMs = 0.0f;
            p.sideOffset = offset; p.sideFeedback = sideFb;
            p.dry = 0.0f; p.wet = 1.0f;
            d.setParams (p);

            // Broadband, not a tone. A bandpass bank cannot move a tone's frequency - it can only
            // attenuate it - so a sine proves nothing about migration however the bands are wired.
            // The side feed redistributes broadband content between lines, which needs content in
            // every band to redistribute.
            Buf b (int (SR * 1.5f));
            unsigned seed = 12345u;
            for (int i = 0; i < 2400; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float v = 0.45f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
                b.l[(size_t) i] = b.r[(size_t) i] = v;
            }
            d.process (b.l.data(), b.r.data(), b.size());
            return b;
        };

        auto none = run (0, 0.0f);
        auto plus = run (+1, 0.55f);
        auto minus = run (-1, 0.55f);

        const int w0 = int (SR * 0.15f), w1 = int (SR * 0.45f), w2 = int (SR * 0.9f);

        std::printf ("     tail still present?  none %.5f  +1 %.5f  -1 %.5f\n",
                     rms (none.l.data(), w1, w2), rms (plus.l.data(), w1, w2),
                     rms (minus.l.data(), w1, w2));
        ok (rms (plus.l.data(), w1, w2) > 1.0e-5, "there is still a tail where it is measured");

        const double bNone  = brightness (none.l.data(),  w1, w2);
        const double bPlus  = brightness (plus.l.data(),  w1, w2);
        const double bMinus = brightness (minus.l.data(), w1, w2);
        std::printf ("     brightness of the tail  none %.4f   +1 %.4f   -1 %.4f\n",
                     bNone, bPlus, bMinus);

        ok (finite (plus) && finite (minus), "it stays finite with the side feed on");

        // +1 feeds band b from band b+1, so content from above propagates down the chain of
        // lines and is replayed at each one's time and level - the top of the spectrum persists.
        // -1 is the other way and the tail darkens. Worth fixing in a test: it is exactly the
        // kind of sign that gets flipped in a refactor, and nothing else would catch it.
        ok (bPlus > bNone, "+1 keeps the top of the spectrum alive in the tail");
        ok (bMinus < bPlus, "-1 sends it the other way");

        const double bPlusEarly = brightness (plus.l.data(), w0, w1);
        const double bMinusEarly = brightness (minus.l.data(), w0, w1);
        std::printf ("     over time  +1: %.4f -> %.4f    -1: %.4f -> %.4f\n",
                     bPlusEarly, bPlus, bMinusEarly, bMinus);
        ok ((bPlus - bPlusEarly) > (bMinus - bMinusEarly),
            "and the two directions keep diverging rather than settling together");
    }

    //==============================================================================================
    heading ("the interaural split lands inside the window, and only inside it");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p;
        p.startTimeMs = p.finishTimeMs = 50.0f;
        p.startFeedback = p.finishFeedback = 0.0f;
        p.startLfoMs = p.finishLfoMs = 0.0f;
        p.startInterMs = 3.0f; p.finishInterMs = 3.0f;
        p.interLfoAmt = 0.0f;                          // hold it still so it can be measured
        p.dry = 0.0f; p.wet = 1.0f;
        d.setParams (p);

        Buf b (48000);
        click (b);
        d.process (b.l.data(), b.r.data(), b.size());

        auto peakOf = [] (const std::vector<float>& v)
        {
            int at = 0; float best = 0.0f;
            for (int i = 1; i < (int) v.size(); ++i)
                if (std::abs (v[(size_t) i]) > best) { best = std::abs (v[(size_t) i]); at = i; }
            return at;
        };
        const int pl = peakOf (b.l), pr = peakOf (b.r);
        const float gapMs = 1000.0f * (float) (pr - pl) / SR;
        std::printf ("     left peak %d, right peak %d, gap %.2f ms\n", pl, pr, gapMs);
        ok (std::abs (gapMs - 3.0f) < 0.6f, "a 3 ms ask produces a 3 ms gap between the ears");
        ok (gapMs > 0.69f, "which is past anything a real head could produce");
        ok (gapMs < 30.0f, "and short of where it would be heard as a second arrival");

        // asking for something outside the window is clamped rather than obeyed
        nsr::SpectralDelay d2; d2.prepare (SR);
        auto p2 = p; p2.startInterMs = 400.0f; p2.finishInterMs = 400.0f;
        d2.setParams (p2);
        Buf c (48000); click (c);
        d2.process (c.l.data(), c.r.data(), c.size());
        const float gap2 = 1000.0f * (float) (peakOf (c.r) - peakOf (c.l)) / SR;
        std::printf ("     a 400 ms ask became %.2f ms\n", gap2);
        ok (gap2 <= nsr::SpectralDelay::kInterMaxMs + 0.5f,
            "an absurd ask is clamped to the top of the window");
    }

    //==============================================================================================
    heading ("it does not run away when both feedbacks are pushed past one");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p;
        p.startFeedback = 0.98f; p.finishFeedback = 0.98f;
        p.sideOffset = 1; p.sideFeedback = 0.95f;      // sums well over unity on purpose
        p.startTimeMs = p.finishTimeMs = 30.0f;
        p.dry = 0.0f; p.wet = 1.0f;
        d.setParams (p);

        Buf b (int (SR * 6.0f));
        tone (b, 400.0f, 9600);
        d.process (b.l.data(), b.r.data(), b.size());

        ok (finite (b), "every sample is finite after six seconds");
        const double head = rms (b.l.data(), int (SR * 0.3f), int (SR * 0.6f));
        const double tail = rms (b.l.data(), int (SR * 5.4f), int (SR * 5.9f));
        std::printf ("     rms early %.4f, late %.4f\n", head, tail);
        ok (tail <= head * 1.05, "it decays rather than growing");
        ok (tail < 8.0, "and nothing has blown up on the way");
    }

    //==============================================================================================
    heading ("fully dry is the signal untouched");
    {
        nsr::SpectralDelay d;
        d.prepare (SR);
        nsr::SpectralDelayParams p; p.dry = 1.0f; p.wet = 0.0f;
        d.setParams (p);
        Buf b (4096);
        tone (b, 220.0f, 4096);
        std::vector<float> before = b.l;
        d.process (b.l.data(), b.r.data(), b.size());
        double diff = 0.0;
        for (int i = 0; i < b.size(); ++i) diff += std::abs (b.l[(size_t) i] - before[(size_t) i]);
        ok (diff == 0.0, "wet at zero leaves the input exactly as it was");
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
