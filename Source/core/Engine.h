// BatchCut - the engine. Finds the sound events in a recording (even in noise), groups the ones that
// sound alike, splits the sound into layers (background, tone, transient, noise) that can be edited on
// the spectrogram, and renders one-shots. Framework-free: the app and the tests run this same code.
//
// One STFT (2048 / 512, Hann) serves everything:
//   noise      a time-varying noise floor per band: the 20th percentile over 8 s windows, corrected to a mean
//   detection  spectral flux of the noise-whitened log band energies, an adaptive (median + MAD) threshold,
//              then the start is refined on a fine STFT (256 / 32) in the bands where the event rose
//   layers     foreground = what stands clear of the noise floor; split by median filtering into
//              tone (steady in time), transient (broad in frequency) and noise (the rest)
//   editing    a list of operations (a painted shape + erase / gain / move to a layer), applied when
//              drawing and when rendering; nothing is ever destroyed, undo is dropping the last one
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace bc
{
constexpr int kN = 2048, kHop = 512, kBins = kN / 2 + 1, kBands = 96, kDet = 48, kRows = 160, kCtx = 8;
constexpr int kAuto = 4, kMaxLayers = 8, kFeat = 16;
constexpr float kFMin = 30.0f;
enum { Background = 0, Tone = 1, Transient = 2, Noise = 3 };

// ---------------------------------------------------------------- FFT (radix 2, interleaved re/im)
class Fft
{
public:
    explicit Fft (int n) : N (n), rev ((size_t) n), tw ((size_t) n)
    {
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) { const double a = -2.0 * 3.14159265358979323846 * i / n; tw[(size_t) (2 * i)] = (float) std::cos (a); tw[(size_t) (2 * i + 1)] = (float) std::sin (a); }
    }
    void run (float* a, bool inverse) const noexcept
    {
        for (int i = 0; i < N; ++i) { const int j = rev[(size_t) i]; if (i < j) { std::swap (a[2 * i], a[2 * j]); std::swap (a[2 * i + 1], a[2 * j + 1]); } }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, stride = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = tw[(size_t) (2 * j * stride)], wi = inverse ? -tw[(size_t) (2 * j * stride + 1)] : tw[(size_t) (2 * j * stride + 1)];
                    float* u = a + 2 * (i + j); float* t = u + 2 * half;
                    const float vr = t[0] * wr - t[1] * wi, vi = t[0] * wi + t[1] * wr;
                    t[0] = u[0] - vr; t[1] = u[1] - vi; u[0] += vr; u[1] += vi;
                }
        }
        if (inverse) { const float s = 1.0f / (float) N; for (int i = 0; i < 2 * N; ++i) a[i] *= s; }
    }
private:
    int N; std::vector<int> rev; std::vector<float> tw;
};

struct Frames   // the shared tables of the main and the fine STFTs
{
    Fft fft { kN };
    std::vector<float> win; float sumW2 = 0;
    // the fine STFT that times a start: about 5 ms long at any rate (64 .. 512 samples)
    struct Fine { int n; Fft fft; std::vector<float> win; float w2 = 0; };
    std::vector<std::unique_ptr<Fine>> fines;
    Frames()
    {
        win.resize (kN);
        for (int i = 0; i < kN; ++i) { win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * 3.14159265358979323846 * i / kN); sumW2 += win[(size_t) i] * win[(size_t) i]; }
        for (int n = 64; n <= 512; n *= 2)
        {
            auto f = std::make_unique<Fine> (Fine { n, Fft (n), std::vector<float> ((size_t) n), 0.0f });
            for (int i = 0; i < n; ++i) { f->win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * 3.14159265358979323846 * i / n); f->w2 += f->win[(size_t) i] * f->win[(size_t) i]; }
            fines.push_back (std::move (f));
        }
    }
    const Fine& fineFor (double sr) const noexcept
    {
        const Fine* best = fines[0].get();
        for (const auto& f : fines) if (std::fabs (std::log ((double) f->n / (0.00533 * sr))) < std::fabs (std::log ((double) best->n / (0.00533 * sr)))) best = f.get();
        return *best;
    }
    static const Frames& get() { static const Frames f; return f; }
};

struct Audio
{
    std::vector<float> l, r;   // r empty: mono
    double sr = 48000.0;
    std::string name;
    bool stereo() const noexcept { return ! r.empty() && r.size() == l.size(); }
    int64_t size() const noexcept { return (int64_t) l.size(); }
};

// frame n is centred on sample n * kHop
inline void frameFft (const std::vector<float>& x, int64_t n, float* buf) noexcept
{
    const auto& fr = Frames::get();
    const int64_t s = n * kHop - kN / 2, len = (int64_t) x.size();
    for (int i = 0; i < kN; ++i) { const int64_t idx = s + i; buf[2 * i] = idx >= 0 && idx < len ? x[(size_t) idx] * fr.win[(size_t) i] : 0.0f; buf[2 * i + 1] = 0.0f; }
    fr.fft.run (buf, false);
}

inline float medianOf (float* v, int n) noexcept { std::nth_element (v, v + n / 2, v + n); return v[n / 2]; }

// ---------------------------------------------------------------- frequency tables for one sample rate
// The bands are the same at every rate (30 Hz .. 20 kHz on a log axis), so events in files of different rates
// are described alike and FIND MORE works across them; bands above a file's top are empty. The display
// axis runs to the file's own top.
constexpr float kFBand = 20000.0f;
inline float bandY (float f) noexcept { return std::log (std::max (kFMin, std::min (kFBand, f)) / kFMin) / std::log (kFBand / kFMin); }
struct Tables
{
    float fMax = 20000.0f;
    int bk0[kBands], bk1[kBands]; float bc[kBands], q[kBands];   // band -> bins (or a centre, for bands under a bin wide); percentile correction
    bool bandOk[kBands];                                          // under the file's top
    float binPos[kBins], binY[kBins];                             // bin -> fractional band, and 0..1 on the log axis
    float rowLo[kRows], rowHi[kRows];                             // display row -> bins
    void build (double sr)
    {
        fMax = (float) std::min (20000.0, 0.49 * sr);
        const double ratio = fMax / kFMin, toBin = kN / sr, bandRatio = kFBand / kFMin;
        for (int b = 0; b < kBands; ++b)
        {
            const double e0 = kFMin * std::pow (bandRatio, (double) b / kBands) * toBin, e1 = kFMin * std::pow (bandRatio, (double) (b + 1) / kBands) * toBin;
            bandOk[b] = e0 < 0.98 * (kBins - 1);
            bk0[b] = (int) std::ceil (e0); bk1[b] = std::min (kBins - 1, (int) std::ceil (e1) - 1); bc[b] = (float) std::min ((double) kBins - 1.001, 0.5 * (e0 + e1));
            const double k = std::max (1.0, (bk1[b] - bk0[b] + 1) / 1.5);   // degrees of freedom of the band mean (Hann overlaps neighbours)
            q[b] = (float) std::pow (1.0 - 1.0 / (9.0 * k) - 0.8416 * std::sqrt (1.0 / (9.0 * k)), 3.0);   // 20th percentile / mean (Wilson-Hilferty)
        }
        for (int k = 0; k < kBins; ++k)
        {
            const double f = std::max ((double) kFMin, std::min ((double) fMax, k * sr / kN));
            const double y = std::log (f / kFMin) / std::log (ratio);
            binY[k] = (float) y;
            binPos[k] = (float) std::max (0.0, std::min ((double) kBands - 1.0, (double) bandY ((float) f) * kBands - 0.5));
        }
        for (int r = 0; r < kRows; ++r)
        {
            rowLo[r] = (float) (kFMin * std::pow (ratio, (double) r / kRows) * toBin);
            rowHi[r] = (float) std::min ((double) kBins - 1.001, kFMin * std::pow (ratio, (double) (r + 1) / kRows) * toBin);
            rowLo[r] = std::min (rowLo[r], rowHi[r]);
        }
    }
    float yOf (float f) const noexcept { return std::log (std::max (kFMin, std::min (fMax, f)) / kFMin) / std::log (fMax / kFMin); }
    float freqOf (float y) const noexcept { return kFMin * std::pow (fMax / kFMin, std::max (0.0f, std::min (1.0f, y))); }
    void bandDensity (const float* m2, float* out) const noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            if (! bandOk[b]) { out[b] = 0.0f; continue; }
            if (bk1[b] >= bk0[b]) { float s = 0; for (int k = bk0[b]; k <= bk1[b]; ++k) s += m2[k]; out[b] = s / (float) (bk1[b] - bk0[b] + 1); }
            else { const int k = (int) bc[b]; const float f = bc[b] - (float) k; out[b] = m2[k] + (m2[k + 1] - m2[k]) * f; }
        }
    }
};

// ---------------------------------------------------------------- what the analysis pass keeps
struct Analysis
{
    double sr = 48000.0; int64_t length = 0; int frames = 0;
    Tables tab;
    int noiseHop = 1, noiseBlocks = 0; std::vector<float> noise;   // blocks x kBands: the mean noise density
    std::vector<float> S;      // frames x kDet: log (1 + energy / noise)
    float sig[kDet] {};        // each band's spread of S in noise (low bands with few bins fluctuate far more)
    std::vector<float> D;      // frames x kDet: S again, but over the background of the last 0.8 s where that is
                               // louder than the long floor (wind gusts, a passing car): what onsets are found in
    float sigD[kDet] {};
    std::vector<float> F;      // frames x kDet: foreground energy density
    std::vector<float> T, P;   // frames x kDet: the tone and transient layers' energy density
    int pool = 1, cols = 0; std::vector<uint8_t> grid[kAuto];   // cols x kRows, dB: 0 = -120 .. 255 = 0
    float dispTop = -20.0f;
    std::vector<float> peakMin, peakMax;   // the overview waveform
    double seconds() const noexcept { return (double) length / sr; }
    void noiseBands (int frame, float* out) const noexcept
    {
        const double pos = std::max (0.0, std::min ((double) noiseBlocks - 1.0, (double) frame / noiseHop));
        const int i0 = (int) pos, i1 = std::min (noiseBlocks - 1, i0 + 1); const float f = (float) (pos - i0);
        const float* a = &noise[(size_t) i0 * kBands]; const float* b = &noise[(size_t) i1 * kBands];
        for (int k = 0; k < kBands; ++k) out[k] = a[k] + (b[k] - a[k]) * f;
    }
};

// ---------------------------------------------------------------- a stretch of frames: power, spectra, layer masks
struct Region
{
    int f0 = 0, f1 = 0;
    std::vector<float> XL, XR;          // (f1 - f0) x kBins x 2, when kept
    std::vector<float> M2;              // (f1 - f0 + 2 kCtx) x kBins: power density (white noise of variance v reads v)
    std::vector<float> mask[kAuto];     // (f1 - f0) x kBins, summing to 1
    const float* power (int i) const noexcept { return &M2[(size_t) (i + kCtx) * kBins]; }
};

