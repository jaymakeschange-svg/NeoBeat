// Variants of a fitted sound, bred: stage 7b of the learner port (docs/PROPOSED-FEATURES.md, "7b, the
// breeding design", settled with the user 27 Nov).
//
// A generation is up to eight children, each made from the parents one of four ways:
//   Body     a parent material moved: a small step in all its laws (in the fit's own unconstrained
//            numbers, so every child is still a playable body) and one larger move drawn at random -
//            transpose, stretch towards metallic, ring, damping tilt, beating, cascade, glide, the
//            contact stop, the low-body rock, the click and noise
//   Cross    two materials crossed: each law from one or the other, the modes split at a frequency
//   File     a sound parent (the dropped sound, or a kept File child) transformed: stretched with its
//            pitch kept, a spectral effect or a granular one from the Matrixxxer's library
//   Through  a sound parent sent through a material, as the MATERIAL room's Send through does
//   Scratch  no parent at all: a material made from one of eight physical shapes (scratchBody, below) -
//            asked for by the user on 27 Nov ("I want to make children from scratch") in place of
//            NeoBeat's voices
// REACH (0..1) is how far a child goes from its parents - and, from scratch, how wild it is.
//
// Two ways to run it, the two tools the user named:
//   Wander (Synplant's branches)  one generation from the parents the user kept
//   Grow   (Petri's "four takes climb toward it")  generations on their own, keeping the closest
//          each time, so the material converges on the dropped sound
//   Scratch  one generation of eight materials from nothing, one of each shape (Synplant's and
//          Petri's random seeds): keep one, and Grow climbs from it toward the sound
//
// Closeness is the learner's loss against the dropped sound, on noise the child was not fitted to:
// a fit is partly a fit to its own noise draw (docs, "Found: a fit is partly a fit to its own
// noise"), so every child is scored on the same fixed draws, none of them a fit's. Grow selects on one
// draw and reports on another, so what it shows is not tuned to what it chose by.
//
// No JUCE: the plugin drives it, the container's tests run it directly.
#pragma once

#include "FitJob.h"
#include "../../dsp/MatrixFx.h"

#include <limits>

namespace ml
{
// ------------------------------------------------------------------ parents and children

// A parent: a material (with its strike), or a sound.
struct Source
{
    std::string name;
    bool isBody = false;
    Body body;                       // isBody: the material
    std::vector<float> sound;        // 48 kHz mono: the material's strike, or the sound itself
};

enum class ChildKind { Body, Cross, File, Through, Scratch };

inline const char* kindName (ChildKind k)
{
    switch (k)
    {
        case ChildKind::Body: return "body"; case ChildKind::Cross: return "cross"; case ChildKind::File: return "file";
        case ChildKind::Through: return "through"; default: return "scratch";
    }
}

struct Child : Source
{
    ChildKind kind = ChildKind::Body;
    std::string how;                 // what was done, in plain words ("transposed +3.0 st")
    double loss = std::numeric_limits<double>::quiet_NaN();   // closeness to the target (lower is closer)
};

// ------------------------------------------------------------------ randomness

struct BreedRandom
{
    explicit BreedRandom (uint32_t seed) : r (seed) {}
    double uni() { return r.uniform(); }
    double uni (double lo, double hi) { return r.uniform (lo, hi); }
    int below (int n) { return n <= 1 ? 0 : (int) std::min<uint64_t> ((uint64_t) n - 1, (uint64_t) (r.uniform() * n)); }
    double sign() { return uni() < 0.5 ? -1.0 : 1.0; }
    double gauss()
    {
        const double u1 = std::max (1e-300, r.uniform()), u2 = r.uniform();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * an::kPi * u2);
    }
    NumpyRandom r;
};

// every child its own seed, from the run, the generation and its place: the same children whatever
// the number of threads
inline uint32_t childSeed (uint32_t run, int generation, int index)
{
    uint64_t x = (uint64_t) run * 0x9E3779B97F4A7C15ull + (uint64_t) generation * 0xBF58476D1CE4E5B9ull + (uint64_t) index * 0x94D049BB133111EBull;
    x ^= x >> 31; x *= 0xD6E8FEB86659FD93ull; x ^= x >> 32;
    return (uint32_t) x;
}

// ------------------------------------------------------------------ body children

// The larger moves, one per Body child. Each works on the unconstrained numbers the fit moves, so the
// result stays inside the learner's bounds; `amount` is signed, about -1..1 at full REACH.
enum class Move { Transpose, Stretch, Ring, Tilt, Beating, Cascade, Glide, Contact, Rock, Click, Count };

inline std::string applyMove (Params& u, Move m, double amount)
{
    char b[96];
    const int k = u.k;
    auto addMode = [&u, k] (int key, double d) { for (int i = 0; i < k; ++i) u.mode (key)[i] += d; };
    switch (m)
    {
        case Move::Transpose:
        {
            const double st = amount * 12.0;
            addMode (kLogF, st * std::log (2.0) / 12.0);
            std::snprintf (b, sizeof b, "transposed %+.1f st", st);
            return b;
        }
        case Move::Stretch:
        {
            // every mode's distance from the lowest, in octaves, times 1 + s: above 0 towards metallic
            const double s = 0.25 * amount;
            double lo = 1e300; for (int i = 0; i < k; ++i) lo = std::min (lo, u.mode (kLogF)[i]);
            for (int i = 0; i < k; ++i) u.mode (kLogF)[i] = lo + (1.0 + s) * (u.mode (kLogF)[i] - lo);
            std::snprintf (b, sizeof b, "partials spread %+.0f%%%s", 100.0 * s, s > 0 ? " (more metallic)" : " (more harmonic)");
            return b;
        }
        case Move::Ring:      u.global (kB1) -= 1.5 * amount; addMode (kRho, -0.5 * amount); return amount > 0 ? "rings longer" : "rings shorter";
        case Move::Tilt:      u.global (kB3) -= 2.0 * amount; return amount > 0 ? "highs ring longer" : "highs die sooner";
        case Move::Beating:   addMode (kDelta, 2.0 * amount); addMode (kBeta, 1.5 * amount); return amount > 0 ? "beats more" : "beats less";
        case Move::Cascade:   u.global (kC) += 2.0 * amount; u.global (kGamma) += 1.0 * amount; return amount > 0 ? "partials swell more after the hit" : "partials swell less after the hit";
        case Move::Glide:     addMode (kGlide, 1.5 * amount); addMode (kTauG, 1.0 * std::fabs (amount)); return amount > 0 ? "pitch settles from above" : "pitch settles from below";
        case Move::Contact:   u.global (kCxT) -= 2.0 * amount; u.global (kCxAmt) += 2.0 * amount; return amount > 0 ? "stopped by a hand sooner" : "stopped by a hand later";
        case Move::Rock:      u.global (kLbM) += 2.0 * amount; u.global (kLbRate) += 1.0 * amount; return amount > 0 ? "the low body rocks more" : "the low body rocks less";
        case Move::Click:
        default:
            for (int i = 0; i < kBands; ++i) { u.band (kNoise)[i] += 1.5 * amount; u.band (kDn)[i] += 1.0 * amount; }
            return amount > 0 ? "harder hit, more noise" : "softer hit, less noise";
    }
}

