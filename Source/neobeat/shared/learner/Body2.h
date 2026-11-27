// The MaterialLab learner's living modal body (engine2.py), in C++: the forward render.
//
// A port of the user's own learner (MaterialLab, materiallab/engine2.py - "Engine v2: a living
// modal body"), so Batchcut can fit any dropped file itself (docs/PROPOSED-FEATURES.md, "Batchcut:
// generate pools of variants from any file"; the user chose to port the learner's fitting, with the
// gradients written by hand). It follows render_physical line for line, so a material fitted here
// and one fitted by the learner are the same thing, and the stages after this one - the loss, the
// gradients, Adam - can be checked against the learner's own numbers.
//
// Each mode is a damped resonator governed by laws that keep it playable:
//   damping law   sigma(f) = b1 + b3 (f/1 kHz)^2, times a per-mode residual exp(rho)
//   contact       damping that switches on at cx_t, below cx_f
//   doublets      every mode a pair split by delta Hz, mixed by beta: it beats
//   cascade       energy from the struck body feeds receiving modes after the strike: they swell
//   tension       per-mode settling (glide, tau_g) and a global elastic term from the body's energy
//   low body      a shared slow exchange of energy in the low modes (rocking)
//   click         band-shaped noise at the strike
//   dense layer   ringing noise by band, decaying by the same law
// Envelopes at control rate (one value per 64 samples), phases at sample rate.
//
// The noise is an input, as in the learner: render_physical takes its noise bands as data. To
// check against the learner, pass its arrays; to play, any noise of the same colour
// (makeNoiseBands, below). Computed in double; the learner runs in float32.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace ml
{
constexpr int kHop = 64;
constexpr int kBands = 16;
constexpr double kTwoPi = 6.283185307179586476925286766559;

inline double sigmoid (double x) noexcept { return x >= 0 ? 1.0 / (1.0 + std::exp (-x)) : std::exp (x) / (1.0 + std::exp (x)); }
inline double softplus (double x) noexcept { return x > 30.0 ? x : std::log1p (std::exp (x)); }

// The physical parameters of one material (the learner's "params", physical side).
struct Body
{
    // per mode
    std::vector<double> f, a, rho, delta, beta, eps, glide, tau_g;
    // laws
    double b1 = 1.0, b3 = 0.01, C = 2.0, gamma = 0.5, tau_c = 0.04, G = 0.0, tau_a = 0.001;
    double cx_amt = 0.002, cx_t = 2.5, cx_f = 500.0;
    double lb_m = 0.02, lb_rate = 4.0, lb_f = 600.0;
    double tau_n = 0.01;
    std::array<double, kBands> noise {}, dn {}, dn_rho {};

    int modes() const noexcept { return (int) f.size(); }
};

inline std::array<double, kBands + 1> bandEdges (double fs)
{
    std::array<double, kBands + 1> e {};
    const double lo = 60.0, hi = std::min (20000.0, 0.45 * fs);
    for (int i = 0; i <= kBands; ++i) e[(size_t) i] = lo * std::pow (hi / lo, (double) i / kBands);
    return e;
}

inline std::array<double, kBands> bandCentres (double fs)
{
    const auto e = bandEdges (fs);
    std::array<double, kBands> c {};
    for (int b = 0; b < kBands; ++b) c[(size_t) b] = std::sqrt (e[(size_t) b] * e[(size_t) b + 1]);
    return c;
}

// The render. `bands` and `body` are kBands rows of n samples each (row-major): the click's noise
// and the dense layer's. `phases` holds the two start phases of each mode's pair (2 x k); the
// learner starts every mode at pi/2, a cosine phase, so a strike is coherent.
//
// `tr` receives the control-rate state for the gradient pass to reuse (may be null). `out` is float
// for playing, double for fitting.
struct BodyTrace
{
    int nf = 0, k = 0;
    std::vector<double> sigT, direct, casc, amp, fpath, source, energy, attack, rockN, nyq;
    std::vector<double> sigma, gain, low;
};

template <typename Out>
inline void render (const Body& p, int n, double fs, const float* bands, const float* body,
                    const double* phases, Out* out, BodyTrace* tr = nullptr)
{
    const int k = p.modes();
    const int nf = n / kHop + 2;
    const double dt = kHop / fs;
    std::vector<double> sigma ((size_t) k), gain ((size_t) k), low ((size_t) k), cxw ((size_t) k);
    for (int m = 0; m < k; ++m)
    {
        const double fk = p.f[(size_t) m] / 1000.0;
        sigma[(size_t) m] = (p.b1 + p.b3 * fk * fk) * std::exp (p.rho[(size_t) m]);
        gain[(size_t) m] = p.C * std::pow (fk, p.gamma) * std::exp (p.eps[(size_t) m]);
        low[(size_t) m] = sigmoid ((p.lb_f - p.f[(size_t) m]) / (0.2 * p.lb_f));
        cxw[(size_t) m] = sigmoid ((p.cx_f - p.f[(size_t) m]) / (0.25 * p.cx_f));
    }

    // ---- control rate: [nf][k]
    std::vector<double> sigT ((size_t) nf * k), direct ((size_t) nf * k), amp ((size_t) nf * k), fpath ((size_t) nf * k),
                        casc ((size_t) nf * k), source ((size_t) nf), energy ((size_t) nf), attack ((size_t) nf), nyq ((size_t) nf * k);
    std::vector<double> decay ((size_t) k, 0.0), acc ((size_t) k, 0.0);
    for (int i = 0; i < nf; ++i)
    {
        const double t = i * dt;
        const double cxOn = p.cx_amt * sigmoid ((t - p.cx_t) / 0.004);
        double e2 = 0.0;
        for (int m = 0; m < k; ++m)
        {
            const size_t ix = (size_t) i * k + m;
            sigT[ix] = sigma[(size_t) m] + cxOn * cxw[(size_t) m];
            // decay = cumsum(sig_t) dt - sig_t[0] dt: the sum of sig_t over frames 1..i
            if (i > 0) decay[(size_t) m] += sigT[ix] * dt;
            direct[ix] = p.a[(size_t) m] * std::exp (-decay[(size_t) m]);
            e2 += direct[ix] * direct[ix];
        }
        energy[(size_t) i] = e2;
        source[(size_t) i] = std::sqrt (e2 + 1e-20) * (1.0 - std::exp (-t / p.tau_c));
        for (int m = 0; m < k; ++m)
        {
            const size_t ix = (size_t) i * k + m;
            acc[(size_t) m] = acc[(size_t) m] * std::exp (-sigT[ix] * dt) + gain[(size_t) m] * source[(size_t) i] * dt;
            casc[ix] = acc[(size_t) m];
        }
        attack[(size_t) i] = 1.0 - std::exp (-t / p.tau_a);
    }
    const double e1 = energy.size() > 1 ? energy[1] : energy[0];
    const double Gc = std::max (-0.9, std::min (3.0, p.G));
    std::vector<double> rockN ((size_t) nf * k);
    for (int i = 0; i < nf; ++i)
    {
        const double t = i * dt;
        const double cosr = std::cos (kTwoPi * p.lb_rate * t);
        const double en = energy[(size_t) i] / (e1 + 1e-20);
        const double elastic = 0.5 * std::log1p (Gc * en);     // NaN below -1, as in the learner
        for (int m = 0; m < k; ++m)
        {
            const size_t ix = (size_t) i * k + m;
            const double rock = (1.0 + p.lb_m * low[(size_t) m] * cosr) / (1.0 + p.lb_m * low[(size_t) m]);
            rockN[ix] = rock;
            const double lf = std::log (p.f[(size_t) m]) + p.glide[(size_t) m] * std::exp (-t / p.tau_g[(size_t) m]) + elastic;
            fpath[ix] = std::exp (lf);
            nyq[ix] = sigmoid ((0.46 * fs - fpath[ix]) / (0.005 * fs));
            amp[ix] = (direct[ix] + casc[ix]) * attack[(size_t) i] * rock * nyq[ix];
        }
    }

    // ---- sample rate
    std::vector<double> ph1 ((size_t) k), ph2 ((size_t) k);
    for (int m = 0; m < k; ++m) { ph1[(size_t) m] = phases[m]; ph2[(size_t) m] = phases[k + m]; }
    const auto centres = bandCentres (fs);
    std::array<double, kBands> sigB {}, cxB {};
    for (int b = 0; b < kBands; ++b)
    {
        const double cb = centres[(size_t) b] / 1000.0;
        sigB[(size_t) b] = (p.b1 + p.b3 * cb * cb) * std::exp (p.dn_rho[(size_t) b]);
        cxB[(size_t) b] = p.cx_amt * sigmoid ((p.cx_f - centres[(size_t) b]) / (0.25 * p.cx_f));
    }
    const double w = kTwoPi / fs;
    for (int s = 0; s < n; ++s)
    {
        const double pos = (double) s / kHop;
        const int i0 = (int) std::floor (pos);
        const double fr = pos - i0;
        double y = 0.0;
        for (int m = 0; m < k; ++m)
        {
            const size_t a0 = (size_t) i0 * k + m, a1 = a0 + (size_t) k;
            const double as = amp[a0] * (1 - fr) + amp[a1] * fr;
            const double fs_ = fpath[a0] * (1 - fr) + fpath[a1] * fr;
            const double half = 0.5 * p.delta[(size_t) m];
            ph1[(size_t) m] += w * (fs_ - half);          // cumsum: this sample's increment included
            ph2[(size_t) m] += w * (fs_ + half);
            const double be = p.beta[(size_t) m];
            y += as * ((1 - be) * std::sin (ph1[(size_t) m]) + be * std::sin (ph2[(size_t) m]));
        }
        const double t = s / fs;
        const double envN = (1.0 - std::exp (-t / 0.0002)) * std::exp (-t / p.tau_n);
        double click = 0.0, bodyS = 0.0;
        const double held = softplus ((t - p.cx_t) / 0.004) * 0.004;
        const double att = 1.0 - std::exp (-t / 0.0003);
        for (int b = 0; b < kBands; ++b)
        {
            click += p.noise[(size_t) b] * bands[(size_t) b * n + s];
            const double envB = std::exp (-sigB[(size_t) b] * t - cxB[(size_t) b] * held) * att;
            bodyS += p.dn[(size_t) b] * envB * body[(size_t) b * n + s];
        }
        out[s] = (Out) (y + click * envN + bodyS);
    }

    if (tr != nullptr)
    {
        tr->nf = nf; tr->k = k;
        tr->sigT = std::move (sigT); tr->direct = std::move (direct); tr->casc = std::move (casc);
        tr->amp = std::move (amp); tr->fpath = std::move (fpath); tr->source = std::move (source);
        tr->energy = std::move (energy); tr->attack = std::move (attack); tr->rockN = std::move (rockN);
        tr->nyq = std::move (nyq); tr->sigma = std::move (sigma); tr->gain = std::move (gain); tr->low = std::move (low);
    }
}

} // namespace ml