inline void computeRegion (const Audio& a, const Analysis& an, int f0, int f1, Region& rg, bool keepSpectra)
{
    const auto& fr = Frames::get();
    const int nf = f1 - f0, tot = nf + 2 * kCtx;
    const bool st = a.stereo();
    rg.f0 = f0; rg.f1 = f1;
    rg.M2.assign ((size_t) tot * kBins, 0.0f);
    if (keepSpectra) { rg.XL.assign ((size_t) nf * kBins * 2, 0.0f); if (st) rg.XR.assign ((size_t) nf * kBins * 2, 0.0f); else rg.XR.clear(); }
    std::vector<float> buf ((size_t) 2 * kN);
    const float norm = 1.0f / fr.sumW2;
    for (int i = 0; i < tot; ++i)
    {
        const int n = f0 - kCtx + i;
        if (n < 0 || n >= an.frames) continue;
        float* m = &rg.M2[(size_t) i * kBins];
        const bool keep = keepSpectra && i >= kCtx && i < kCtx + nf;
        frameFft (a.l, n, buf.data());
        for (int k = 0; k < kBins; ++k) m[k] = (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)]) * norm;
        if (keep) std::memcpy (&rg.XL[(size_t) (i - kCtx) * kBins * 2], buf.data(), sizeof (float) * kBins * 2);
        if (st)
        {
            frameFft (a.r, n, buf.data());
            for (int k = 0; k < kBins; ++k) m[k] = 0.5f * (m[k] + (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)]) * norm);
            if (keep) std::memcpy (&rg.XR[(size_t) (i - kCtx) * kBins * 2], buf.data(), sizeof (float) * kBins * 2);
        }
    }
    for (int L = 0; L < kAuto; ++L) rg.mask[L].assign ((size_t) nf * kBins, L == Background ? 1.0f : 0.0f);
    if (nf <= 0) return;
    // the power smoothed over 3 x 3 cells steadies the comparison with the noise floor
    std::vector<float> fs ((size_t) (nf + 2) * kBins);
    for (int i = -1; i <= nf; ++i)
    {
        const float* m = rg.power (i); float* o = &fs[(size_t) (i + 1) * kBins];
        for (int k = 0; k < kBins; ++k) o[k] = m[std::max (k - 1, 0)] + m[k] + m[std::min (k + 1, kBins - 1)];
    }
    float nb[kBands], nd[kBins], tmp[32];
    for (int i = 0; i < nf; ++i)
    {
        an.noiseBands (f0 + i, nb);
        for (int k = 0; k < kBins; ++k) { const float p = an.tab.binPos[k]; const int b0 = (int) p; nd[k] = nb[b0] + (nb[std::min (b0 + 1, kBands - 1)] - nb[b0]) * (p - (float) b0); }
        const float* m = rg.power (i);
        const float* s0 = &fs[(size_t) i * kBins]; const float* s1 = s0 + kBins; const float* s2 = s1 + kBins;
        float* m0 = &rg.mask[0][(size_t) i * kBins]; float* m1 = &rg.mask[1][(size_t) i * kBins]; float* m2 = &rg.mask[2][(size_t) i * kBins]; float* m3 = &rg.mask[3][(size_t) i * kBins];
        for (int k = 0; k < kBins; ++k)
        {
            const float snr = (s0[k] + s1[k] + s2[k]) * (1.0f / 9.0f) / nd[k];
            if (snr <= 1.5f) continue;
            const float fg = std::min (1.0f, (snr - 1.5f) / 2.5f) * std::sqrt (1.0f - 1.0f / snr);   // clear of the floor x spectral subtraction
            if (fg < 0.002f) continue;
            for (int j = -kCtx; j <= kCtx; ++j) tmp[j + kCtx] = rg.M2[(size_t) (i + kCtx + j) * kBins + (size_t) k];
            const float h2 = medianOf (tmp, 2 * kCtx + 1);                     // steady in time: tone
            const int lo = std::max (0, k - 10), hi = std::min (kBins - 1, k + 10);
            for (int j = lo; j <= hi; ++j) tmp[j - lo] = m[j];
            const float p2 = medianOf (tmp, hi - lo + 1);                      // broad in frequency: transient
            const float rH = std::sqrt ((h2 + 1.0e-20f) / (p2 + 1.0e-20f));
            const float tH = std::max (0.0f, std::min (1.0f, rH - 1.0f)), tP = std::max (0.0f, std::min (1.0f, 1.0f / rH - 1.0f));
            m0[k] = 1.0f - fg; m1[k] = fg * tH; m2[k] = fg * tP; m3[k] = fg * (1.0f - tH - tP);
        }
    }
}

// ---------------------------------------------------------------- the analysis pass
// progress (0..1) may be called from worker threads; return false from it to cancel (analyze then returns null)
inline std::shared_ptr<Analysis> analyze (const Audio& a, const std::function<bool (float)>& progress = {}, int threads = 0)
{
    auto an = std::make_shared<Analysis>();
    an->sr = a.sr; an->length = a.size();
    an->tab.build (a.sr);
    an->frames = (int) ((a.size() + kN / 2) / kHop + 1);
    const int frames = an->frames;
    if (threads <= 0) threads = (int) std::max (1u, std::min (4u, std::thread::hardware_concurrency()));
    std::atomic<bool> cancel { false };
    std::atomic<int> done { 0 };
    const int totalWork = frames / 4 + frames;
    auto tick = [&] (int n) { const int d = done.fetch_add (n) + n; if (progress && ! progress ((float) d / (float) std::max (1, totalWork))) cancel = true; };
    auto parallel = [&] (int count, int align, const std::function<void (int, int)>& fn)
    {
        const int T = std::max (1, std::min (threads, count / std::max (align, 64) + 1));
        std::vector<std::thread> th;
        int part = (count / T / align + 1) * align;
        for (int t = 0; t < T; ++t) { const int a0 = t * part, a1 = std::min (count, a0 + part); if (a0 < a1) th.emplace_back (fn, a0, a1); }
        for (auto& t : th) t.join();
    };
    const auto& fr = Frames::get();
    const bool st = a.stereo();
    const float norm = 1.0f / fr.sumW2;

    // ---- pass A: every 4th frame, the band densities the noise floor is read from
    const int cA = (frames + 3) / 4;
    std::vector<float> pa ((size_t) cA * kBands);
    parallel (cA, 1, [&] (int i0, int i1)
    {
        std::vector<float> buf ((size_t) 2 * kN), m ((size_t) kBins);
        for (int i = i0; i < i1 && ! cancel.load(); ++i)
        {
            frameFft (a.l, (int64_t) i * 4, buf.data());
            for (int k = 0; k < kBins; ++k) m[(size_t) k] = (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)]) * norm;
            if (st)
            {
                frameFft (a.r, (int64_t) i * 4, buf.data());
                for (int k = 0; k < kBins; ++k) m[(size_t) k] = 0.5f * (m[(size_t) k] + (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)]) * norm);
            }
            an->tab.bandDensity (m.data(), &pa[(size_t) i * kBands]);
            if ((i & 63) == 63) tick (64);
        }
    });
    if (cancel.load()) return nullptr;
    {
        const int wA = std::max (8, (int) std::lround (8.0 * a.sr / (4.0 * kHop))), hopA = std::max (1, wA / 2);
        an->noiseHop = hopA * 4;
        an->noiseBlocks = std::max (1, cA / hopA + 1);
        an->noise.assign ((size_t) an->noiseBlocks * kBands, 1.0e-9f);
        std::vector<float> v;
        for (int bi = 0; bi < an->noiseBlocks; ++bi)
        {
            int c0 = bi * hopA - wA / 2, c1 = bi * hopA + wA / 2;
            if (cA <= wA) { c0 = 0; c1 = cA; }
            else { if (c0 < 0) { c1 -= c0; c0 = 0; } if (c1 > cA) { c0 -= c1 - cA; c1 = cA; } }
            for (int b = 0; b < kBands; ++b)
            {
                v.clear();
                for (int c = c0; c < c1; ++c) v.push_back (pa[(size_t) c * kBands + (size_t) b]);
                if (v.empty()) continue;
                const size_t at = (size_t) (0.2 * (double) (v.size() - 1));
                std::nth_element (v.begin(), v.begin() + (ptrdiff_t) at, v.end());
                an->noise[(size_t) bi * kBands + (size_t) b] = std::max (1.0e-9f, v[at] / an->tab.q[b]);   // never under -90 dBFS: digital silence is not a floor
            }
        }
    }

    // ---- pass B: masks, detection bands and the display grid
    an->S.assign ((size_t) frames * kDet, 0.0f); an->F.assign ((size_t) frames * kDet, 0.0f);
    an->T.assign ((size_t) frames * kDet, 0.0f); an->P.assign ((size_t) frames * kDet, 0.0f);
    an->pool = 1; while (frames / an->pool > 60000) an->pool *= 2;   // about 40 MB of display grid at most
    an->cols = (frames + an->pool - 1) / an->pool;
    for (auto& g : an->grid) g.assign ((size_t) an->cols * kRows, 0);
    const float dispScale = std::sqrt (6.0f / (float) kN);   // a full-scale sine reads 0 dB
    parallel (frames, 128, [&] (int fa, int fb)
    {
        Region rg; float d[kBands], nb[kBands]; std::vector<float> fgm ((size_t) kBins), amp ((size_t) kBins), tm ((size_t) kBins), pm ((size_t) kBins);
        for (int b0 = fa; b0 < fb && ! cancel.load(); b0 += 128)
        {
            const int b1 = std::min (fb, b0 + 128);
            computeRegion (a, *an, b0, b1, rg, false);
            for (int n = b0; n < b1; ++n)
            {
                const int i = n - b0;
                const float* m = rg.power (i);
                an->tab.bandDensity (m, d); an->noiseBands (n, nb);
                float* S = &an->S[(size_t) n * kDet];
                for (int b = 0; b < kDet; ++b) S[b] = std::log (1.0f + (d[2 * b] + d[2 * b + 1]) / (nb[2 * b] + nb[2 * b + 1]));
                const float* mk[kAuto] = { &rg.mask[0][(size_t) i * kBins], &rg.mask[1][(size_t) i * kBins], &rg.mask[2][(size_t) i * kBins], &rg.mask[3][(size_t) i * kBins] };
                for (int k = 0; k < kBins; ++k)
                {
                    const float fg = 1.0f - mk[0][k];
                    fgm[(size_t) k] = fg * fg * m[k]; amp[(size_t) k] = std::sqrt (m[k]) * dispScale;
                    tm[(size_t) k] = mk[1][k] * mk[1][k] * m[k]; pm[(size_t) k] = mk[2][k] * mk[2][k] * m[k];
                }
                an->tab.bandDensity (fgm.data(), d);
                float* Fp = &an->F[(size_t) n * kDet];
                for (int b = 0; b < kDet; ++b) Fp[b] = d[2 * b] + d[2 * b + 1];
                an->tab.bandDensity (tm.data(), d);
                float* Tp = &an->T[(size_t) n * kDet];
                for (int b = 0; b < kDet; ++b) Tp[b] = d[2 * b] + d[2 * b + 1];
                an->tab.bandDensity (pm.data(), d);
                float* Pp = &an->P[(size_t) n * kDet];
                for (int b = 0; b < kDet; ++b) Pp[b] = d[2 * b] + d[2 * b + 1];
                const size_t col = (size_t) (n / an->pool);
                for (int r = 0; r < kRows; ++r)
                {
                    const float lo = an->tab.rowLo[r], hi = an->tab.rowHi[r];
                    for (int L = 0; L < kAuto; ++L)
                    {
                        float v = 0.0f;
                        if (hi - lo < 1.0f) { const float c = 0.5f * (lo + hi); const int k = (int) c; const float f = c - (float) k; v = mk[L][k] * amp[(size_t) k] * (1.0f - f) + mk[L][k + 1] * amp[(size_t) k + 1] * f; }
                        else for (int k = (int) std::ceil (lo); k <= (int) hi; ++k) v = std::max (v, mk[L][k] * amp[(size_t) k]);
                        const float db = 20.0f * std::log10 (v + 1.0e-7f);
                        const uint8_t u = (uint8_t) std::max (0.0f, std::min (255.0f, (db + 120.0f) * 2.125f + 0.5f));
                        uint8_t& cell = an->grid[L][col * kRows + (size_t) r];
                        if (u > cell) cell = u;
                    }
                }
            }
            tick (b1 - b0);
        }
    });
    if (cancel.load()) return nullptr;
    {
        // each detection band's spread in noise: the lower half of its values, never under what the band's
        // width allows in theory (0.5 / sqrt (effective bins)), so a quiet high band's small rise counts
        std::vector<float> v;
        for (int b = 0; b < kDet; ++b)
        {
            int bins = 0; for (int j = 2 * b; j <= 2 * b + 1; ++j) bins += std::max (0, an->tab.bk1[j] - an->tab.bk0[j] + 1);
            const float theory = 0.5f / std::sqrt (std::max (1.0f, (float) bins / 1.5f));
            v.clear(); for (int n = 0; n < frames; n += 2) v.push_back (an->S[(size_t) n * kDet + (size_t) b]);
            float spread = 0.0f;
            if (v.size() > 8)
            {
                std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (v.size() / 2), v.end()); const float p50 = v[v.size() / 2];
                std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (v.size() * 16 / 100), v.end()); const float p16 = v[v.size() * 16 / 100];
                spread = p50 - p16;
            }
            an->sig[b] = std::max (theory, std::min (spread, 4.0f * theory));
        }
        // The detection view. Per-channel energy normalisation (Wang et al. 2017; Lostanlen et al., "PCEN: why and
        // how", IEEE SPL 2019) divides each band by a smoothed recent energy, which turns a changing background into
        // steady, even noise, so its swells and its fluctuations at a higher level no longer read as onsets. Here the
        // recent background is the 10th percentile of the past 0.8 s: a sustained background (wind, traffic) lifts it,
        // events do not, even dense ones (a drum loop's tails), and it only counts where it stands above the long
        // floor, so steady recordings are left as they were.
        const int W = std::max (12, (int) std::lround (0.8 * a.sr / kHop)), stepD = 4;
        an->D.assign ((size_t) frames * kDet, 0.0f);
        std::vector<float> R ((size_t) frames), loc, w;
        for (int b = 0; b < kDet; ++b)
        {
            for (int n = 0; n < frames; ++n) R[(size_t) n] = std::exp (an->S[(size_t) n * kDet + (size_t) b]) - 1.0f;
            // the same percentile over the whole part: what the background reads when it is at its floor
            w.clear(); for (int n = 0; n < frames; n += 2) w.push_back (R[(size_t) n]);
            float ref = 1.0f;
            if (! w.empty()) { const size_t at = w.size() / 10; std::nth_element (w.begin(), w.begin() + (ptrdiff_t) at, w.end()); ref = std::max (1.0e-3f, w[at]); }
            loc.assign ((size_t) (frames / stepD + 2), ref);
            for (int i = 0; i * stepD < frames; ++i)
            {
                const int n = i * stepD, m0 = std::max (0, n - W), m1 = n - 2;   // the past only: an onset never lifts its own floor
                if (m1 - m0 < 6) continue;
                w.assign (R.begin() + m0, R.begin() + m1);
                const size_t at = w.size() / 10;
                std::nth_element (w.begin(), w.begin() + (ptrdiff_t) at, w.end());
                loc[(size_t) i] = w[at];
            }
            for (int n = 0; n < frames; ++n)
            {
                const int i0 = n / stepD, i1 = std::min ((int) loc.size() - 1, i0 + 1); const float f = (float) (n % stepD) / (float) stepD;
                const float g = std::max (1.0f, (loc[(size_t) i0] + (loc[(size_t) i1] - loc[(size_t) i0]) * f) / ref);
                an->D[(size_t) n * kDet + (size_t) b] = std::log (1.0f + R[(size_t) n] / g);
            }
        }
        for (int b = 0; b < kDet; ++b)
        {
            int bins = 0; for (int j = 2 * b; j <= 2 * b + 1; ++j) bins += std::max (0, an->tab.bk1[j] - an->tab.bk0[j] + 1);
            const float theory = 0.5f / std::sqrt (std::max (1.0f, (float) bins / 1.5f));
            v.clear(); for (int n = 0; n < frames; n += 2) v.push_back (an->D[(size_t) n * kDet + (size_t) b]);
            float spread = 0.0f;
            if (v.size() > 8)
            {
                std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (v.size() / 2), v.end()); const float p50 = v[v.size() / 2];
                std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (v.size() * 16 / 100), v.end()); const float p16 = v[v.size() * 16 / 100];
                spread = p50 - p16;
            }
            an->sigD[b] = std::max (theory, std::min (spread, 4.0f * theory));
        }
    }
    {
        std::array<int, 256> hist {}; int64_t total = 0;
        for (size_t i = 0; i < an->grid[0].size(); i += 3) { uint8_t v = 0; for (int L = 0; L < kAuto; ++L) v = std::max (v, an->grid[L][i]); ++hist[v]; ++total; }
        int64_t acc = 0; int top = 255;
        for (int v = 255; v >= 0; --v) { acc += hist[(size_t) v]; if (acc * 1000 >= total) { top = v; break; } }
        an->dispTop = std::max (-90.0f, (float) top / 2.125f - 120.0f);
    }
    {
        const int buckets = 2048; an->peakMin.assign (buckets, 0.0f); an->peakMax.assign (buckets, 0.0f);
        const int64_t len = a.size();
        for (int b = 0; b < buckets && len > 0; ++b)
        {
            const int64_t s0 = len * b / buckets, s1 = std::max (s0 + 1, len * (b + 1) / buckets);
            const int64_t step = std::max<int64_t> (1, (s1 - s0) / 256);
            float mn = 0, mx = 0;
            for (int64_t s = s0; s < s1 && s < len; s += step) { const float v = st ? 0.5f * (a.l[(size_t) s] + a.r[(size_t) s]) : a.l[(size_t) s]; mn = std::min (mn, v); mx = std::max (mx, v); }
            an->peakMin[(size_t) b] = mn; an->peakMax[(size_t) b] = mx;
        }
    }
    if (progress) progress (1.0f);
    return an;
}