// The small step every Body child takes: all the laws and noise bands, and each mode's damping,
// gain, beating and swell a little; frequencies are left to the larger moves.
inline void smallStep (Params& u, double reach, BreedRandom& r)
{
    const double s = 0.05 + 0.45 * reach;
    for (int key = 0; key < kGlobalKeys; ++key) u.global (key) += s * r.gauss();
    for (int key = 0; key < kBandKeys; ++key) for (int b = 0; b < kBands; ++b) u.band (key)[b] += 0.5 * s * r.gauss();
    for (const int key : { (int) kRho, (int) kEps, (int) kDelta, (int) kBeta }) for (int i = 0; i < u.k; ++i) u.mode (key)[i] += 0.5 * s * r.gauss();
    for (int i = 0; i < u.k; ++i) u.mode (kLogA)[i] += 0.3 * s * r.gauss();
}

inline Body mutateBody (const Body& parent, double reach, BreedRandom& r, std::string& how)
{
    Params u = unconstrained (parent);
    smallStep (u, reach, r);
    const auto move = (Move) r.below ((int) Move::Count);
    const double amount = r.sign() * (0.05 + 0.95 * reach) * r.uni (0.3, 1.0);
    how = applyMove (u, move, amount);
    return physical (u);
}

// Two materials crossed: each law and each noise band from one parent or the other, the modes split at
// a frequency - those below from `a`, those above from `b` - then a small step of its own.
inline Body crossBodies (const Body& a, const Body& b, double reach, BreedRandom& r, std::string& how, double* splitHz = nullptr)
{
    auto range = [] (const Body& p, double& lo, double& hi) { lo = 1e300; hi = 0; for (double f : p.f) { lo = std::min (lo, f); hi = std::max (hi, f); } };
    double alo, ahi, blo, bhi; range (a, alo, ahi); range (b, blo, bhi);
    double lo = std::max (alo, blo), hi = std::min (ahi, bhi);
    if (! (hi > lo)) { lo = std::sqrt (alo * ahi); hi = std::sqrt (blo * bhi); if (lo > hi) std::swap (lo, hi); }
    const double split = std::exp (std::log (std::max (20.0, lo)) + (std::log (std::max (21.0, hi)) - std::log (std::max (20.0, lo))) * r.uni (0.2, 0.8));
    Body c;
    auto take = [&c] (const Body& p, int m)
    {
        c.f.push_back (p.f[(size_t) m]); c.a.push_back (p.a[(size_t) m]); c.rho.push_back (p.rho[(size_t) m]); c.delta.push_back (p.delta[(size_t) m]);
        c.beta.push_back (p.beta[(size_t) m]); c.eps.push_back (p.eps[(size_t) m]); c.glide.push_back (p.glide[(size_t) m]); c.tau_g.push_back (p.tau_g[(size_t) m]);
    };
    for (int m = 0; m < a.modes(); ++m) if (a.f[(size_t) m] < split) take (a, m);
    for (int m = 0; m < b.modes(); ++m) if (b.f[(size_t) m] >= split) take (b, m);
    if (c.modes() == 0) c = a;
    // the laws
    double* cg[] = { &c.b1, &c.b3, &c.C, &c.gamma, &c.tau_c, &c.G, &c.tau_a, &c.cx_amt, &c.cx_t, &c.cx_f, &c.lb_m, &c.lb_rate, &c.lb_f, &c.tau_n };
    const double ag[] = { a.b1, a.b3, a.C, a.gamma, a.tau_c, a.G, a.tau_a, a.cx_amt, a.cx_t, a.cx_f, a.lb_m, a.lb_rate, a.lb_f, a.tau_n };
    const double bg[] = { b.b1, b.b3, b.C, b.gamma, b.tau_c, b.G, b.tau_a, b.cx_amt, b.cx_t, b.cx_f, b.lb_m, b.lb_rate, b.lb_f, b.tau_n };
    for (int i = 0; i < kGlobalKeys; ++i) *cg[i] = r.uni() < 0.5 ? ag[i] : bg[i];
    for (int i = 0; i < kBands; ++i)
    {
        const bool fromA = r.uni() < 0.5;
        c.noise[(size_t) i] = (fromA ? a : b).noise[(size_t) i]; c.dn[(size_t) i] = (fromA ? a : b).dn[(size_t) i]; c.dn_rho[(size_t) i] = (fromA ? a : b).dn_rho[(size_t) i];
    }
    // at most 160 modes: the loudest
    if (c.modes() > 160)
    {
        std::vector<int> order ((size_t) c.modes());
        for (int i = 0; i < c.modes(); ++i) order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [&c] (int x, int y) { return c.a[(size_t) x] > c.a[(size_t) y]; });
        order.resize (160);
        std::sort (order.begin(), order.end());
        Body d = c; d.f.clear(); d.a.clear(); d.rho.clear(); d.delta.clear(); d.beta.clear(); d.eps.clear(); d.glide.clear(); d.tau_g.clear();
        for (int m : order) { d.f.push_back (c.f[(size_t) m]); d.a.push_back (c.a[(size_t) m]); d.rho.push_back (c.rho[(size_t) m]); d.delta.push_back (c.delta[(size_t) m]); d.beta.push_back (c.beta[(size_t) m]); d.eps.push_back (c.eps[(size_t) m]); d.glide.push_back (c.glide[(size_t) m]); d.tau_g.push_back (c.tau_g[(size_t) m]); }
        c = std::move (d);
    }
    Params u = unconstrained (c);
    smallStep (u, 0.3 * reach, r);
    if (splitHz != nullptr) *splitHz = split;
    char t[96];
    std::snprintf (t, sizeof t, "split at %.0f Hz", split);
    how = t;
    return physical (u);
}

