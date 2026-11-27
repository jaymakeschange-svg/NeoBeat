// Any file, fitted: the learner's fit run in the background, several times side by side.
//
// Stage 7a of the learner port (docs/PROPOSED-FEATURES.md, "Batchcut: generate pools of variants from
// any file"). The user decided (27 Nov) that a dropped file gets several fits and every one goes into
// the pool, ranked by how close it came: the learner's fit is path-dependent (stage 6), so each fit,
// started a rounding step apart, ends as a different material - the spread becomes variants.
//
// What happens to a file, in the learner's own terms:
//   1. prepareClip   mono, 48 kHz, cut to start just before the strike (fit_v2.trim_to_strike),
//                    at most 1.5 s with a 30 ms fade (analysis.crop_hit's length and fade), and the
//                    noise floor measured as the training targets record it
//   2. initialBody   partials.py's analysis and fit2.initial_params, once
//   3. fit, N times  fit2.fit, each started a float32 rounding step apart, on worker threads
//
// The noise the body renders with is an input, as in the learner. The learner draws it from numpy's
// generator; here it is the same colour (the learner's band edges and Butterworth band-pass, the same
// normalisation) from a different draw - Box-Muller on numpy's own PCG64 generator, so it is the same on
// every computer. A material fitted here plays the same in the learner, and the other way round, up to
// the draw of the noise.
//
// No JUCE: the plugin drives it, the container's tests run it directly.
#pragma once

#include "Analysis2.h"
#include "Fit2.h"
#include "NumpyRandom.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace ml
{
constexpr double kFitRate = 48000.0;

// ------------------------------------------------------------------ the clip

// A windowed-sinc resampler (Kaiser, beta 9, 32 zero crossings each side), cut off just below the
// lower Nyquist. The learner uses scipy's resample_poly; any clean resampler serves.
inline std::vector<float> resample (const float* x, int n, double from, double to)
{
    if (n <= 0) return {};
    if (std::fabs (from - to) < 1e-9) return std::vector<float> (x, x + n);
    const double ratio = to / from;
    const int out = (int) std::floor (n * ratio);
    const double cut = 0.97 * std::min (1.0, ratio);         // of the input's Nyquist
    const int half = (int) std::ceil (32.0 / cut);
    auto bessel0 = [] (double v) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (v / (2.0 * k)) * (v / (2.0 * k)); s += t; } return s; };
    const double beta = 9.0, i0b = bessel0 (beta);
    std::vector<float> y ((size_t) out);
    for (int j = 0; j < out; ++j)
    {
        const double c = j / ratio;
        const int i0 = (int) std::floor (c);
        double acc = 0.0, wsum = 0.0;
        for (int i = i0 - half + 1; i <= i0 + half; ++i)
        {
            const double d = c - i;
            const double r = d / half;
            if (r <= -1.0 || r >= 1.0) continue;
            const double win = bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
            const double a = an::kPi * cut * d;
            const double s = std::fabs (a) < 1e-12 ? 1.0 : std::sin (a) / a;
            const double w = cut * s * win;
            wsum += w;
            if (i >= 0 && i < n) acc += w * x[i];
        }
        y[(size_t) j] = (float) (wsum > 0.0 ? acc * cut / wsum * (wsum / cut) : 0.0);
    }
    return y;
}

// fit_v2.trim_to_strike: start 1.5 ms before the first millisecond within 30 dB of the loudest
inline int strikeStart (const float* x, int n, double fs, double keepMs = 1.5)
{
    const int w = (int) (0.001 * fs);
    const int frames = w > 0 ? n / w : 0;
    if (frames == 0) return 0;
    std::vector<double> env ((size_t) frames);
    double mx = -1e300;
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        for (int i = 0; i < w; ++i) s += (double) x[f * w + i] * x[f * w + i];
        env[(size_t) f] = 20.0 * std::log10 (std::sqrt (s / w) + 1e-9);
        mx = std::max (mx, env[(size_t) f]);
    }
    int first = 0;
    while (first < frames && ! (env[(size_t) first] > mx - 30.0)) ++first;
    return std::max (0, first * w - (int) (keepMs / 1000.0 * fs));
}

// The training targets' noise_floor_db: the quietest 5% of the 10 ms / 5 ms RMS envelope against its
// loudest frame (checked on the three targets the user has: within 0.9 dB).
inline double noiseFloorDb (const float* x, int n, double fs)
{
    const auto e = framedDb (x, n, (int) (0.01 * fs), (int) (0.005 * fs));
    if (e.size() < 2) return -70.0;
    auto s = e;
    std::sort (s.begin(), s.end());
    const double pos = 0.05 * (double) (s.size() - 1);        // numpy percentile, linear
    const size_t lo = (size_t) std::floor (pos);
    const double p5 = s[lo] + (s[std::min (lo + 1, s.size() - 1)] - s[lo]) * (pos - (double) lo);
    return std::max (-70.0, std::min (-6.0, p5 - s.back()));
}