// ---------------------------------------------------------------- events
struct DetectParams
{
    float sensitivity = 0.5f;   // 0 only the obvious .. 1 everything
    float minGapMs = 50.0f;     // two starts closer than this are one event
    float preRollMs = 3.0f;     // kept before the start
    float tailMs = 20.0f;       // kept after the sound has sunk into the floor
    float maxLenS = 4.0f, minLenMs = 15.0f;
    bool cutAtNext = true;      // an event ends where the next one starts
    // where an event ends: 0 where it sinks into the background, 1 at the next event (full length),
    // 2 at the quietest moment in the last tenth before the next event (clean cuts in breaks),
    // 3 where it has fallen 40 dB below its peak
    int endMode = 0;
    float grouping = 0.5f;      // 0 a few broad groups .. 1 many fine ones
};

struct Event
{
    int64_t start = 0, end = 0;   // samples
    int64_t onset = 0;            // the refined start, before the pre-roll
    int frame = 0;
    float salience = 0, snrDb = 0, peak = 0;
    int group = -1;
    bool enabled = true, manual = false;
    bool userEnd = false;         // the user set its end: cut-at-next leaves it alone
    bool found = false;           // found by FIND MORE (the search for more like one event)
    std::array<float, kFeat> feat {};
    std::array<float, kDet> rise {};   // where its onset rose (weights over the detection bands, summing to 1)
    bool reattack = false;        // struck again inside an earlier event (a bounce train, a flam): described like it
};

struct Group { std::string name; int count = 0, colour = 0; bool enabled = true; std::array<float, kFeat> centroid {}; };

inline float featDist (const std::array<float, kFeat>& a, const std::array<float, kFeat>& b) noexcept
{
    float s = 0; for (int i = 0; i < kFeat; ++i) s += (a[(size_t) i] - b[(size_t) i]) * (a[(size_t) i] - b[(size_t) i]);
    return std::sqrt (s);
}

