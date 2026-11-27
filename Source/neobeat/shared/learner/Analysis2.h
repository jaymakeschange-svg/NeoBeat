// The MaterialLab learner's analysis (partials.py) and starting point (fit2.initial_params), in C++.
//
// Part of the learner port (Body2.h). What a recording becomes before any learning: every partial
// tracked (Spectral Modeling Synthesis style, McAulay-Quatieri continuation), each partial's
// behaviour (decays, beats, swells, sweeps), long sweeps the frame tracker misses, a sudden contact
// stop, and the noise the partials do not explain, by band - then the starting body built from all
// of it. Function for function with the learner, including its random phases (NumpyRandom.h), so the
// starting body is the learner's. The scipy pieces it relies on are written out here: the windows,
// stft / istft (legacy, 'spectrum' scaling), a 4th-order Butterworth band-pass and sosfiltfilt.
#pragma once

#include "Body2.h"
#include "Loss2.h"
#include "NumpyRandom.h"
#include <algorithm>
#include <complex>
#include <numeric>
#include <array>
#include <optional>
#include <vector>

namespace ml
{
namespace an
{
constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

// ------------------------------------------------------------------ windows
inline std::vector<double> blackmanHarrisPeriodic (int n)
{
    std::vector<double> w ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double x = 2.0 * kPi * i / n;
        w[(size_t) i] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
    }
    return w;
}
inline std::vector<double> hannPeriodic (int n)
{
    std::vector<double> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
    return w;
}
inline std::vector<double> hanningSymmetric (int n)          // np.hanning
{
    std::vector<double> w ((size_t) n);
    if (n == 1) { w[0] = 1.0; return w; }
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
    return w;
}

// rfft of x (length m) zero-padded to nfft (a power of two)
inline std::vector<cd> rfftPow2 (const double* x, int m, int nfft)
{
    std::vector<cd> buf ((size_t) nfft);
    for (int i = 0; i < std::min (m, nfft); ++i) buf[(size_t) i] = x[i];
    fft (buf);
    buf.resize ((size_t) nfft / 2 + 1);
    return buf;
}
// |rfft| of any length, by the definition (only short sequences use this)
inline std::vector<double> rfftMagAny (const std::vector<double>& x)
{
    const int n = (int) x.size(), bins = n / 2 + 1;
    std::vector<double> m ((size_t) bins);
    for (int k = 0; k < bins; ++k)
    {
        cd s = 0;
        for (int i = 0; i < n; ++i) s += x[(size_t) i] * std::polar (1.0, -2.0 * kPi * (double) k * i / n);
        m[(size_t) k] = std::abs (s);
    }
    return m;
}

// ------------------------------------------------------------------ tracks
struct Track { int start = 0; std::vector<double> f, a; int misses = 0; int length() const { return (int) f.size(); } };

struct Analysis
{
    int fs = 48000, hop = 128, nWin = 2048;
    std::vector<Track> tracks;
    int resBins = 0, resFrames = 0;
    std::vector<double> residual;      // [bins][frames], as residual_env
};

inline void peaks (const std::vector<double>& db, double df, double floorDb, double fmin, double fmax,
                   std::vector<double>& pf, std::vector<double>& pa)
{
    pf.clear(); pa.clear();
    for (size_t k = 1; k + 1 < db.size(); ++k)
    {
        if (! (db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > floorDb)) continue;
        const double fk = k * df;
        if (fk < fmin || fk > fmax) continue;
        const double y0 = db[k - 1], y1 = db[k], y2 = db[k + 1];
        const double den = y0 - 2 * y1 + y2;
        const double off = den != 0 ? 0.5 * (y0 - y2) / den : 0.0;
        pf.push_back (fk + off * df);
        pa.push_back (y1 - 0.25 * (y0 - y2) * off);
    }
}

// ---- scipy.signal.stft (legacy): hann (periodic), nperseg 1024, noverlap 768, boundary zeros,
// padded, 'spectrum' scaling (1 / window sum). [bins][frames].
inline std::vector<cd> scipyStft (const std::vector<double>& x, int& bins, int& frames, int nper = 1024, int nover = 768)
{
    const int step = nper - nover;
    const auto win = hannPeriodic (nper);
    const double scale = 1.0 / std::accumulate (win.begin(), win.end(), 0.0);
    std::vector<double> xp ((size_t) nper / 2, 0.0);
    xp.insert (xp.end(), x.begin(), x.end());
    xp.insert (xp.end(), (size_t) nper / 2, 0.0);
    const int nadd = (int) (((-(long) ((long) xp.size() - nper) % step) + step) % step) % nper;
    xp.insert (xp.end(), (size_t) nadd, 0.0);
    frames = 1 + ((int) xp.size() - nper) / step;
    bins = nper / 2 + 1;
    std::vector<cd> out ((size_t) bins * frames);
    std::vector<double> seg ((size_t) nper);
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < nper; ++i) seg[(size_t) i] = xp[(size_t) (f * step + i)] * win[(size_t) i];
        const auto s = rfftPow2 (seg.data(), nper, nper);
        for (int b = 0; b < bins; ++b) out[(size_t) b * frames + f] = s[(size_t) b] * scale;
    }
    return out;
}

