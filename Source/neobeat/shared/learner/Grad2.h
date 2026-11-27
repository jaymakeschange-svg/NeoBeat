// The learner's gradient, by hand: from the loss's gradient with respect to each sample back through
// the body (Body2.h) to every parameter, and through engine2.physical to the unconstrained values
// the learner's Adam moves.
//
// The learner gets this from JAX's automatic differentiation; the user chose to have it written out
// (docs/PROPOSED-FEATURES.md, "Batchcut: generate pools of variants from any file"). Each line is the
// derivative of the matching line of render(), taken in reverse order. Checked against JAX's own
// gradient of the same objective in double precision (tools/test_learner.cpp, stage 5b).
//
// Cost: one extra pass over the samples (phases recomputed, never stored), so a gradient costs
// about three renders and the memory of the control-rate state only.
#pragma once

#include "Body2.h"
#include "Loss2.h"

#include <string>

namespace ml
{
// ---------------------------------------------------------------------------------------------
// The unconstrained parameters, flat: what the optimiser moves. The layout follows engine2's
// MODE_KEYS then GLOBAL_KEYS.
// ---------------------------------------------------------------------------------------------
enum ModeKey { kLogF, kLogA, kRho, kDelta, kBeta, kEps, kGlide, kTauG, kModeKeys };
enum GlobalKey { kB1, kB3, kC, kGamma, kTauC, kG, kTauA, kCxAmt, kCxT, kCxF, kLbM, kLbRate, kLbF, kTauN, kGlobalKeys };
enum BandKey { kNoise, kDn, kDnRho, kBandKeys };

inline const char* modeKeyName (int i) { static const char* n[] = { "logf", "loga", "rho", "delta", "beta", "eps", "glide", "tau_g" }; return n[i]; }
inline const char* globalKeyName (int i) { static const char* n[] = { "b1", "b3", "C", "gamma", "tau_c", "G", "tau_a", "cx_amt", "cx_t", "cx_f", "lb_m", "lb_rate", "lb_f", "tau_n" }; return n[i]; }
inline const char* bandKeyName (int i) { static const char* n[] = { "noise", "dn", "dn_rho" }; return n[i]; }

struct Params
{
    int k = 0;
    std::vector<double> v;           // kModeKeys x k, then kGlobalKeys, then kBandKeys x kBands