namespace detail
{
// where, around frame n, the event's own bands leave the floor (a 256 / 32 STFT of the mono mix)
inline int64_t refineOnset (const Audio& a, const Analysis& an, int n, const float* w48)
{
    const auto& fine = Frames::get().fineFor (a.sr);
    const int FN = fine.n, FH = std::max (8, FN / 8), FB = FN / 2 + 1;
    const double half = 0.5 * kN / a.sr;   // the main window reaches this far either side of a frame's centre
    const int64_t c = (int64_t) n * kHop, len = a.size();
    const int64_t ws = c - (int64_t) ((half + 0.039) * a.sr), we = c + (int64_t) (std::max (0.045, (double) kHop / a.sr + 0.02) * a.sr);
    const int mF = (int) ((we - ws - FN) / FH);
    if (mF < 8) return std::max<int64_t> (0, c - kHop);
    std::vector<float> nd ((size_t) FB), wk ((size_t) FB);
    float nb[kBands]; float wsum = 0;
    an.noiseBands (n, nb);
    for (int j = 0; j < FB; ++j)
    {
        const float y = bandY ((float) (j * a.sr / FN));
        const float p = std::max (0.0f, std::min ((float) kBands - 1.0f, y * kBands - 0.5f)); const int b0 = (int) p;
        nd[j] = nb[b0] + (nb[std::min (b0 + 1, kBands - 1)] - nb[b0]) * (p - (float) b0);
        wk[j] = j == 0 ? 0.0f : w48[std::min (kDet - 1, (int) (y * kDet))]; wsum += wk[j];
    }
    if (wsum <= 0) { for (int j = 1; j < FB; ++j) wk[j] = 1.0f; wsum = (float) (FB - 1); }
    std::vector<float> L ((size_t) mF), buf ((size_t) 2 * FN);
    const bool st = a.stereo();
    for (int m = 0; m < mF; ++m)
    {
        const int64_t s = ws + (int64_t) m * FH;
        for (int i = 0; i < FN; ++i) { const int64_t idx = s + i; const float v = idx >= 0 && idx < len ? (st ? 0.5f * (a.l[(size_t) idx] + a.r[(size_t) idx]) : a.l[(size_t) idx]) : 0.0f; buf[(size_t) (2 * i)] = v * fine.win[(size_t) i]; buf[(size_t) (2 * i + 1)] = 0.0f; }
        fine.fft.run (buf.data(), false);
        float acc = 0;
        for (int j = 1; j < FB; ++j) if (wk[(size_t) j] > 0) acc += wk[(size_t) j] * std::log (1.0f + (buf[(size_t) (2 * j)] * buf[(size_t) (2 * j)] + buf[(size_t) (2 * j + 1)] * buf[(size_t) (2 * j + 1)]) / fine.w2 / nd[(size_t) j]);
        L[(size_t) m] = acc / wsum;
    }
    auto frameOf = [&] (double sec) { return std::max (0, std::min (mF - 1, (int) ((c + (int64_t) (sec * a.sr) - ws - FN / 2) / FH))); };
    const int p1 = std::max (3, frameOf (-(half + 0.009)));
    std::vector<float> pre (L.begin(), L.begin() + p1);
    const float floor = medianOf (pre.data(), (int) pre.size());
    int mPk = frameOf (-(half + 0.004)); for (int m = mPk; m <= frameOf (std::max (0.040, (double) kHop / a.sr + 0.015)); ++m) if (L[(size_t) m] > L[(size_t) mPk]) mPk = m;
    const float rise = L[(size_t) mPk] - floor;
    if (rise < 0.05f) return std::max<int64_t> (0, c - kHop);
    const float thr = floor + 0.25f * rise;
    int m = mPk; while (m > 0 && L[(size_t) (m - 1)] >= thr) --m;
    return std::max<int64_t> (0, std::min (len, ws + (int64_t) m * FH + FN - 40 * FN / 256));   // the frame's leading edge has just reached the sound
}

// The level of the bands w48 (log over the noise floor, weighted) on the fine STFT's grid: L[m] covers
// [ws + m * FH, ws + m * FH + FN). For starts closer together than the main frames can tell apart.
inline void fineLevel (const Audio& a, const Analysis& an, int noiseFrame, const float* w48, int64_t ws, int count, int FH, std::vector<float>& L)
{
    const auto& fine = Frames::get().fineFor (a.sr);
    const int FN = fine.n, FB = FN / 2 + 1;
    std::vector<float> nd ((size_t) FB), wk ((size_t) FB), buf ((size_t) 2 * FN);
    float nb[kBands]; float wsum = 0;
    an.noiseBands (std::max (0, std::min (an.frames - 1, noiseFrame)), nb);
    for (int j = 0; j < FB; ++j)
    {
        const float y = bandY ((float) (j * a.sr / FN));
        const float p = std::max (0.0f, std::min ((float) kBands - 1.0f, y * kBands - 0.5f)); const int b0 = (int) p;
        nd[(size_t) j] = nb[b0] + (nb[std::min (b0 + 1, kBands - 1)] - nb[b0]) * (p - (float) b0);
        wk[(size_t) j] = j == 0 ? 0.0f : w48[std::min (kDet - 1, (int) (y * kDet))]; wsum += wk[(size_t) j];
    }
    if (wsum <= 0) { for (int j = 1; j < FB; ++j) wk[(size_t) j] = 1.0f; wsum = (float) (FB - 1); }
    L.assign ((size_t) std::max (0, count), 0.0f);
    const bool st = a.stereo(); const int64_t len = a.size();
    for (int m = 0; m < count; ++m)
    {
        const int64_t s0 = ws + (int64_t) m * FH;
        for (int i = 0; i < FN; ++i) { const int64_t idx = s0 + i; const float v = idx >= 0 && idx < len ? (st ? 0.5f * (a.l[(size_t) idx] + a.r[(size_t) idx]) : a.l[(size_t) idx]) : 0.0f; buf[(size_t) (2 * i)] = v * fine.win[(size_t) i]; buf[(size_t) (2 * i + 1)] = 0.0f; }
        fine.fft.run (buf.data(), false);
        float acc = 0;
        for (int j = 1; j < FB; ++j) if (wk[(size_t) j] > 0) acc += wk[(size_t) j] * std::log (1.0f + (buf[(size_t) (2 * j)] * buf[(size_t) (2 * j)] + buf[(size_t) (2 * j + 1)] * buf[(size_t) (2 * j + 1)]) / fine.w2 / nd[(size_t) j]);
        L[(size_t) m] = acc / wsum;
    }
}

// weights over the detection bands: where the event rose (in each band's own noise spreads)
inline void riseWeights (const Analysis& an, int n, float* w)
{
    float sum = 0;
    for (int b = 0; b < kDet; ++b)
    {
        float top = 0; for (int j = 0; j <= 3; ++j) top = std::max (top, an.S[(size_t) std::min (an.frames - 1, n + j) * kDet + (size_t) b]);
        const float pre = an.S[(size_t) std::max (0, n - 3) * kDet + (size_t) b];
        const float z = (top - pre) / an.sig[b];
        w[b] = z > 1.0f ? z - 1.0f : 0.0f; sum += w[b];
    }
    if (sum < 1.0e-6f) { for (int b = 0; b < kDet; ++b) w[b] = 1.0f / kDet; return; }
    for (int b = 0; b < kDet; ++b) w[b] /= sum;
}

// the event's level at frame m, in noise spreads (0 = the floor's typical value)
inline float levelAt (const Analysis& an, int m, const float* w) noexcept
{
    const float* S = &an.S[(size_t) std::max (0, std::min (an.frames - 1, m)) * kDet];
    float acc = 0; for (int b = 0; b < kDet; ++b) if (w[b] > 0) acc += w[b] * S[b] / an.sig[b];
    return acc;
}

// floor, peak and end of the event that rises at frame n; false if nothing stands out
// afterEvent: an event was found just before, and may still be dying away
inline bool bounds (const Analysis& an, const DetectParams& p, int n, const float* w, int& endFrame, float& rise, float& peakLevel, bool afterEvent = false)
{
    float pre[16]; int np = 0;
    for (int m = n - 14; m <= n - 4; ++m) if (m >= 0) pre[np++] = levelAt (an, m, w);
    float floor = np > 0 ? medianOf (pre, np) : 0.0f;
    // just before the start: lower when an earlier event is still dying away (a hat right after a snare)
    float justBefore = 0.0f; int nj = 0;
    for (int m = n - 3; m <= n - 2; ++m) if (m >= 0) { justBefore += levelAt (an, m, w); ++nj; }
    if (nj > 0 && afterEvent) floor = std::min (floor, justBefore / (float) nj);   // only after an event, never in plain noise
    int mp = n; float pk = levelAt (an, n, w);
    for (int m = n; m <= std::min (an.frames - 1, n + 8); ++m) { const float v = levelAt (an, m, w); if (v > pk) { pk = v; mp = m; } }
    rise = pk - floor; peakLevel = pk;
    const int maxFrames = std::max (2, (int) (p.maxLenS * an.sr / kHop));
    endFrame = std::min (an.frames - 1, n + maxFrames);
    int below = 0;
    for (int m = mp + 1; m <= std::min (an.frames - 1, n + maxFrames); ++m)
    {
        const float lv = (levelAt (an, m - 1, w) + levelAt (an, m, w) + levelAt (an, m + 1, w)) / 3.0f;
        below = lv < floor + std::max (1.0f, 0.08f * rise) ? below + 1 : 0;
        if (below >= 3) { endFrame = m - 2; break; }
    }
    return rise > 0.0f;
}

// how far the event stands over the noise floor in its own bands, at its peak (dB)
inline float snrDb (const Analysis& an, int n, const float* w) noexcept
{
    float best = 0;
    for (int m = n; m <= std::min (an.frames - 1, n + 6); ++m)
    {
        const float* S = &an.S[(size_t) m * kDet]; float acc = 0;
        for (int b = 0; b < kDet; ++b) acc += w[b] * S[b];
        best = std::max (best, acc);
    }
    return 10.0f * std::log10 (std::max (1.0e-3f, std::exp (best) - 1.0f));
}

inline void features (const Audio& a, const Analysis& an, Event& e)
{
    const int n0 = std::max (0, std::min (an.frames - 1, (int) ((e.onset + kHop / 2) / kHop)));
    const int n1 = std::max (n0 + 2, std::min (an.frames, (int) (e.end / kHop) + 1));
    const int nSpec = std::min (n1, n0 + 14);
    // what was already sounding (an earlier event's tail, a steady tone): the quieter of the two frames
    // just before the start, taken off every band, so the event is described by its own sound
    float pre[kDet], preT[kDet], preP[kDet];
    for (int b = 0; b < kDet; ++b)
    {
        pre[b] = preT[b] = preP[b] = 1.0e30f;
        for (int m = n0 - 3; m <= n0 - 2; ++m)
        {
            if (m < 0) { pre[b] = preT[b] = preP[b] = 0.0f; break; }
            pre[b] = std::min (pre[b], an.F[(size_t) m * kDet + (size_t) b]);
            preT[b] = std::min (preT[b], an.T[(size_t) m * kDet + (size_t) b]);
            preP[b] = std::min (preP[b], an.P[(size_t) m * kDet + (size_t) b]);
        }
    }
    float mean[kDet] = {}; float tone = 0, trans = 0, all = 0;
    std::vector<float> env;
    for (int n = n0; n < std::min (n1, an.frames); ++n)
    {
        float s = 0;
        for (int b = 0; b < kDet; ++b)
        {
            const size_t i = (size_t) n * kDet + (size_t) b;
            const float f = std::max (0.0f, an.F[i] - pre[b]);
            if (n < nSpec) mean[b] += f;
            s += f; all += f;
            tone += std::max (0.0f, an.T[i] - preT[b]); trans += std::max (0.0f, an.P[i] - preP[b]);
        }
        env.push_back (s);
    }
    float total = 0, top = 0, cen = 0, logSum = 0;
    for (int b = 0; b < kDet; ++b) { total += mean[b]; top = std::max (top, mean[b]); cen += (float) b * mean[b]; }
    float lb[kDet];
    for (int b = 0; b < kDet; ++b) { lb[b] = std::max (-9.0f, std::log ((mean[b] + 1.0e-20f) / (top + 1.0e-20f))); logSum += lb[b]; }
    for (int i = 1; i <= 10; ++i)
    {
        float c = 0; for (int b = 0; b < kDet; ++b) c += lb[b] * std::cos (3.14159265f * (float) i * ((float) b + 0.5f) / kDet);
        e.feat[(size_t) (i - 1)] = c / kDet * (i <= 4 ? 1.0f : 0.6f);
    }
    e.feat[10] = total > 0 ? cen / total / kDet * 4.0f : 0.0f;                                   // where the weight sits on the log axis
    e.feat[11] = total > 0 ? std::exp (logSum / kDet) * top / (total / kDet) * 1.0f : 0.0f;      // flat (noisy) or peaked
    const float dur = (float) ((double) (e.end - e.onset) / a.sr);
    e.feat[12] = std::log2 (std::max (0.01f, dur)) * 0.45f;
    int pk = 0; for (int i = 1; i < (int) env.size(); ++i) if (env[(size_t) i] > env[(size_t) pk]) pk = i;
    int dec = (int) env.size() - pk; for (int i = pk; i < (int) env.size(); ++i) if (env[(size_t) i] < 0.01f * env[(size_t) pk]) { dec = i - pk; break; }
    e.feat[13] = std::log2 (std::max (0.01f, (float) dec * kHop / (float) a.sr)) * 0.35f;       // time to fall 20 dB
    e.feat[14] = all > 0 ? std::min (1.0f, tone / all) * 1.2f : 0.0f;
    e.feat[15] = all > 0 ? std::min (1.0f, trans / all) * 1.2f : 0.0f;
    float peak = 0; for (int64_t s = std::max<int64_t> (0, e.start); s < std::min (a.size(), e.end); ++s) { peak = std::max (peak, std::fabs (a.l[(size_t) s])); if (a.stereo()) peak = std::max (peak, std::fabs (a.r[(size_t) s])); }
    e.peak = peak;
    riseWeights (an, std::max (0, std::min (an.frames - 1, e.frame)), e.rise.data());
}
} // namespace detail