struct Clip
{
    std::vector<float> x;            // 48 kHz mono, from the strike
    double noiseFloor = -70.0;       // dB against the loudest 10 ms
    double sourceStart = 0.0;        // seconds into the source where the strike was found
    bool ok() const noexcept { return x.size() >= 4096; }
};

inline Clip prepareClip (const float* mono, int n, double sr, double maxSeconds = 1.5)
{
    Clip c;
    auto y = resample (mono, n, sr, kFitRate);
    const int start = strikeStart (y.data(), (int) y.size(), kFitRate);
    c.sourceStart = start / kFitRate;
    const int len = std::min ((int) y.size() - start, (int) (maxSeconds * kFitRate));
    if (len <= 0) return c;
    c.x.assign (y.begin() + start, y.begin() + start + len);
    const int fade = std::min (len / 8, (int) (0.03 * kFitRate));
    for (int i = 0; i < fade; ++i) c.x[(size_t) (len - fade + i)] *= (float) (1.0 - (double) i / std::max (1, fade - 1));
    double peak = 0.0;
    for (float v : c.x) peak = std::max (peak, (double) std::fabs (v));
    if (peak < 1e-5) { c.x.clear(); return c; }             // silence: nothing to fit
    c.noiseFloor = noiseFloorDb (c.x.data(), len, kFitRate);
    return c;
}

// ------------------------------------------------------------------ noise of the learner's colour

// engine2.noise_bands: per band, white noise through the band's 4th-order Butterworth band-pass
// (2048 samples of run-in dropped), normalised to unit RMS. kBands rows of n.
inline std::vector<float> noiseBands (int n, double fs, uint32_t seed)
{
    NumpyRandom rng (seed);
    const auto edges = bandEdges (fs);
    std::vector<float> out ((size_t) kBands * n);
    std::vector<double> w ((size_t) n + 2048);
    for (int b = 0; b < kBands; ++b)
    {
        for (size_t i = 0; i < w.size(); i += 2)
        {
            const double u1 = std::max (1e-300, rng.uniform()), u2 = rng.uniform();
            const double r = std::sqrt (-2.0 * std::log (u1));
            w[i] = r * std::cos (2.0 * an::kPi * u2);
            if (i + 1 < w.size()) w[i + 1] = r * std::sin (2.0 * an::kPi * u2);
        }
        const auto sos = an::butterBand4 (edges[(size_t) b], edges[(size_t) b + 1], fs);
        const auto y = an::sosfilt (sos, w, std::vector<std::array<double, 2>> (sos.size(), { 0.0, 0.0 }));
        double s2 = 0.0;
        for (int i = 0; i < n; ++i) s2 += y[(size_t) i + 2048] * y[(size_t) i + 2048];
        const double g = 1.0 / (std::sqrt (s2 / std::max (1, n)) + 1e-12);
        for (int i = 0; i < n; ++i) out[(size_t) b * n + i] = (float) (y[(size_t) i + 2048] * g);
    }
    return out;
}

inline std::vector<double> strikePhases (int k) { return std::vector<double> ((size_t) 2 * k, an::kPi / 2.0); }

// How long the body rings: the slowest of its audible modes to fall 60 dB.
inline double ringSeconds (const Body& p)
{
    double loudest = 0.0;
    for (double a : p.a) loudest = std::max (loudest, a);
    double slowest = 0.05;
    for (int m = 0; m < p.modes(); ++m)
    {
        if (p.a[(size_t) m] < 1e-3 * loudest) continue;
        const double fk = p.f[(size_t) m] / 1000.0;
        const double s = (p.b1 + p.b3 * fk * fk) * std::exp (p.rho[(size_t) m]);
        if (s > 1e-4) slowest = std::max (slowest, 6.9078 / s);
    }
    return std::min (20.0, slowest);
}

// One strike of a body, rendered by the learner's engine at 48 kHz, `seconds` long.
inline std::vector<float> renderStrike (const Body& p, double seconds, uint32_t seed = 11)
{
    const int n = std::max (64, (int) (seconds * kFitRate));
    const auto bands = noiseBands (n, kFitRate, seed), body = noiseBands (n, kFitRate, seed + 90);
    const auto ph = strikePhases (p.modes());
    std::vector<float> y ((size_t) n);
    render (p, n, kFitRate, bands.data(), body.data(), ph.data(), y.data());
    for (auto& v : y) if (! std::isfinite (v)) v = 0.0f;
    return y;
}

