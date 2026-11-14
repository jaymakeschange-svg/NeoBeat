// Shared by NeoBeat and Batchcut — what a one-shot is: its measurements, the part of a drum kit it
// could play, and how usable it is. Framework-free.
//
//   analyse()        length, attack, decay, where its energy sits, pitch, noise, clipping, onsets
//   rolePrior()      how well it fits each of NeoBeat's 16 lanes, by rule (Learner.h adds what it learned)
//   musicalScore()   0..1: a clean single hit, not clipped, clear of its noise floor, a usable length
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace nbk
{
constexpr int kRoles = 16;
enum Role : int { Kick = 0, Kick2, Snare, Snare2, ClosedHat, OpenHat, Hat3, Rim, Thud, Clank, Junk, Drop, Horn, Stab, Bell, Fx };
inline const char* roleName (int r)
{
    static const char* n[] = { "Kick", "Kick 2", "Snare", "Snare 2", "Closed hat", "Open hat", "Hat 3", "Rim", "Thud", "Clank", "Junk", "Drop", "Horn", "Stab", "Bell", "FX" };
    return n[std::max (0, std::min (r, kRoles - 1))];
}

constexpr int kBands = 16;      // log-spaced, 40 Hz .. 16 kHz
constexpr int kDim = 32;        // the learner's view of a sound

struct Features
{
    float seconds = 0.0f;       // from its start to where it has died away (-60 dB)
    float attackMs = 1.0f;      // start to 90 % of its peak
    float decayMs = 100.0f;     // peak to -20 dB
    float centroidHz = 1000.0f; // where its energy sits
    float earlyCentroidHz = 1000.0f;   // ... in the first 60 ms
    float flatness = 0.5f;      // 0 tonal .. 1 noise
    float low = 0.0f, mid = 0.0f, high = 0.0f;   // share of energy under 150 Hz, 150 Hz - 5 kHz, over 5 kHz
    float zcr = 0.0f;           // zero crossings per second / 10000
    float pitchHz = 0.0f, pitchStrength = 0.0f;  // of its body (0: none found)
    float pitchFall = 0.0f;     // octaves its pitch falls from the start to the body (a kick's sweep, a drop)
    float crestDb = 12.0f;
    float peakDb = -6.0f, rmsDb = -20.0f;
    float clipped = 0.0f;       // share of samples at full scale
    float snrDb = 60.0f;        // its peak over its noise floor
    int onsets = 1;             // separate hits inside it
    float width = 0.0f;         // side / mid
    std::array<float, kBands> bands {};          // share of energy per band
    bool valid = false;
};

namespace detail
{
inline void fft (std::vector<std::complex<float>>& a)
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
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}
// power spectrum of x[a, a+n) (Hann), accumulated into p (size nfft/2 + 1)
inline void addSpectrum (const std::vector<float>& x, int a, int n, int nfft, std::vector<double>& p)
{
    std::vector<std::complex<float>> buf ((size_t) nfft);
    for (int i = 0; i < nfft; ++i)
    {
        const int k = a + i;
        const float w = i < n ? 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) std::max (2, n)) : 0.0f;
        buf[(size_t) i] = k >= 0 && k < (int) x.size() && i < n ? x[(size_t) k] * w : 0.0f;
    }
    fft (buf);
    for (int k = 0; k <= nfft / 2; ++k) p[(size_t) k] += (double) std::norm (buf[(size_t) k]);
}
// the strongest period in x[a, b) between loHz and hiHz: normalised autocorrelation
inline float pitchOf (const std::vector<float>& x, int a, int b, float sr, float loHz, float hiHz, float& strength)
{
    strength = 0.0f;
    a = std::max (0, a); b = std::min ((int) x.size(), b);
    const int n = b - a;
    const int minLag = std::max (2, (int) (sr / hiHz)), maxLag = std::min (n / 2, (int) (sr / loHz));
    if (n < 64 || maxLag <= minLag) return 0.0f;
    double e0 = 0.0; for (int i = a; i < b; ++i) e0 += (double) x[(size_t) i] * x[(size_t) i];
    if (e0 < 1.0e-12) return 0.0f;
    float best = 0.0f; int bestLag = 0;
    std::vector<float> r ((size_t) maxLag + 2, 0.0f);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0, e1 = 0.0, e2 = 0.0;
        for (int i = a; i + lag < b; ++i) { s += (double) x[(size_t) i] * x[(size_t) (i + lag)]; e1 += (double) x[(size_t) i] * x[(size_t) i]; e2 += (double) x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
        r[(size_t) lag] = (float) (s / std::sqrt (std::max (1.0e-20, e1 * e2)));
    }
    // the first clear peak (not a multiple of the true period)
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (r[(size_t) lag] > r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1] && r[(size_t) lag] > best + 0.08f) { best = r[(size_t) lag]; bestLag = lag; }
    if (bestLag == 0) return 0.0f;
    strength = std::max (0.0f, std::min (1.0f, best));
    return sr / (float) bestLag;
}
inline float near (float x, float target, float width) noexcept { const float d = (x - target) / width; return std::exp (-0.5f * d * d); }
inline float above (float x, float edge, float soft) noexcept { return 1.0f / (1.0f + std::exp (-(x - edge) / soft)); }
inline float below (float x, float edge, float soft) noexcept { return 1.0f - above (x, edge, soft); }
inline float log2f0 (float x) noexcept { return std::log2 (std::max (1.0e-6f, x)); }
} // namespace detail