// the detection curve: the rise of the whitened bands, each in its own noise spreads, and its threshold
inline void fluxCurve (const Analysis& an, float sensitivity, std::vector<float>& flux, std::vector<float>& thr, std::vector<float>& spread)
{
    const int frames = an.frames;
    flux.assign ((size_t) frames, 0.0f); thr.assign ((size_t) frames, 0.0f); spread.assign ((size_t) frames, 1.0f);
    for (int n = 3; n < frames; ++n)
    {
        const float* S = &an.D[(size_t) n * kDet]; const float* A = &an.D[(size_t) (n - 2) * kDet]; const float* B = &an.D[(size_t) (n - 3) * kDet];
        float acc = 0;
        for (int b = 0; b < kDet; ++b)
        {
            float ref = 0; for (int j = std::max (0, b - 1); j <= std::min (kDet - 1, b + 1); ++j) ref = std::max (ref, std::max (A[j], B[j]));   // a gliding pitch is not an onset
            const float z = (S[b] - ref) / an.sigD[b];
            if (z > 0.0f) acc += std::min (z, 12.0f);   // no one band can make an event alone
        }
        flux[(size_t) n] = acc;
    }
    // Noise statistics from the quiet part of a long window: the 25th percentile and the spread of the lower half.
    // Events (a few frames each) do not reach them even when they come every 100 ms; a busy texture (rain, a crowd) does.
    const int W = std::max (16, (int) (2.0 * an.sr / kHop)), step = 8;
    const float s = std::max (0.0f, std::min (1.0f, sensitivity));
    const float lambda = 3.5f + 9.0f * (1.0f - s) * (1.0f - s), delta = 1.0f + 6.0f * (1.0f - s);
    std::vector<float> v, cen, spr;
    float globalSpread = 1.0f;
    {
        std::vector<float> all; for (int n = 3; n < frames; n += 2) all.push_back (flux[(size_t) n]);
        if (all.size() > 8)
        {
            std::nth_element (all.begin(), all.begin() + (ptrdiff_t) (all.size() / 2), all.end()); const float p50 = all[all.size() / 2];
            std::nth_element (all.begin(), all.begin() + (ptrdiff_t) (all.size() / 10), all.end()); const float p10 = all[all.size() / 10];
            globalSpread = std::max (0.5f, p50 - p10);
        }
    }
    for (int n = 0; n < frames; n += step)
    {
        v.assign (flux.begin() + std::max (3, n - W), flux.begin() + std::max (std::max (3, n - W) + 1, std::min (frames, n + W + 1)));
        const size_t m = v.size();
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 2), v.end()); const float p50 = v[m / 2];
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 4), v.end()); const float p25 = v[m / 4];
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 10), v.end()); const float p10 = v[m / 10];
        cen.push_back (p25); spr.push_back (std::max (0.6f * globalSpread, p50 - p10));
    }
    for (int n = 0; n < frames; ++n)
    {
        const int i0 = n / step, i1 = std::min ((int) cen.size() - 1, i0 + 1); const float f = (float) (n % step) / (float) step;
        const float c = cen[(size_t) i0] + (cen[(size_t) i1] - cen[(size_t) i0]) * f, d = spr[(size_t) i0] + (spr[(size_t) i1] - spr[(size_t) i0]) * f;
        thr[(size_t) n] = c + lambda * d + delta; spread[(size_t) n] = d;
    }
}

inline void finishEvent (const Audio& a, const Analysis&, const DetectParams& p, Event& e, int endFrame)
{
    e.start = std::max<int64_t> (0, e.onset - (int64_t) (p.preRollMs * 0.001 * a.sr));
    e.end = std::min (a.size(), (int64_t) endFrame * kHop + (int64_t) (p.tailMs * 0.001 * a.sr));
    e.end = std::min (e.end, e.onset + (int64_t) (p.maxLenS * a.sr));
    e.end = std::max (e.end, std::min (a.size(), e.onset + (int64_t) (p.minLenMs * 0.001 * a.sr)));
}

// an event the user placed by hand: it starts exactly where they clicked
inline Event makeManual (const Audio& a, const Analysis& an, const DetectParams& p, double seconds)
{
    Event e; e.manual = true;
    e.onset = std::max<int64_t> (0, std::min (a.size() - 1, (int64_t) (seconds * a.sr)));
    e.frame = std::max (0, std::min (an.frames - 1, (int) ((e.onset + kHop) / kHop)));
    float w[kDet]; detail::riseWeights (an, e.frame, w);
    int endFrame = e.frame + 4; float rise = 0, pk = 0;
    detail::bounds (an, p, e.frame, w, endFrame, rise, pk);
    DetectParams q = p; q.preRollMs = 0.0f;
    finishEvent (a, an, q, e, endFrame);
    e.salience = rise; e.snrDb = detail::snrDb (an, e.frame, w); (void) pk;
    detail::features (a, an, e);
    return e;
}

// What FIND MORE searches with: the bands one event rose in, and how it sounds.
struct Template
{
    bool valid = false;
    std::array<float, kDet> w {};
    std::array<float, kFeat> feat {};
};

inline Template makeTemplate (const Analysis& an, const Event& e)
{
    Template t;
    t.valid = true;
    float sum = 0; for (float v : e.rise) sum += v;
    if (sum > 0.0f) t.w = e.rise;   // where it rose (a strike inside another event has its first strike's)
    else detail::riseWeights (an, std::max (0, std::min (an.frames - 1, e.frame)), t.w.data());
    t.feat = e.feat;
    return t;
}

inline float likeRadius (float similarity) noexcept { const float s = 1.0f - std::max (0.0f, std::min (1.0f, similarity)); return 0.3f + 2.7f * s * std::sqrt (s); }

// how alike an onset is to the reference's: the cosine of where the two rose (1: the same bands)
inline float onsetCos (const std::array<float, kDet>& a, const std::array<float, kDet>& b) noexcept
{
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < kDet; ++i) { ab += (double) a[(size_t) i] * b[(size_t) i]; aa += (double) a[(size_t) i] * a[(size_t) i]; bb += (double) b[(size_t) i] * b[(size_t) i]; }
    return aa > 0 && bb > 0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
}

// The distance LIKE and FIND MORE judge by. An event's description is blurred by whatever sounds with it (talk,
// rain, a tail), but where its onset rose is not: steady sound underneath does not rise with it. So an onset
// that rose in the reference's bands gets its description's distance shrunk (down to 0.4 of it), one that rose
// elsewhere is not like it at all, and in between the description decides as before.
inline float likeDist (const Event& e, const std::array<float, kFeat>& feat, const std::array<float, kDet>& rise, float similarity) noexcept
{
    const float d = featDist (e.feat, feat);
    const float c = onsetCos (e.rise, rise);
    const float cMin = 0.45f + 0.3f * std::max (0.0f, std::min (1.0f, similarity));
    if (c < cMin) return 1.0e9f;
    return d * std::max (0.4f, std::min (1.0f, (1.0f - c) / 0.2f));
}

namespace detail
{
// robust running statistics of a curve: the 25th percentile and the spread of the lower half (p50 - p10)
inline void runningStats (const std::vector<float>& x, int from, int W, float minSpread, std::vector<float>& cen, std::vector<float>& spr)
{
    const int frames = (int) x.size(), step = 8;
    cen.assign ((size_t) frames, 0.0f); spr.assign ((size_t) frames, minSpread);
    std::vector<float> v, c, d;
    for (int n = 0; n < frames; n += step)
    {
        const int lo = std::max (from, n - W), hi = std::max (lo + 1, std::min (frames, n + W + 1));
        v.assign (x.begin() + lo, x.begin() + hi);
        const size_t m = v.size();
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 2), v.end()); const float p50 = v[m / 2];
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 4), v.end()); const float p25 = v[m / 4];
        std::nth_element (v.begin(), v.begin() + (ptrdiff_t) (m / 10), v.end()); const float p10 = v[m / 10];
        c.push_back (p25); d.push_back (std::max (minSpread, p50 - p10));
    }
    for (int n = 0; n < frames; ++n)
    {
        const int i0 = n / step, i1 = std::min ((int) c.size() - 1, i0 + 1); const float f = (float) (n % step) / (float) step;
        cen[(size_t) n] = c[(size_t) i0] + (c[(size_t) i1] - c[(size_t) i0]) * f;
        spr[(size_t) n] = d[(size_t) i0] + (d[(size_t) i1] - d[(size_t) i0]) * f;
    }
}

// the user's events win; detected ones too close to them, or where the user deleted one, go
inline void merge (std::vector<Event>& out, const std::vector<Event>& keep, const std::vector<int64_t>& deleted, int64_t gap, double sr)
{
    std::vector<Event> kept;
    for (const auto& e : out)
    {
        bool drop = false;
        for (const auto& k : keep) if (std::llabs (k.onset - e.onset) < gap) { drop = true; break; }
        for (const auto d : deleted) if (std::llabs (d - e.onset) < std::max<int64_t> (gap, (int64_t) (0.03 * sr))) { drop = true; break; }
        if (! drop) kept.push_back (e);
    }
    for (const auto& k : keep) kept.push_back (k);
    std::sort (kept.begin(), kept.end(), [] (const Event& x, const Event& y) { return x.onset < y.onset; });
    out.swap (kept);
}

// ends at the next start (unless the user set the end), and the description of each
// a 2 ms RMS envelope of [s0, s1), one value per 2 ms
inline void envelope (const Audio& a, int64_t s0, int64_t s1, int step, std::vector<float>& env)
{
    env.clear();
    const bool st = a.stereo();
    for (int64_t s = s0; s < s1; s += step)
    {
        double e = 0; int n = 0;
        for (int64_t i = s; i < std::min (s1, s + step); ++i, ++n) { const double v = st ? 0.5 * ((double) a.l[(size_t) i] + a.r[(size_t) i]) : a.l[(size_t) i]; e += v * v; }
        env.push_back ((float) std::sqrt (e / std::max (1, n)));
    }
}