// ---- scipy.signal.istft (legacy) of [bins][frames], the same settings
inline std::vector<double> scipyIstft (const std::vector<cd>& Z, int bins, int frames, int nper = 1024, int nover = 768)
{
    const int step = nper - nover;
    const auto win = hannPeriodic (nper);
    const double wsum = std::accumulate (win.begin(), win.end(), 0.0);
    const int outLen = nper + (frames - 1) * step;
    std::vector<double> y ((size_t) outLen, 0.0), norm ((size_t) outLen, 0.0);
    std::vector<cd> buf ((size_t) nper);
    for (int f = 0; f < frames; ++f)
    {
        // irfft: the imaginary parts of DC and Nyquist are ignored, as numpy does
        for (int b = 0; b < bins; ++b) buf[(size_t) b] = Z[(size_t) b * frames + f];
        buf[0] = cd (buf[0].real(), 0.0);
        buf[(size_t) nper / 2] = cd (buf[(size_t) nper / 2].real(), 0.0);
        for (int b = 1; b < nper / 2; ++b) buf[(size_t) (nper - b)] = std::conj (buf[(size_t) b]);
        fft (buf, true);
        for (int i = 0; i < nper; ++i)
        {
            const double v = buf[(size_t) i].real() / nper * wsum;
            y[(size_t) (f * step + i)] += v * win[(size_t) i];
            norm[(size_t) (f * step + i)] += win[(size_t) i] * win[(size_t) i];
        }
    }
    const int h = nper / 2;
    std::vector<double> out;
    for (int i = h; i < outLen - h; ++i) out.push_back (y[(size_t) i] / (norm[(size_t) i] > 1e-10 ? norm[(size_t) i] : 1.0));
    return out;
}

inline std::vector<double> synthesizePartials (const std::vector<Track>& tracks, int fs, int hop, int n, uint32_t seed = 3)
{
    NumpyRandom rng (seed);
    std::vector<double> out ((size_t) n + 2 * hop, 0.0);
    for (const auto& tr : tracks)
    {
        const int len = tr.length();
        const int t0 = tr.start * hop;
        const int ns = (len - 1) * hop + 1;
        std::vector<double> fi ((size_t) ns), ai ((size_t) ns);
        for (int s = 0; s < ns; ++s)
        {
            const double idx = (double) s / hop;
            const int i0 = std::min ((int) idx, len - 1), i1 = std::min (i0 + 1, len - 1);
            const double fr = idx - i0;
            fi[(size_t) s] = tr.f[(size_t) i0] + (tr.f[(size_t) i1] - tr.f[(size_t) i0]) * fr;
            ai[(size_t) s] = tr.a[(size_t) i0] + (tr.a[(size_t) i1] - tr.a[(size_t) i0]) * fr;
        }
        const int ramp = std::min (hop, ns / 2);
        if (ramp > 1)
            for (int i = 0; i < ramp; ++i)
            {
                const double up = (double) i / (ramp - 1);
                ai[(size_t) i] *= up;
                ai[(size_t) (ns - ramp + i)] *= 1.0 - up;
            }
        const double ph0 = rng.uniform (0.0, 2.0 * kPi);
        double acc = 0.0;
        const int end = std::min (t0 + ns, (int) out.size());
        for (int s = 0; s < ns; ++s)
        {
            acc += fi[(size_t) s];
            if (t0 + s < end) out[(size_t) (t0 + s)] += ai[(size_t) s] * std::sin (ph0 + 2.0 * kPi * acc / fs);
        }
    }
    out.resize ((size_t) n);
    for (auto& v : out) v = (double) (float) v;          // the learner returns float32
    return out;
}

