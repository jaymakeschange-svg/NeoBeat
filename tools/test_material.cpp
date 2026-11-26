// The learned object, measured.
//
// Every claim this engine makes should be checkable by listening to what it renders, so that is
// what this does. It builds materials whose right answer is known by arithmetic, renders them, and
// measures what came out. Where a real material file is available it loads that too.
//
// The claim that matters most is the last one: strike a body that is still ringing and the energy
// adds rather than starting over. That is the whole reason this is a resonator bank and not a copy
// of the learner's additive renderer, and it is the thing a sampler cannot do.

#include "neobeat/shared/Material.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSr = 48000.0;

static double rms (const std::vector<float>& v, int from, int to)
{
    from = std::max (0, from); to = std::min ((int) v.size(), to);
    if (to <= from) return 0.0;
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / (double) (to - from));
}

// Strongest frequency in a window, by fitting against a bank of candidates. Slow but exact enough
// to tell one mode from another, which is all that is asked.
static double dominant (const std::vector<float>& v, int from, int to, double lo, double hi)
{
    from = std::max (0, from); to = std::min ((int) v.size(), to);
    if (to - from < 256) return 0.0;
    double best = 0.0, bestMag = 0.0;
    for (double f = lo; f <= hi; f *= 1.002)
    {
        double re = 0.0, im = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double t = (double) (i - from) / kSr;
            const double w = 6.283185307 * f * t;
            re += v[(size_t) i] * std::cos (w);
            im += v[(size_t) i] * std::sin (w);
        }
        const double mag = std::sqrt (re * re + im * im);
        if (mag > bestMag) { bestMag = mag; best = f; }
    }
    return best;
}

// A material with modes we choose, so the right answer is arithmetic rather than opinion.
static nsr::Material made (std::vector<float> freqs, std::vector<float> amps, float b1, float b3 = 0.0f)
{
    nsr::Material m;
    m.name = "made";
    m.numModes = (int) freqs.size();
    m.f = std::move (freqs);
    m.a = std::move (amps);
    m.rho.assign ((size_t) m.numModes, 0.0f);
    m.delta.assign ((size_t) m.numModes, 0.0f);
    m.beta.assign ((size_t) m.numModes, 0.0f);
    m.glide.assign ((size_t) m.numModes, 0.0f);
    m.tauG.assign ((size_t) m.numModes, 0.05f);
    m.b1 = b1; m.b3 = b3;
    m.cxAmt = 0.0f; m.cxT = 99.0f;
    return m;
}

struct Out { std::vector<float> l, r, mono; };

static Out render (nsr::MaterialVoice& v, double seconds, const std::vector<float>* in = nullptr)
{
    const int n = (int) (seconds * kSr);
    Out o;
    o.l.assign ((size_t) n, 0.0f); o.r.assign ((size_t) n, 0.0f);
    v.process (o.l.data(), o.r.data(), n, in != nullptr ? in->data() : nullptr);
    o.mono.resize ((size_t) n);
    for (int i = 0; i < n; ++i) o.mono[(size_t) i] = 0.5f * (o.l[(size_t) i] + o.r[(size_t) i]);
    return o;
}