// ------------------------------------------------------------------ materials from scratch

// A material with no parent: one of eight physical shapes, with its own partial ratios (the textbook
// ones: free-free bar, simply supported plate, ideal circular membrane, stiff and closed tubes, a church
// bell, a cluster of metal junk, a rubber body that drops in pitch), and the learner's laws drawn
// around what its fits found in real objects (MaterialLab engine_v3: base damping 13-65, cascade
// 0.4-1.0, a click 0.1-1% of the loudest mode, beating mostly under 1 Hz). WILD (0..1) pushes past
// physics: partials stretched and jittered, big shared glides, tension that bends the pitch with the
// energy, beating and swelling far beyond any real object, highs that ring longer than lows.
enum class Shape { Bar, Plate, Membrane, Tube, ClosedTube, Bell, Junk, Rubber, Count };

inline const char* shapeName (Shape s)
{
    static const char* n[] = { "metal bar", "plate", "drum skin", "tube", "closed tube", "bell", "metal junk", "rubber" };
    return n[(int) s];
}

inline Body scratchBody (BreedRandom& r, double wild, std::string& how, int shape = -1)
{
    const auto sh = shape >= 0 ? (Shape) shape : (Shape) r.below ((int) Shape::Count);
    auto logU = [&r] (double lo, double hi) { return std::exp (r.uni (std::log (lo), std::log (hi))); };
    // the partials, as ratios to the lowest
    std::vector<double> ratio;
    double f0 = 200.0;
    switch (sh)
    {
        case Shape::Bar:
        {
            f0 = logU (150, 1500);
            const double beta[] = { 4.7300, 7.8532, 10.9956, 14.1372, 17.2788, 20.4204, 23.5619, 26.7035 };
            for (double b : beta) ratio.push_back ((b / 4.7300) * (b / 4.7300));
            break;
        }
        case Shape::Plate:
        {
            f0 = logU (80, 600);
            const double asp = r.uni (0.6, 1.6);
            for (int m = 1; m <= 9; ++m) for (int n = 1; n <= 9; ++n) ratio.push_back ((m * m + n * n * asp * asp) / (1.0 + asp * asp));
            std::sort (ratio.begin(), ratio.end());
            ratio.resize ((size_t) (30 + r.below (40)));
            break;
        }
        case Shape::Membrane:
        {
            f0 = logU (50, 300);
            const double z[] = { 2.4048, 3.8317, 5.1356, 5.5201, 6.3802, 7.0156, 7.5883, 8.4172, 8.6537, 8.7715, 9.7610, 9.9361, 10.1735, 11.0647, 11.0864, 11.6198 };
            for (double v : z) ratio.push_back (v / 2.4048);
            break;
        }
        case Shape::Tube:
        {
            f0 = logU (60, 800);
            const double B = logU (1e-5, 0.01);
            const int k = 16 + r.below (32);
            for (int n = 1; n <= k; ++n) ratio.push_back (n * std::sqrt (1.0 + B * n * n) / std::sqrt (1.0 + B));
            break;
        }
        case Shape::ClosedTube:
        {
            f0 = logU (80, 600);
            const int k = 12 + r.below (20);
            for (int n = 0; n < k; ++n) ratio.push_back (2 * n + 1);
            break;
        }
        case Shape::Bell:
        {
            f0 = logU (150, 900);
            const double b[] = { 0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215, 9.83 };
            for (double v : b) ratio.push_back (v / 0.5);
            break;
        }
        case Shape::Junk:
        {
            f0 = logU (150, 900);
            const int k = 40 + r.below (60);
            ratio.push_back (1.0);
            for (int i = 1; i < k; ++i) ratio.push_back (std::exp (r.uni (0.0, std::log (40.0))));
            std::sort (ratio.begin(), ratio.end());
            break;
        }
        case Shape::Rubber:
        default:
        {
            f0 = logU (40, 250);
            const int k = 6 + r.below (9);
            for (int i = 0; i < k; ++i) ratio.push_back ((i + 1) * r.uni (0.85, 1.25));
            std::sort (ratio.begin(), ratio.end());
            break;
        }
    }
    // WILD: partials stretched and jittered away from the shape's own
    const double stretch = 1.0 + wild * r.uni (-0.35, 0.35);
    Body p;
    const double strikeAt = r.uni (0.05, 0.5), tilt = r.uni (0.2, 1.2);
    for (size_t i = 0; i < ratio.size(); ++i)
    {
        const double f = f0 * std::pow (ratio[i], stretch) * std::exp (0.04 * wild * r.gauss());
        if (f > 18000.0 || f < 25.0) continue;
        double a = 0.3 * std::pow ((double) i + 1.0, -tilt) * std::exp (0.4 * r.gauss());
        if (sh == Shape::Bar || sh == Shape::Tube || sh == Shape::ClosedTube) a *= std::fabs (std::sin (an::kPi * (double) (i + 1) * strikeAt)) + 0.1;
        p.f.push_back (f); p.a.push_back (a);
    }
    if (p.f.empty()) { p.f.push_back (f0); p.a.push_back (0.3); }
    const int k = p.modes();
    // the damping law by what the shape is made of
    const bool soft = sh == Shape::Rubber || sh == Shape::Membrane || (sh == Shape::Tube && r.uni() < 0.3);
    if (sh == Shape::Rubber)        { p.b1 = logU (25, 80);  p.b3 = logU (1, 10); }
    else if (soft)                  { p.b1 = logU (10, 40);  p.b3 = logU (0.5, 5); }
    else if (sh == Shape::Plate || sh == Shape::Junk) { p.b1 = logU (5, 40); p.b3 = logU (0.001, 0.5); }
    else                            { p.b1 = logU (1.5, 20); p.b3 = logU (0.001, 0.3); }
    // WILD: highs that ring longer than lows
    const double rhoSlope = wild * r.uni (-1.5, 0.5);
    for (int m = 0; m < k; ++m)
    {
        p.rho.push_back (0.5 * r.gauss() + rhoSlope * std::log (p.f[(size_t) m] / p.f[0]) / std::log (20.0));
        const double d = r.uni() < 0.15 + 0.3 * wild ? r.uni (2.0, 10.0 + 20.0 * wild) : -0.4 * std::log (std::max (1e-6, r.uni()));
        p.delta.push_back (std::min (39.0, d));
        p.beta.push_back (std::min (0.49, r.uni (0.0, 0.15 + 0.3 * wild)));
        p.eps.push_back (r.gauss());
        p.glide.push_back (0.15 * r.gauss());
        p.tau_g.push_back (logU (0.01, 0.08));
    }
    p.C = logU (0.3, 1.2 + 2.0 * wild); p.gamma = r.uni (-0.8, 0.5); p.tau_c = r.uni (0.03, 0.3);
    p.G = std::max (-0.9, std::min (3.0, r.uni (-0.25, 0.4) * (1.0 + 3.0 * wild)));
    p.tau_a = r.uni (0.002, 0.004);
    p.cx_amt = 0.002; p.cx_t = 2.5; p.cx_f = 500.0;
    std::string extra;
    if (r.uni() < 0.2 + 0.3 * wild) { p.cx_t = r.uni (0.1, 1.2); p.cx_amt = logU (5, 100); p.cx_f = logU (60, 800); extra += ", hand-stopped"; }
    p.lb_m = logU (0.005, 0.5); p.lb_rate = r.uni (1.5, 7.0); p.lb_f = logU (100, 2000);
    p.tau_n = logU (0.004, 0.03);
    // one glide shared by every partial: rubber always drops; anything else, now and then, more with WILD
    if (sh == Shape::Rubber || r.uni() < 0.15 + 0.5 * wild)
    {
        const double g = sh == Shape::Rubber ? r.uni (0.3, 1.2) : r.sign() * r.uni (0.1, 0.4 + 1.2 * wild);
        const double tg = logU (0.04, 0.2 + 0.4 * wild);
        for (int m = 0; m < k; ++m) { p.glide[(size_t) m] += g; p.tau_g[(size_t) m] = tg; }
        extra += g > 0 ? ", drops in pitch" : ", rises in pitch";
    }
    double amax = 0.0; for (double a : p.a) amax = std::max (amax, a);
    const double click = logU (0.0005, 0.01), dense = logU (0.003, 0.08), slope = r.uni (-0.15, 0.15);
    for (int b = 0; b < kBands; ++b)
    {
        p.noise[(size_t) b] = amax * click * std::exp (slope * (b - 8) + 0.3 * r.gauss());
        p.dn[(size_t) b] = amax * dense * std::exp (slope * (b - 8) + 0.3 * r.gauss());
        p.dn_rho[(size_t) b] = 0.9 * r.gauss();
    }
    char t[96];
    std::snprintf (t, sizeof t, "%s, %.0f Hz%s", shapeName (sh), p.f[0], extra.c_str());
    how = t;
    return physical (unconstrained (p));      // inside the learner's bounds, so it fits, grows and saves like any material
}