inline Features analyse (const float* l, const float* r, int n, float sr)
{
    Features f;
    if (l == nullptr || n < 32 || sr < 4000.0f) return f;
    if (r == nullptr) r = l;
    // work at no more than 48 kHz: plenty for these measures, and it keeps long files quick
    const int step = sr > 60000.0f ? (int) std::lround (sr / 48000.0f) : 1;
    const float fs = sr / (float) step;
    std::vector<float> m; m.reserve ((size_t) (n / step + 1));
    double side = 0.0, midE = 0.0; int clip = 0;
    for (int i = 0; i + step <= n; i += step)
    {
        float a = 0.0f, b = 0.0f;
        for (int k = 0; k < step; ++k) { a += l[i + k]; b += r[i + k]; if (std::fabs (l[i + k]) >= 0.999f || std::fabs (r[i + k]) >= 0.999f) ++clip; }
        a /= (float) step; b /= (float) step;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        m.push_back (0.5f * (a + b));
        side += (double) (a - b) * (a - b); midE += (double) (a + b) * (a + b);
    }
    const int N = (int) m.size();
    float peak = 0.0f; int peakAt = 0;
    for (int i = 0; i < N; ++i) if (std::fabs (m[(size_t) i]) > peak) { peak = std::fabs (m[(size_t) i]); peakAt = i; }
    if (peak < 1.0e-5f) return f;
    f.valid = true;
    f.peakDb = 20.0f * std::log10 (peak);
    f.clipped = (float) clip / (float) std::max (1, n);
    f.width = (float) std::sqrt (side / std::max (1.0e-20, midE));

    // the envelope (peak follower, 4 ms release), then the start and the end
    std::vector<float> env ((size_t) N);
    { const float rel = std::exp (-1.0f / (0.004f * fs)); float e = 0.0f; for (int i = 0; i < N; ++i) { e = std::max (std::fabs (m[(size_t) i]), e * rel); env[(size_t) i] = e; } }
    int start = 0; while (start < N - 1 && env[(size_t) start] < 0.01f * peak) ++start;
    int end = N; while (end > start + 16 && env[(size_t) end - 1] < 0.001f * peak) --end;
    f.seconds = (float) (end - start) / fs;
    float envPeak = 0.0f; int envPeakAt = start;
    for (int i = start; i < end; ++i) if (env[(size_t) i] > envPeak) { envPeak = env[(size_t) i]; envPeakAt = i; }
    (void) peakAt;
    { int i = start; while (i < envPeakAt && env[(size_t) i] < 0.9f * envPeak) ++i; f.attackMs = std::max (0.05f, 1000.0f * (float) (i - start) / fs); }
    { int i = envPeakAt; while (i < end && env[(size_t) i] > 0.1f * envPeak) ++i; f.decayMs = std::max (1.0f, 1000.0f * (float) (i - envPeakAt) / fs); }
    double sum = 0.0; for (int i = start; i < end; ++i) sum += (double) m[(size_t) i] * m[(size_t) i];
    const float rms = (float) std::sqrt (sum / std::max (1, end - start));
    f.rmsDb = 20.0f * std::log10 (std::max (1.0e-7f, rms));
    f.crestDb = f.peakDb - f.rmsDb;
    { int z = 0; for (int i = start + 1; i < end; ++i) z += (m[(size_t) i] >= 0.0f) != (m[(size_t) i - 1] >= 0.0f) ? 1 : 0; f.zcr = (float) z / std::max (1.0e-3f, f.seconds) / 10000.0f; }

    // the noise floor: the quietest 10 ms anywhere in the file (before the hit or after it has gone)
    {
        const int w = std::max (16, (int) (0.01f * fs));
        double quiet = 1.0e30;
        for (int a = 0; a + w <= N; a += w) { double e = 0.0; for (int i = a; i < a + w; ++i) e += (double) m[(size_t) i] * m[(size_t) i]; quiet = std::min (quiet, e / w); }
        f.snrDb = quiet >= 1.0e29 ? 60.0f : std::min (96.0f, f.peakDb - 10.0f * (float) std::log10 (std::max (1.0e-14, quiet)));
        // a file cut while its sound is still falling has shown no floor yet: the floor is lower than that
        if (N >= 3 * w)
        {
            double e1 = 0.0, e2 = 0.0;
            for (int i = N - 2 * w; i < N - w; ++i) e1 += (double) m[(size_t) i] * m[(size_t) i];
            for (int i = N - w; i < N; ++i) e2 += (double) m[(size_t) i] * m[(size_t) i];
            if (e2 <= quiet * w * 1.05 && e1 > e2 * 1.15) f.snrDb = std::min (96.0f, f.snrDb + 18.0f);
        }
    }
    // separate hits: the level (over 10 ms) rising above 40 % of its highest again after falling under 20 %
    {
        const int w = std::max (8, (int) (0.01f * fs));
        std::vector<float> lev; float top = 0.0f;
        for (int a = start; a + w <= end; a += w / 2)
        {
            double e = 0.0; for (int i = a; i < a + w; ++i) e += (double) m[(size_t) i] * m[(size_t) i];
            lev.push_back ((float) std::sqrt (e / w)); top = std::max (top, lev.back());
        }
        int count = 0; bool armed = true;
        for (float e : lev)
        {
            if (armed && e > 0.4f * top) { ++count; armed = false; }
            else if (! armed && e < 0.2f * top) armed = true;
        }
        f.onsets = std::max (1, count);
    }

    // the spectrum: all of it, and its first 60 ms
    const int nfft = 4096;
    std::vector<double> all ((size_t) nfft / 2 + 1, 0.0), early ((size_t) nfft / 2 + 1, 0.0);
    { int frames = 0; for (int a = start; a < end && frames < 64; a += nfft / 2) { detail::addSpectrum (m, a, std::min (nfft, end - a), nfft, all); ++frames; } }
    detail::addSpectrum (m, start, std::min ({ nfft, end - start, (int) (0.06f * fs) }), nfft, early);
    auto centroid = [&] (const std::vector<double>& p)
    {
        double num = 0.0, den = 0.0;
        for (int k = 2; k <= nfft / 2; ++k) { const double hz = (double) k * fs / nfft; if (hz < 30.0) continue; num += hz * p[(size_t) k]; den += p[(size_t) k]; }
        return den > 0.0 ? (float) (num / den) : 1000.0f;
    };
    f.centroidHz = centroid (all); f.earlyCentroidHz = centroid (early);
    {
        double total = 0.0, lo = 0.0, hi = 0.0, logSum = 0.0; int count = 0;
        std::array<double, kBands> b {};
        for (int k = 2; k <= nfft / 2; ++k)
        {
            const double hz = (double) k * fs / nfft; const double e = all[(size_t) k];
            if (hz < 30.0) continue;
            total += e; if (hz < 150.0) lo += e; if (hz > 5000.0) hi += e;
            if (hz >= 100.0 && hz <= 12000.0) { logSum += std::log (e + 1.0e-20); ++count; }
            const int bi = (int) std::floor (std::log2 (hz / 40.0) / std::log2 (16000.0 / 40.0) * kBands);
            if (bi >= 0 && bi < kBands) b[(size_t) bi] += e;
        }
        if (total > 0.0)
        {
            f.low = (float) (lo / total); f.high = (float) (hi / total); f.mid = std::max (0.0f, 1.0f - f.low - f.high);
            for (int i = 0; i < kBands; ++i) f.bands[(size_t) i] = (float) (b[(size_t) i] / total);
            double arith = 0.0; int c2 = 0;
            for (int k = 2; k <= nfft / 2; ++k) { const double hz = (double) k * fs / nfft; if (hz >= 100.0 && hz <= 12000.0) { arith += all[(size_t) k]; ++c2; } }
            if (count > 0 && arith > 0.0) f.flatness = (float) std::min (1.0, std::exp (logSum / count) / (arith / c2));
        }
    }
    // pitch: of the body (after the strike), and of the very start, to see a fall
    {
        const int bodyA = std::min (end - 64, envPeakAt + (int) (0.012f * fs)), bodyB = std::min (end, bodyA + (int) (0.12f * fs));
        float s1 = 0.0f, s0 = 0.0f;
        f.pitchHz = detail::pitchOf (m, bodyA, bodyB, fs, 30.0f, 2500.0f, s1);
        f.pitchStrength = s1;
        const float p0 = detail::pitchOf (m, envPeakAt, std::min (end, envPeakAt + (int) (0.03f * fs)), fs, 40.0f, 2500.0f, s0);
        const int lateA = std::min (end - 64, envPeakAt + (int) (0.5f * (float) (end - envPeakAt)));
        float s2 = 0.0f;
        const float p2 = detail::pitchOf (m, lateA, std::min (end, lateA + (int) (0.12f * fs)), fs, 25.0f, 2500.0f, s2);
        if (p0 > 0.0f && f.pitchHz > 0.0f && s0 > 0.5f && s1 > 0.5f) f.pitchFall = std::max (-3.0f, std::min (3.0f, std::log2 (p0 / f.pitchHz)));
        if (f.pitchHz > 0.0f && p2 > 0.0f && s2 > 0.5f && s1 > 0.5f) f.pitchFall = std::max (f.pitchFall, std::max (-3.0f, std::min (3.0f, std::log2 (f.pitchHz / p2))));
        if (s1 < 0.25f) f.pitchHz = 0.0f;
    }
    return f;
}