    explicit Params (int modes = 0) : k (modes), v ((size_t) (kModeKeys * modes + kGlobalKeys + kBandKeys * kBands), 0.0) {}
    double* mode (int key) { return v.data() + (size_t) key * k; }
    const double* mode (int key) const { return v.data() + (size_t) key * k; }
    double& global (int key) { return v[(size_t) (kModeKeys * k + key)]; }
    double global (int key) const { return v[(size_t) (kModeKeys * k + key)]; }
    double* band (int key) { return v.data() + (size_t) (kModeKeys * k + kGlobalKeys + key * kBands); }
    const double* band (int key) const { return v.data() + (size_t) (kModeKeys * k + kGlobalKeys + key * kBands); }
    size_t size() const noexcept { return v.size(); }
};

// engine2's bounds: _sig (linear) or _lsig (log-spaced)
struct Bound { double lo, hi; bool log; };
inline Bound modeBound (int key)
{
    switch (key)
    {
        case kRho:   return { -3.0, 3.0, false };
        case kDelta: return { 0.0, 40.0, false };
        case kBeta:  return { 0.0, 0.5, false };
        case kEps:   return { -6.0, 6.0, false };
        case kGlide: return { -3.0, 3.0, false };
        case kTauG:  return { 0.003, 1.5, true };
        default:     return { 0.0, 0.0, false };       // logf, loga: exp
    }
}
inline Bound globalBound (int key)
{
    static const Bound b[] = { { 0.05, 400.0, true }, { 1e-4, 50.0, true }, { 1e-3, 3e3, true }, { -2.0, 4.0, false },
                               { 0.001, 0.5, true }, { -1.0, 3.0, false }, { 5e-5, 0.004, true }, { 1e-3, 300.0, true },
                               { 0.0, 3.0, false }, { 30.0, 20000.0, true }, { 0.0, 1.0, false }, { 0.3, 25.0, true },
                               { 40.0, 3000.0, true }, { 0.001, 1.0, true } };
    return b[key];
}

inline double boundedValue (double x, Bound b)
{
    if (b.log) return std::exp (std::log (b.lo) + (std::log (b.hi) - std::log (b.lo)) * sigmoid (x));
    return b.lo + (b.hi - b.lo) * sigmoid (x);
}
// d value / d x, given x
inline double boundedSlope (double x, Bound b)
{
    const double s = sigmoid (x), ds = s * (1.0 - s);
    if (b.log) return boundedValue (x, b) * (std::log (b.hi) - std::log (b.lo)) * ds;
    return (b.hi - b.lo) * ds;
}
// numpy's _isig / _ilsig (clipped to [1e-4, 1 - 1e-4], as in the learner)
inline double boundedInverse (double v, Bound b)
{
    double z;
    if (b.log) { const double lv = std::log (std::min (b.hi, std::max (b.lo, v))); z = (lv - std::log (b.lo)) / (std::log (b.hi) - std::log (b.lo)); }
    else z = (v - b.lo) / (b.hi - b.lo);
    z = std::min (1.0 - 1e-4, std::max (1e-4, z));
    return std::log (z / (1.0 - z));
}

// engine2.physical
inline Body physical (const Params& u)
{
    Body p;
    const int k = u.k;
    p.f.resize ((size_t) k); p.a.resize ((size_t) k); p.rho.resize ((size_t) k); p.delta.resize ((size_t) k);
    p.beta.resize ((size_t) k); p.eps.resize ((size_t) k); p.glide.resize ((size_t) k); p.tau_g.resize ((size_t) k);
    std::vector<double>* modes[] = { &p.f, &p.a, &p.rho, &p.delta, &p.beta, &p.eps, &p.glide, &p.tau_g };
    for (int key = 0; key < kModeKeys; ++key)
        for (int m = 0; m < k; ++m)
            (*modes[key])[(size_t) m] = key <= kLogA ? std::exp (u.mode (key)[m]) : boundedValue (u.mode (key)[m], modeBound (key));
    double* globals[] = { &p.b1, &p.b3, &p.C, &p.gamma, &p.tau_c, &p.G, &p.tau_a, &p.cx_amt, &p.cx_t, &p.cx_f, &p.lb_m, &p.lb_rate, &p.lb_f, &p.tau_n };
    for (int key = 0; key < kGlobalKeys; ++key) *globals[key] = boundedValue (u.global (key), globalBound (key));
    for (int b = 0; b < kBands; ++b)
    {
        p.noise[(size_t) b] = std::exp (u.band (kNoise)[b]);
        p.dn[(size_t) b] = std::exp (u.band (kDn)[b]);
        p.dn_rho[(size_t) b] = boundedValue (u.band (kDnRho)[b], { -3.0, 3.0, false });
    }
    return p;
}

// engine2.unconstrained
inline Params unconstrained (const Body& p)
{
    Params u (p.modes());
    const std::vector<double>* modes[] = { &p.f, &p.a, &p.rho, &p.delta, &p.beta, &p.eps, &p.glide, &p.tau_g };
    for (int key = 0; key < kModeKeys; ++key)
        for (int m = 0; m < u.k; ++m)
        {
            const double v = (*modes[key])[(size_t) m];
            u.mode (key)[m] = key == kLogF ? std::log (v) : key == kLogA ? std::log (std::max (v, 1e-9)) : boundedInverse (v, modeBound (key));
        }
    const double globals[] = { p.b1, p.b3, p.C, p.gamma, p.tau_c, p.G, p.tau_a, p.cx_amt, p.cx_t, p.cx_f, p.lb_m, p.lb_rate, p.lb_f, p.tau_n };
    for (int key = 0; key < kGlobalKeys; ++key) u.global (key) = boundedInverse (globals[key], globalBound (key));
    for (int b = 0; b < kBands; ++b)
    {
        u.band (kNoise)[b] = std::log (std::max (p.noise[(size_t) b], 1e-9));
        u.band (kDn)[b] = std::log (std::max (p.dn[(size_t) b], 1e-9));
        u.band (kDnRho)[b] = boundedInverse (p.dn_rho[(size_t) b], { -3.0, 3.0, false });
    }
    return u;
}

// ---------------------------------------------------------------------------------------------
// The body backwards: d loss / d physical parameter, given d loss / d sample. Laid out as a Body
// (each field holds its own gradient).
// ---------------------------------------------------------------------------------------------
inline Body bodyBackward (const Body& p, const BodyTrace& tr, int n, double fs, const float* bands, const float* body,
                          const double* phases, const double* gy)
{
    const int k = p.modes(), nf = tr.nf;
    const double dt = kHop / fs;
    Body g;
    g.f.assign ((size_t) k, 0.0); g.a.assign ((size_t) k, 0.0); g.rho.assign ((size_t) k, 0.0); g.delta.assign ((size_t) k, 0.0);
    g.beta.assign ((size_t) k, 0.0); g.eps.assign ((size_t) k, 0.0); g.glide.assign ((size_t) k, 0.0); g.tau_g.assign ((size_t) k, 0.0);
    g.b1 = g.b3 = g.C = g.gamma = g.tau_c = g.G = g.tau_a = 0.0;
    g.cx_amt = g.cx_t = g.cx_f = g.lb_m = g.lb_rate = g.lb_f = g.tau_n = 0.0;
    g.noise.fill (0.0); g.dn.fill (0.0); g.dn_rho.fill (0.0);

    // ---- sample rate, forwards in time. Each phase is a running sum of increments, so its
    // gradient reaches an increment from every later sample: dL/d inc[r] = sum_{s >= r} dL/d ph[s]
    // = total - (prefix before r). Prefixes are summed as the pass goes and the totals applied at
    // the end, so the phases are recomputed exactly as render() made them and never stored.
    std::vector<double> gAmp ((size_t) nf * k, 0.0), Ap ((size_t) nf * k, 0.0), Cs ((size_t) nf, 0.0);
    std::vector<double> ph1 ((size_t) k), ph2 ((size_t) k), P1 ((size_t) k, 0.0), P2 ((size_t) k, 0.0), SD ((size_t) k, 0.0);
    for (int m = 0; m < k; ++m) { ph1[(size_t) m] = phases[m]; ph2[(size_t) m] = phases[k + m]; }
    const auto centres = bandCentres (fs);
    std::array<double, kBands> sigB {}, cxB {}, gSigB {}, gCxB {};
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
        Cs[(size_t) i0] += 1 - fr; Cs[(size_t) i0 + 1] += fr;
        const double gs = gy[s];
        for (int m = 0; m < k; ++m)
        {
            const size_t a0 = (size_t) i0 * k + m, a1 = a0 + (size_t) k;
            const double as = tr.amp[a0] * (1 - fr) + tr.amp[a1] * fr;
            const double fs_ = tr.fpath[a0] * (1 - fr) + tr.fpath[a1] * fr;
            const double half = 0.5 * p.delta[(size_t) m];
            ph1[(size_t) m] += w * (fs_ - half);
            ph2[(size_t) m] += w * (fs_ + half);
            const double s1 = std::sin (ph1[(size_t) m]), c1 = std::cos (ph1[(size_t) m]);
            const double s2 = std::sin (ph2[(size_t) m]), c2 = std::cos (ph2[(size_t) m]);
            const double be = p.beta[(size_t) m];
            const double gas = gs * ((1 - be) * s1 + be * s2);
            gAmp[a0] += gas * (1 - fr); gAmp[a1] += gas * fr;
            g.beta[(size_t) m] += gs * as * (s2 - s1);
            const double P = P1[(size_t) m] + P2[(size_t) m];
            Ap[a0] += (1 - fr) * P; Ap[a1] += fr * P;
            SD[(size_t) m] += P2[(size_t) m] - P1[(size_t) m];
            P1[(size_t) m] += gs * as * (1 - be) * c1;
            P2[(size_t) m] += gs * as * be * c2;
        }
        // click and dense layer
        const double t = s / fs;
        const double onN = 1.0 - std::exp (-t / 0.0002), decN = std::exp (-t / p.tau_n);
        const double held = softplus ((t - p.cx_t) / 0.004) * 0.004;
        const double att = 1.0 - std::exp (-t / 0.0003);
        double click = 0.0, gHeld = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const double nb = bands[(size_t) b * n + s];
            click += p.noise[(size_t) b] * nb;
            g.noise[(size_t) b] += gs * onN * decN * nb;
            const double envB = std::exp (-sigB[(size_t) b] * t - cxB[(size_t) b] * held) * att;
            const double bb = body[(size_t) b * n + s];
            g.dn[(size_t) b] += gs * envB * bb;
            const double v = gs * p.dn[(size_t) b] * envB * bb;
            gSigB[(size_t) b] -= t * v;
            gCxB[(size_t) b] -= held * v;
            gHeld -= cxB[(size_t) b] * v;
        }
        g.tau_n += gs * click * onN * decN * t / (p.tau_n * p.tau_n);
        g.cx_t -= gHeld * sigmoid ((t - p.cx_t) / 0.004);
    }
    std::vector<double> gFpath ((size_t) nf * k);
    for (int m = 0; m < k; ++m)
    {
        const double T = P1[(size_t) m] + P2[(size_t) m];
        for (int i = 0; i < nf; ++i) gFpath[(size_t) i * k + m] = w * (T * Cs[(size_t) i] - Ap[(size_t) i * k + m]);
        g.delta[(size_t) m] = 0.5 * w * ((double) n * (P2[(size_t) m] - P1[(size_t) m]) - SD[(size_t) m]);
    }
    for (int b = 0; b < kBands; ++b)
    {
        const double cb = centres[(size_t) b] / 1000.0, er = std::exp (p.dn_rho[(size_t) b]);
        g.b1 += gSigB[(size_t) b] * er;
        g.b3 += gSigB[(size_t) b] * cb * cb * er;
        g.dn_rho[(size_t) b] += gSigB[(size_t) b] * sigB[(size_t) b];
        const double sg = sigmoid ((p.cx_f - centres[(size_t) b]) / (0.25 * p.cx_f));
        g.cx_amt += gCxB[(size_t) b] * sg;
        g.cx_f += gCxB[(size_t) b] * p.cx_amt * sg * (1 - sg) * 4.0 * centres[(size_t) b] / (p.cx_f * p.cx_f);
    }

    // ---- control rate
    std::vector<double> gDirect ((size_t) nf * k, 0.0), gCasc ((size_t) nf * k, 0.0), gSigT ((size_t) nf * k, 0.0);
    std::vector<double> gE ((size_t) nf, 0.0), gLow ((size_t) k, 0.0), gEl ((size_t) nf, 0.0), gGain ((size_t) k, 0.0);
    for (int i = 0; i < nf; ++i)
    {
        const double t = i * dt;
        const double cosr = std::cos (kTwoPi * p.lb_rate * t), sinr = std::sin (kTwoPi * p.lb_rate * t);
        double gAtt = 0.0, el = 0.0;
        for (int m = 0; m < k; ++m)
        {
            const size_t ix = (size_t) i * k + m;
            const double base = tr.direct[ix] + tr.casc[ix], at = tr.attack[(size_t) i], rk = tr.rockN[ix], ny = tr.nyq[ix];
            const double ga = gAmp[ix];
            gDirect[ix] += ga * at * rk * ny;
            gCasc[ix] = ga * at * rk * ny;
            gAtt += ga * base * rk * ny;
            const double gRock = ga * base * at * ny;
            const double gNy = ga * base * at * rk;
            double gf = gFpath[ix] - gNy * ny * (1 - ny) / (0.005 * fs);
            // rock = (1 + lb_m low cos) / (1 + lb_m low)
            const double lo = tr.low[(size_t) m], den = 1.0 + p.lb_m * lo;
            g.lb_m += gRock * lo * (cosr - 1.0) / (den * den);
            gLow[(size_t) m] += gRock * p.lb_m * (cosr - 1.0) / (den * den);
            g.lb_rate += gRock * p.lb_m * lo * (-sinr) * kTwoPi * t / den;
            // fpath = exp (log f + glide exp (-t / tau_g) + elastic)
            const double gLf = gf * tr.fpath[ix];
            const double eg = std::exp (-t / p.tau_g[(size_t) m]);
            g.f[(size_t) m] += gLf / p.f[(size_t) m];
            g.glide[(size_t) m] += gLf * eg;
            g.tau_g[(size_t) m] += gLf * p.glide[(size_t) m] * eg * t / (p.tau_g[(size_t) m] * p.tau_g[(size_t) m]);
            el += gLf;
        }
        gEl[(size_t) i] = el;
        g.tau_a -= gAtt * std::exp (-t / p.tau_a) * t / (p.tau_a * p.tau_a);
    }
    for (int m = 0; m < k; ++m)
    {
        const double lo = tr.low[(size_t) m];
        g.lb_f += gLow[(size_t) m] * lo * (1 - lo) * 5.0 * p.f[(size_t) m] / (p.lb_f * p.lb_f);
        g.f[(size_t) m] -= gLow[(size_t) m] * lo * (1 - lo) * 5.0 / p.lb_f;
    }
    // elastic = 0.5 log1p (clip (G) energy / energy[1])
    {
        const double e1 = (nf > 1 ? tr.energy[1] : tr.energy[0]) + 1e-20;
        const double Gc = std::max (-0.9, std::min (3.0, p.G));
        double gGc = 0.0, gE1 = 0.0;
        for (int i = 0; i < nf; ++i)
        {
            const double en = tr.energy[(size_t) i] / e1, inside = 1.0 + Gc * en;
            gGc += gEl[(size_t) i] * 0.5 * en / inside;
            const double gEn = gEl[(size_t) i] * 0.5 * Gc / inside;
            gE[(size_t) i] += gEn / e1;
            gE1 -= gEn * tr.energy[(size_t) i] / (e1 * e1);
        }
        gE[nf > 1 ? 1 : 0] += gE1;
        if (p.G > -0.9 && p.G < 3.0) g.G += gGc;
    }
    // the cascade's leaky integrator, backwards in time
    {
        std::vector<double> carry ((size_t) k, 0.0);
        std::vector<double> gSource ((size_t) nf, 0.0);
        for (int i = nf - 1; i >= 0; --i)
        {
            double gs = 0.0;
            for (int m = 0; m < k; ++m)
            {
                const size_t ix = (size_t) i * k + m;
                const double gacc = gCasc[ix] + carry[(size_t) m];
                const double ex = std::exp (-tr.sigT[ix] * dt);
                const double prev = i > 0 ? tr.casc[ix - (size_t) k] : 0.0;
                gSigT[ix] -= gacc * prev * ex * dt;
                gGain[(size_t) m] += gacc * tr.source[(size_t) i] * dt;
                gs += gacc * tr.gain[(size_t) m] * dt;
                carry[(size_t) m] = gacc * ex;
            }
            gSource[(size_t) i] = gs;
        }
        for (int i = 0; i < nf; ++i)
        {
            const double t = i * dt;
            const double sq = std::sqrt (tr.energy[(size_t) i] + 1e-20), ec = std::exp (-t / p.tau_c);
            gE[(size_t) i] += gSource[(size_t) i] * 0.5 / sq * (1.0 - ec);
            g.tau_c -= gSource[(size_t) i] * sq * ec * t / (p.tau_c * p.tau_c);
        }
    }
    // energy = sum direct^2; direct = a exp (-decay); decay[i] = dt sum_{j=1..i} sig_t[j]
    for (int m = 0; m < k; ++m)
    {
        double run = 0.0;
        for (int i = nf - 1; i >= 0; --i)
        {
            const size_t ix = (size_t) i * k + m;
            const double gd = gDirect[ix] + gE[(size_t) i] * 2.0 * tr.direct[ix];
            g.a[(size_t) m] += gd * tr.direct[ix] / p.a[(size_t) m];
            run -= gd * tr.direct[ix];                      // d/d decay[i], summed over i >= this one
            if (i >= 1) gSigT[ix] += run * dt;
        }
    }
    // sig_t = sigma + cx_amt sigmoid ((t - cx_t) / 4 ms) cxw
    for (int i = 0; i < nf; ++i)
    {
        const double t = i * dt;
        const double so = sigmoid ((t - p.cx_t) / 0.004), cxOn = p.cx_amt * so;
        double gOn = 0.0;
        for (int m = 0; m < k; ++m)
        {
            const size_t ix = (size_t) i * k + m;
            const double cw = sigmoid ((p.cx_f - p.f[(size_t) m]) / (0.25 * p.cx_f));
            gOn += gSigT[ix] * cw;
            const double gCw = gSigT[ix] * cxOn * cw * (1 - cw);
            g.cx_f += gCw * 4.0 * p.f[(size_t) m] / (p.cx_f * p.cx_f);
            g.f[(size_t) m] -= gCw * 4.0 / p.cx_f;
        }
        g.cx_amt += gOn * so;
        g.cx_t -= gOn * p.cx_amt * so * (1 - so) / 0.004;
    }
    for (int m = 0; m < k; ++m)
    {
        double gSigma = 0.0;
        for (int i = 0; i < nf; ++i) gSigma += gSigT[(size_t) i * k + m];
        const double fk = p.f[(size_t) m] / 1000.0, er = std::exp (p.rho[(size_t) m]);
        g.b1 += gSigma * er;
        g.b3 += gSigma * fk * fk * er;
        g.rho[(size_t) m] += gSigma * tr.sigma[(size_t) m];
        g.f[(size_t) m] += gSigma * p.b3 * 2.0 * fk / 1000.0 * er;
        // gain = C (f / 1 kHz)^gamma exp (eps)
        const double gn = gGain[(size_t) m] * tr.gain[(size_t) m];
        g.C += gn / p.C;
        g.gamma += gn * std::log (fk);
        g.eps[(size_t) m] += gn;
        g.f[(size_t) m] += gn * p.gamma / p.f[(size_t) m];
    }
    return g;
}

