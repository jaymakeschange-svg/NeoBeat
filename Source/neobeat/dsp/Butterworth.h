// NeoBeat — Butterworth band-pass design as biquad sections, matching
// scipy.signal.butter(N, [lo, hi], "band", fs=..., output="sos") (prewarped
// bilinear transform of the analog prototype), so filtered noise has the same
// colour as the MaterialLab learner's.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace nb
{
struct Biquad2 { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// order = the prototype order N (the band-pass has 2N poles = N sections).
inline std::vector<Biquad2> butterBandpass (int order, double lo, double hi, double fs)
{
    using C = std::complex<double>;
    const double pi = 3.14159265358979323846;
    const double w1 = 2.0 * fs * std::tan (pi * lo / fs), w2 = 2.0 * fs * std::tan (pi * hi / fs);
    const double bw = w2 - w1, wo = std::sqrt (w1 * w2);
    std::vector<C> poles;
    for (int k = 0; k < order; ++k)
    {
        const C p = std::exp (C (0.0, pi * (2.0 * k + order + 1.0) / (2.0 * order)));   // analog LP prototype
        const C a = p * (bw / 2.0);
        const C r = std::sqrt (a * a - wo * wo);
        poles.push_back (a + r);
        poles.push_back (a - r);
    }
    // bilinear transform of the poles
    std::vector<C> zp;
    for (const auto& s : poles) zp.push_back ((2.0 * fs + s) / (2.0 * fs - s));
    // pair conjugates: keep poles with positive imaginary part
    std::vector<C> upper;
    for (const auto& z : zp) if (z.imag() > 1.0e-12) upper.push_back (z);
    std::vector<Biquad2> out;
    for (const auto& z : upper)
    {
        Biquad2 q;
        q.b0 = 1.0; q.b1 = 0.0; q.b2 = -1.0;             // one zero at z = +1 and one at z = -1 per section
        q.a1 = -2.0 * z.real(); q.a2 = std::norm (z);
        out.push_back (q);
    }
    // overall gain: unity at the band centre (normalization is done on the noise afterwards anyway)
    const double wc = 2.0 * std::atan (wo / (2.0 * fs));
    const C e1 = std::exp (C (0.0, -wc)), e2 = e1 * e1;
    C h (1.0, 0.0);
    for (const auto& q : out) h *= (q.b0 + q.b1 * e1 + q.b2 * e2) / (1.0 + q.a1 * e1 + q.a2 * e2);
    const double g = 1.0 / std::max (std::abs (h), 1.0e-30);
    if (! out.empty())
    {
        const double gs = std::pow (g, 1.0 / (double) out.size());
        for (auto& q : out) { q.b0 *= gs; q.b1 *= gs; q.b2 *= gs; }
    }
    return out;
}

// Direct-form II transposed cascade, double precision (used offline to fill noise tables).
struct SosCascade
{
    std::vector<Biquad2> s;
    std::vector<double> z1, z2;
    explicit SosCascade (std::vector<Biquad2> sections) : s (std::move (sections)), z1 (s.size(), 0.0), z2 (s.size(), 0.0) {}
    double process (double x) noexcept
    {
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto& q = s[i];
            const double y = q.b0 * x + z1[i];
            z1[i] = q.b1 * x - q.a1 * y + z2[i];
            z2[i] = q.b2 * x - q.a2 * y;
            x = y;
        }
        return x;
    }
};

// Butterworth low/high-pass sections (scipy butter(N, f, "low"/"high")), for the room and the air.
inline std::vector<Biquad2> butterLowHigh (int order, double fc, double fs, bool high)
{
    using C = std::complex<double>;
    const double pi = 3.14159265358979323846;
    const double w = 2.0 * fs * std::tan (pi * fc / fs);
    std::vector<Biquad2> out;
    for (int k = 0; k < order / 2; ++k)
    {
        C p = std::exp (C (0.0, pi * (2.0 * k + order + 1.0) / (2.0 * order)));
        C s = high ? w / p : p * w;
        C z = (2.0 * fs + s) / (2.0 * fs - s);
        Biquad2 q;
        q.a1 = -2.0 * z.real(); q.a2 = std::norm (z);
        if (high) { q.b0 = 1; q.b1 = -2; q.b2 = 1; } else { q.b0 = 1; q.b1 = 2; q.b2 = 1; }
        // unity gain at DC (low) or Nyquist (high)
        const double num = high ? (q.b0 - q.b1 + q.b2) : (q.b0 + q.b1 + q.b2);
        const double den = high ? (1.0 - q.a1 + q.a2) : (1.0 + q.a1 + q.a2);
        const double g = den / num;
        q.b0 *= g; q.b1 *= g; q.b2 *= g;
        out.push_back (q);
    }
    if (order % 2 == 1)
    {   // first-order section
        const double K = std::tan (pi * fc / fs);
        Biquad2 q;
        if (high) { q.b0 = 1.0 / (1.0 + K); q.b1 = -q.b0; } else { q.b0 = K / (1.0 + K); q.b1 = q.b0; }
        q.a1 = (K - 1.0) / (K + 1.0); q.a2 = 0.0; q.b2 = 0.0;
        out.push_back (q);
    }
    return out;
}
} // namespace nb