// The learner's view: every measure on a comparable scale (roughly -1..1).
inline std::array<float, kDim> vectorOf (const Features& f)
{
    std::array<float, kDim> v {};
    int i = 0;
    auto put = [&] (float x) { if (i < kDim) v[(size_t) i++] = std::max (-2.0f, std::min (2.0f, x)); };
    put ((detail::log2f0 (f.seconds) + 2.0f) / 3.0f);            // 0.25 s -> 0
    put ((detail::log2f0 (f.attackMs) - 2.0f) / 3.0f);           // 4 ms -> 0
    put ((detail::log2f0 (f.decayMs) - 7.0f) / 3.0f);            // 128 ms -> 0
    put ((detail::log2f0 (f.centroidHz) - 10.0f) / 3.0f);        // 1 kHz -> 0
    put ((detail::log2f0 (f.earlyCentroidHz) - 10.0f) / 3.0f);
    put (2.0f * std::sqrt (f.flatness) - 1.0f);
    put (2.0f * f.low - 1.0f); put (2.0f * f.mid - 1.0f); put (2.0f * f.high - 1.0f);
    put (f.zcr - 0.5f);
    put (f.pitchHz > 0.0f ? (detail::log2f0 (f.pitchHz) - 8.0f) / 3.0f : -1.5f);
    put (2.0f * f.pitchStrength - 1.0f);
    put (f.pitchFall);
    put ((f.crestDb - 14.0f) / 10.0f);
    put (std::min (1.0f, (float) (f.onsets - 1) / 3.0f) * 2.0f - 1.0f);
    put (std::min (1.0f, f.width) * 2.0f - 1.0f);
    for (int b = 0; b < kBands; ++b) put (2.0f * std::sqrt (f.bands[(size_t) b]) - 0.6f);
    return v;
}