// ------------------------------------------------------------------ sound children

// A phase-vocoder time stretch that keeps the pitch: 2048-point frames every 256 samples in and every
// 256 x factor out, each peak's phase advanced at its own measured frequency and the bins around it
// locked to it (identity phase locking, Laroche and Dolson), so a struck sound does not go soft and
// phasey.
inline std::vector<float> stretchKeepPitch (const std::vector<float>& x, double factor)
{
    constexpr int N = 2048, Ha = 256, B = N / 2 + 1;
    factor = std::max (0.25, std::min (4.0, factor));
    const int Hs = std::max (1, (int) std::lround (Ha * factor));
    const int n = (int) x.size();
    if (n == 0) return {};
    std::vector<double> in ((size_t) (n + 2 * N), 0.0);
    for (int i = 0; i < n; ++i) in[(size_t) (i + N)] = x[(size_t) i];
    const int frames = (int) (in.size() - N) / Ha + 1;
    std::vector<double> out ((size_t) ((frames - 1) * Hs + N), 0.0), norm (out.size(), 0.0);
    std::vector<double> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * an::kPi * i / N);
    const auto& plan = fftPlan (N);
    std::vector<std::complex<double>> buf ((size_t) N);
    std::vector<double> mag ((size_t) B), ph ((size_t) B), prevPh ((size_t) B, 0.0), outPh ((size_t) B, 0.0);
    std::vector<int> peakOf ((size_t) B);
    auto princ = [] (double p) { return p - 2.0 * an::kPi * std::floor ((p + an::kPi) / (2.0 * an::kPi)); };
    for (int t = 0; t < frames; ++t)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = in[(size_t) (t * Ha + i)] * win[(size_t) i];
        plan.run (buf.data());
        for (int k = 0; k < B; ++k) { mag[(size_t) k] = std::abs (buf[(size_t) k]); ph[(size_t) k] = std::arg (buf[(size_t) k]); }
        // the peaks, and each bin's nearest peak
        std::vector<int> peaks;
        for (int k = 2; k < B - 2; ++k)
            if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1] && mag[(size_t) k] > mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2]) peaks.push_back (k);
        if (peaks.empty()) peaks.push_back (0);
        for (int k = 0, p = 0; k < B; ++k)
        {
            while (p + 1 < (int) peaks.size() && std::abs (peaks[(size_t) p + 1] - k) < std::abs (peaks[(size_t) p] - k)) ++p;
            peakOf[(size_t) k] = peaks[(size_t) p];
        }
        std::vector<double> newPh ((size_t) B);
        for (int pk : peaks)
        {
            const double w = 2.0 * an::kPi * pk / N;
            const double inst = w + princ (ph[(size_t) pk] - prevPh[(size_t) pk] - Ha * w) / Ha;
            newPh[(size_t) pk] = t == 0 ? ph[(size_t) pk] : outPh[(size_t) pk] + Hs * inst;
        }
        for (int k = 0; k < B; ++k)
        {
            const int pk = peakOf[(size_t) k];
            if (k != pk) newPh[(size_t) k] = newPh[(size_t) pk] + ph[(size_t) k] - ph[(size_t) pk];
        }
        for (int k = 0; k < B; ++k)
        {
            const auto X = std::polar (mag[(size_t) k], newPh[(size_t) k]);
            buf[(size_t) k] = std::conj (X);                                // inverse transform by conjugation
            if (k > 0 && k < N / 2) buf[(size_t) (N - k)] = X;
        }
        plan.run (buf.data());
        for (int i = 0; i < N; ++i)
        {
            out[(size_t) (t * Hs + i)] += buf[(size_t) i].real() / N * win[(size_t) i];
            norm[(size_t) (t * Hs + i)] += win[(size_t) i] * win[(size_t) i];
        }
        prevPh = ph; outPh = newPh;
    }
    const int start = (int) std::lround (N * (double) Hs / Ha), len = (int) std::lround (n * (double) Hs / Ha);
    std::vector<float> y ((size_t) std::max (0, len));
    double peakNorm = 0.0; for (double v : norm) peakNorm = std::max (peakNorm, v);
    for (int i = 0; i < len && start + i < (int) out.size(); ++i)
        y[(size_t) i] = (float) (out[(size_t) (start + i)] / std::max (norm[(size_t) (start + i)], 0.1 * peakNorm));
    return y;
}

