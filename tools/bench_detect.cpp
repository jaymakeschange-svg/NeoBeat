// BatchCut detection benchmark: harder, field-recording-like scenes than the unit tests (changing
// backgrounds, reverb, rain, babble, fast bounce trains). Every target's true start is known.
// bench_detect [outdir]   prints recall / extras / timing per scene, and writes the scenes as WAVs.
#include "core/Engine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include "Portable.h"
#include <cstdlib>

using namespace bc;
static const double SR = 48000.0;

static void writeWav (const std::string& path, const std::vector<float>& l, double sr)
{
    std::FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const uint32_t n = (uint32_t) l.size(), bytes = n * 2, rate = (uint32_t) sr, br = rate * 2, riff = 36 + bytes, sixteen = 16;
    const uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&sixteen, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&rate, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { const int16_t s = (int16_t) std::lrint (32767.0f * std::max (-1.0f, std::min (1.0f, l[i]))); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

struct Rng
{
    PortableRandom r;
    explicit Rng (unsigned s) : r (s) {}
    float white() { return r.gauss(); }
    double uni (double a, double b) { return r.uni (a, b); }
};

static float rmsOf (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return (float) std::sqrt (e / std::max<size_t> (1, x.size())); }
static void scaleTo (std::vector<float>& x, float rms) { const float k = rms / std::max (1.0e-12f, rmsOf (x)); for (auto& v : x) v *= k; }
static void mixIn (std::vector<float>& a, const std::vector<float>& b) { for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i]; }

// pink noise (Paul Kellet)
static std::vector<float> pink (size_t n, Rng& g)
{
    std::vector<float> x (n); double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = g.white();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        x[i] = (float) (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362); b6 = w * 0.115926;
    }
    return x;
}
static void onePole (std::vector<float>& x, double fc) { const double a = 1.0 - std::exp (-6.283185307 * fc / SR); double s = 0; for (auto& v : x) { s += a * (v - s); v = (float) s; } }
// a level that wanders between lo and hi dB, a new target every tMin..tMax s, slewed with time constant tau
static std::vector<float> wander (size_t n, Rng& g, double lo, double hi, double tMin, double tMax, double tau)
{
    std::vector<float> e (n); double target = g.uni (lo, hi), cur = target, next = 0;
    const double a = 1.0 - std::exp (-1.0 / (tau * SR));
    for (size_t i = 0; i < n; ++i)
    {
        if ((double) i / SR >= next) { target = g.uni (lo, hi); next = (double) i / SR + g.uni (tMin, tMax); }
        cur += a * (target - cur); e[i] = (float) std::pow (10.0, cur / 20.0);
    }
    return e;
}

// ---- sounds
static void clank (std::vector<float>& x, size_t at, float amp, double f0, double decay, Rng& g)
{
    const double ratios[] = { 1.0, 1.61, 2.32, 3.71, 4.93, 6.4 };
    for (size_t i = 0; i < (size_t) (SR * 0.8) && at + i < x.size(); ++i)
    {
        const double t = (double) i / SR; double v = 0;
        for (int k = 0; k < 6; ++k) v += std::exp (-decay * (1 + 0.6 * k) * t) * std::sin (6.283185307 * f0 * ratios[k] * t + k) / (1 + 0.5 * k);
        v += 0.6 * g.white() * std::exp (-300.0 * t);
        x[at + i] += (float) (amp * std::min (1.0, t * 4000.0) * v * 0.4);
    }
}
static void woodTick (std::vector<float>& x, size_t at, float amp, Rng& g)
{
    for (size_t i = 0; i < (size_t) (SR * 0.12) && at + i < x.size(); ++i)
    {
        const double t = (double) i / SR;
        const double v = 0.6 * std::exp (-t * 45) * std::sin (6.283185307 * 1820 * t) + 0.35 * std::exp (-t * 70) * std::sin (6.283185307 * 3150 * t + 1) + 0.3 * std::exp (-t * 900) * g.white();
        x[at + i] += (float) (amp * std::min (1.0, t * 6000.0) * v);
    }
}
static void tap (std::vector<float>& x, size_t at, float amp, Rng& g)   // a knuckle on a table
{
    double lp = 0;
    for (size_t i = 0; i < (size_t) (SR * 0.15) && at + i < x.size(); ++i)
    {
        const double t = (double) i / SR; lp += 0.25 * (g.white() - lp);
        const double v = 0.7 * std::exp (-t * 38) * std::sin (6.283185307 * 240 * t) + 0.4 * std::exp (-t * 60) * std::sin (6.283185307 * 610 * t + 0.5) + 0.8 * std::exp (-t * 400) * lp;
        x[at + i] += (float) (amp * std::min (1.0, t * 5000.0) * v);
    }
}