// How well a sound fits each lane, by rule: scores that add up to 1.
inline std::array<float, kRoles> rolePrior (const Features& f)
{
    using namespace detail;
    std::array<float, kRoles> s {};
    if (! f.valid) { s.fill (1.0f / kRoles); return s; }
    const float lc = log2f0 (f.centroidHz), ld = log2f0 (f.decayMs), ls = log2f0 (f.seconds * 1000.0f);
    const float tonal = f.pitchStrength * (1.0f - f.flatness), noisy = std::sqrt (f.flatness);
    const float fast = below (f.attackMs, 12.0f, 4.0f), slow = above (f.attackMs, 25.0f, 8.0f);
    const float lowPitch = f.pitchHz > 0.0f ? near (log2f0 (f.pitchHz), log2f0 (60.0f), 0.8f) : 0.3f;
    const float body = f.bands[3] + f.bands[4] + f.bands[5];                 // 130 - 400 Hz: a drum's shell
    const float bright = f.bands[10] + f.bands[11] + f.bands[12] + f.bands[13] + f.bands[14] + f.bands[15];   // over 1.7 kHz
    s[Kick]      = above (f.low, 0.35f, 0.12f) * below (lc, log2f0 (700.0f), 0.8f) * fast * near (ls, log2f0 (350.0f), 1.6f) * (0.5f + 0.5f * lowPitch) * 1.6f;
    s[Kick2]     = above (f.low, 0.22f, 0.12f) * below (lc, log2f0 (1100.0f), 0.8f) * fast * (0.6f * near (ls, log2f0 (150.0f), 1.0f) + 0.6f * near (ls, log2f0 (900.0f), 1.0f));
    // a snare: a shell and its wires together (a hat has no shell, a tom no wires)
    s[Snare]     = above (body, 0.04f, 0.02f) * above (bright, 0.12f, 0.05f) * above (noisy, 0.25f, 0.1f) * fast * near (ls, log2f0 (260.0f), 1.3f) * below (f.low, 0.45f, 0.1f) * 1.8f;
    s[Snare2]    = near (lc, log2f0 (3400.0f), 1.3f) * above (noisy, 0.3f, 0.1f) * near (ls, log2f0 (200.0f), 1.6f) * below (f.low, 0.3f, 0.1f) * (0.5f + 0.4f * above ((float) f.onsets, 1.5f, 0.3f)) * (0.6f + 0.4f * below (body, 0.04f, 0.02f));
    const float noShell = below (body, 0.03f, 0.015f);
    s[ClosedHat] = above (lc, log2f0 (5000.0f), 0.5f) * above (f.high, 0.4f, 0.12f) * below (ld, log2f0 (90.0f), 0.7f) * fast * noShell * 1.6f;
    s[OpenHat]   = above (lc, log2f0 (4500.0f), 0.5f) * above (f.high, 0.35f, 0.12f) * near (ld, log2f0 (350.0f), 1.2f) * above (ls, log2f0 (160.0f), 0.5f) * noShell * 1.5f;
    s[Hat3]      = above (lc, log2f0 (3500.0f), 0.6f) * above (f.high, 0.25f, 0.12f) * above (noisy, 0.35f, 0.1f) * near (ld, log2f0 (150.0f), 1.0f) * (0.6f + 0.4f * slow) * noShell;
    s[Rim]       = near (lc, log2f0 (2200.0f), 1.2f) * below (ls, log2f0 (90.0f), 0.6f) * fast * below (noisy, 0.55f, 0.12f) * below (f.low, 0.3f, 0.1f) * 1.3f;
    s[Thud]      = near (lc, log2f0 (350.0f), 1.0f) * below (tonal, 0.45f, 0.15f) * near (ls, log2f0 (220.0f), 1.3f) * below (f.high, 0.2f, 0.1f) * below (f.low, 0.6f, 0.15f);
    s[Clank]     = near (lc, log2f0 (2800.0f), 1.3f) * below (noisy, 0.45f, 0.12f) * near (ls, log2f0 (450.0f), 1.5f) * fast * (0.5f + 0.5f * above (f.crestDb, 14.0f, 3.0f));
    s[Junk]      = above (noisy, 0.4f, 0.12f) * near (lc, log2f0 (1800.0f), 1.5f) * (0.4f + 0.6f * above ((float) f.onsets, 1.5f, 0.4f)) * near (ls, log2f0 (500.0f), 1.8f) * 0.8f;
    s[Drop]      = (above (f.pitchFall, 0.5f, 0.2f) * above (ls, log2f0 (350.0f), 0.6f) + 0.5f * above (f.low, 0.5f, 0.1f) * above (ls, log2f0 (900.0f), 0.5f)) * 0.9f;
    s[Horn]      = above (tonal, 0.35f, 0.12f) * above (ls, log2f0 (350.0f), 0.6f) * near (lc, log2f0 (900.0f), 1.3f) * (0.5f + 0.5f * slow);
    s[Stab]      = above (tonal, 0.3f, 0.12f) * fast * near (ls, log2f0 (350.0f), 1.2f) * near (lc, log2f0 (1500.0f), 1.5f) * below (f.low, 0.4f, 0.1f) * 0.9f;
    s[Bell]      = above (tonal, 0.3f, 0.12f) * above (lc, log2f0 (1500.0f), 0.7f) * above (ls, log2f0 (500.0f), 0.6f) * fast * below (noisy, 0.4f, 0.12f) * 1.2f;
    s[Fx]        = 0.08f + 0.9f * above (ls, log2f0 (1800.0f), 0.5f) + 0.3f * above ((float) f.onsets, 2.5f, 0.5f);
    float total = 0.0f; for (float& x : s) { x = std::max (1.0e-4f, x); total += x; }
    for (float& x : s) x /= total;
    return s;
}