// A sound sent through a material's modes, each a pair of two-pole resonators (the doublet) ringing at
// its frequency with its damping law: the MATERIAL room's Send through, without JUCE.
inline std::vector<float> throughBody (const std::vector<float>& x, const Body& p, double fs = kFitRate)
{
    const int tail = (int) (std::min (4.0, ringSeconds (p)) * fs);
    const int n = (int) x.size() + tail;
    std::vector<double> y ((size_t) n, 0.0);
    for (int m = 0; m < p.modes(); ++m)
    {
        const double fk = p.f[(size_t) m] / 1000.0;
        const double sigma = (p.b1 + p.b3 * fk * fk) * std::exp (p.rho[(size_t) m]);
        if (p.f[(size_t) m] >= 0.46 * fs) continue;
        const double r = std::exp (-sigma / fs);
        for (int side = 0; side < 2; ++side)
        {
            const double f = p.f[(size_t) m] + (side == 0 ? -0.5 : 0.5) * p.delta[(size_t) m];
            const double g = p.a[(size_t) m] * (side == 0 ? 1.0 - p.beta[(size_t) m] : p.beta[(size_t) m]) * (1.0 - r);
            if (g == 0.0 || f <= 0.0) continue;
            const double c = 2.0 * r * std::cos (2.0 * an::kPi * f / fs);
            double y1 = 0.0, y2 = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double v = c * y1 - r * r * y2 + g * (i < (int) x.size() ? x[(size_t) i] : 0.0);
                y2 = y1; y1 = v;
                y[(size_t) i] += v;
            }
        }
    }
    std::vector<float> o ((size_t) n);
    for (int i = 0; i < n; ++i) o[(size_t) i] = std::isfinite (y[(size_t) i]) ? (float) y[(size_t) i] : 0.0f;
    return o;
}

inline double rmsOf (const float* x, int n) { double s = 0.0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / std::max (1, n)); }

// The sound effects a File child draws from: the spectral and granular ones of the Matrixxxer's library
// that keep a struck sound recognisable at low REACH.
inline const std::vector<nb::MatrixFx>& fileEffects (bool granular)
{
    static const std::vector<nb::MatrixFx> spectral { nb::MatrixFx::Blurring, nb::MatrixFx::Freezing, nb::MatrixFx::Shimmer, nb::MatrixFx::Stretch,
                                                      nb::MatrixFx::Weave, nb::MatrixFx::PartialGlide, nb::MatrixFx::Pulsing };
    static const std::vector<nb::MatrixFx> grains { nb::MatrixFx::Granulation, nb::MatrixFx::IdeeFixer, nb::MatrixFx::GrainStreamer };
    return granular ? grains : spectral;
}

inline std::vector<float> transformSound (const std::vector<float>& x, double reach, BreedRandom& r, std::string& how)
{
    const int pick = r.below (3);                       // stretch, spectral, granular
    std::vector<float> y;
    if (pick == 0)
    {
        const double factor = std::exp (r.sign() * r.uni (0.1, 0.1 + 0.6 * reach));
        y = stretchKeepPitch (x, std::max (0.5, std::min (2.0, factor)));
        char b[64]; std::snprintf (b, sizeof b, "stretched %.2fx, pitch kept", std::max (0.5, std::min (2.0, factor)));
        how = b;
    }
    else
    {
        const auto& list = fileEffects (pick == 2);
        const auto fx = list[(size_t) r.below ((int) list.size())];
        const auto& info = nb::matrixFxInfo()[(size_t) fx];
        nb::FxSettings s = nb::FxSettings::defaults (fx);
        for (size_t i = 0; i < info.params.size(); ++i)
        {
            const auto& q = info.params[i];
            s.p[i] = (float) (q.def + reach * (r.uni (q.lo, q.hi) - q.def));
            if (std::string (q.name) == "length" || std::string (q.name) == "tail") s.p[i] = std::min (s.p[i], 3.0f);
            if (std::string (q.name) == "mix") s.p[i] = std::max (s.p[i], 0.5f);
        }
        s.seed = (uint32_t) (r.r.next64() & 0x7fffffff) + 1u;
        nb::MatrixFxEngine e;
        std::vector<float> l, rr;
        e.process (fx, s, x.data(), x.data(), (int) x.size(), (float) kFitRate, l, rr);
        y.resize (l.size());
        for (size_t i = 0; i < l.size(); ++i) y[i] = 0.5f * (l[i] + (i < rr.size() ? rr[i] : l[i]));
        how = info.name;
    }
    for (auto& v : y) if (! std::isfinite (v)) v = 0.0f;
    if (y.size() > (size_t) (6.0 * kFitRate)) y.resize ((size_t) (6.0 * kFitRate));
    return y;
}

// ------------------------------------------------------------------ scoring