// Strikes inside an event that the main frames (43 ms long) cannot tell apart: the last bounces of a ball, a
// flam, a rattle. Each event's own bands are followed on the fine grid (about 5 ms windows) up to the next start,
// and a sharp new rise there, at least the minimum gap from the starts either side, is a start of its own. Only
// when the gap allows starts closer than the main frames resolve (the multi-resolution idea of Böck's onset
// detectors). The rise must be a real strike: a large part of the event's own first rise, not a ringing's beat,
// and well beyond what the sound around it does anyway.
inline void reattacks (const Audio& a, const Analysis& an, const DetectParams& p, std::vector<Event>& ev)
{
    const int64_t gap = std::max<int64_t> (1, (int64_t) (p.minGapMs * 0.001 * a.sr));
    const auto& fine = Frames::get().fineFor (a.sr);
    const int FN = fine.n, FH = std::max (4, FN / 4), span = FN / FH;   // L[m] - L[m - span]: two windows side by side
    std::vector<Event> add; std::vector<float> L;
    float w[kDet];
    for (size_t i = 0; i < ev.size(); ++i)
    {
        const Event& e = ev[i];
        const int64_t next = i + 1 < ev.size() ? ev[i + 1].onset : a.size();
        const int64_t limit = std::min<int64_t> (next, e.onset + (int64_t) (0.4 * a.sr));
        if (limit - e.onset < 2 * gap) continue;
        const int64_t ws = e.onset - FN - 4 * FH;
        const int count = (int) ((limit - ws - FN) / FH);
        if (count < 2 * span + 8) continue;
        riseWeights (an, std::max (0, std::min (an.frames - 1, e.frame)), w);
        fineLevel (a, an, e.frame, w, ws, count, FH, L);
        const float pre = std::min (std::min (L[0], L[1]), std::min (L[2], L[3]));
        float peak0 = pre; for (int m = 4; m < std::min (count, 4 + (int) (0.015 * a.sr / FH) + span); ++m) peak0 = std::max (peak0, L[(size_t) m]);
        const float rise0 = peak0 - pre;
        if (rise0 < 1.5f) continue;   // a clear event only
        // what the texture around it does all the time (rain, talk) is not a new strike: the rises over the
        // stretch, their median and spread (a bounce train between strikes only decays, so it sets no bar)
        std::vector<float> dv; for (int m = span + 4; m < count; ++m) dv.push_back (L[(size_t) m] - L[(size_t) (m - span)]);
        std::nth_element (dv.begin(), dv.begin() + (ptrdiff_t) (dv.size() / 2), dv.end()); const float med = dv[dv.size() / 2];
        for (auto& x : dv) x = std::fabs (x - med);
        std::nth_element (dv.begin(), dv.begin() + (ptrdiff_t) (dv.size() / 2), dv.end()); const float mad = dv[dv.size() / 2];
        const float need = std::max (std::max (0.9f, 0.4f * rise0), med + 8.0f * mad);
        int64_t last = e.onset;
        const int half = std::max (1, (int) (gap / FH / 2));
        for (int m = span + 4; m < count; ++m)
        {
            const float d = L[(size_t) m] - L[(size_t) (m - span)];
            if (d < need) continue;
            bool top = true;
            for (int k = std::max (span, m - half); k <= std::min (count - 1, m + half) && top; ++k) if (k != m && L[(size_t) k] - L[(size_t) (k - span)] > d) top = false;
            if (! top) continue;
            // where the rise begins: the first window reaching a quarter of it (its leading edge has just met the sound)
            int k = m - span; while (k < m && L[(size_t) k] < L[(size_t) (m - span)] + 0.25f * d) ++k;
            const int64_t on = ws + (int64_t) k * FH + FN - 40 * FN / 256;
            if (on - last < gap || next - on < gap) continue;
            Event r; r.onset = on; r.reattack = true;
            r.frame = std::max (0, std::min (an.frames - 1, (int) ((on + kHop / 2) / kHop)));
            r.salience = d / 0.9f; r.snrDb = e.snrDb;
            finishEvent (a, an, p, r, std::min (an.frames - 1, (int) (limit / kHop) + 1));
            add.push_back (r);
            last = on;
        }
    }
    if (add.empty()) return;
    ev.insert (ev.end(), add.begin(), add.end());
    std::sort (ev.begin(), ev.end(), [] (const Event& x, const Event& y) { return x.onset < y.onset; });
}

inline void finish (const Audio& a, const Analysis& an, const DetectParams& p, std::vector<Event>& out)
{
    const int step = std::max (8, (int) (0.002 * a.sr));
    std::vector<float> env;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& e = out[i];
        const int64_t next = i + 1 < out.size() ? out[i + 1].start : a.size();
        if (! e.userEnd)
        {
            if (p.endMode >= 1) e.end = std::min (a.size(), e.onset + (int64_t) (p.maxLenS * a.sr));   // as far as it may go
            if ((p.cutAtNext || p.endMode == 1 || p.endMode == 2) && i + 1 < out.size()) e.end = std::max (e.onset + 32, std::min (e.end, next));
            if (p.endMode == 2 && e.end - e.onset > 4 * step)
            {
                // the quietest 2 ms in the last tenth: the cut falls in the gap, not in the next hit's lead-in
                const int64_t from = e.end - (e.end - e.onset) / 10;
                envelope (a, from, e.end, step, env);
                size_t q = env.size() - 1; for (size_t k = 0; k < env.size(); ++k) if (env[k] < env[q]) q = k;
                e.end = std::min (e.end, from + (int64_t) (q + 1) * step);
            }
            if (p.endMode == 3 && e.end - e.onset > 4 * step)
            {
                envelope (a, e.onset, e.end, step, env);
                size_t pk = 0; for (size_t k = 0; k < env.size(); ++k) if (env[k] > env[pk]) pk = k;
                for (size_t k = pk; k < env.size(); ++k) if (env[k] < 0.01f * env[pk]) { e.end = e.onset + (int64_t) (k + 1) * step; break; }
            }
        }
        e.end = std::max (e.end, e.start + 32);
        detail::features (a, an, e);
        // a strike inside an earlier event is the same thing struck again: described like the one it is in
        if (e.reattack)
            for (size_t j = i; j-- > 0;)
                if (! out[j].reattack) { const float dur = e.feat[12]; e.feat = out[j].feat; e.feat[12] = dur; e.rise = out[j].rise; break; }
    }
}
} // namespace detail

// Events like a template, searched for in the template's own bands only: noise elsewhere in the spectrum
// no longer hides them, so much quieter ones are found. Returns new events not within gap of `existing`.
inline std::vector<Event> findLike (const Audio& a, const Analysis& an, const DetectParams& p, const Template& tp, float similarity, const std::vector<Event>& existing)
{
    std::vector<Event> out;
    const int frames = an.frames;
    if (! tp.valid || frames < 8) return out;
    std::vector<float> fw ((size_t) frames, 0.0f);
    for (int n = 3; n < frames; ++n)
    {
        const float* S = &an.D[(size_t) n * kDet]; const float* A = &an.D[(size_t) (n - 2) * kDet]; const float* B = &an.D[(size_t) (n - 3) * kDet];
        float acc = 0;
        for (int b = 0; b < kDet; ++b)
        {
            if (tp.w[(size_t) b] <= 0.0f) continue;
            float ref = 0; for (int j = std::max (0, b - 1); j <= std::min (kDet - 1, b + 1); ++j) ref = std::max (ref, std::max (A[j], B[j]));
            const float z = (S[b] - ref) / an.sigD[b];
            if (z > 0.0f) acc += tp.w[(size_t) b] * std::min (z, 12.0f);
        }
        fw[(size_t) n] = acc;
    }
    std::vector<float> cen, spr;
    detail::runningStats (fw, 3, std::max (16, (int) (2.0 * an.sr / kHop)), 0.15f, cen, spr);
    const float sens = std::max (0.0f, std::min (1.0f, p.sensitivity));
    const float lambda = 4.5f + 4.0f * (1.0f - sens) * (1.0f - sens);
    const int64_t gap = std::max<int64_t> (1, (int64_t) (p.minGapMs * 0.001 * a.sr));
    const int g = std::max (1, (int) (gap / kHop / 2));
    const float radius = likeRadius (similarity) * 1.3f;
    for (int n = 3; n < frames - 1; ++n)
    {
        const float z = (fw[(size_t) n] - cen[(size_t) n]) / spr[(size_t) n];
        if (z < lambda) continue;
        bool top = true;
        for (int m = std::max (3, n - g); m <= std::min (frames - 1, n + g) && top; ++m) if (fw[(size_t) m] > fw[(size_t) n] || (fw[(size_t) m] == fw[(size_t) n] && m < n)) top = false;
        if (! top) continue;
        int endFrame = n; float rise = 0, pk = 0;
        if (! detail::bounds (an, p, n, tp.w.data(), endFrame, rise, pk) || rise < 1.0f) continue;
        Event e; e.frame = n; e.found = true;
        e.onset = detail::refineOnset (a, an, n, tp.w.data());
        bool near = false;
        for (const auto& x : existing) if (std::llabs (x.onset - e.onset) < gap) { near = true; break; }
        for (const auto& x : out) if (std::llabs (x.onset - e.onset) < gap) { near = true; break; }
        if (near) continue;
        e.salience = z / lambda;
        e.snrDb = detail::snrDb (an, n, tp.w.data());
        finishEvent (a, an, p, e, endFrame);
        detail::features (a, an, e);
        // it rose in the same bands; does it sound alike? A faint one's description is blurred by the noise
        // over it, so the fainter it is, the more slack it gets
        if (likeDist (e, tp.feat, tp.w, similarity) > radius * (1.0f + 1.5f / std::max (1.0f, rise))) continue;
        out.push_back (e);
    }
    return out;
}

// keep: the user's own events (kept as they are). deleted: starts the user removed (not found again).
// tp (optional): also FIND MORE like it.
inline std::vector<Event> detect (const Audio& a, const Analysis& an, const DetectParams& p, const std::vector<Event>& keep = {}, const std::vector<int64_t>& deleted = {},
                                  const Template* tp = nullptr, float similarity = 0.5f)
{
    std::vector<Event> out;
    const int frames = an.frames;
    if (frames < 8 || a.size() < 64) { out = keep; return out; }
    std::vector<float> flux, thr, spread;
    fluxCurve (an, p.sensitivity, flux, thr, spread);
    const int64_t gap = std::max<int64_t> (1, (int64_t) (p.minGapMs * 0.001 * a.sr));
    const int g = std::max (1, (int) (gap / kHop / 2));
    float w[kDet];
    int last = -1000000;
    const int recent = (int) (0.6 * an.sr / kHop);
    // The first frames compare with the silence before the file, so every recording would "start" there.
    // An event at the very start only when the opening stands well clear of the noise (a file that begins
    // on a hit), not when it begins in the middle of a steady background.
    const int firstFrame = kN / (2 * kHop) + 3;
    {
        float open = 0.0f; for (int b = 0; b < kDet; ++b) open += an.S[(size_t) std::min (frames - 1, 2) * kDet + (size_t) b];
        open /= (float) kDet;
        if (open > 2.0f)
        {
            Event e; e.frame = 2; e.onset = 0;
            for (int b = 0; b < kDet; ++b) w[b] = 1.0f / kDet;
            int endFrame = 2; float rise = 0, pk = 0;
            detail::bounds (an, p, 2, w, endFrame, rise, pk);
            e.salience = open; e.snrDb = detail::snrDb (an, 2, w);
            finishEvent (a, an, p, e, endFrame);
            out.push_back (e);
            last = 2;
        }
    }
    for (int n = firstFrame; n < frames - 1; ++n)
    {
        if (flux[(size_t) n] <= thr[(size_t) n]) continue;
        bool top = true;
        for (int m = std::max (3, n - g); m <= std::min (frames - 1, n + g) && top; ++m) if (flux[(size_t) m] > flux[(size_t) n] || (flux[(size_t) m] == flux[(size_t) n] && m < n)) top = false;
        if (! top) continue;
        detail::riseWeights (an, n, w);
        int endFrame = n; float rise = 0, pk = 0;
        if (! detail::bounds (an, p, n, w, endFrame, rise, pk, n - last < recent) || rise < 2.0f) continue;   // the rise must hold, not be one flicker
        if (rise >= 5.0f) last = n;   // only a clear event lets the next one be measured from its dying tail
        Event e; e.frame = n;
        e.onset = detail::refineOnset (a, an, n, w);
        e.salience = (flux[(size_t) n] - (thr[(size_t) n] - spread[(size_t) n])) / spread[(size_t) n];
        e.snrDb = detail::snrDb (an, n, w);
        finishEvent (a, an, p, e, endFrame);
        out.push_back (e);
    }
    std::sort (out.begin(), out.end(), [] (const Event& x, const Event& y) { return x.onset < y.onset; });
    // too close together: the stronger one stays
    std::vector<Event> spaced;
    for (const auto& e : out)
    {
        if (! spaced.empty() && e.onset - spaced.back().onset < gap) { if (e.salience > spaced.back().salience) spaced.back() = e; continue; }
        spaced.push_back (e);
    }
    out.swap (spaced);
    if (p.minGapMs < 45.0f) detail::reattacks (a, an, p, out);   // closer starts than the main frames resolve
    if (tp != nullptr && tp->valid)
    {
        for (auto& e : out) detail::features (a, an, e);   // the search compares descriptions
        auto more = findLike (a, an, p, *tp, similarity, out);
        out.insert (out.end(), more.begin(), more.end());
        std::sort (out.begin(), out.end(), [] (const Event& x, const Event& y) { return x.onset < y.onset; });
    }
    detail::merge (out, keep, deleted, gap, a.sr);
    detail::finish (a, an, p, out);
    return out;
}