// 0..1: how usable it is as a one-shot, whatever it is.
inline float musicalScore (const Features& f)
{
    using namespace detail;
    if (! f.valid) return 0.0f;
    const float ms = f.seconds * 1000.0f;
    const float length = above (ms, 18.0f, 6.0f) * below (ms, 4500.0f, 900.0f);
    const float clean = above (f.snrDb, 28.0f, 6.0f);
    const float unclipped = below (f.clipped, 0.002f, 0.001f);
    const float single = f.onsets <= 1 ? 1.0f : (f.onsets == 2 ? 0.75f : 0.45f);
    const float loud = above (f.peakDb, -34.0f, 5.0f);
    const float shape = 0.6f + 0.4f * above (f.crestDb, 8.0f, 2.5f);       // has a strike or a contour, not a block of noise
    const float starts = 0.7f + 0.3f * below (f.attackMs, 60.0f, 20.0f);   // begins with its sound, not with a wait
    return std::max (0.0f, std::min (1.0f, length * (0.35f + 0.65f * clean) * (0.3f + 0.7f * unclipped) * single * loud * shape * starts));
}

// 0 (the same) and up: how different two sounds are, for choosing a varied set
inline float distance (const std::array<float, kDim>& a, const std::array<float, kDim>& b)
{
    float d = 0.0f; for (int i = 0; i < kDim; ++i) d += (a[(size_t) i] - b[(size_t) i]) * (a[(size_t) i] - b[(size_t) i]);
    return std::sqrt (d / kDim);
}
} // namespace nbk