// ---- a reverb: four series all-pass diffusers into an 8-line feedback delay network (Householder
// feedback), so the reflections build up smoothly as in a room rather than arriving as one slapback
static std::vector<float> reverb (const std::vector<float>& dry, double rt60, double wet)
{
    std::vector<float> x = dry;
    for (int d : { 211, 337, 503, 769 })   // 4..16 ms
    {
        std::vector<float> buf ((size_t) d, 0.0f); int pos = 0; const float gA = 0.7f;
        for (auto& v : x) { const float z = buf[(size_t) pos]; const float in = v + gA * z; buf[(size_t) pos] = in; pos = (pos + 1) % d; v = z - gA * in; }
    }
    const int L = 8; const int d[L] = { 613, 787, 997, 1193, 1409, 1601, 1811, 2027 };
    std::vector<std::vector<float>> buf (L); std::vector<int> pos (L, 0); std::vector<float> gk (L);
    for (int k = 0; k < L; ++k) { buf[(size_t) k].assign ((size_t) d[k], 0.0f); gk[(size_t) k] = (float) std::pow (10.0, -3.0 * d[k] / (rt60 * SR)); }
    std::vector<float> out (dry.size()); float o[L]; double lp[L] = {};
    for (size_t i = 0; i < dry.size(); ++i)
    {
        float sum = 0; for (int k = 0; k < L; ++k) { o[k] = buf[(size_t) k][(size_t) pos[(size_t) k]]; sum += o[k]; }
        const float h = 2.0f * sum / L;
        double w = 0;
        for (int k = 0; k < L; ++k)
        {
            float v = (o[k] - h) * gk[(size_t) k] + x[i] * 0.3f * ((k & 1) ? 1.0f : -1.0f);
            lp[k] += 0.55 * (v - lp[k]); v = (float) lp[k];   // air absorbs the highs
            buf[(size_t) k][(size_t) pos[(size_t) k]] = v; pos[(size_t) k] = (pos[(size_t) k] + 1) % d[k];
            w += o[k] * ((k & 2) ? 1.0 : -1.0);
        }
        out[i] = (float) (dry[i] + wet * (0.4 * x[i] + 0.6 * w));
    }
    return out;
}

// ---- scoring
struct Score { int hits = 0, missed = 0, extra = 0; double meanErrMs = 0, maxErrMs = 0; std::vector<int> match; };
static Score score (const std::vector<Event>& ev, const std::vector<int64_t>& truth, const std::vector<double>& tolMs)
{
    Score s; s.match.assign (truth.size(), -1);
    std::vector<bool> used (ev.size(), false);
    for (size_t t = 0; t < truth.size(); ++t)
    {
        int best = -1; double bd = 1e9;
        for (size_t i = 0; i < ev.size(); ++i) if (! used[i]) { const double dd = std::fabs ((double) (ev[i].onset - truth[t])) / SR * 1000.0; if (dd < bd) { bd = dd; best = (int) i; } }
        if (best >= 0 && bd <= tolMs[t]) { used[(size_t) best] = true; s.match[t] = best; ++s.hits; s.meanErrMs += bd; s.maxErrMs = std::max (s.maxErrMs, bd); }
        else ++s.missed;
    }
    for (bool u : used) if (! u) ++s.extra;
    if (s.hits) s.meanErrMs /= s.hits;
    return s;
}

