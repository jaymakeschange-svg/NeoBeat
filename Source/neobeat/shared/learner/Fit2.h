// The learner's fit (fit2.fit), in C++: level match, then Adam on the unconstrained parameters with
// the learner's rates, schedule, regularisation and freeze mask, keeping the best point seen.
//
// Port of the user's MaterialLab learner (docs/PROPOSED-FEATURES.md, "Batchcut: generate pools of
// variants from any file", stage 6). The gradient is Grad2.h's, written by hand and checked against
// JAX. The learner runs in float32 and this in double, so the two trajectories part after a few
// steps (the log-spectral term's gradient changes sign at near-silent bins, where float32 noise
// decides); the check is the outcome - the final loss - not the path (tools/test_learner.cpp,
// stage 6).
#pragma once

#include "Grad2.h"

#include <atomic>
#include <functional>
#include <limits>

namespace ml
{
struct FitOptions
{
    int steps = 300;
    std::vector<double> keepGlide;          // per mode: 1 learns glide and tau_g, 0 keeps the measured glide
    bool startInFloat = true;               // round the starting point to float32, as the learner does
    std::function<void (int, int, double)> progress;     // (step, steps, loss), every 25 steps and the last
    const std::atomic<bool>* cancel = nullptr;
    int everyStepUntil = 0;                 // report every step before this one too (tests)
    std::function<void (Params&)> adjustStart;           // change the starting point (tests)
};

struct FitResult
{
    Body params;
    double loss = 0.0, startLoss = 0.0;
    std::vector<double> history;
    Params best;
    bool cancelled = false;
};

// fit2.fit's objective: the loss plus 1e-3 (mean rho^2 + 0.1 mean eps^2), physical rho and eps
inline double fitObjective (const Params& u, const Loss2& loss, const Scene& sc, Params& gu)
{
    double v = objective (u, loss, sc, &gu);
    const int k = u.k;
    double r2 = 0.0, e2 = 0.0;
    for (int m = 0; m < k; ++m)
    {
        const double rho = boundedValue (u.mode (kRho)[m], modeBound (kRho));
        const double eps = boundedValue (u.mode (kEps)[m], modeBound (kEps));
        r2 += rho * rho; e2 += eps * eps;
        gu.mode (kRho)[m] += 1e-3 * 2.0 * rho / k * boundedSlope (u.mode (kRho)[m], modeBound (kRho));
        gu.mode (kEps)[m] += 1e-4 * 2.0 * eps / k * boundedSlope (u.mode (kEps)[m], modeBound (kEps));
    }
    return v + 1e-3 * (r2 / k + 0.1 * e2 / k);
}

inline FitResult fit (const float* x, const Loss2& loss, const Scene& sc, Body init, const FitOptions& opt = {})
{
    FitResult res;
    const int k = init.modes(), n = sc.n;

    // level match before learning
    {
        std::vector<double> y0 ((size_t) n);
        render (init, n, sc.fs, sc.bands, sc.body, sc.phases, y0.data());
        double sx = 0.0, sy = 0.0;
        for (int i = 0; i < n; ++i) { sx += (double) x[i] * x[i]; if (std::isfinite (y0[(size_t) i])) sy += y0[(size_t) i] * y0[(size_t) i]; }
        const double scale = std::sqrt (sx / n) / (std::sqrt (sy / n) + 1e-12);
        for (auto& a : init.a) a *= scale;
    }
    Params u = unconstrained (init);
    if (opt.startInFloat) for (auto& v : u.v) v = (double) (float) v;
    if (opt.adjustStart) opt.adjustStart (u);

    Params lr (k);
    for (auto& v : lr.v) v = 0.04;
    for (int m = 0; m < k; ++m) lr.mode (kLogF)[m] = 0.002;
    Params mask (k);
    for (auto& v : mask.v) v = 1.0;
    if ((int) opt.keepGlide.size() == k)
        for (int m = 0; m < k; ++m) { mask.mode (kGlide)[m] = opt.keepGlide[(size_t) m]; mask.mode (kTauG)[m] = opt.keepGlide[(size_t) m]; }

    std::vector<double> m1 (u.size(), 0.0), m2 (u.size(), 0.0);
    Params best = u, gu;
    double bestV = std::numeric_limits<double>::infinity();
    const double big = (double) std::numeric_limits<float>::max();     // jnp.nan_to_num in float32
    for (int i = 0; i < opt.steps; ++i)
    {
        if (opt.cancel != nullptr && opt.cancel->load()) { res.cancelled = true; break; }
        const double val = fitObjective (u, loss, sc, gu);
        if (! std::isfinite (val))
        {
            u = best;
            for (auto& l : lr.v) l *= 0.5;
            continue;
        }
        res.history.push_back (val);
        if (val < bestV) { bestV = val; best = u; }
        const double sched = 0.55 + 0.45 * std::cos (3.14159265358979323846 * i / opt.steps);
        const double c1 = 1.0 - std::pow (0.9, i + 1), c2 = 1.0 - std::pow (0.999, i + 1);
        for (size_t j = 0; j < u.size(); ++j)
        {
            double g = gu.v[j];
            if (std::isnan (g)) g = 0.0; else if (std::isinf (g)) g = g > 0 ? big : -big;
            g *= mask.v[j];
            m1[j] = 0.9 * m1[j] + 0.1 * g;
            m2[j] = 0.999 * m2[j] + 0.001 * g * g;
            u.v[j] -= lr.v[j] * sched * (m1[j] / c1) / (std::sqrt (m2[j] / c2) + 1e-8);
        }
        if (opt.progress && (i % 25 == 0 || i == opt.steps - 1 || i < opt.everyStepUntil)) opt.progress (i + 1, opt.steps, val);
    }
    res.best = best;
    res.params = physical (best);
    res.loss = bestV;
    res.startLoss = res.history.empty() ? 0.0 : res.history.front();
    return res;
}

} // namespace ml