inline Analysis analyze (const std::vector<double>& x, int fs, int nWin = 2048, int nFft = 8192, int hop = 128,
                         double rangeDb = 75.0, double fmin = 30.0, double fmax = 18000.0, int minFrames = 12, int maxMisses = 3)
{
    Analysis A; A.fs = fs; A.hop = hop; A.nWin = nWin;
    const auto win = blackmanHarrisPeriodic (nWin);
    const double norm = 2.0 / std::accumulate (win.begin(), win.end(), 0.0);
    std::vector<double> xp ((size_t) nWin / 2, 0.0);
    xp.insert (xp.end(), x.begin(), x.end());
    xp.insert (xp.end(), (size_t) nWin, 0.0);
    const int nFrames = 1 + ((int) xp.size() - nWin) / hop;
    const int bins = nFft / 2 + 1;
    const double df = (double) fs / nFft;
    std::vector<std::vector<double>> db ((size_t) nFrames);
    double globalTop = -1e300;
    std::vector<double> seg ((size_t) nWin);
    for (int i = 0; i < nFrames; ++i)
    {
        for (int j = 0; j < nWin; ++j) seg[(size_t) j] = xp[(size_t) (i * hop + j)] * win[(size_t) j];
        const auto s = rfftPow2 (seg.data(), nWin, nFft);
        auto& d = db[(size_t) i]; d.resize ((size_t) bins);
        for (int b = 0; b < bins; ++b) { d[(size_t) b] = 20.0 * std::log10 (std::abs (s[(size_t) b]) * norm + 1e-12); globalTop = std::max (globalTop, d[(size_t) b]); }
    }

    std::vector<Track> active, done;
    std::vector<double> pf, pa;
    for (int i = 0; i < nFrames; ++i)
    {
        const auto& d = db[(size_t) i];
        const double fl = std::max (globalTop - rangeDb, *std::max_element (d.begin(), d.end()) - 60.0);
        peaks (d, df, fl, fmin, fmax, pf, pa);
        // np.argsort(-pa): loudest first
        std::vector<int> order ((size_t) pf.size());
        std::iota (order.begin(), order.end(), 0);
        std::stable_sort (order.begin(), order.end(), [&] (int u, int v) { return -pa[(size_t) u] < -pa[(size_t) v]; });
        std::vector<double> f2, a2;
        for (int o : order) { f2.push_back (pf[(size_t) o]); a2.push_back (pa[(size_t) o]); }
        std::vector<bool> claimed (f2.size(), false);
        // loud tracks choose first (Python's sort is stable)
        std::stable_sort (active.begin(), active.end(), [] (const Track& u, const Track& v) { return -u.a.back() < -v.a.back(); });
        for (auto& tr : active)
        {
            const double last = tr.f.back();
            const double tol = std::max (15.0, 0.035 * last);
            int best = -1; double bd = tol;
            for (size_t j = 0; j < f2.size(); ++j)
            {
                if (claimed[j]) continue;
                const double dd = std::fabs (f2[j] - last);
                if (dd < bd) { best = (int) j; bd = dd; }
            }
            if (best >= 0)
            {
                claimed[(size_t) best] = true;
                tr.f.push_back (f2[(size_t) best]);
                tr.a.push_back (std::pow (10.0, a2[(size_t) best] / 20.0));
                tr.misses = 0;
            }
            else { ++tr.misses; tr.f.push_back (last); tr.a.push_back (0.0); }
        }
        std::vector<Track> still;
        for (auto& tr : active)
        {
            if (tr.misses > maxMisses)
            {
                for (int m = 0; m < tr.misses; ++m) { tr.f.pop_back(); tr.a.pop_back(); }
                if (tr.length() >= minFrames) done.push_back (tr);
            }
            else still.push_back (tr);
        }
        active = std::move (still);
        for (size_t j = 0; j < f2.size(); ++j)
            if (! claimed[j]) { Track t; t.start = i; t.f.push_back (f2[j]); t.a.push_back (std::pow (10.0, a2[j] / 20.0)); active.push_back (t); }
    }
    for (auto& tr : active)
    {
        while (! tr.a.empty() && tr.a.back() == 0.0) { tr.a.pop_back(); tr.f.pop_back(); }
        if (tr.length() >= minFrames) done.push_back (tr);
    }
    A.tracks = done;

    // the noise part: what the partials do not explain, smoothed across frequency
    const auto det = synthesizePartials (done, fs, hop, (int) x.size());
    int b1 = 0, f1 = 0, b2 = 0, f2n = 0;
    const auto X = scipyStft (x, b1, f1), D = scipyStft (det, b2, f2n);
    A.resBins = b1; A.resFrames = f1;
    std::vector<double> res ((size_t) b1 * f1);
    for (size_t j = 0; j < res.size(); ++j) res[j] = std::max (std::abs (X[j]) - std::abs (D[j]), 0.0);
    A.residual.assign (res.size(), 0.0);
    for (int f = 0; f < f1; ++f)
        for (int b = 0; b < b1; ++b)
        {
            double s = 0.0;
            for (int k = -4; k <= 4; ++k) { const int bb = b + k; if (bb >= 0 && bb < b1) s += res[(size_t) bb * f1 + f]; }
            A.residual[(size_t) b * f1 + f] = s / 9.0;
        }
    return A;
}

inline std::vector<double> synthesizeNoise (const Analysis& A, int n, uint32_t seed = 5)
{
    NumpyRandom rng (seed);
    std::vector<cd> Z (A.residual.size());
    for (size_t j = 0; j < Z.size(); ++j) Z[j] = A.residual[j] * std::polar (1.0, rng.uniform (0.0, 2.0 * kPi));
    auto y = scipyIstft (Z, A.resBins, A.resFrames);
    y.resize ((size_t) n, 0.0);
    for (auto& v : y) v = (double) (float) v;
    return y;
}

// ------------------------------------------------------------------ each partial's behaviour
struct Desc
{
    bool valid = false;
    double freqHz = 0, bornMs = 0, peakMs = 0, riseDb = 0, glideCents = 0, beatHz = 0, beatDepthDb = 0, energy = 0;
    std::optional<double> decayDbPerS;
    enum Kind { Decays, Beats, Swells, Sweeps } kind = Decays;
    std::vector<double> t, f, db;
};

// least-squares line: slope, intercept
inline void polyfit1 (const std::vector<double>& x, const std::vector<double>& y, double& slope, double& icpt)
{
    const double n = (double) x.size();
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < x.size(); ++i) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    icpt = (sy - slope * sx) / n;
}

// Python's round(v, n): round half to even at n decimals, as the learner's dict stores them
inline double pyRound (double v, int nd)
{
    const double p = std::pow (10.0, nd);
    return std::nearbyint (v * p) / p;
}