struct Scene
{
    std::string name, note;
    Audio a;
    std::vector<int64_t> truth;
    std::vector<float> amp;          // each target's level (to pick the template: the loudest)
    std::vector<double> tol;         // per target, ms
    std::vector<double> interval;    // to the previous target, s (bounce trains)
    std::vector<float> bg;           // the background's level envelope, when it changes (for the report)
};
static bool verbose = false;

static void addTruth (Scene& s, double t, float amp, double tolMs = 8.0) { s.truth.push_back ((int64_t) (t * SR)); s.amp.push_back (amp); s.tol.push_back (tolMs); }

static Scene windScene (Rng& g)
{
    Scene s; s.name = "clanks in gusty wind"; s.note = "wind rumble whose level swings 18 dB in gusts";
    const size_t n = (size_t) (SR * 60); s.a.l.assign (n, 0.0f);
    double t = 1.3;
    while (t < 58.5) { const float amp = (float) std::pow (10.0, g.uni (-26, -6) / 20.0); clank (s.a.l, (size_t) (t * SR), amp, g.uni (500, 1400), g.uni (10, 25), g); addTruth (s, t, amp); t += g.uni (1.0, 2.6); }
    auto w = pink (n, g); onePole (w, 500.0); auto w2 = pink (n, g); onePole (w2, 4000.0);
    for (size_t i = 0; i < n; ++i) w[i] += 0.25f * w2[i];
    const auto env = wander (n, g, -12.0, 6.0, 0.4, 2.5, 0.25);
    for (size_t i = 0; i < n; ++i) w[i] *= env[i];
    scaleTo (w, 0.05f); mixIn (s.a.l, w);
    s.bg = env;
    return s;
}

static Scene carScene (Rng& g)
{
    Scene s; s.name = "taps with cars passing"; s.note = "a quiet street; three cars swell 30 dB over 4 s and fade";
    const size_t n = (size_t) (SR * 60); s.a.l.assign (n, 0.0f);
    double t = 1.1;
    while (t < 58.5) { const float amp = (float) std::pow (10.0, g.uni (-24, -8) / 20.0); tap (s.a.l, (size_t) (t * SR), amp, g); addTruth (s, t, amp); t += g.uni (0.8, 2.2); }
    auto bg = pink (n, g); scaleTo (bg, 0.003f); mixIn (s.a.l, bg);
    auto car = pink (n, g); onePole (car, 900.0); auto tyre = pink (n, g); onePole (tyre, 3500.0);
    for (size_t i = 0; i < n; ++i) car[i] = car[i] + 0.4f * tyre[i];
    const double passes[] = { 12.0, 31.0, 47.0 };
    std::vector<float> env (n, 0.0f);
    for (size_t i = 0; i < n; ++i) { const double tt = (double) i / SR; double e = 0; for (double c : passes) e += std::exp (-((tt - c) / 2.2) * ((tt - c) / 2.2)); env[i] = (float) e; }
    for (size_t i = 0; i < n; ++i) car[i] *= env[i];
    scaleTo (car, 0.06f); mixIn (s.a.l, car);
    s.bg = env;
    return s;
}