// d loss / d unconstrained, from d loss / d physical (engine2.physical, backwards)
inline Params physicalBackward (const Params& u, const Body& p, const Body& g)
{
    Params gu (u.k);
    const std::vector<double>* pv[] = { &p.f, &p.a };
    const std::vector<double>* gv[] = { &g.f, &g.a, &g.rho, &g.delta, &g.beta, &g.eps, &g.glide, &g.tau_g };
    for (int key = 0; key < kModeKeys; ++key)
        for (int m = 0; m < u.k; ++m)
            gu.mode (key)[m] = (*gv[key])[(size_t) m] * (key <= kLogA ? (*pv[key])[(size_t) m] : boundedSlope (u.mode (key)[m], modeBound (key)));
    const double gg[] = { g.b1, g.b3, g.C, g.gamma, g.tau_c, g.G, g.tau_a, g.cx_amt, g.cx_t, g.cx_f, g.lb_m, g.lb_rate, g.lb_f, g.tau_n };
    for (int key = 0; key < kGlobalKeys; ++key) gu.global (key) = gg[key] * boundedSlope (u.global (key), globalBound (key));
    for (int b = 0; b < kBands; ++b)
    {
        gu.band (kNoise)[b] = g.noise[(size_t) b] * p.noise[(size_t) b];
        gu.band (kDn)[b] = g.dn[(size_t) b] * p.dn[(size_t) b];
        gu.band (kDnRho)[b] = g.dn_rho[(size_t) b] * boundedSlope (u.band (kDnRho)[b], { -3.0, 3.0, false });
    }
    return gu;
}

// The learner's objective at u and its gradient: render physical (u), score it, and back again.
struct Scene
{
    int n = 0;
    double fs = 48000.0;
    const float* bands = nullptr;      // kBands x n, the click's noise
    const float* body = nullptr;       // kBands x n, the dense layer's noise
    const double* phases = nullptr;    // 2 x k
};

inline double objective (const Params& u, const Loss2& loss, const Scene& sc, Params* gu = nullptr, std::vector<double>* yOut = nullptr)
{
    const Body p = physical (u);
    BodyTrace tr;
    std::vector<double> y ((size_t) sc.n);
    render (p, sc.n, sc.fs, sc.bands, sc.body, sc.phases, y.data(), gu != nullptr ? &tr : nullptr);
    double v;
    if (gu != nullptr)
    {
        std::vector<double> gy;
        v = loss.valueAndGrad (y, gy);
        const Body gp = bodyBackward (p, tr, sc.n, sc.fs, sc.bands, sc.body, sc.phases, gy.data());
        *gu = physicalBackward (u, p, gp);
    }
    else
        v = loss.value (y.data());
    if (yOut != nullptr) *yOut = std::move (y);
    return v;
}

} // namespace ml