inline Desc describeTrack (const Track& tr, int hop, int fs, int win = 2048)
{
    Desc D;
    const int n = tr.length();
    if (n < 12) return D;
    const double amax = *std::max_element (tr.a.begin(), tr.a.end());
    if (amax <= 0) return D;
    std::vector<double> db ((size_t) n), t ((size_t) n);
    for (int i = 0; i < n; ++i) { db[(size_t) i] = 20.0 * std::log10 (tr.a[(size_t) i] + 1e-9); t[(size_t) i] = (double) (tr.start + i) * hop / fs; }
    const double top = *std::max_element (db.begin(), db.end());
    int liveCount = 0;
    for (double v : db) liveCount += v > top - 40 ? 1 : 0;
    if (liveCount < 10) return D;
    const int ip = (int) (std::max_element (db.begin(), db.end()) - db.begin());
    std::vector<double> tl, dl;
    for (int i = ip; i < n; ++i) if (db[(size_t) i] > top - 40) { tl.push_back (t[(size_t) i]); dl.push_back (db[(size_t) i]); }
    double amRate = 0.0, amDepth = 0.0;
    if (tl.size() >= 8)
    {
        double slope, icpt; polyfit1 (tl, dl, slope, icpt);
        D.decayDbPerS = -slope;
        std::vector<double> resid (tl.size());
        for (size_t i = 0; i < tl.size(); ++i) resid[i] = dl[i] - (slope * tl[i] + icpt);
        if (resid.size() >= 16)
        {
            const auto w = hanningSymmetric ((int) resid.size());
            std::vector<double> rw (resid.size());
            for (size_t i = 0; i < rw.size(); ++i) rw[i] = resid[i] * w[i];
            const auto spec = rfftMagAny (rw);
            const double dfq = 1.0 / (resid.size() * ((double) hop / fs));
            std::vector<double> okSpec, okFq;
            for (size_t k = 0; k < spec.size(); ++k) { const double fq = k * dfq; if (fq >= 2 && fq <= 60) { okSpec.push_back (spec[k]); okFq.push_back (fq); } }
            if (! okSpec.empty())
            {
                const size_t kk = (size_t) (std::max_element (okSpec.begin(), okSpec.end()) - okSpec.begin());
                std::vector<double> sorted = okSpec;
                std::sort (sorted.begin(), sorted.end());
                const size_t m = sorted.size();
                const double median = m % 2 ? sorted[m / 2] : 0.5 * (sorted[m / 2 - 1] + sorted[m / 2]);
                const double prom = okSpec[kk] / (median + 1e-9);
                double mean = 0; for (double r : resid) mean += r; mean /= (double) resid.size();
                double var = 0; for (double r : resid) var += (r - mean) * (r - mean); var /= (double) resid.size();
                amDepth = 2.0 * std::sqrt (2.0) * std::sqrt (var);
                amRate = prom > 3 ? okFq[kk] : 0.0;
            }
        }
    }
    std::vector<double> fl, wl;
    for (int i = 0; i < n; ++i) if (db[(size_t) i] > top - 40) { fl.push_back (tr.f[(size_t) i]); wl.push_back (tr.a[(size_t) i] * tr.a[(size_t) i]); }
    const double glide = fl.size() > 2 ? 1200.0 * std::log2 (fl.back() / fl.front()) : 0.0;
    double sw = 0, sfw = 0; for (size_t i = 0; i < fl.size(); ++i) { sw += wl[i]; sfw += fl[i] * wl[i]; }
    const double bornMs = tr.start * (double) hop / fs * 1000.0, peakMs = t[(size_t) ip] * 1000.0;
    const double riseDb = top - db[0];
    double energy = 0; for (double v : tr.a) energy += v * v;
    if (std::fabs (glide) > 100) D.kind = Desc::Sweeps;
    else if (peakMs - bornMs > std::max (30.0, 0.75 * win / (double) fs * 1000.0 + 15) && riseDb > 6) D.kind = Desc::Swells;
    else if (amDepth >= 3 && amRate > 0) D.kind = Desc::Beats;
    else D.kind = Desc::Decays;
    // the learner rounds what it stores; initial_params reads the rounded values
    D.valid = true;
    D.freqHz = pyRound (sfw / sw, 1); D.bornMs = pyRound (bornMs, 1); D.peakMs = pyRound (peakMs, 1);
    D.riseDb = pyRound (riseDb, 1); D.glideCents = pyRound (glide, 0); D.beatHz = pyRound (amRate, 2);
    D.beatDepthDb = pyRound (amDepth, 1); D.energy = energy;
    if (D.decayDbPerS) D.decayDbPerS = pyRound (*D.decayDbPerS, 1);
    for (int i = 0; i < n; ++i) { D.t.push_back (pyRound (t[(size_t) i], 4)); D.f.push_back (pyRound (tr.f[(size_t) i], 2)); D.db.push_back (pyRound (db[(size_t) i], 2)); }
    return D;
}

// ------------------------------------------------------------------ events the frame tracker misses
struct Spectrogram { int frames = 0, bins = 0; std::vector<double> db, fq, t; double at (int i, int k) const { return db[(size_t) i * bins + k]; } };