static Scene babbleScene (Rng& g)
{
    Scene s; s.name = "wood ticks over talking"; s.note = "three voices (syllables are onsets too: FIND MORE should pick the ticks)";
    const size_t n = (size_t) (SR * 45); s.a.l.assign (n, 0.0f);
    double t = 1.0;
    while (t < 43.5) { const float amp = (float) std::pow (10.0, g.uni (-22, -10) / 20.0); woodTick (s.a.l, (size_t) (t * SR), amp, g); addTruth (s, t, amp); t += g.uni (1.0, 2.4); }
    std::vector<float> talk (n, 0.0f);
    const double formants[][2] = { { 700, 1100 }, { 500, 1500 }, { 300, 2300 }, { 400, 800 }, { 600, 1900 } };
    for (int v = 0; v < 3; ++v)
    {
        double ph = 0, f0 = g.uni (95, 210), tt = g.uni (0, 0.5);
        std::vector<float> src (n, 0.0f), env (n, 0.0f);
        std::vector<double> F1 (n), F2 (n);
        while (tt < 45.0)
        {
            const double len = g.uni (0.11, 0.26), gap = g.uni (0.03, 0.18) + (g.uni (0, 1) < 0.15 ? g.uni (0.3, 1.0) : 0.0);
            const int fi = (int) g.uni (0, 4.99);
            for (size_t i = (size_t) (tt * SR); i < (size_t) ((tt + len) * SR) && i < n; ++i)
            {
                const double u = (double) i / SR - tt;
                env[i] = (float) (std::min (1.0, u / 0.018) * std::min (1.0, (len - u) / 0.05));
                F1[i] = formants[fi][0]; F2[i] = formants[fi][1];
            }
            tt += len + gap;
        }
        for (size_t i = 0; i < n; ++i)
        {
            f0 += 0.0005 * (g.uni (95, 210) - f0);
            ph += f0 / SR; ph -= std::floor (ph);
            src[i] = (float) ((2.0 * ph - 1.0) * env[i]);   // a buzz, gated by the syllables
        }
        // two formant resonators (biquad band-passes, retuned every 64 samples)
        double z1[2] = {}, z2[2] = {};
        double b0 = 0, a1 = 0, a2 = 0, c0 = 0, d1 = 0, d2 = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (i % 64 == 0)
            {
                auto design = [] (double f, double q, double& bb0, double& aa1, double& aa2) { const double w = 6.283185307 * f / SR, al = std::sin (w) / (2 * q), a0 = 1 + al; bb0 = al / a0; aa1 = -2 * std::cos (w) / a0; aa2 = (1 - al) / a0; };
                design (std::max (200.0, F1[i]), 5.0, b0, a1, a2); design (std::max (500.0, F2[i]), 7.0, c0, d1, d2);
            }
            const double x = src[i];
            const double y1 = b0 * x + z1[0]; z1[0] = -a1 * y1 + z1[1]; z1[1] = -b0 * x - a2 * y1;
            const double y2 = c0 * x + z2[0]; z2[0] = -d1 * y2 + z2[1]; z2[1] = -c0 * x - d2 * y2;
            talk[i] += (float) (y1 + 0.6 * y2);
        }
    }
    scaleTo (talk, 0.06f); mixIn (s.a.l, talk);
    auto bg = pink (n, g); scaleTo (bg, 0.002f); mixIn (s.a.l, bg);
    return s;
}

static Scene rainScene (Rng& g)
{
    Scene s; s.name = "knocks in rain"; s.note = "about 150 raindrop clicks a second on a roof";
    const size_t n = (size_t) (SR * 45); s.a.l.assign (n, 0.0f);
    double t = 1.0;
    while (t < 43.5) { const float amp = (float) std::pow (10.0, g.uni (-24, -10) / 20.0); tap (s.a.l, (size_t) (t * SR), amp, g); addTruth (s, t, amp); t += g.uni (1.0, 2.4); }
    std::vector<float> rain (n, 0.0f);
    double tt = 0;
    while (tt < 45.0)
    {
        tt += -std::log (std::max (1e-9, g.uni (0, 1))) / 150.0;
        const size_t at = (size_t) (tt * SR); const double f = g.uni (1800, 7500), dec = g.uni (300, 900); const float a = 0.02f * std::exp (0.6f * g.white());
        for (size_t i = 0; i < (size_t) (0.012 * SR) && at + i < n; ++i) { const double u = (double) i / SR; rain[at + i] += (float) (a * std::exp (-dec * u) * std::sin (6.283185307 * f * u)); }
    }
    auto hiss = pink (n, g); scaleTo (hiss, 0.004f); mixIn (rain, hiss);
    mixIn (s.a.l, rain);
    return s;
}