// Two fixed noise draws for the whole of 7b, never a fit's (a fit uses seeds 11 and 101): Grow
// chooses on the first, and every closeness shown is measured on the second.
constexpr uint32_t kChooseSeed = 2027, kShowSeed = 4049;

struct Scorer
{
    Scorer (const Clip& target) : n ((int) target.x.size()), loss (target.x.data(), (int) target.x.size(), kFitRate, true, target.noiseFloor),
                                  rms (rmsOf (target.x.data(), (int) target.x.size()))
    {
        for (const uint32_t seed : { kChooseSeed, kShowSeed })
        {
            Draw d; d.bands = noiseBands (n, kFitRate, seed); d.body = noiseBands (n, kFitRate, seed + 90);
            draws.push_back (std::move (d));
        }
    }

    // a sound's closeness: its first n samples (silence after its end), at the target's level
    double sound (const std::vector<float>& y) const
    {
        std::vector<float> z ((size_t) n, 0.0f);
        std::copy (y.begin(), y.begin() + (long) std::min (y.size(), (size_t) n), z.begin());
        const double g = rms / (rmsOf (z.data(), n) + 1e-12);
        for (auto& v : z) v = (float) (v * g);
        return loss.value (z.data());
    }
    // a material's closeness: struck with draw 0 (choosing) or 1 (showing)
    double body (const Body& p, int draw) const
    {
        const auto ph = strikePhases (p.modes());
        std::vector<float> y ((size_t) n);
        render (p, n, kFitRate, draws[(size_t) draw].bands.data(), draws[(size_t) draw].body.data(), ph.data(), y.data());
        for (auto& v : y) if (! std::isfinite (v)) return std::numeric_limits<double>::infinity();
        return sound (y);
    }

    struct Draw { std::vector<float> bands, body; };
    int n;
    Loss2 loss;
    double rms;
    std::vector<Draw> draws;
};

// a material's strike as heard: as long as it rings (plus 0.1 s, at most 6 s), on a draw of its own
inline bool strikeOf (const Body& p, uint32_t seed, std::vector<float>& out)
{
    out = renderStrike (p, std::min (6.0, ringSeconds (p) + 0.1), seed);
    double peak = 0.0;
    for (float v : out) { if (! std::isfinite (v)) return false; peak = std::max (peak, (double) std::fabs (v)); }
    return peak > 1e-6 && peak < 1e6;
}

// ------------------------------------------------------------------ one child

// Child `index` of a generation, made from the parents in the way `kind` says. Returns false when no
// playable child came of it (a body that would not sound, after a few tries).
inline bool makeChild (ChildKind kind, const std::vector<const Source*>& bodies, const std::vector<const Source*>& sounds,
                       double reach, uint32_t seed, Child& c)
{
    BreedRandom r (seed);
    c = Child(); c.kind = kind;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        if (kind == ChildKind::Body || kind == ChildKind::Cross)
        {
            if (bodies.empty() || (kind == ChildKind::Cross && bodies.size() < 2)) return false;
            if (kind == ChildKind::Body)
            {
                const auto* p = bodies[(size_t) r.below ((int) bodies.size())];
                c.body = mutateBody (p->body, reach, r, c.how);
                c.name = p->name;
            }
            else
            {
                const int i = r.below ((int) bodies.size());
                int j = r.below ((int) bodies.size() - 1); if (j >= i) ++j;
                c.body = crossBodies (bodies[(size_t) i]->body, bodies[(size_t) j]->body, reach, r, c.how);
                c.how = bodies[(size_t) i]->name + " below, " + bodies[(size_t) j]->name + " above, " + c.how;
            }
            c.isBody = true;
            if (strikeOf (c.body, (uint32_t) (r.r.next64() & 0xffffffff), c.sound)) return true;
        }
        else
        {
            if (sounds.empty() || (kind == ChildKind::Through && bodies.empty())) return false;
            const auto* s = sounds[(size_t) r.below ((int) sounds.size())];
            if (kind == ChildKind::File) c.sound = transformSound (s->sound, reach, r, c.how);
            else
            {
                const auto* b = bodies[(size_t) r.below ((int) bodies.size())];
                const auto wet = throughBody (s->sound, b->body);
                const double mix = 0.5 + 0.5 * reach;
                const double g = rmsOf (s->sound.data(), (int) s->sound.size()) / (rmsOf (wet.data(), (int) s->sound.size()) + 1e-12);
                c.sound.assign (wet.size(), 0.0f);
                for (size_t i = 0; i < wet.size(); ++i) c.sound[i] = (float) ((1.0 - mix) * (i < s->sound.size() ? s->sound[i] : 0.0f) + mix * g * wet[i]);
                c.how = "through " + b->name;
                if (c.sound.size() > (size_t) (6.0 * kFitRate)) c.sound.resize ((size_t) (6.0 * kFitRate));
            }
            c.name = s->name;
            c.isBody = false;
            if (rmsOf (c.sound.data(), (int) c.sound.size()) > 1e-7) return true;
        }
        r = BreedRandom (childSeed (seed, 99, attempt + 1));
    }
    return false;
}

// What a generation of `count` is made of, from what there is to breed from (design table, 7b).
inline std::vector<ChildKind> generationPlan (int bodyParents, int soundParents, int count = 8)
{
    std::vector<ChildKind> k;
    auto add = [&k] (ChildKind kind, int times) { for (int i = 0; i < times; ++i) k.push_back (kind); };
    // two or more materials kept: half the generation are crosses
    if (bodyParents >= 2 && soundParents > 0) { add (ChildKind::Cross, 4); add (ChildKind::Body, 2); add (ChildKind::File, 1); add (ChildKind::Through, 1); }
    else if (bodyParents >= 2)                { add (ChildKind::Cross, 4); add (ChildKind::Body, 4); }
    else if (bodyParents == 1 && soundParents > 0) { add (ChildKind::Body, 5); add (ChildKind::File, 2); add (ChildKind::Through, 1); }
    else if (bodyParents == 1)                { add (ChildKind::Body, 8); }
    else if (soundParents > 0)                { add (ChildKind::File, 8); }
    k.resize ((size_t) std::min ((int) k.size(), count));
    return k;
}

// ------------------------------------------------------------------ the job