// ---------------------------------------------------------------- groups of events that sound alike
inline void group (std::vector<Event>& ev, std::vector<Group>& groups, float grouping)
{
    groups.clear();
    const int n = (int) ev.size();
    if (n == 0) return;
    const float tau = 2.3f - 1.6f * std::max (0.0f, std::min (1.0f, grouping));
    std::vector<int> order ((size_t) n); for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::sort (order.begin(), order.end(), [&] (int x, int y) { return ev[(size_t) x].salience > ev[(size_t) y].salience; });
    std::vector<std::array<float, kFeat>> cen; std::vector<int> cnt; std::vector<int> member ((size_t) n, -1);
    for (int i : order)   // the strongest events found the groups
    {
        int best = -1; float bd = 1.0e9f;
        for (int c = 0; c < (int) cen.size(); ++c) { const float d = featDist (ev[(size_t) i].feat, cen[(size_t) c]); if (d < bd) { bd = d; best = c; } }
        if (best < 0 || (bd > tau && cen.size() < 24)) { cen.push_back (ev[(size_t) i].feat); cnt.push_back (1); member[(size_t) i] = (int) cen.size() - 1; continue; }
        auto& c = cen[(size_t) best]; const float k = (float) cnt[(size_t) best];
        for (int f = 0; f < kFeat; ++f) c[(size_t) f] = (c[(size_t) f] * k + ev[(size_t) i].feat[(size_t) f]) / (k + 1.0f);
        ++cnt[(size_t) best]; member[(size_t) i] = best;
    }
    for (int it = 0; it < 4; ++it)
    {
        // merge groups that drifted together, then every event to its nearest
        for (int x = 0; x < (int) cen.size(); ++x) for (int y = x + 1; y < (int) cen.size(); ++y)
            if (cnt[(size_t) x] > 0 && cnt[(size_t) y] > 0 && featDist (cen[(size_t) x], cen[(size_t) y]) < 0.5f * tau)
            {
                const float kx = (float) cnt[(size_t) x], ky = (float) cnt[(size_t) y];
                for (int f = 0; f < kFeat; ++f) cen[(size_t) x][(size_t) f] = (cen[(size_t) x][(size_t) f] * kx + cen[(size_t) y][(size_t) f] * ky) / (kx + ky);
                cnt[(size_t) x] += cnt[(size_t) y]; cnt[(size_t) y] = 0;
            }
        for (int i = 0; i < n; ++i)
        {
            int best = 0; float bd = 1.0e9f;
            for (int c = 0; c < (int) cen.size(); ++c) if (cnt[(size_t) c] > 0) { const float d = featDist (ev[(size_t) i].feat, cen[(size_t) c]); if (d < bd) { bd = d; best = c; } }
            member[(size_t) i] = best;
        }
        std::vector<std::array<float, kFeat>> sum (cen.size()); std::fill (cnt.begin(), cnt.end(), 0);
        for (auto& s : sum) s.fill (0.0f);
        for (int i = 0; i < n; ++i) { ++cnt[(size_t) member[(size_t) i]]; for (int f = 0; f < kFeat; ++f) sum[(size_t) member[(size_t) i]][(size_t) f] += ev[(size_t) i].feat[(size_t) f]; }
        for (size_t c = 0; c < cen.size(); ++c) if (cnt[c] > 0) for (int f = 0; f < kFeat; ++f) cen[c][(size_t) f] = sum[c][(size_t) f] / (float) cnt[c];
    }
    std::vector<int> ids; for (int c = 0; c < (int) cen.size(); ++c) if (cnt[(size_t) c] > 0) ids.push_back (c);
    std::sort (ids.begin(), ids.end(), [&] (int x, int y) { return cnt[(size_t) x] > cnt[(size_t) y]; });
    std::vector<int> remap (cen.size(), 0);
    for (int gi = 0; gi < (int) ids.size(); ++gi)
    {
        const int c = ids[(size_t) gi]; remap[(size_t) c] = gi;
        Group gr; gr.count = cnt[(size_t) c]; gr.colour = gi; gr.centroid = cen[(size_t) c];
        // a plain name from how it sounds
        const float y = cen[(size_t) c][10] / 4.0f, dur = std::pow (2.0f, cen[(size_t) c][12] / 0.45f), tonal = cen[(size_t) c][14] / 1.2f, flat = cen[(size_t) c][11];
        const float hz = kFMin * std::pow (20000.0f / kFMin, y);
        const char* reg = hz < 300.0f ? "low" : hz < 2500.0f ? "mid" : "high";
        const char* kind = dur < 0.07f ? "click" : (tonal > 0.5f && dur > 0.25f) ? "ring" : (flat > 0.35f && dur > 0.12f) ? "burst" : "hit";
        gr.name = std::string (reg) + " " + kind;
        int same = 0; for (const auto& o : groups) if (o.name.compare (0, gr.name.size(), gr.name) == 0) ++same;
        if (same > 0) gr.name += " " + std::to_string (same + 1);
        groups.push_back (gr);
    }
    for (int i = 0; i < n; ++i) ev[(size_t) i].group = remap[(size_t) member[(size_t) i]];
}

// ---------------------------------------------------------------- which events are exported
enum class Pick { All = 0, Best = 1, Like = 2 };
struct PickParams
{
    Pick mode = Pick::All; int bestN = 1; float similarity = 0.5f;
    std::array<float, kFeat> ref {}; bool hasRef = false;      // LIKE: the reference's description
    std::array<float, kDet> refRise {}; bool hasRise = false;  // and where its onset rose (sharper; see likeDist)
};


