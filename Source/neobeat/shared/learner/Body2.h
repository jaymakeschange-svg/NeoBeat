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

// The phasors of every mode's pair of partials, stepped through one control frame at a time.
//
// A mode's frequency is a straight line across a control frame (interpolated between the frame's
// two values), so its phase step grows by the same amount each sample, and e^{i phase} can be turned
// by complex multiplication instead of computed with sin and cos: the step is itself a phasor,
// turned by a fixed rotation each sample. Two sin per mode per sample were most of a render and of
// the gradient pass; this costs a handful of sin per mode per frame. The phases are still summed
// sample by sample exactly as the learner sums them (a cumsum), and each phasor is taken afresh from
// that sum at the start of every frame, so the rotations' rounding never runs longer than 64 samples
// (tools/test_learner.cpp holds the render and the gradient to the learner's, as before).
struct PairPhasors
{
    int k = 0;
    std::vector<double> ph1, ph2;                      // the running phase sums
    std::vector<double> z1r, z1i, z2r, z2i;            // e^{i phase} this sample
    std::vector<double> r1r, r1i, r2r, r2i;            // e^{i step} of the next sample
    std::vector<double> qr, qi;                        // how far the step turns each sample

    PairPhasors (const double* phases, int modes)
        : k (modes), ph1 (phases, phases + modes), ph2 (phases + modes, phases + 2 * modes),
          z1r ((size_t) modes), z1i ((size_t) modes), z2r ((size_t) modes), z2i ((size_t) modes),
          r1r ((size_t) modes), r1i ((size_t) modes), r2r ((size_t) modes), r2i ((size_t) modes),
          qr ((size_t) modes), qi ((size_t) modes) {}

    // The first sample of a frame (fr = 0). F0, F1: each mode's frequency at this frame and the next.
    void begin (const double* F0, const double* F1, const double* delta, double w)
    {
        constexpr double fr = 1.0 / kHop;
        for (int m = 0; m < k; ++m)
        {
            const double half = 0.5 * delta[m];
            const double f0 = F0[m] * (1 - 0.0) + F1[m] * 0.0;      // the learner's interpolation at fr = 0
            ph1[(size_t) m] += w * (f0 - half);
            ph2[(size_t) m] += w * (f0 + half);
            z1r[(size_t) m] = std::cos (ph1[(size_t) m]); z1i[(size_t) m] = std::sin (ph1[(size_t) m]);
            z2r[(size_t) m] = std::cos (ph2[(size_t) m]); z2i[(size_t) m] = std::sin (ph2[(size_t) m]);
            const double f1 = F0[m] * (1 - fr) + F1[m] * fr;
            r1r[(size_t) m] = std::cos (w * (f1 - half)); r1i[(size_t) m] = std::sin (w * (f1 - half));
            r2r[(size_t) m] = std::cos (w * (f1 + half)); r2i[(size_t) m] = std::sin (w * (f1 + half));
            const double dq = w * (F1[m] - F0[m]) * fr;
            qr[(size_t) m] = std::cos (dq); qi[(size_t) m] = std::sin (dq);
        }
    }

    // Each later sample of the frame (fr = j / kHop, j >= 1): the sums as the learner makes them,
    // the phasors turned by one step and the steps by one rotation. One plain loop over the modes,
    // so the compiler can run several modes at once.
    void advance (const double* F0, const double* F1, double fr, const double* delta, double w) noexcept
    {
        double* P1 = ph1.data(); double* P2 = ph2.data();
        double* Z1r = z1r.data(); double* Z1i = z1i.data(); double* Z2r = z2r.data(); double* Z2i = z2i.data();
        double* R1r = r1r.data(); double* R1i = r1i.data(); double* R2r = r2r.data(); double* R2i = r2i.data();
        const double* Qr = qr.data(); const double* Qi = qi.data();
        for (int i = 0; i < k; ++i)
        {
            const double f = F0[i] * (1 - fr) + F1[i] * fr, half = 0.5 * delta[i];
            P1[i] += w * (f - half);
            P2[i] += w * (f + half);
            const double a1 = Z1r[i] * R1r[i] - Z1i[i] * R1i[i], b1 = Z1r[i] * R1i[i] + Z1i[i] * R1r[i];
            const double a2 = Z2r[i] * R2r[i] - Z2i[i] * R2i[i], b2 = Z2r[i] * R2i[i] + Z2i[i] * R2r[i];
            const double c1 = R1r[i] * Qr[i] - R1i[i] * Qi[i], d1 = R1r[i] * Qi[i] + R1i[i] * Qr[i];
            const double c2 = R2r[i] * Qr[i] - R2i[i] * Qi[i], d2 = R2r[i] * Qi[i] + R2i[i] * Qr[i];
            Z1r[i] = a1; Z1i[i] = b1; Z2r[i] = a2; Z2i[i] = b2;
            R1r[i] = c1; R1i[i] = d1; R2r[i] = c2; R2i[i] = d2;
        }
    }
};