struct BreedRequest
{
    enum Mode { Wander, Grow, Scratch } mode = Wander;
    std::vector<Source> parents;     // materials and sounds to breed from
    bool hasTarget = false;
    Clip target;                     // the dropped sound, prepared (closeness is measured against it)
    double reach = 0.4;
    int generations = 20;            // Grow
    int polishSteps = 5;             // Grow: steps of the learner's own fit each child takes after its mutation
    uint32_t seed = 1;               // the run: the same seed, the same children
    int threads = 1;
};

// Runs one Wander generation or one Grow on worker threads. Poll progress() and finished(); results()
// once finished, closest first (in the order made, without a target). Destroying it cancels and waits.
class BreedJob
{
public:
    explicit BreedJob (BreedRequest request) : q (std::move (request))
    {
        runner = std::thread ([this] { run(); });
    }
    ~BreedJob() { cancel(); if (runner.joinable()) runner.join(); }

    void cancel() noexcept { stop.store (true); }
    bool finished() const noexcept { return done.load(); }
    bool wasCancelled() const noexcept { return stop.load(); }
    double progress() const noexcept { return done.load() ? 1.0 : std::min (0.99, (double) workDone.load() / std::max (1, workTotal.load())); }
    double elapsed() const noexcept { return std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count(); }
    std::string error() const { std::lock_guard<std::mutex> l (m); return err; }
    std::vector<Child> results() const { std::lock_guard<std::mutex> l (m); return out; }
    // Grow: the closest's closeness on the choosing draw after each generation (for the tests and the record)
    std::vector<double> history() const { std::lock_guard<std::mutex> l (m); return hist; }

private:
    // runs fn (i) for i in 0..count-1 on the job's threads
    template <typename Fn> void parallel (int count, Fn fn)
    {
        std::atomic<int> next { 0 };
        auto worker = [&] { for (int i = next++; i < count && ! stop.load(); i = next++) fn (i); };
        std::vector<std::thread> pool;
        for (int w = 1; w < std::max (1, std::min (q.threads, count)); ++w) pool.emplace_back (worker);
        worker();
        for (auto& t : pool) t.join();
    }

    void fail (const std::string& e) { std::lock_guard<std::mutex> l (m); err = e; }

    void run()
    {
        std::unique_ptr<Scorer> sc;
        if (q.hasTarget && q.target.ok()) sc = std::make_unique<Scorer> (q.target);
        if (q.mode == BreedRequest::Grow) grow (sc.get());
        else if (q.mode == BreedRequest::Scratch) scratch (sc.get());
        else wander (sc.get());
        done.store (true);
    }

    void split (const std::vector<Source>& from, std::vector<const Source*>& bodies, std::vector<const Source*>& sounds)
    {
        for (const auto& s : from) (s.isBody ? bodies : sounds).push_back (&s);
    }

    void wander (const Scorer* sc)
    {
        std::vector<const Source*> bodies, sounds;
        split (q.parents, bodies, sounds);
        const auto plan = generationPlan ((int) bodies.size(), (int) sounds.size());
        if (plan.empty()) { fail ("Nothing to breed from yet."); return; }
        workTotal.store ((int) plan.size());
        std::vector<Child> kids (plan.size());
        std::vector<char> ok (plan.size(), 0);
        parallel ((int) plan.size(), [&] (int i)
        {
            Child c;
            if (makeChild (plan[(size_t) i], bodies, sounds, q.reach, childSeed (q.seed, 0, i), c))
            {
                if (sc != nullptr) c.loss = c.isBody ? sc->body (c.body, 1) : sc->sound (c.sound);
                kids[(size_t) i] = std::move (c); ok[(size_t) i] = 1;
            }
            ++workDone;
        });
        if (stop.load()) return;
        std::vector<Child> made;
        for (size_t i = 0; i < kids.size(); ++i) if (ok[i]) made.push_back (std::move (kids[i]));
        if (sc != nullptr) std::stable_sort (made.begin(), made.end(), [] (const Child& a, const Child& b) { return a.loss < b.loss; });
        if (made.empty()) fail ("No child came out playable - try a smaller REACH.");
        std::lock_guard<std::mutex> l (m);
        out = std::move (made);
    }

    // Scratch: eight materials from nothing, one of each shape in an order of the run's own, WILD = REACH.
    void scratch (const Scorer* sc)
    {
        constexpr int count = (int) Shape::Count;
        workTotal.store (count);
        std::vector<int> order ((size_t) count);
        for (int i = 0; i < count; ++i) order[(size_t) i] = i;
        BreedRandom shuffle (childSeed (q.seed, 0, 99));
        for (int i = count - 1; i > 0; --i) std::swap (order[(size_t) i], order[(size_t) shuffle.below (i + 1)]);
        std::vector<Child> kids ((size_t) count);
        std::vector<char> ok ((size_t) count, 0);
        parallel (count, [&] (int i)
        {
            for (int attempt = 0; attempt < 4 && ! ok[(size_t) i]; ++attempt)
            {
                BreedRandom r (childSeed (q.seed, attempt, i));
                Child c; c.kind = ChildKind::Scratch; c.isBody = true; c.name = "scratch";
                c.body = scratchBody (r, q.reach, c.how, order[(size_t) i]);
                if (! strikeOf (c.body, childSeed (q.seed, 500 + attempt, i), c.sound)) continue;
                if (sc != nullptr) c.loss = sc->body (c.body, 1);
                kids[(size_t) i] = std::move (c); ok[(size_t) i] = 1;
            }
            ++workDone;
        });
        if (stop.load()) return;
        std::vector<Child> made;
        for (int i = 0; i < count; ++i) if (ok[(size_t) i]) made.push_back (std::move (kids[(size_t) i]));
        if (sc != nullptr) std::stable_sort (made.begin(), made.end(), [] (const Child& a, const Child& b) { return a.loss < b.loss; });
        if (made.empty()) fail ("Nothing playable came of it - try again.");
        std::lock_guard<std::mutex> l (m);
        out = std::move (made);
    }