inline std::vector<int> pick (const std::vector<Event>& ev, const std::vector<Group>& groups, const PickParams& p)
{
    std::vector<int> out;
    auto usable = [&] (int i) { const auto& e = ev[(size_t) i]; return e.enabled && (e.group < 0 || e.group >= (int) groups.size() || groups[(size_t) e.group].enabled); };
    if (p.mode == Pick::Like && p.hasRef)
    {
        const float rad = likeRadius (p.similarity);
        for (int i = 0; i < (int) ev.size(); ++i)
            if (usable (i) && (p.hasRise ? likeDist (ev[(size_t) i], p.ref, p.refRise, p.similarity) : featDist (ev[(size_t) i].feat, p.ref)) <= rad) out.push_back (i);
    }
    else if (p.mode == Pick::Best)
    {
        for (int gi = 0; gi < (int) groups.size(); ++gi)
        {
            std::vector<std::pair<float, int>> q;
            for (int i = 0; i < (int) ev.size(); ++i)
                if (ev[(size_t) i].group == gi && usable (i)) q.push_back ({ ev[(size_t) i].snrDb - 8.0f * featDist (ev[(size_t) i].feat, groups[(size_t) gi].centroid), i });   // clear of the noise, typical of its group
            std::sort (q.begin(), q.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
            for (int k = 0; k < std::min ((int) q.size(), std::max (1, p.bestN)); ++k) out.push_back (q[(size_t) k].second);
        }
        std::sort (out.begin(), out.end());
    }
    else for (int i = 0; i < (int) ev.size(); ++i) if (usable (i)) out.push_back (i);
    return out;
}

// ---------------------------------------------------------------- layers and edits
// A painted area of the spectrogram: a small bitmap over a time range and a range of the log-frequency axis.
struct Shape
{
    double t0 = 0, t1 = 0; float y0 = 0, y1 = 1;
    int w = 1, h = 1; std::vector<uint8_t> a { 255 };   // row 0 is the lowest frequency
    static Shape rect (double ta, double tb, float ya, float yb) { Shape s; s.t0 = std::min (ta, tb); s.t1 = std::max (ta, tb); s.y0 = std::min (ya, yb); s.y1 = std::max (ya, yb); return s; }
    bool empty() const noexcept { if (t1 <= t0 || y1 <= y0) return true; for (auto v : a) if (v) return false; return true; }
    float at (double t, float y) const noexcept   // 0..1
    {
        if (t < t0 || t > t1 || y < y0 || y > y1) return 0.0f;
        if (w == 1 && h == 1) return (float) a[0] * (1.0f / 255.0f);
        const float u = std::max (0.0f, std::min ((float) w - 1.0f, (float) ((t - t0) / (t1 - t0)) * (float) w - 0.5f));
        const float v = std::max (0.0f, std::min ((float) h - 1.0f, (y - y0) / (y1 - y0) * (float) h - 0.5f));
        const int i0 = (int) u, j0 = (int) v, i1 = std::min (w - 1, i0 + 1), j1 = std::min (h - 1, j0 + 1);
        const float fu = u - (float) i0, fv = v - (float) j0;
        const float p = (float) a[(size_t) (j0 * w + i0)] * (1 - fu) + (float) a[(size_t) (j0 * w + i1)] * fu, q = (float) a[(size_t) (j1 * w + i0)] * (1 - fu) + (float) a[(size_t) (j1 * w + i1)] * fu;
        return (p * (1 - fv) + q * fv) * (1.0f / 255.0f);
    }
};

struct EditOp
{
    enum Kind { Gain = 0, Move = 1, Copy = 2 } kind = Gain;
    int layer = -1;      // the layer it works on; -1: every layer
    int target = -1;     // Move / Copy: the layer it goes to
    float gain = 0.0f;   // Gain: linear (0 erases)
    Shape shape;
};

struct Layer { std::string name; float gainDb = 0.0f; bool mute = false, solo = false; };

struct LayerState
{
    std::vector<Layer> layers { { "Background" }, { "Tone" }, { "Transient" }, { "Noise" } };
    std::vector<EditOp> ops, undone;
    float gainOf (int d, int only = -1) const noexcept
    {
        if (d < 0 || d >= (int) layers.size()) return 0.0f;
        const float g = std::pow (10.0f, layers[(size_t) d].gainDb / 20.0f);
        if (only >= 0) return d == only ? g : 0.0f;
        bool anySolo = false; for (const auto& l : layers) anySolo = anySolo || l.solo;
        if (layers[(size_t) d].mute || (anySolo && ! layers[(size_t) d].solo)) return 0.0f;
        return g;
    }
    bool audible (int d) const noexcept { return gainOf (d) > 0.0f; }
    // nothing would change between t0 and t1: the original samples can be used as they are
    bool transparent (double t0, double t1) const noexcept
    {
        for (int d = 0; d < (int) layers.size(); ++d) if (d < kAuto ? std::fabs (gainOf (d) - 1.0f) > 1.0e-4f : false) return false;
        for (const auto& o : ops) if (o.shape.t1 > t0 && o.shape.t0 < t1) return false;
        return true;
    }
    void apply (EditOp op) { ops.push_back (std::move (op)); undone.clear(); }
    bool undo() { if (ops.empty()) return false; undone.push_back (std::move (ops.back())); ops.pop_back(); return true; }
    bool redo() { if (undone.empty()) return false; ops.push_back (std::move (undone.back())); undone.pop_back(); return true; }
    int addLayer() { if ((int) layers.size() >= kMaxLayers) return (int) layers.size() - 1; layers.push_back ({ "Layer " + std::to_string ((int) layers.size() - kAuto + 1) }); return (int) layers.size() - 1; }
};

// How much of each automatic component (L) each layer (D) holds, per cell of a grid of times x heights.
// Untouched planes are not stored: layer D starts as exactly component D.
struct Coefs
{
    int nt = 0, ny = 0, nl = kAuto;
    std::vector<std::vector<float>> p;
    void reset (int times, int heights, int layers) { nt = times; ny = heights; nl = layers; p.assign ((size_t) (layers * kAuto), {}); }
    bool stored (int d, int l) const noexcept { return ! p[(size_t) (d * kAuto + l)].empty(); }
    bool zero (int d, int l) const noexcept { return ! stored (d, l) && d != l; }
    std::vector<float>& plane (int d, int l) { auto& v = p[(size_t) (d * kAuto + l)]; if (v.empty()) v.assign ((size_t) nt * (size_t) ny, d == l ? 1.0f : 0.0f); return v; }
    float get (int d, int l, size_t idx) const noexcept { const auto& v = p[(size_t) (d * kAuto + l)]; return v.empty() ? (d == l ? 1.0f : 0.0f) : v[idx]; }
    bool any() const noexcept { for (const auto& v : p) if (! v.empty()) return true; return false; }
};

// t: nt ascending times (s). y: ny ascending heights (0..1). Cell index = it * ny + iy.
inline void applyOps (const LayerState& st, const double* t, int nt, const float* y, int ny, Coefs& c)
{
    c.reset (nt, ny, (int) st.layers.size());
    if (nt <= 0 || ny <= 0) return;
    for (const auto& op : st.ops)
    {
        const auto& sh = op.shape;
        if (sh.t1 < t[0] || sh.t0 > t[nt - 1] || sh.t1 <= sh.t0) continue;
        const int i0 = (int) (std::lower_bound (t, t + nt, sh.t0) - t), i1 = (int) (std::upper_bound (t, t + nt, sh.t1) - t);
        const int j0 = (int) (std::lower_bound (y, y + ny, sh.y0) - y), j1 = (int) (std::upper_bound (y, y + ny, sh.y1) - y);
        if (i0 >= i1 || j0 >= j1) continue;
        if (op.kind != EditOp::Gain && (op.target < 0 || op.target >= c.nl)) continue;
        for (int d = 0; d < c.nl; ++d)
        {
            if (op.layer >= 0 && d != op.layer) continue;
            if (op.kind != EditOp::Gain && d == op.target) continue;
            for (int l = 0; l < kAuto; ++l)
            {
                if (c.zero (d, l)) continue;
                auto& src = c.plane (d, l);
                std::vector<float>* dst = op.kind == EditOp::Gain ? nullptr : &c.plane (op.target, l);
                for (int i = i0; i < i1; ++i)
                    for (int j = j0; j < j1; ++j)
                    {
                        const float s = sh.at (t[i], y[j]);
                        if (s <= 0.0f) continue;
                        const size_t idx = (size_t) i * (size_t) ny + (size_t) j;
                        float& v = src[idx];
                        if (op.kind == EditOp::Gain) v *= 1.0f + s * (op.gain - 1.0f);
                        else { (*dst)[idx] += s * v; if (op.kind == EditOp::Move) v *= 1.0f - s; }
                    }
            }
        }
    }
}

// ---------------------------------------------------------------- rendering
// Samples [s0, s1) with the layers and edits applied. only >= 0: that layer alone.
inline void render (const Audio& a, const Analysis& an, const LayerState& st, int64_t s0, int64_t s1, std::vector<float>& outL, std::vector<float>& outR, int only = -1, bool forceStft = false)
{
    s0 = std::max<int64_t> (0, s0); s1 = std::min (a.size(), s1);
    const int64_t len = std::max<int64_t> (0, s1 - s0);
    outL.assign ((size_t) len, 0.0f); outR.assign ((size_t) len, 0.0f);
    if (len == 0) return;
    const bool stereo = a.stereo();
    if (! forceStft && only < 0 && st.transparent ((double) s0 / a.sr, (double) s1 / a.sr))
    {
        std::copy (a.l.begin() + s0, a.l.begin() + s1, outL.begin());
        std::copy ((stereo ? a.r : a.l).begin() + s0, (stereo ? a.r : a.l).begin() + s1, outR.begin());
        return;
    }
    const auto& fr = Frames::get();
    const int nA = (int) std::max<int64_t> (0, (s0 - kN / 2) / kHop + ((s0 - kN / 2) >= 0 ? 1 : 0));
    const int nB = (int) std::min<int64_t> (an.frames - 1, (s1 + kN / 2 + kHop - 1) / kHop - 1);
    const int nl = (int) st.layers.size();
    std::vector<float> g ((size_t) nl); for (int d = 0; d < nl; ++d) g[(size_t) d] = st.gainOf (d, only);
    Region rg; Coefs co;
    std::vector<double> tt; std::vector<float> buf ((size_t) 2 * kN), mult ((size_t) kBins);
    const float ola = 1.0f / 1.5f;
    for (int b0 = nA; b0 <= nB; b0 += 256)
    {
        const int b1 = std::min (nB + 1, b0 + 256), nf = b1 - b0;
        computeRegion (a, an, b0, b1, rg, true);
        tt.resize ((size_t) nf); for (int i = 0; i < nf; ++i) tt[(size_t) i] = (double) (b0 + i) * kHop / a.sr;
        applyOps (st, tt.data(), nf, an.tab.binY, kBins, co);
        for (int i = 0; i < nf; ++i)
        {
            for (int k = 0; k < kBins; ++k)
            {
                const size_t idx = (size_t) i * kBins + (size_t) k;
                float m = 0.0f;
                for (int l = 0; l < kAuto; ++l)
                {
                    const float mk = rg.mask[l][idx];
                    if (mk <= 0.0f) continue;
                    float wgt = 0.0f; for (int d = 0; d < nl; ++d) if (g[(size_t) d] != 0.0f) wgt += g[(size_t) d] * co.get (d, l, idx);
                    m += wgt * mk;
                }
                mult[(size_t) k] = m;
            }
            const int64_t pos = (int64_t) (b0 + i) * kHop - kN / 2 - s0;
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            {
                const float* X = &(ch == 0 ? rg.XL : rg.XR)[(size_t) i * kBins * 2];
                for (int k = 0; k < kBins; ++k) { buf[(size_t) (2 * k)] = X[2 * k] * mult[(size_t) k]; buf[(size_t) (2 * k + 1)] = X[2 * k + 1] * mult[(size_t) k]; }
                for (int k = 1; k < kN / 2; ++k) { buf[(size_t) (2 * (kN - k))] = buf[(size_t) (2 * k)]; buf[(size_t) (2 * (kN - k) + 1)] = -buf[(size_t) (2 * k + 1)]; }
                fr.fft.run (buf.data(), true);
                auto& out = ch == 0 ? outL : outR;
                const int i0 = (int) std::max<int64_t> (0, -pos), i1 = (int) std::min<int64_t> (kN, len - pos);
                for (int j = i0; j < i1; ++j) out[(size_t) (pos + j)] += buf[(size_t) (2 * j)] * fr.win[(size_t) j] * ola;
            }
        }
    }
    if (! stereo) outR = outL;
}

// ---------------------------------------------------------------- loudness (ITU-R BS.1770)
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

struct ExportParams
{
    float fadeInMs = 1.0f, fadeOutMs = 10.0f;
    // bringing the one-shots to one level: 0 as recorded, 1 peak (every peak at normDb), 2 loudness (every one
    // equally loud, measured as BS.1770 does: see measureLevel)
    int normMode = 0;
    float normDb = -1.0f;    // peak: where every peak goes. loudness: the ceiling no peak may pass (dBFS)
    float loudDb = -16.0f;   // loudness: the target (LUFS, the loudest 400 ms)
    bool matchAll = true;    // loudness: if some cannot reach the target under the ceiling, all come down to match them
    bool separate = false;   // one file per layer
    int bits = 24;
};

// The gain (dB) that brings a one-shot to the level asked for. lv: its level; peakDb: the highest peak among what
// will be written for it (the mix, or every layer file: they all get this one gain, so the layers keep their
// balance). held: set when the ceiling kept it under the loudness target.
inline float normGainDb (const Level& lv, float peakDb, const ExportParams& p, bool* held = nullptr)
{
    if (held != nullptr) *held = false;
    if (p.normMode == 1) return peakDb > -119.0f ? p.normDb - peakDb : 0.0f;
    if (p.normMode == 2)
    {
        if (lv.lufs <= -119.0f) return 0.0f;
        float g = p.loudDb - lv.lufs;
        if (peakDb + g > p.normDb) { g = p.normDb - peakDb; if (held != nullptr) *held = true; }
        return g;
    }
    return 0.0f;
}

inline float peakDbOf (const std::vector<float>& l, const std::vector<float>& r) noexcept
{
    float pk = 0.0f; for (float v : l) pk = std::max (pk, std::fabs (v)); for (float v : r) pk = std::max (pk, std::fabs (v));
    return pk > 0.0f ? std::max (-120.0f, 20.0f * std::log10 (pk)) : -120.0f;
}

inline void applyGainDb (std::vector<float>& l, std::vector<float>& r, float db)
{
    if (db == 0.0f) return;
    const float k = std::pow (10.0f, db / 20.0f);
    for (auto& v : l) v *= k;
    for (auto& v : r) v *= k;
}

// one event as it will be written: rendered, faded, brought to level (gainDb: a gain decided elsewhere, such as
// the mix's for a layer file; null: from this render's own level)
inline void renderEvent (const Audio& a, const Analysis& an, const LayerState& st, const Event& e, const ExportParams& p, int only, std::vector<float>& l, std::vector<float>& r, const float* gainDb = nullptr)
{
    render (a, an, st, e.start, e.end, l, r, only);
    const int n = (int) l.size();
    if (n == 0) return;
    const int fin = (int) std::max<int64_t> (0, std::min<int64_t> ((int64_t) (p.fadeInMs * 0.001 * a.sr), std::min<int64_t> (e.onset - e.start, n / 2)));   // never into the attack
    const int fout = std::min (n / 2, (int) (p.fadeOutMs * 0.001 * a.sr));
    for (int i = 0; i < fin; ++i) { const float gg = 0.5f - 0.5f * std::cos (3.14159265f * (float) i / (float) fin); l[(size_t) i] *= gg; r[(size_t) i] *= gg; }
    for (int i = 0; i < fout; ++i) { const float gg = 0.5f - 0.5f * std::cos (3.14159265f * (float) i / (float) fout); l[(size_t) (n - 1 - i)] *= gg; r[(size_t) (n - 1 - i)] *= gg; }
    if (p.normMode == 0) return;
    if (gainDb != nullptr) { applyGainDb (l, r, *gainDb); return; }
    const Level lv = measureLevel (l, r, a.sr);
    applyGainDb (l, r, normGainDb (lv, lv.peakDb, p));
}
} // namespace bc