// partials._stft_db: hann (periodic), padded like analyze, 20 log10 (|rfft| + 1e-12)
inline Spectrogram stftDb (const std::vector<double>& x, int fs, int nWin, int nFft, int hop)
{
    Spectrogram S;
    const auto win = hannPeriodic (nWin);
    std::vector<double> xp ((size_t) nWin / 2, 0.0);
    xp.insert (xp.end(), x.begin(), x.end());
    xp.insert (xp.end(), (size_t) nWin, 0.0);
    S.frames = 1 + ((int) xp.size() - nWin) / hop; S.bins = nFft / 2 + 1;
    S.db.resize ((size_t) S.frames * S.bins);
    std::vector<double> seg ((size_t) nWin);
    for (int i = 0; i < S.frames; ++i)
    {
        for (int j = 0; j < nWin; ++j) seg[(size_t) j] = xp[(size_t) (i * hop + j)] * win[(size_t) j];
        const auto s = rfftPow2 (seg.data(), nWin, nFft);
        for (int b = 0; b < S.bins; ++b) S.db[(size_t) i * S.bins + b] = 20.0 * std::log10 (std::abs (s[(size_t) b]) + 1e-12);
    }
    for (int b = 0; b < S.bins; ++b) S.fq.push_back ((double) b * fs / nFft);
    for (int i = 0; i < S.frames; ++i) S.t.push_back ((double) i * hop / fs);
    return S;
}

inline double median (std::vector<double> v)
{
    std::sort (v.begin(), v.end());
    const size_t m = v.size();
    return m == 0 ? 0.0 : (m % 2 ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]));
}

struct Sweep { double fEnd = 0, fStart = 0, glideCents = 0, glideLog = 0, tStart = 0, tauG = 0, levelDb = 0; };

inline std::vector<Sweep> findSweeps (const std::vector<double>& x, int fs, double minGlideCents = 300.0, int maxSweeps = 4)
{
    const auto S = stftDb (x, fs, 1024, 8192, 96);
    double top = -1e300; for (double v : S.db) top = std::max (top, v);
    const int n = S.frames;
    const int l0 = (int) (n * 0.35), l1 = (int) (n * 0.75);
    std::vector<double> lateSpec ((size_t) S.bins, -1e300);
    for (int i = l0; i < l1; ++i) for (int b = 0; b < S.bins; ++b) lateSpec[(size_t) b] = std::max (lateSpec[(size_t) b], S.at (i, b));
    std::vector<int> idx;
    for (int b = 0; b < S.bins; ++b) if (S.fq[(size_t) b] > 80 && S.fq[(size_t) b] < 0.4 * fs) idx.push_back (b);
    std::vector<int> cand;
    for (size_t j = 1; j + 1 < idx.size(); ++j)
    {
        const int c = idx[j];
        if (lateSpec[(size_t) c] > lateSpec[(size_t) idx[j - 1]] && lateSpec[(size_t) c] >= lateSpec[(size_t) idx[j + 1]] && lateSpec[(size_t) c] > top - 45) cand.push_back (c);
    }
    std::stable_sort (cand.begin(), cand.end(), [&] (int u, int v) { return -lateSpec[(size_t) u] < -lateSpec[(size_t) v]; });
    if (cand.size() > 12) cand.resize (12);
    std::vector<Sweep> out;
    for (int k0 : cand)
    {
        int frame = l0; double best = -1e300;
        for (int i = l0; i < l1; ++i) if (S.at (i, k0) > best) { best = S.at (i, k0); frame = i; }
        int k = k0;
        struct P { double t, f, d; };
        std::vector<P> path;
        for (int i = frame; i >= 0; --i)
        {
            const int lo = std::max (1, (int) (k / 1.09));
            const int hi = std::min (S.bins - 1, (int) (k * 1.09) + 2);
            int kb = lo; double vb = -1e300;
            for (int kk = lo; kk < hi; ++kk) if (S.at (i, kk) > vb) { vb = S.at (i, kk); kb = kk; }
            k = kb;
            if (S.at (i, k) < top - 55) break;
            path.push_back ({ S.t[(size_t) i], S.fq[(size_t) k], S.at (i, k) });
        }
        if (path.size() < 10) continue;
        std::reverse (path.begin(), path.end());
        std::vector<double> tt, ff;
        for (auto& q : path) { tt.push_back (q.t); ff.push_back (q.f); }
        const double fEnd = median (std::vector<double> (ff.end() - 10, ff.end()));
        const double fStart = median (std::vector<double> (ff.begin(), ff.begin() + 4));
        const double glide = 1200.0 * std::log2 (fStart / fEnd);
        if (std::fabs (glide) < minGlideCents) continue;
        const double g0 = std::fabs (std::log (ff[0] / fEnd));
        double tau = 0.05;
        for (size_t i = 0; i < ff.size(); ++i) if (std::fabs (std::log (ff[i] / fEnd)) <= g0 / std::exp (1.0)) { tau = tt[i] - tt[0]; break; }
        bool dup = false;
        for (auto& o : out) dup = dup || std::fabs (std::log (o.fEnd / fEnd)) < 0.03;
        if (dup) continue;
        if (tau < 0.02 || fEnd < 250 || path[0].d < top - 35) continue;
        Sweep sw; sw.fEnd = fEnd; sw.fStart = fStart; sw.glideCents = glide; sw.glideLog = std::log (fStart / fEnd);
        sw.tStart = tt[0]; sw.tauG = std::min (1.2, std::max (0.004, tau)); sw.levelDb = lateSpec[(size_t) k0] - top;
        out.push_back (sw);
        if ((int) out.size() >= maxSweeps) break;
    }
    return out;
}