// ------------------------------------------------------------------ the job

struct Fitted
{
    int index = 0;                   // 0: the learner's own starting point; 1..: nudged
    Body params;
    double loss = 0.0, startLoss = 0.0, seconds = 0.0;
    bool cancelled = false;
};

// Runs `fits` fits of one clip on up to `threads` worker threads. Poll progress() and finished();
// results() once finished, best first. Destroying it cancels and waits.
class FitJob
{
public:
    FitJob (Clip clip, int fits, int threads, int steps = 300)
        : c (std::move (clip)), total (std::max (1, fits)), steps (std::max (1, steps)), stepsDone ((size_t) total)
    {
        for (auto& s : stepsDone) s.store (0);
        const int workers = std::max (1, std::min (threads, total));
        runner = std::thread ([this, workers] { run (workers); });
    }

    ~FitJob() { cancel(); if (runner.joinable()) runner.join(); }

    void cancel() noexcept { stop.store (true); }
    bool finished() const noexcept { return done.load(); }
    bool wasCancelled() const noexcept { return stop.load(); }
    // 0..1 over every fit, the analysis counted as the first 3%
    double progress() const noexcept
    {
        if (done.load()) return 1.0;
        if (! analysed.load()) return 0.0;
        double s = 0.0;
        for (const auto& d : stepsDone) s += d.load();
        return 0.03 + 0.97 * s / ((double) total * steps);
    }
    int fits() const noexcept { return total; }
    const Clip& clip() const noexcept { return c; }
    std::string error() const { std::lock_guard<std::mutex> l (m); return err; }

    // best first; only complete fits
    std::vector<Fitted> results() const
    {
        std::lock_guard<std::mutex> l (m);
        auto r = out;
        std::sort (r.begin(), r.end(), [] (const Fitted& a, const Fitted& b) { return a.loss < b.loss; });
        return r;
    }

private:
    void run (int workers)
    {
        if (! c.ok()) { std::lock_guard<std::mutex> l (m); err = "The sound is too short or silent to fit."; done.store (true); return; }
        const int n = (int) c.x.size();
        const std::vector<double> xd (c.x.begin(), c.x.end());
        std::vector<double> keep;
        const Body p0 = an::initialBody (xd, (int) kFitRate, &keep);
        if (p0.modes() == 0) { std::lock_guard<std::mutex> l (m); err = "No partials found to fit."; done.store (true); return; }
        const Loss2 loss (c.x.data(), n, kFitRate, true, c.noiseFloor);
        const auto bands = noiseBands (n, kFitRate, 11), body = noiseBands (n, kFitRate, 101);
        const auto ph = strikePhases (p0.modes());
        Scene sc; sc.n = n; sc.fs = kFitRate; sc.bands = bands.data(); sc.body = body.data(); sc.phases = ph.data();
        analysed.store (true);

        std::atomic<int> next { 0 };
        auto worker = [&]
        {
            for (int i = next++; i < total && ! stop.load(); i = next++)
            {
                FitOptions opt;
                opt.steps = steps;
                opt.keepGlide = keep;
                opt.cancel = &stop;
                opt.adjustStart = [i] (Params& u)
                {
                    if (i == 0) return;
                    NumpyRandom r ((uint32_t) (1000 + i));
                    for (auto& v : u.v) v += (r.next64() & 1) ? 1e-7 : -1e-7;
                };
                opt.progress = [this, i] (int step, int, double) { stepsDone[(size_t) i].store (step); };
                opt.everyStepUntil = steps;           // every step, for the progress bar
                const auto t0 = std::chrono::steady_clock::now();
                auto r = fit (c.x.data(), loss, sc, p0, opt);
                Fitted f;
                f.index = i; f.params = std::move (r.params); f.loss = r.loss; f.startLoss = r.startLoss;
                f.cancelled = r.cancelled;
                f.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                stepsDone[(size_t) i].store (steps);
                if (! r.cancelled && std::isfinite (f.loss) && ! r.history.empty())
                {
                    std::lock_guard<std::mutex> l (m);
                    out.push_back (std::move (f));
                }
            }
        };
        std::vector<std::thread> pool;
        for (int w = 1; w < workers; ++w) pool.emplace_back (worker);
        worker();
        for (auto& t : pool) t.join();
        done.store (true);
    }

    Clip c;
    const int total, steps;
    std::vector<std::atomic<int>> stepsDone;
    std::atomic<bool> stop { false }, done { false }, analysed { false };
    mutable std::mutex m;
    std::vector<Fitted> out;
    std::string err;
    std::thread runner;
};

} // namespace ml