static Scene hallScene (Rng& g)
{
    Scene s; s.name = "hits in a hall"; s.note = "RT60 1.8 s; every other hit is 18 dB quieter and lands in the loud one's tail";
    const size_t n = (size_t) (SR * 45); std::vector<float> dry (n, 0.0f);
    double t = 1.0; int k = 0;
    while (t < 42.0)
    {
        const bool quiet = (k++ % 2) == 1;
        const float amp = quiet ? 0.04f : 0.32f;
        clank (dry, (size_t) (t * SR), amp, g.uni (350, 1200), g.uni (8, 20), g); addTruth (s, t, amp, 10.0);
        t += quiet ? g.uni (1.0, 1.6) : g.uni (0.35, 0.8);
    }
    s.a.l = reverb (dry, 1.8, 0.8);
    auto bg = pink (n, g); scaleTo (bg, 0.0015f); mixIn (s.a.l, bg);
    return s;
}

static Scene bounceScene (Rng& g)
{
    Scene s; s.name = "bounce trains"; s.note = "five balls dropped; the gaps shrink to under 15 ms";
    const size_t n = (size_t) (SR * 40); s.a.l.assign (n, 0.0f);
    for (int b = 0; b < 5; ++b)
    {
        double t = 1.0 + b * 7.5, gap = g.uni (0.45, 0.6); float amp = (float) g.uni (0.3, 0.5); const double f0 = g.uni (500, 1100);
        double prev = -1;
        while (gap > 0.012)
        {
            clank (s.a.l, (size_t) (t * SR), amp, f0, 22.0, g);
            addTruth (s, t, amp, std::min (6.0, prev < 0 ? 6.0 : 0.45 * (t - prev) * 1000.0));
            s.interval.push_back (prev < 0 ? 10.0 : t - prev);
            prev = t; t += gap; gap *= 0.8; amp *= 0.94f;
        }
    }
    auto bg = pink (n, g); scaleTo (bg, 0.006f); mixIn (s.a.l, bg);
    return s;
}

