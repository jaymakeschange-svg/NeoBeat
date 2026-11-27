// The MaterialLab learner's loss (fit2.make_loss), in C++: how far a render is from the target.
//
// Part of the port of the user's learner (see Body2.h). Term for term:
//   spectral   at FFT sizes 256, 1024 and 4096 (Hann, hop n/4): the mean absolute log-magnitude
//              difference above a floor 70 dB under the target's peak, plus the relative
//              Frobenius distance of the magnitudes; averaged over the three sizes
//   envelope   10 ms RMS in dB (5 ms hop), both clamped at the target's floor, mean |difference|/10
//   transport  per 2048-frame (hop 512), the energy across 160 log-spaced bands from 30 Hz to
//              0.47 fs, normalised; the 1-D Wasserstein distance between the two in octaves,
//              weighted by the target frame's energy; times 3
//   attack     the first 30 ms in 1 ms windows (0.5 ms hop), clamped 50 dB under the target's
//              peak, mean |difference|/10
// The learner computes in float32; this in double.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace ml
{
// ------------------------------------------------------------------ a plain radix-2 FFT
inline void fft (std::vector<std::complex<double>>& a, bool inverse = false)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Short-time magnitudes, as fit2._mag: frames of n every n/4 (only whole frames), Hann window
// (numpy's symmetric hanning), |rfft|. Row-major [frames][n/2 + 1].
struct Stft
{
    int n = 0, hop = 0, frames = 0, bins = 0;
    std::vector<double> win, mag;
    std::vector<std::complex<double>> spec;          // kept for the gradient

    void compute (const float* x, int len, int size)
    {
        n = size; hop = n / 4; bins = n / 2 + 1;
        frames = len >= n ? 1 + (len - n) / hop : 0;
        win.resize ((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = n > 1 ? 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / (n - 1)) : 1.0;
        mag.assign ((size_t) frames * bins, 0.0);
        spec.assign ((size_t) frames * bins, {});
        std::vector<std::complex<double>> buf ((size_t) n);
        for (int f = 0; f < frames; ++f)
        {
            for (int i = 0; i < n; ++i) buf[(size_t) i] = x[f * hop + i] * win[(size_t) i];
            fft (buf);
            for (int b = 0; b < bins; ++b) { spec[(size_t) f * bins + b] = buf[(size_t) b]; mag[(size_t) f * bins + b] = std::abs (buf[(size_t) b]); }
        }
    }
};

// 10 log10 (mean square + 1e-12) over frames of `win` every `hop`, as fit2's rms_db / attack_db.
inline std::vector<double> framedDb (const float* x, int len, int win, int hop)
{
    const int count = len >= win ? 1 + (len - win) / hop : 0;
    std::vector<double> d ((size_t) count);
    for (int f = 0; f < count; ++f)
    {
        double s = 0.0;
        for (int i = 0; i < win; ++i) { const double v = x[f * hop + i]; s += v * v; }
        d[(size_t) f] = 10.0 * std::log10 (s / win + 1e-12);
    }
    return d;
}

class Loss2
{
public:
    static constexpr int kSizes[3] = { 256, 1024, 4096 };
    static constexpr int kTransportFft = 2048, kTransportBands = 160;

    Loss2 (const float* target, int len, double fs, bool hasFloor, double noiseFloorDb)
        : n (len), sr (fs)
    {
        tgt.assign (target, target + len);
        for (int i = 0; i < 3; ++i) tStft[i].compute (target, len, kSizes[i]);
        envWin = (int) (0.01 * fs); envHop = (int) (0.005 * fs);
        tEnv = framedDb (target, len, envWin, envHop);
        const double tMax = *std::max_element (tEnv.begin(), tEnv.end());
        floorDb = tMax + (hasFloor ? noiseFloorDb : -70.0);
        floorDb = std::max (floorDb, tMax - 70.0);
        attN = (int) (0.03 * fs); attWin = (int) (0.001 * fs); attHop = (int) (0.0005 * fs);
        tAtt = framedDb (target, std::min (attN, len), attWin, attHop);
        attFloor = *std::max_element (tAtt.begin(), tAtt.end()) - 50.0;
        buildBands();
        Stft t2; t2.compute (target, len, kTransportFft);
        tBandP = bandPower (t2);
    }

    // The loss of a render (same length as the target).
    double value (const float* y) const
    {
        double total = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            Stft a; a.compute (y, n, kSizes[i]);
            const auto& b = tStft[i];
            const double fl = *std::max_element (b.mag.begin(), b.mag.end()) * std::pow (10.0, -70.0 / 20.0) + 1e-9;
            double lsum = 0.0, d2 = 0.0, b2 = 0.0;
            for (size_t j = 0; j < a.mag.size(); ++j)
            {
                lsum += std::fabs (std::log (a.mag[j] + fl) - std::log (b.mag[j] + fl));
                const double d = a.mag[j] - b.mag[j];
                d2 += d * d; b2 += b.mag[j] * b.mag[j];
            }
            total += lsum / (double) a.mag.size() + std::sqrt (d2) / (std::sqrt (b2) + 1e-9);
        }
        total /= 3.0;

        const auto e = framedDb (y, n, envWin, envHop);
        double env = 0.0;
        for (size_t f = 0; f < e.size(); ++f) env += std::fabs (std::max (e[f], floorDb) - std::max (tEnv[f], floorDb));
        env = env / (double) e.size() / 10.0;

        Stft a2; a2.compute (y, n, kTransportFft);
        const auto pa = bandPower (a2);
        const double wass = transport (pa);

        const auto at = framedDb (y, std::min (attN, n), attWin, attHop);
        double att = 0.0;
        for (size_t f = 0; f < at.size(); ++f) att += std::fabs (std::max (at[f], attFloor) - std::max (tAtt[f], attFloor));
        att = att / (double) at.size() / 10.0;

        return total + env + 3.0 * wass + att;
    }

private:
    void buildBands()
    {
        const int bins = kTransportFft / 2 + 1;
        const double fmin = 30.0, fmax = 0.47 * sr, df = sr / kTransportFft;
        std::vector<double> c ((size_t) kTransportBands + 2);
        for (int i = 0; i < kTransportBands + 2; ++i) c[(size_t) i] = fmin * std::pow (fmax / fmin, (double) i / (kTransportBands + 1));
        c.front() = fmin; c.back() = fmax;
        W.assign ((size_t) bins * kTransportBands, 0.0);
        for (int b = 0; b < kTransportBands; ++b)
        {
            double lo = c[(size_t) b], mid = c[(size_t) b + 1], hi = c[(size_t) b + 2];
            lo = std::min (lo, mid - 1.5 * df); hi = std::max (hi, mid + 1.5 * df);
            for (int k = 0; k < bins; ++k)
            {
                const double fq = k * df;
                W[(size_t) k * kTransportBands + b] = std::max (0.0, std::min ((fq - lo) / (mid - lo), (hi - fq) / (hi - mid)));
            }
        }
        octaves = std::log2 (fmax / fmin);
    }

    // [frames][160]: |X|^2 @ W
    std::vector<double> bandPower (const Stft& s) const
    {
        std::vector<double> p ((size_t) s.frames * kTransportBands, 0.0);
        for (int f = 0; f < s.frames; ++f)
            for (int k = 0; k < s.bins; ++k)
            {
                const double m2 = s.mag[(size_t) f * s.bins + k] * s.mag[(size_t) f * s.bins + k];
                if (m2 == 0.0) continue;
                const double* w = &W[(size_t) k * kTransportBands];
                double* row = &p[(size_t) f * kTransportBands];
                for (int b = 0; b < kTransportBands; ++b) row[b] += m2 * w[b];
            }
        return p;
    }

    double transport (const std::vector<double>& pa) const
    {
        const int frames = (int) (pa.size() / kTransportBands);
        double num = 0.0, den = 0.0;
        for (int f = 0; f < frames; ++f)
        {
            const double* a = &pa[(size_t) f * kTransportBands];
            const double* b = &tBandP[(size_t) f * kTransportBands];
            double sa = 0.0, sb = 0.0;
            for (int j = 0; j < kTransportBands; ++j) { sa += a[j]; sb += b[j]; }
            double ca = 0.0, cb = 0.0, w1 = 0.0;
            for (int j = 0; j < kTransportBands; ++j)
            {
                ca += a[j] / (sa + 1e-12); cb += b[j] / (sb + 1e-12);
                w1 += std::fabs (ca - cb);
            }
            w1 = w1 * octaves / kTransportBands;
            num += w1 * sb; den += sb;
        }
        return num / (den + 1e-12);
    }

    int n; double sr;
    std::vector<float> tgt;
    Stft tStft[3];
    int envWin = 0, envHop = 0, attN = 0, attWin = 0, attHop = 0;
    std::vector<double> tEnv, tAtt, W, tBandP;
    double floorDb = 0.0, attFloor = 0.0, octaves = 0.0;
};

} // namespace ml