struct ContactStop { double t = 0, fCut = 0, rateDbPerS = 0; };

inline std::optional<ContactStop> findContactStop (const std::vector<double>& x, int fs)
{
    const auto S = stftDb (x, fs, 1024, 4096, 240);
    const double dur = (double) x.size() / fs;
    std::optional<ContactStop> found;
    for (double fc : { 150.0, 300.0, 600.0, 1200.0, 2400.0, 4800.0 })
    {
        std::vector<double> e ((size_t) S.frames);
        for (int i = 0; i < S.frames; ++i)
        {
            double s = 0.0;
            for (int b = 0; b < S.bins; ++b) if (S.fq[(size_t) b] > 30 && S.fq[(size_t) b] < fc) s += std::pow (10.0, S.at (i, b) / 10.0);
            e[(size_t) i] = 10.0 * std::log10 (s + 1e-20);
        }
        const int L = (int) e.size();
        const int pk = (int) (std::max_element (e.begin(), e.end()) - e.begin());
        const int step = std::max (1, (int) (0.03 * fs / 240));
        if (L < pk + 3 * step) continue;
        const int last = (int) (std::lower_bound (S.t.begin(), S.t.end(), dur - 0.08) - S.t.begin());
        const int len = std::max (0, std::min (L - pk - 2 * step, last - pk - 2 * step));
        if (len == 0) continue;
        int j = 0; double dj = -1e300;
        for (int q = 0; q < len; ++q) { const double d = e[(size_t) (pk + step + q)] - e[(size_t) (pk + 2 * step + q)]; if (d > dj) { dj = d; j = q; } }
        const double when = S.t[(size_t) (pk + step + j + step / 2)];
        const int nb = step + j;           // e[pk : pk + step + j]
        double typical = 0.0;
        if (nb > 1) typical = (e[(size_t) pk] - e[(size_t) (pk + nb - 1)]) / std::max (S.t[(size_t) (pk + step + j)] - S.t[(size_t) pk], 1e-3);
        const double rate = dj / 0.03;
        const int iBefore = pk + step + j;
        const int iAfter = std::min (L - 1, iBefore + 4 * step);
        const bool staysDown = e[(size_t) iAfter] < e[(size_t) iBefore] - 15;
        if (dj > 15 && rate > 3 * std::max (typical, 20.0) && when < dur - 0.05 && e[(size_t) iBefore] > e[(size_t) pk] - 40 && staysDown)
            found = ContactStop { when, fc, rate };
        else if (found) break;
    }
    return found;
}

// ------------------------------------------------------------------ the noise layer, by band
// scipy.signal.butter (4, [lo, hi], 'band', fs, output='sos'): four sections whose product is the
// same filter (scipy pairs zeros and poles differently; the filter is the same).
struct Section { double b0, b1, b2, a1, a2; };

inline std::vector<Section> butterBand4 (double lo, double hi, double fs)
{
    const double fsi = 2.0;                       // scipy works with Wn normalised to Nyquist, fs = 2
    const double w1 = 2.0 * fsi * std::tan (kPi * (2.0 * lo / fs) / fsi), w2 = 2.0 * fsi * std::tan (kPi * (2.0 * hi / fs) / fsi);
    const double bw = w2 - w1, wo = std::sqrt (w1 * w2);
    std::vector<cd> p;
    for (int m : { -3, -1, 1, 3 }) p.push_back (-std::exp (cd (0.0, kPi * m / 8.0)));
    std::vector<cd> pbp;
    for (auto q : p) { const cd pl = q * bw / 2.0; pbp.push_back (pl + std::sqrt (pl * pl - wo * wo)); }
    for (auto q : p) { const cd pl = q * bw / 2.0; pbp.push_back (pl - std::sqrt (pl * pl - wo * wo)); }
    const double kbp = std::pow (bw, 4);
    const double fs2 = 2.0 * fsi;
    std::vector<cd> pz; cd den = 1.0;
    for (auto q : pbp) { pz.push_back ((fs2 + q) / (fs2 - q)); den *= (fs2 - q); }
    const double kz = kbp * std::real (cd (std::pow (fs2, 4)) / den);    // zeros at the origin map to +1, the rest to -1
    // pair conjugates: keep those with positive imaginary part
    std::vector<cd> up;
    for (auto q : pz) if (q.imag() > 0) up.push_back (q);
    std::sort (up.begin(), up.end(), [] (cd a, cd b) { return std::abs (a) < std::abs (b); });
    std::vector<Section> out;
    for (size_t i = 0; i < up.size(); ++i)
        out.push_back ({ i == 0 ? kz : 1.0, 0.0, i == 0 ? -kz : -1.0, -2.0 * up[i].real(), std::norm (up[i]) });
    return out;
}

inline std::vector<double> sosfilt (const std::vector<Section>& sos, const std::vector<double>& x, std::vector<std::array<double, 2>> zi)
{
    std::vector<double> y = x;
    for (size_t s = 0; s < sos.size(); ++s)
    {
        const auto& q = sos[s];
        double z0 = zi[s][0], z1 = zi[s][1];
        for (auto& v : y)
        {
            const double in = v;
            const double out = q.b0 * in + z0;
            z0 = q.b1 * in - q.a1 * out + z1;
            z1 = q.b2 * in - q.a2 * out;
            v = out;
        }
    }
    return y;
}