static void run (Scene& s, const std::string& out)
{
    s.a.sr = SR;
    std::printf ("\n== %s (%s): %zu targets\n", s.name.c_str(), s.note.c_str(), s.truth.size());
    const auto t0 = std::chrono::steady_clock::now();
    auto an = analyze (s.a);
    const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    for (float sens : { 0.5f, 0.8f })
    {
        DetectParams p; p.sensitivity = sens;
        if (s.name == "bounce trains") p.minGapMs = 10.0f;
        const auto ev = detect (s.a, *an, p);
        const auto sc = score (ev, s.truth, s.tol);
        std::printf ("   detect  sens %.1f: %3d of %3zu found, %3d extra   start error mean %.2f ms, worst %.2f ms\n", sens, sc.hits, s.truth.size(), sc.extra, sc.meanErrMs, sc.maxErrMs);
        if (! s.interval.empty() && sens == 0.5f)
        {
            const double edges[] = { 1e9, 0.1, 0.05, 0.025, 0.0 };
            for (int b = 0; b < 4; ++b)
            {
                int tot = 0, hit = 0;
                for (size_t i = 0; i < s.truth.size(); ++i) if (s.interval[i] < edges[b] && s.interval[i] >= edges[b + 1]) { ++tot; hit += sc.match[i] >= 0 ? 1 : 0; }
                std::printf ("        gaps %s: %d of %d\n", b == 0 ? "over 100 ms" : b == 1 ? "50-100 ms  " : b == 2 ? "25-50 ms   " : "under 25 ms", hit, tot);
            }
        }
        if (sens == 0.5f && s.interval.empty())
        {
            DetectParams q = p; q.minGapMs = 20.0f;
            const auto sq = score (detect (s.a, *an, q), s.truth, s.tol);
            std::printf ("   detect  gap 20 ms: %3d of %3zu found, %3d extra\n", sq.hits, s.truth.size(), sq.extra);
        }
        if (verbose && sens == 0.5f)
        {
            std::vector<bool> isHit (ev.size(), false); for (int m : sc.match) if (m >= 0) isHit[(size_t) m] = true;
            for (size_t i = 0; i < ev.size(); ++i)
            {
                if (isHit[i]) continue;
                const double t = (double) ev[i].onset / SR;
                double since = 1e9; for (auto tr : s.truth) if (tr <= ev[i].onset) since = std::min (since, (double) (ev[i].onset - tr) / SR * 1000.0);
                float bgDb = 0, bgRise = 0;
                if (! s.bg.empty()) { const size_t k = (size_t) ev[i].onset; bgDb = 20 * std::log10 (s.bg[k] + 1e-9f); const size_t k0 = k > (size_t) (0.3 * SR) ? k - (size_t) (0.3 * SR) : 0; bgRise = 20 * std::log10 ((s.bg[k] + 1e-9f) / (s.bg[k0] + 1e-9f)); }
                float w[kDet]; detail::riseWeights (*an, ev[i].frame, w);
                int top[3] = { 0, 0, 0 }; for (int b = 0; b < kDet; ++b) { if (w[b] > w[top[0]]) { top[2] = top[1]; top[1] = top[0]; top[0] = b; } else if (w[b] > w[top[1]]) { top[2] = top[1]; top[1] = b; } else if (w[b] > w[top[2]]) top[2] = b; }
                auto hz = [] (int b) { return 30.0 * std::pow (20000.0 / 30.0, (2.0 * b + 1.0) / 96.0); };
                std::printf ("        extra at %6.3f s: salience %5.2f snr %5.1f dB, %6.0f ms after a target, bands %.0f/%.0f/%.0f Hz (%.0f%%)%s", t, ev[i].salience, ev[i].snrDb, since > 1e8 ? -1.0 : since, hz (top[0]), hz (top[1]), hz (top[2]), 100.0 * (w[top[0]] + w[top[1]] + w[top[2]]), s.bg.empty() ? "\n" : "");
                if (! s.bg.empty()) std::printf (", background %+.1f dB (%+.1f dB over the last 0.3 s)\n", bgDb, bgRise);
            }
            if (! s.bg.empty()) for (size_t i = 0; i < s.truth.size(); ++i) if (sc.match[i] < 0) std::printf ("        missed at %6.3f s (%.0f dB), background %+.1f dB\n", (double) s.truth[i] / SR, 20 * std::log10 (s.amp[i]), 20 * std::log10 (s.bg[(size_t) s.truth[i]] + 1e-9f));
        }
        if (sens == 0.5f)
        {
            // which targets were missed: how loud were they?
            std::vector<float> missedDb; for (size_t i = 0; i < s.truth.size(); ++i) if (sc.match[i] < 0) missedDb.push_back (20.0f * std::log10 (s.amp[i]));
            if (! missedDb.empty() && s.interval.empty()) { std::printf ("        missed levels (dB):"); for (float v : missedDb) std::printf (" %.0f", v); std::printf ("\n"); }
            // FIND MORE from the loudest target that was found
            int best = -1; for (size_t i = 0; i < s.truth.size(); ++i) if (sc.match[i] >= 0 && (best < 0 || s.amp[i] > s.amp[(size_t) best])) best = (int) i;
            if (best >= 0)
            {
                const Template tp = makeTemplate (*an, ev[(size_t) sc.match[(size_t) best]]);
                const auto more = detect (s.a, *an, p, {}, {}, &tp, 0.5f);
                const auto sm = score (more, s.truth, s.tol);
                // LIKE pick: what an export of "more like this" would hold
                std::vector<Group> groups; auto mv = more; group (mv, groups, p.grouping);
                PickParams pk; pk.mode = Pick::Like; pk.ref = tp.feat; pk.hasRef = true; pk.refRise = tp.w; pk.hasRise = true; pk.similarity = 0.5f;
                const auto chosen = pick (mv, groups, pk);
                std::vector<Event> liked; for (int c : chosen) liked.push_back (mv[(size_t) c]);
                const auto sl = score (liked, s.truth, s.tol);
                if (verbose)
                {
                    const float rad = likeRadius (0.5f);
                    std::printf ("        LIKE radius %.2f; distances of the targets:", rad);
                    const auto sm2 = score (mv, s.truth, s.tol);
                    for (size_t i = 0; i < s.truth.size(); ++i) if (sm2.match[i] >= 0) std::printf (" %.2f", featDist (mv[(size_t) sm2.match[i]].feat, tp.feat));
                    std::printf ("\n        nearest non-targets:");
                    std::vector<bool> isT (mv.size(), false); for (int m : sm2.match) if (m >= 0) isT[(size_t) m] = true;
                    std::vector<float> nd; for (size_t i = 0; i < mv.size(); ++i) if (! isT[i]) nd.push_back (featDist (mv[i].feat, tp.feat));
                    std::sort (nd.begin(), nd.end()); for (size_t i = 0; i < std::min<size_t> (12, nd.size()); ++i) std::printf (" %.2f", nd[i]);
                    std::printf ("\n");
                    // onset shape: the cosine between where each event rose and where the template rose
                    auto cosw = [&] (const Event& e) { float w[kDet]; detail::riseWeights (*an, e.frame, w); double ab = 0, aa = 0, bb = 0; for (int b = 0; b < kDet; ++b) { ab += w[b] * tp.w[(size_t) b]; aa += w[b] * w[b]; bb += tp.w[(size_t) b] * tp.w[(size_t) b]; } return (float) (ab / std::sqrt (aa * bb + 1e-30)); };
                    std::vector<float> ct, cn; for (size_t i = 0; i < mv.size(); ++i) (isT[i] ? ct : cn).push_back (cosw (mv[i]));
                    std::sort (ct.begin(), ct.end()); std::sort (cn.rbegin(), cn.rend());
                    std::printf ("        onset-shape cosine, targets (lowest first):"); for (size_t i = 0; i < std::min<size_t> (10, ct.size()); ++i) std::printf (" %.2f", ct[i]);
                    std::printf ("\n        onset-shape cosine, others (highest first): "); for (size_t i = 0; i < std::min<size_t> (10, cn.size()); ++i) std::printf (" %.2f", cn[i]);
                    std::printf ("\n");
                    if (std::getenv ("BENCH_SCATTER")) for (size_t i = 0; i < mv.size(); ++i) std::printf ("SCATTER %s %d %.3f %.3f\n", s.name.substr (0, 5).c_str(), isT[i] ? 1 : 0, featDist (mv[i].feat, tp.feat), cosw (mv[i]));
                }
                std::printf ("   FIND MORE (from the loudest): %3d found, %3d extra  |  exported as LIKE: %3d of %3zu, %3d wrong\n", sm.hits, sm.extra, sl.hits, s.truth.size(), sl.extra);
            }
        }
    }
    std::printf ("   (analysis %.2f s for %.0f s)\n", took, an->seconds());
    if (! out.empty())
    {
        std::string f = s.name; for (auto& c : f) if (c == ' ') c = '_';
        writeWav (out + "/" + f + ".wav", s.a.l, SR);
    }
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";
    verbose = std::getenv ("BENCH_VERBOSE") != nullptr;
    const char* only = std::getenv ("BENCH_ONLY");
    std::vector<Scene> scenes;
    { Rng g (11); scenes.push_back (windScene (g)); }
    { Rng g (12); scenes.push_back (carScene (g)); }
    { Rng g (13); scenes.push_back (babbleScene (g)); }
    { Rng g (14); scenes.push_back (rainScene (g)); }
    { Rng g (15); scenes.push_back (hallScene (g)); }
    { Rng g (16); scenes.push_back (bounceScene (g)); }
    for (auto& s : scenes) if (only == nullptr || s.name.find (only) != std::string::npos) run (s, out);
    return 0;
}