// The dense layer's decay in each band, exp (-sigB t - cxB held (t)), sample by sample. The decay
// part is a running product, taken afresh from exp at the start of each control frame (so its
// rounding never runs past 64 samples, as with the phasors); the contact part is 1 to double
// precision until the contact has begun, and only then costs an exp per band. Sixteen exp per
// sample, in the render and again in the gradient pass, were most of what was left of a render's
// time once the modes were cheap.
struct BandDecay
{
    std::array<double, kBands> e {}, r {};
    double cxMax = 0.0;

    void setup (const std::array<double, kBands>& sigB, const std::array<double, kBands>& cxB, double fs)
    {
        cxMax = 0.0;
        for (int b = 0; b < kBands; ++b) { r[(size_t) b] = std::exp (-sigB[(size_t) b] / fs); cxMax = std::max (cxMax, cxB[(size_t) b]); }
    }
    // to sample s (t = s / fs); `frameStart` at the first sample of each control frame
    void step (bool frameStart, double t, const std::array<double, kBands>& sigB) noexcept
    {
        for (int b = 0; b < kBands; ++b)
            e[(size_t) b] = frameStart ? std::exp (-sigB[(size_t) b] * t) : e[(size_t) b] * r[(size_t) b];
    }
    // exp (-cxB held) is exactly 1 in double once cxB held < 2^-54; below 1e-17 that holds for every band
    bool contact (double held) const noexcept { return cxMax * held > 1e-17; }
    double env (int b, double cxB, double held, bool inContact) const noexcept
    { return inContact ? e[(size_t) b] * std::exp (-cxB * held) : e[(size_t) b]; }
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
    PairPhasors z (phases, k);
    const auto centres = bandCentres (fs);
    std::array<double, kBands> sigB {}, cxB {};
    for (int b = 0; b < kBands; ++b)
    {
        const double cb = centres[(size_t) b] / 1000.0;
        sigB[(size_t) b] = (p.b1 + p.b3 * cb * cb) * std::exp (p.dn_rho[(size_t) b]);
        cxB[(size_t) b] = p.cx_amt * sigmoid ((p.cx_f - centres[(size_t) b]) / (0.25 * p.cx_f));
    }
    BandDecay dec;
    dec.setup (sigB, cxB, fs);
    const double w = kTwoPi / fs;
    for (int s = 0; s < n; ++s)
    {
        const int i0 = s / kHop, j = s - i0 * kHop;
        const double fr = (double) j / kHop;
        const double* A0 = &amp[(size_t) i0 * k];
        const double* A1 = A0 + k;
        const double* F0 = &fpath[(size_t) i0 * k];
        const double* F1 = F0 + k;
        if (j == 0) z.begin (F0, F1, p.delta.data(), w);
        else        z.advance (F0, F1, fr, p.delta.data(), w);
        double y = 0.0;
        const double* beta = p.beta.data();
        const double* s1 = z.z1i.data();
        const double* s2 = z.z2i.data();
        for (int m = 0; m < k; ++m)
            y += (A0[m] * (1 - fr) + A1[m] * fr) * ((1 - beta[m]) * s1[m] + beta[m] * s2[m]);
        const double t = s / fs;
        const double envN = (1.0 - std::exp (-t / 0.0002)) * std::exp (-t / p.tau_n);
        double click = 0.0, bodyS = 0.0;
        const double held = softplus ((t - p.cx_t) / 0.004) * 0.004;
        const double att = 1.0 - std::exp (-t / 0.0003);
        dec.step (j == 0, t, sigB);
        const bool touching = dec.contact (held);
        for (int b = 0; b < kBands; ++b)
        {
            click += p.noise[(size_t) b] * bands[(size_t) b * n + s];
            const double envB = dec.env (b, cxB[(size_t) b], held, touching) * att;
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