// scipy.signal.sosfiltfilt with its defaults: odd extension of 3 x (2 x sections + 1) samples,
// steady-state initial conditions (sosfilt_zi) scaled by the first sample of each pass
inline std::vector<double> sosfiltfilt (const std::vector<Section>& sos, const std::vector<double>& x)
{
    const int pad = 3 * (2 * (int) sos.size() + 1);
    const int n = (int) x.size();
    std::vector<double> ext;
    for (int i = pad; i >= 1; --i) ext.push_back (2.0 * x[0] - x[(size_t) i]);
    ext.insert (ext.end(), x.begin(), x.end());
    for (int i = 1; i <= pad; ++i) ext.push_back (2.0 * x[(size_t) (n - 1)] - x[(size_t) (n - 1 - i)]);
    std::vector<std::array<double, 2>> zi;
    double scale = 1.0;
    for (const auto& q : sos)
    {
        // lfilter_zi for a 2nd-order section: (I - companion(a)^T) zi = b[1:] - a[1:] b0
        const double B0 = q.b1 - q.a1 * q.b0, B1 = q.b2 - q.a2 * q.b0;
        const double m00 = 1.0 + q.a1, m01 = -1.0, m10 = q.a2, m11 = 1.0;
        const double det = m00 * m11 - m01 * m10;
        zi.push_back ({ scale * (B0 * m11 - m01 * B1) / det, scale * (m00 * B1 - m10 * B0) / det });
        scale *= (q.b0 + q.b1 + q.b2) / (1.0 + q.a1 + q.a2);
    }
    auto withX0 = [&] (double x0) { auto z = zi; for (auto& r : z) { r[0] *= x0; r[1] *= x0; } return z; };
    auto y = sosfilt (sos, ext, withX0 (ext.front()));
    std::reverse (y.begin(), y.end());
    y = sosfilt (sos, y, withX0 (y.front()));
    std::reverse (y.begin(), y.end());
    return std::vector<double> (y.begin() + pad, y.end() - pad);
}

inline void bodyNoiseBands (const std::vector<double>& x, int fs, const Analysis& A, std::array<double, kBands>& gains, std::array<double, kBands>& rates)
{
    const auto nz = synthesizeNoise (A, (int) x.size());
    const auto edges = bandEdges (fs);
    const int hop = (int) (0.01 * fs);
    for (int b = 0; b < kBands; ++b)
    {
        const auto y = sosfiltfilt (butterBand4 (edges[(size_t) b], edges[(size_t) b + 1], fs), nz);
        const int n = (int) y.size() / hop;
        std::vector<double> env ((size_t) n), edb ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double s = 0; for (int j = 0; j < hop; ++j) s += y[(size_t) (i * hop + j)] * y[(size_t) (i * hop + j)];
            env[(size_t) i] = std::sqrt (s / hop) + 1e-12;
            edb[(size_t) i] = 20.0 * std::log10 (env[(size_t) i]);
        }
        const int pk = (int) (std::max_element (edb.begin(), edb.end()) - edb.begin());
        int stop = n - pk;
        for (int i = pk; i < n; ++i) if (! (edb[(size_t) i] > edb[(size_t) pk] - 40)) { stop = i - pk; break; }
        double rate = 30.0;
        if (stop >= 4)
        {
            std::vector<double> tx, ty;
            for (int i = 0; i < stop; ++i) { tx.push_back (i * 0.01); ty.push_back (edb[(size_t) (pk + i)]); }
            double slope, icpt; polyfit1 (tx, ty, slope, icpt);
            rate = std::max (-slope / 8.686, 0.5);
        }
        const double g0 = env[(size_t) pk] * std::exp (rate * std::min (pk * 0.01, 0.03));
        gains[(size_t) b] = std::min (g0, 4.0 * env[(size_t) pk]);
        rates[(size_t) b] = rate;
    }
}