    // Grow: a (2 + 8) elitist search on the materials, from wherever the parents are toward the sound.
    // Random steps across a thousand numbers at once almost never improve on a fitted body (measured:
    // twelve generations from the learner's own PVC pipe found nothing closer), so each child is
    //   mutation  every number a small Gaussian step of size sigma, adapted each generation by how many
    //             children beat their parent (the one-fifth rule), and now and then one larger move
    //   polish    a few steps of the learner's own fit (Adam on the hand-written gradient, on the fit's
    //             noise draw), which carries the mutation down into the valley it landed in
    // and two of each eight are crosses of the two closest. Chosen on the choosing draw; the eight shown
    // at the end are measured on the showing draw, which no step and no choice ever used.
    void grow (const Scorer* sc)
    {
        if (sc == nullptr) { fail ("Grow needs the sound to grow toward - drop a sound to fit first."); return; }
        struct Cand { Body body; std::string how; double score; };
        std::vector<const Source*> bodies, sounds;
        split (q.parents, bodies, sounds);
        if (bodies.empty()) { fail ("Grow breeds materials: keep or fit one first."); return; }
        const int G = std::max (1, q.generations), lambda = 8, elites = 2;
        workTotal.store ((int) bodies.size() + G * lambda + 8);
        const int n = sc->n;
        const auto fitBands = noiseBands (n, kFitRate, 11), fitBody = noiseBands (n, kFitRate, 101);
        std::vector<Cand> elite;
        for (const auto* b : bodies) { elite.push_back ({ b->body, "from " + b->name, sc->body (b->body, 0) }); ++workDone; }
        auto byScore = [] (const Cand& a, const Cand& b) { return a.score < b.score; };
        std::stable_sort (elite.begin(), elite.end(), byScore);
        elite.resize ((size_t) std::min ((int) elite.size(), elites));
        { std::lock_guard<std::mutex> l (m); hist.push_back (elite.front().score); }
        std::vector<Cand> seen;              // what Grow made (the parents themselves are not shown back)
        double sigma = 0.02 + 0.2 * q.reach;
        for (int g = 0; g < G && ! stop.load(); ++g)
        {
            std::vector<Cand> kids ((size_t) lambda);
            std::vector<char> ok ((size_t) lambda, 0), better ((size_t) lambda, 0);
            parallel (lambda, [&] (int i)
            {
                BreedRandom r (childSeed (q.seed, g + 1, i));
                const auto& parent = elite[(size_t) (i % (int) elite.size())];
                Cand c;
                if (elite.size() >= 2 && i >= lambda - 2)
                {
                    c.body = crossBodies (elite[0].body, elite[1].body, q.reach, r, c.how);
                    c.how = "crossed: " + c.how;
                }
                else
                {
                    Params u = unconstrained (parent.body);
                    for (int key = 0; key < kModeKeys; ++key)
                        for (int k = 0; k < u.k; ++k) u.mode (key)[k] += (key == kLogF ? 0.02 : 1.0) * sigma * r.gauss();
                    for (int key = 0; key < kGlobalKeys; ++key) u.global (key) += sigma * r.gauss();
                    for (int key = 0; key < kBandKeys; ++key) for (int b = 0; b < kBands; ++b) u.band (key)[b] += sigma * r.gauss();
                    c.how = "grown";
                    if (r.uni() < 0.2) c.how = applyMove (u, (Move) r.below ((int) Move::Count), r.sign() * r.uni (0.1, 0.4) * (0.2 + q.reach));
                    c.body = physical (u);
                }
                if (q.polishSteps > 0)
                {
                    FitOptions opt; opt.steps = q.polishSteps; opt.startInFloat = false; opt.cancel = &stop;
                    Scene scn; scn.n = n; scn.bands = fitBands.data(); scn.body = fitBody.data();
                    const auto ph = strikePhases (c.body.modes()); scn.phases = ph.data();
                    auto fr = fit (q.target.x.data(), sc->loss, scn, c.body, opt);
                    if (! fr.cancelled && std::isfinite (fr.loss)) c.body = fr.params;
                }
                c.score = sc->body (c.body, 0);
                if (std::isfinite (c.score)) { better[(size_t) i] = c.score < parent.score; kids[(size_t) i] = std::move (c); ok[(size_t) i] = 1; }
                ++workDone;
            });
            if (stop.load()) break;
            int wins = 0, tries = 0;
            for (int i = 0; i < lambda; ++i) if (ok[(size_t) i]) { elite.push_back (kids[(size_t) i]); seen.push_back (kids[(size_t) i]); wins += better[(size_t) i]; ++tries; }
            sigma *= std::exp ((tries > 0 ? (double) wins / tries : 0.0) - 0.2);    // the one-fifth rule
            sigma = std::max (0.002, std::min (0.5, sigma));
            std::stable_sort (elite.begin(), elite.end(), byScore);
            elite.resize ((size_t) std::min ((int) elite.size(), elites));
            std::lock_guard<std::mutex> l (m);
            hist.push_back (elite.front().score);
        }
        if (stop.load()) return;
        // the eight closest of everything Grow made, struck and measured on the showing draw
        std::stable_sort (seen.begin(), seen.end(), byScore);
        std::vector<Cand> best;
        for (const auto& c : seen) if ((int) best.size() < 8) best.push_back (c);
        std::vector<Child> made (best.size());
        std::vector<char> ok (best.size(), 0);
        parallel ((int) best.size(), [&] (int i)
        {
            Child c; c.kind = ChildKind::Body; c.isBody = true; c.body = best[(size_t) i].body; c.how = best[(size_t) i].how;
            c.name = "grown";
            if (strikeOf (c.body, childSeed (q.seed, 1000, i), c.sound)) { c.loss = sc->body (c.body, 1); made[(size_t) i] = std::move (c); ok[(size_t) i] = 1; }
            ++workDone;
        });
        if (stop.load()) return;
        std::vector<Child> res;
        for (size_t i = 0; i < made.size(); ++i) if (ok[i]) res.push_back (std::move (made[i]));
        std::stable_sort (res.begin(), res.end(), [] (const Child& a, const Child& b) { return a.loss < b.loss; });
        std::lock_guard<std::mutex> l (m);
        out = std::move (res);
    }

    BreedRequest q;
    std::atomic<bool> stop { false }, done { false };
    std::atomic<int> workDone { 0 }, workTotal { 1 };
    mutable std::mutex m;
    std::vector<Child> out;
    std::vector<double> hist;
    std::string err;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::thread runner;
};

} // namespace ml
