// Shared by NeoBeat and Batchcut — how loud a one-shot is (ITU-R BS.1770 momentary loudness, the same
// measure Batchcut levels its exports with), and the gain that brings a sound to a target.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace nbk
{
struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// The K-weighting at any rate: a high shelf (about +4 dB above 1.7 kHz, the head) and a high-pass (about 38 Hz).
// The Recommendation tabulates them for 48 kHz only and asks other rates for the same response; these are the
// analog prototypes behind its tables, made digital for the rate (as libebur128 and FFmpeg's ebur128 do).
inline void kWeighting (double sr, Biquad& shelf, Biquad& hp)
{
    const double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sr), a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0; hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

struct Level { float lufs = -120.0f, peakDb = -120.0f; };

// How loud a one-shot is: BS.1770's K-weighted mean square, the channels summed, over a 400 ms window (EBU's
// momentary loudness), at its loudest. A sound shorter than the window is measured with the silence after it,
// which is how the ear adds up a short sound: a 5 ms click is heard quieter than 400 ms at the same level.
// r empty: one channel. Peak: the sample peak, dBFS.
inline Level measureLevel (const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    Level lv;
    const size_t n = l.size();
    if (n == 0 || sr <= 0) return lv;
    Biquad sh, hp; kWeighting (sr, sh, hp);
    std::vector<double> e (n + 1, 0.0);
    float pk = 0.0f;
    const int chans = r.size() == n ? 2 : 1;
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto& x = ch == 0 ? l : r;
        double s1 = 0, s2 = 0, h1 = 0, h2 = 0;   // transposed direct form II states
        for (size_t i = 0; i < n; ++i)
        {
            const double v = x[i];
            pk = std::max (pk, std::fabs (x[i]));
            const double y = sh.b0 * v + s1; s1 = sh.b1 * v - sh.a1 * y + s2; s2 = sh.b2 * v - sh.a2 * y;
            const double z = hp.b0 * y + h1; h1 = hp.b1 * y - hp.a1 * z + h2; h2 = hp.b2 * y - hp.a2 * z;
            e[i + 1] += z * z;
        }
    }
    for (size_t i = 1; i <= n; ++i) e[i] += e[i - 1];
    const size_t W = (size_t) std::max (1.0, std::round (0.4 * sr)), step = (size_t) std::max (1.0, std::round (0.01 * sr));
    double best = 0.0;
    if (n <= W) best = e[n] / (double) W;
    else for (size_t t = 0; t + W <= n; t += step) best = std::max (best, (e[t + W] - e[t]) / (double) W);
    lv.lufs = best > 0.0 ? (float) std::max (-120.0, -0.691 + 10.0 * std::log10 (best)) : -120.0f;
    lv.peakDb = pk > 0.0f ? std::max (-120.0f, 20.0f * std::log10 (pk)) : -120.0f;
    return lv;
}

// What NeoBeat's own kit sounds measure at a lane level of 0 dB: -15 LUFS on average over the four
// kits (they range from -10 to -23). Imported sounds are brought to this, so a quiet recording sits
// beside the stock sounds.
constexpr float kKitLoudness = -15.0f;

// The gain (dB) that brings a sound to `target` LUFS without its peak passing -1 dBFS, within +-24 dB.
inline float matchGainDb (const Level& lv, float target = kKitLoudness)
{
    if (lv.lufs <= -100.0f) return 0.0f;
    float g = target - lv.lufs;
    g = std::min (g, -1.0f - lv.peakDb);
    return std::max (-24.0f, std::min (24.0f, g));
}
} // namespace nbk