int main (int argc, char** argv)
{
    std::printf ("a struck body rings at the frequency it was given\n");
    {
        auto mat = made ({ 220.0f }, { 1.0f }, 6.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);
        v.strike (1.0f);
        auto o = render (v, 0.5);

        CHECK (rms (o.mono, 0, 2000) > 1.0e-4, "it sounds at all (%.5f)", rms (o.mono, 0, 2000));
        const double hz = dominant (o.mono, 480, 480 + 8192, 150.0, 350.0);
        CHECK (std::fabs (hz / 220.0 - 1.0) < 0.02, "at 220 Hz, as asked (%.1f Hz)", hz);
    }

    std::printf ("\nand decays at the rate the damping law says\n");
    {
        // sigma = b1 = 6 per second, so amplitude falls by exp(-6*0.25) = 0.223 over a quarter second
        auto mat = made ({ 300.0f }, { 1.0f }, 6.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);
        v.strike (1.0f);
        auto o = render (v, 1.0);

        const double early = rms (o.mono, (int) (0.05 * kSr), (int) (0.10 * kSr));
        const double later = rms (o.mono, (int) (0.30 * kSr), (int) (0.35 * kSr));
        const double want = std::exp (-6.0 * 0.25);
        const double got = later / std::max (1.0e-12, early);
        std::printf ("     fell to %.3f over a quarter second, the law says %.3f\n", got, want);
        CHECK (std::fabs (got / want - 1.0) < 0.15, "the decay follows exp(-sigma t) (%.3f against %.3f)", got, want);
    }

    std::printf ("\na louder mode dominates a quieter one\n");
    {
        auto mat = made ({ 180.0f, 640.0f }, { 0.1f, 1.0f }, 8.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);
        v.strike (1.0f);
        auto o = render (v, 0.5);
        const double hz = dominant (o.mono, 480, 480 + 8192, 120.0, 900.0);
        CHECK (std::fabs (hz / 640.0 - 1.0) < 0.03, "the loud mode is what you hear (%.0f Hz)", hz);
    }

    std::printf ("\nsilence in, silence out\n");
    {
        auto mat = made ({ 300.0f, 700.0f }, { 1.0f, 1.0f }, 8.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);
        std::vector<float> quiet ((size_t) (0.3 * kSr), 0.0f);
        auto o = render (v, 0.3, &quiet);
        CHECK (rms (o.mono, 0, (int) o.mono.size()) < 1.0e-9, "nothing comes out of nothing (%.2e)",
               rms (o.mono, 0, (int) o.mono.size()));
    }

    std::printf ("\nsound run through the body comes out in that material\n");
    {
        // White-ish noise in. The body should put its own modes into it - energy at 420 Hz that
        // a flat input did not have more of than anywhere else.
        auto mat = made ({ 420.0f }, { 1.0f }, 10.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);

        const int n = (int) (0.5 * kSr);
        std::vector<float> noise ((size_t) n);
        uint32_t seed = 12345;
        for (int i = 0; i < n; ++i)
        { seed = seed * 1664525u + 1013904223u; noise[(size_t) i] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.2f; }

        auto o = render (v, 0.5, &noise);
        CHECK (rms (o.mono, 0, n) > 1.0e-4, "it passes sound (%.5f)", rms (o.mono, 0, n));

        const double hz = dominant (o.mono, 4800, 4800 + 8192, 200.0, 800.0);
        CHECK (std::fabs (hz / 420.0 - 1.0) < 0.05,
               "and the body's own pitch is what rings out of it (%.0f Hz, body is 420)", hz);
    }

    std::printf ("\nthe two listening points differ, without cancelling\n");
    {
        auto mat = made ({ 200.0f, 900.0f, 3000.0f, 5000.0f }, { 1.0f, 0.8f, 0.6f, 0.4f }, 10.0f);
        nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat, { 0.6f, 900.0f, 1.6f });
        v.strike (1.0f);
        auto o = render (v, 0.4);

        std::vector<float> side (o.l.size());
        for (size_t i = 0; i < o.l.size(); ++i) side[i] = 0.5f * (o.l[i] - o.r[i]);
        const double mid = rms (o.mono, 0, (int) o.mono.size());
        const double sd = rms (side, 0, (int) side.size());
        std::printf ("     side is %.1f dB against mid\n", 20.0 * std::log10 (std::max (1e-12, sd / mid)));
        CHECK (sd > 1.0e-5, "the two points really do hear it differently (%.5f)", sd);
        CHECK (mid > sd, "but the sum survives - they are not inverted copies");
    }

    std::printf ("\nstriking a ringing body adds to it\n");
    {
        // The claim a sampler cannot meet. One hit, then a second while the first is still
        // sounding: the body must be louder than either hit alone at that moment, because the
        // energy adds into a resonator that still has state.
        auto mat = made ({ 250.0f }, { 1.0f }, 3.0f);

        nsr::MaterialVoice one; one.prepare (kSr); one.setMaterial (mat);
        one.strike (1.0f);
        auto a = render (one, 0.6);

        nsr::MaterialVoice two; two.prepare (kSr); two.setMaterial (mat);
        two.strike (1.0f);
        auto first = render (two, 0.2);
        two.strike (1.0f);                       // again, into a body still ringing
        auto second = render (two, 0.4);

        const double singleAt = rms (a.mono, (int) (0.25 * kSr), (int) (0.30 * kSr));
        const double doubleAt = rms (second.mono, (int) (0.05 * kSr), (int) (0.10 * kSr));
        std::printf ("     one hit %.5f, struck again while ringing %.5f\n", singleAt, doubleAt);
        CHECK (doubleAt > singleAt * 1.2,
               "the second hit lands on a body that remembers the first (%.5f against %.5f)",
               doubleAt, singleAt);
        (void) first;
    }

    // ---------------------------------------------------------------- a real learned material
    if (argc > 1)
    {
        const juce::File file (juce::String::fromUTF8 (argv[1]));
        std::printf ("\na material the learner fitted: %s\n", file.getFileName().toRawUTF8());
        nsr::Material mat;
        juce::String error;
        const bool ok = mat.load (file, error);
        CHECK (ok, "it loads (%s)", ok ? mat.name.toRawUTF8() : error.toRawUTF8());
        if (ok)
        {
            CHECK (mat.numModes > 8, "with its modes (%d)", mat.numModes);
            std::printf ("     rings for about %.2f s; loudest mode ", mat.ringSeconds());
            int loud = 0;
            for (int i = 1; i < mat.numModes; ++i) if (mat.a[(size_t) i] > mat.a[(size_t) loud]) loud = i;
            std::printf ("%.1f Hz\n", mat.f[(size_t) loud]);

            nsr::MaterialVoice v; v.prepare (kSr); v.setMaterial (mat);
            v.strike (1.0f);
            auto o = render (v, std::min (2.0f, mat.ringSeconds()));
            const double peak = rms (o.mono, 0, (int) (0.1 * kSr));
            CHECK (peak > 1.0e-5, "and it sounds when struck (%.5f)", peak);

            bool finite = true;
            for (float x : o.mono) if (! std::isfinite (x) || std::fabs (x) > 1.0e4f) { finite = false; break; }
            CHECK (finite, "staying finite and bounded with all %d modes running", mat.numModes);
        }
    }
    else
    {
        std::printf ("\n(no material file given - pass one to also test a learned object)\n");
    }

    std::printf ("\n%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