// ------------------------------------------------------------------ the starting body
// fit2.initial_params. `frozen` marks the modes whose glide was measured directly (a long sweep):
// the fitter leaves their glide and tau_g alone.
inline Body initialBody (const std::vector<double>& x, int fs, std::vector<double>* keepGlide = nullptr, int nModes = 128)
{
    const auto A = analyze (x, fs);
    std::vector<Desc> desc;
    for (const auto& tr : A.tracks) { auto d = describeTrack (tr, A.hop, fs); if (d.valid) desc.push_back (std::move (d)); }
    std::stable_sort (desc.begin(), desc.end(), [] (const Desc& u, const Desc& v) { return -u.energy < -v.energy; });
    struct M { double f, energy, sigma, a, swell, delta, beta, glide, tau_g; };
    std::vector<M> modes;
    for (const auto& d : desc)
    {
        const int L = (int) d.f.size();
        const int tail = std::max (3, L / 4);
        const double fEnd = median (std::vector<double> (d.f.end() - std::min (tail, L), d.f.end()));
        if (d.kind != Desc::Sweeps)
        {
            M* hit = nullptr;
            for (auto& m : modes) if (std::fabs (std::log (m.f / fEnd)) < 0.012) { hit = &m; break; }
            if (hit != nullptr) { hit->energy += d.energy; continue; }
        }
        if ((int) modes.size() >= nModes) continue;
        const double aBorn = std::pow (10.0, d.db.front() / 20.0);
        const double aPeak = std::pow (10.0, *std::max_element (d.db.begin(), d.db.end()) / 20.0);
        const double dec = d.decayDbPerS && *d.decayDbPerS > 1 ? *d.decayDbPerS : 40.0;
        M m { fEnd, d.energy, dec / 8.686, aPeak, 0.0, 0.3, 0.04, 0.0, 0.05 };
        if (d.kind == Desc::Swells) { m.a = aBorn; m.swell = aPeak; }
        if (d.kind == Desc::Beats && d.beatHz > 0) { m.delta = d.beatHz; m.beta = 0.3; }
        if (d.kind == Desc::Sweeps)
        {
            const double g = std::log (d.f.front() / fEnd);
            m.glide = std::min (2.9, std::max (-2.9, g));
            double tg = 0.05;
            for (int i = 0; i < L; ++i) if (std::fabs (std::log (d.f[(size_t) i] / fEnd)) < std::fabs (g) / std::exp (1.0)) { tg = d.t[(size_t) i] - d.t[0]; break; }
            m.tau_g = std::min (1.2, std::max (0.004, tg));
        }
        modes.push_back (m);
    }
    std::stable_sort (modes.begin(), modes.end(), [] (const M& u, const M& v) { return u.f < v.f; });

    Body p;
    // damping law by least squares: sigma = b1 + b3 (f/1k)^2
    double s00 = 0, s01 = 0, s11 = 0, r0 = 0, r1 = 0;
    for (const auto& m : modes) { const double q = (m.f / 1000) * (m.f / 1000); s00 += 1; s01 += q; s11 += q * q; r0 += m.sigma; r1 += m.sigma * q; }
    const double det = s00 * s11 - s01 * s01;
    double b1 = (r0 * s11 - s01 * r1) / det, b3 = (s00 * r1 - s01 * r0) / det;
    b1 = std::min (390.0, std::max (0.06, b1)); b3 = std::min (49.0, std::max (1.1e-4, b3));
    p.b1 = b1; p.b3 = b3;
    std::vector<double> lowRates;
    for (const auto& m : modes)
    {
        const double law = b1 + b3 * (m.f / 1000) * (m.f / 1000);
        p.f.push_back (m.f); p.a.push_back (m.a);
        p.rho.push_back (std::min (2.9, std::max (-2.9, std::log (m.sigma / law))));
        p.delta.push_back (m.delta); p.beta.push_back (m.beta);
        p.eps.push_back (m.swell > 0 ? 1.0 : -4.0);
        p.glide.push_back (m.glide); p.tau_g.push_back (m.tau_g);
        if (m.f < 600 && m.beta > 0.2) lowRates.push_back (m.delta);
    }
    if (lowRates.size() >= 2) { p.lb_m = 0.4; p.lb_rate = median (lowRates); } else { p.lb_m = 0.02; p.lb_rate = 4.0; }
    p.C = 2.0; p.gamma = 0.5; p.tau_c = 0.04; p.G = 0.0; p.tau_a = 0.001;
    p.cx_amt = 0.002; p.cx_t = 2.5; p.cx_f = 500.0; p.lb_f = 600.0; p.tau_n = 0.01;
    p.noise.fill (1e-3);
    std::vector<double> keep (p.f.size(), 1.0);
    // long sweeps the frame tracker breaks apart: followed backwards from where they settle
    for (const auto& sw : findSweeps (x, fs))
    {
        size_t j = 0; double bestD = 1e300;
        for (size_t i = 0; i < p.f.size(); ++i) { const double d = std::fabs (std::log (p.f[i] / sw.fEnd)); if (d < bestD) { bestD = d; j = i; } }
        if (bestD > 0.015)
        {
            const double aMed = median (p.a);
            p.f.push_back (sw.fEnd); p.a.push_back (aMed); p.rho.push_back (0.0); p.delta.push_back (0.3); p.beta.push_back (0.04);
            p.eps.push_back (-4.0); p.glide.push_back (0.0); p.tau_g.push_back (0.05); keep.push_back (1.0);
            j = p.f.size() - 1;
        }
        p.glide[j] = std::min (2.9, std::max (-2.9, sw.glideLog));
        p.tau_g[j] = sw.tauG;
        keep[j] = 0.0;
    }
    if (const auto stop = findContactStop (x, fs))
    {
        p.cx_t = stop->t; p.cx_f = stop->fCut;
        p.cx_amt = std::min (250.0, std::max (5.0, stop->rateDbPerS / 8.686));
    }
    std::array<double, kBands> gains {}, rates {};
    bodyNoiseBands (x, fs, A, gains, rates);
    const auto fc = bandCentres (fs);
    for (int b = 0; b < kBands; ++b)
    {
        const double lawB = p.b1 + p.b3 * (fc[(size_t) b] / 1000.0) * (fc[(size_t) b] / 1000.0);
        p.dn[(size_t) b] = std::max (gains[(size_t) b], 1e-7);
        p.dn_rho[(size_t) b] = std::min (2.9, std::max (-2.9, std::log (rates[(size_t) b] / lawB)));
    }
    if (keepGlide != nullptr) *keepGlide = keep;
    return p;
}

} // namespace an
} // namespace ml
