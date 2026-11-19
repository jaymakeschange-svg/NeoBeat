// NeoBeat — MaterialLab space and gestures, in real time.
//
// A port of materiallab/spatial.py and gesture.py by Marbelite.
//   * Two listening points on the object (done in the voices: MaterialBody /
//     StretchVoice take the place's width and f_d).
//   * Surroundings: early reflections (sparse taps, different for each ear) and
//     a diffuse tail in 8 bands whose decay shortens with frequency; the two
//     points share the long wavelengths (coherence sinc(2 f d / c)). Generated
//     exactly as room_ir() does, then convolved (a shared bus per place).
//   * Distance (Chowning 1971): the direct sound falls as 1/r, the room only as
//     1/sqrt(r), air takes the highs; a moving object is delayed by r / c, so
//     it bends in pitch (Doppler).
//   * Gestures: one drawn stroke y(t) in [-1, 1] moves pitch, distance (log),
//     room send, width and pan together, each between its value at the bottom
//     and the top of the canvas. Bounces: each flight lasts e times the last,
//     each impact is e times slower (e = 0.79, measured from the wooden ball);
//     e > 1 is impossible: the object gains energy.
// These are what the Matrixxxer effects and the modulation matrix drive: the
// ObjectPlacer's controls (distance, width, send, pan) and the strike pitch are
// modulation destinations; a gesture is a modulation source.
#pragma once

#include "Butterworth.h"
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <random>
#include <vector>

namespace nb
{
constexpr float kSpeedOfSound = 343.0f;

// spatial.Space
struct PlaceSpec
{
    const char* name = "calibrated";
    float f_d = 12000.0f, q = 1.2f, width = 1.0f, distance = 1.0f, room = 6.0f, rt60 = 1.4f, hf_ratio = 0.35f;
    float wet = 0.3f, early = 0.15f, pan = 0.0f, ears = 0.6f;
};

// tools/stereo_demo.py PLACES (built on spatial.CALIBRATED)
inline PlaceSpec placePreset (int i)
{
    PlaceSpec c;   // calibrated
    switch (i)
    {
        case 0: c.name = "mono and dry"; c.width = 0.0f; c.wet = 0.0f; c.early = 0.0f; break;
        case 1: c.name = "calibrated"; break;
        case 2: c.name = "close and dry"; c.distance = 0.5f; c.wet = 0.12f; c.early = 0.08f; break;
        case 3: c.name = "far away"; c.distance = 12.0f; break;
        case 4: c.name = "small room"; c.room = 3.0f; c.rt60 = 0.45f; c.wet = 0.35f; break;
        case 5: c.name = "large hall"; c.room = 25.0f; c.rt60 = 3.2f; c.wet = 0.45f; c.early = 0.25f; break;
        case 6: c.name = "narrow"; c.width = 0.35f; break;
        case 7: c.name = "hyper-wide"; c.width = 2.2f; c.f_d = 4000.0f; break;
        default: break;
    }
    return c;
}
constexpr int kNumPlaces = 8;

// Energy of a unit direct sound heard in a place (direct 1/r^2, room (wet^2 + early taps)/r).
inline float placeEnergy (const PlaceSpec& sp)
{
    const float r = std::max (sp.distance, 0.2f);
    float early = 0.0f;
    for (int k = 0; k < 8; ++k) { const float g = sp.early * std::pow (0.8f, (float) k) / (1.0f + 0.6f * k); early += g * g; }
    return 1.0f / (r * r) + (sp.wet * sp.wet + early) / r;
}
// The learner's listening files were level-matched place by place; so is NeoBeat: a place changes
// where the object is, not how loud the pad is (gestures keep their own distance dynamics).
inline float placeMakeup (const PlaceSpec& sp)
{
    return std::sqrt (placeEnergy (placePreset (1)) / std::max (placeEnergy (sp), 1.0e-6f));
}

// spatial.room_ir: stereo impulse response of the surroundings.
inline void roomImpulse (double fs, const PlaceSpec& sp, std::vector<float>& left, std::vector<float>& right, uint32_t seed = 5)
{
    std::mt19937_64 rng (seed);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    std::normal_distribution<double> nd;
    const int n = (int) (fs * std::min (sp.rt60 * 1.6 + 0.1, 6.0));
    left.assign ((size_t) n, 0.0f); right.assign ((size_t) n, 0.0f);
    const double t0 = sp.room / kSpeedOfSound;
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < 8; ++k)
        {
            const double delay = t0 * (0.35 + 1.4 * uni (rng)) * (1.0 + 0.25 * k);
            const int i = (int) (delay * fs);
            const double sgn = uni (rng) < 0.5 ? -1.0 : 1.0;
            if (i < n) (ch == 0 ? left : right)[(size_t) i] += (float) (sp.early * std::pow (0.8, k) * sgn / (1.0 + 0.6 * k));
        }
    const int start = (int) (t0 * 1.2 * fs);
    const int m = n - start;
    if (m <= 0 || sp.wet <= 0.0f) return;
    std::vector<double> w1 ((size_t) m), w2 ((size_t) m), tl ((size_t) m, 0.0), tr ((size_t) m, 0.0);
    for (auto& v : w1) v = nd (rng);
    for (auto& v : w2) v = nd (rng);
    const double top = std::min (18000.0, 0.45 * fs);
    double edges[9];
    for (int b = 0; b <= 8; ++b) edges[b] = 60.0 * std::pow (top / 60.0, b / 8.0);
    for (int b = 0; b < 8; ++b)
    {
        const double fc = std::sqrt (edges[b] * edges[b + 1]);
        const double x = std::min (1.0, std::max (0.0, std::log2 (fc / 500.0) / std::log2 (16.0)));
        const double rt = sp.rt60 * (1.0 - (1.0 - sp.hf_ratio) * x);
        const double arg = 2.0 * fc * sp.ears / kSpeedOfSound;
        double coh = arg == 0.0 ? 1.0 : std::sin (3.14159265358979 * arg) / (3.14159265358979 * arg);
        coh = std::min (1.0, std::max (0.0, coh));
        SosCascade fa (butterBandpass (2, edges[b], edges[b + 1], fs)), fb (butterBandpass (2, edges[b], edges[b + 1], fs));
        const double k = -6.91 / std::max (rt, 0.02);
        for (int i = 0; i < m; ++i)
        {
            const double env = std::exp (k * i / fs);
            const double a = fa.process (w1[(size_t) i]), c = fb.process (w2[(size_t) i]);
            tl[(size_t) i] += a * env;
            tr[(size_t) i] += (coh * a + std::sqrt (1.0 - coh * coh) * c) * env;
        }
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& tail = ch == 0 ? tl : tr;
        double e = 0;
        for (int i = 0; i < m; ++i) { tail[(size_t) i] *= std::min (1.0, (i / fs) / 0.01); e += tail[(size_t) i] * tail[(size_t) i]; }
        const double g = sp.wet / (std::sqrt (e) + 1e-12);
        auto& out = ch == 0 ? left : right;
        for (int i = 0; i < m; ++i) out[(size_t) (start + i)] += (float) (tail[(size_t) i] * g);
    }
}

// ---------------------------------------------------------------------------
// Uniformly partitioned convolution (mono in, stereo out): the room bus
// ---------------------------------------------------------------------------
class RoomBus
{
public:
    static constexpr int kB = 512, kN = 1024;

    // phase: where in its 512-sample block this room starts, so the rooms of a bus do their FFTs on
    // different samples (the output is the same for any phase: the latency is always one block)
    void configure (double fs, const PlaceSpec& sp, int phase = 0)
    {
        if ((int) tw.size() != kN / 2)
        {
            tw.resize (kN / 2);
            for (int i = 0; i < kN / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * 3.14159265358979 * i / kN);
        }
        spec = sp;
        std::vector<float> l, r;
        roomImpulse (fs, sp, l, r);
        parts = std::max (1, (int) ((l.size() + kB - 1) / kB));
        HL.assign ((size_t) parts * kN, {}); HR.assign ((size_t) parts * kN, {});
        for (int p = 0; p < parts; ++p)
        {
            std::vector<std::complex<float>> a (kN), b (kN);
            for (int i = 0; i < kB; ++i)
            {
                const size_t s = (size_t) (p * kB + i);
                if (s < l.size()) { a[(size_t) i] = l[s]; b[(size_t) i] = r[s]; }
            }
            fft (a, false); fft (b, false);
            std::copy (a.begin(), a.end(), HL.begin() + (ptrdiff_t) p * kN);
            std::copy (b.begin(), b.end(), HR.begin() + (ptrdiff_t) p * kN);
        }
        X.assign ((size_t) parts * kN, {});
        inBuf.assign (kN, 0.0f); outL.assign (kB, 0.0f); outR.assign (kB, 0.0f);
        accL.assign (kN, {}); accR.assign (kN, {}); scratch.assign (kN, {});
        nextL.assign (kN / 2 + 1, {}); nextR.assign (kN / 2 + 1, {});
        nextPart = parts; pendingHead = 0;
        sliceParts = std::max (1, (parts - 1 + (kB / 16) - 1) / (kB / 16));
        pos = ((phase % kB) + kB) % kB; head = 0; inUse = true; silentBlocks = 0;
    }

    bool configured() const noexcept { return ! HL.empty(); }
    const PlaceSpec& place() const noexcept { return spec; }

    // one sample in, one stereo sample out (one block of latency-free direct path is the caller's)
    void process (float in, float& l, float& r) noexcept
    {
        inBuf[(size_t) (kB + pos)] = in;
        l = outL[(size_t) pos]; r = outR[(size_t) pos];
        if (++pos == kB) block();
        else if ((pos & 15) == 0) accumulate (sliceParts);   // the past blocks' share, spread over the block
    }

private:
    // Time-distributed partitioned convolution: during each block the partitions that use past input
    // (p >= 1) are summed a slice at a time, so the block boundary only does the newest partition and
    // the FFTs. The audio thread never sees one big burst of work.
    void accumulate (int count) noexcept
    {
        constexpr int half = kN / 2;
        const int end = std::min (parts, nextPart + count);
        for (int p = nextPart; p < end; ++p)
        {
            mac (reinterpret_cast<float*> (nextL.data()), reinterpret_cast<float*> (nextR.data()),
                 reinterpret_cast<const float*> (&X[(size_t) ((pendingHead + p - 1) % parts) * kN]),
                 reinterpret_cast<const float*> (&HL[(size_t) p * kN]), reinterpret_cast<const float*> (&HR[(size_t) p * kN]), half + 1);
        }
        nextPart = std::max (nextPart, end);
    }

    void block()
    {
        pos = 0;
        accumulate (parts);   // whatever the slices did not reach
        // input spectrum of the last 2B samples (overlap-save)
        std::vector<std::complex<float>>& xs = scratch;
        for (int i = 0; i < kN; ++i) xs[(size_t) i] = inBuf[(size_t) i];
        fft (xs, false);
        head = (head + parts - 1) % parts;
        std::copy (xs.begin(), xs.end(), X.begin() + (ptrdiff_t) head * kN);
        constexpr int half = kN / 2;
        mac (reinterpret_cast<float*> (nextL.data()), reinterpret_cast<float*> (nextR.data()),
             reinterpret_cast<const float*> (xs.data()), reinterpret_cast<const float*> (&HL[0]), reinterpret_cast<const float*> (&HR[0]), half + 1);
        // both outputs are real, so one inverse FFT does the pair: z = L + iR, Z[N-k] = conj(L[k]) + i conj(R[k])
        auto& z = accL;
        for (int k = 0; k <= half; ++k)
        {
            const auto a = nextL[(size_t) k], b = nextR[(size_t) k];
            z[(size_t) k] = { a.real() - b.imag(), a.imag() + b.real() };
            if (k > 0 && k < half) z[(size_t) (kN - k)] = { a.real() + b.imag(), b.real() - a.imag() };
        }
        fft (z, true);
        for (int i = 0; i < kB; ++i) { outL[(size_t) i] = z[(size_t) (kB + i)].real(); outR[(size_t) i] = z[(size_t) (kB + i)].imag(); }
        // slide the input
        for (int i = 0; i < kB; ++i) inBuf[(size_t) i] = inBuf[(size_t) (kB + i)];
        // start the next block's sum of past partitions
        std::fill (nextL.begin(), nextL.end(), std::complex<float>());
        std::fill (nextR.begin(), nextR.end(), std::complex<float>());
        pendingHead = head; nextPart = 1;
    }

    static void mac (float* accL, float* accR, const float* x, const float* hl, const float* hr, int bins) noexcept
    {
        for (int k = 0; k < 2 * bins; k += 2)
        {
            const float xr = x[k], xi = x[k + 1];
            accL[k] += xr * hl[k] - xi * hl[k + 1]; accL[k + 1] += xr * hl[k + 1] + xi * hl[k];
            accR[k] += xr * hr[k] - xi * hr[k + 1]; accR[k + 1] += xr * hr[k + 1] + xi * hr[k];
        }
    }

    void fft (std::vector<std::complex<float>>& v, bool inverse) const noexcept
    {
        const int n = kN;
        auto* a = reinterpret_cast<float*> (v.data());
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (a[2 * i], a[2 * j]); std::swap (a[2 * i + 1], a[2 * j + 1]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int halfLen = len / 2, stride = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < halfLen; ++j)
                {
                    const auto& w = tw[(size_t) (j * stride)];
                    const float wr = (float) w.real(), wi = inverse ? -(float) w.imag() : (float) w.imag();
                    float* u = a + 2 * (i + j);
                    float* t = a + 2 * (i + j + halfLen);
                    const float vr = t[0] * wr - t[1] * wi, vi = t[0] * wi + t[1] * wr;
                    t[0] = u[0] - vr; t[1] = u[1] - vi;
                    u[0] += vr; u[1] += vi;
                }
        }
        if (inverse) { const float s = 1.0f / (float) n; for (int i = 0; i < 2 * n; ++i) a[i] *= s; }
    }

    PlaceSpec spec;
    int parts = 0, pos = 0, head = 0, silentBlocks = 0, nextPart = 0, pendingHead = 0, sliceParts = 1;
    bool inUse = false;
    std::vector<std::complex<float>> HL, HR, X, accL, accR, scratch, nextL, nextR;
    std::vector<std::complex<double>> tw;   // e^{-2 pi i k / N}
    std::vector<float> inBuf, outL, outR;
};

// ---------------------------------------------------------------------------
// Gestures (gesture.Gesture) and bounces (gesture.bounce_times)
// ---------------------------------------------------------------------------
struct Gesture
{
    std::vector<std::pair<float, float>> points { { 0.0f, 1.0f }, { 1.0f, 1.0f } };   // (u 0..1, y -1..1) as drawn
    float pitchLo = 0, pitchHi = 0;          // semitones at y = -1 / +1 (applied to each strike)
    float distLo = 1, distHi = 1;            // metres (log-interpolated)
    float sendLo = 1, sendHi = 1;            // room send
    float widthLo = 1, widthHi = 1;          // side signal of the two listening points
    float panLo = 0, panHi = 0;
    bool doppler = true;
    float seconds = 3.0f;                    // how long the stroke takes

    float y (float u) const noexcept
    {
        if (u <= points.front().first) return points.front().second;
        for (size_t i = 1; i < points.size(); ++i)
            if (u <= points[i].first)
            {
                const auto& a = points[i - 1]; const auto& b = points[i];
                const float f = (u - a.first) / std::max (b.first - a.first, 1e-6f);
                return a.second + (b.second - a.second) * f;
            }
        return points.back().second;
    }
    static float lerp (float lo, float hi, float yy, bool logScale = false) noexcept
    {
        const float a = (yy + 1.0f) * 0.5f;
        return logScale ? std::exp (std::log (lo) + a * (std::log (hi) - std::log (lo))) : lo + a * (hi - lo);
    }
    float pitch (float u) const noexcept { return lerp (pitchLo, pitchHi, y (u)); }
    float distance (float u) const noexcept { return lerp (distLo, distHi, y (u), true); }
    float send (float u) const noexcept { return lerp (sendLo, sendHi, y (u)); }
    float width (float u) const noexcept { return lerp (widthLo, widthHi, y (u)); }
    float pan (float u) const noexcept { return lerp (panLo, panHi, y (u)); }
};

// gestures_demo.py: the strokes of the basketball demos
inline Gesture gesturePreset (int i)
{
    Gesture g;
    auto away = [&] { g.pitchLo = -7.0f; g.pitchHi = 0.0f; g.distLo = 18.0f; g.distHi = 1.0f; g.sendLo = 3.0f; g.sendHi = 1.0f; g.widthLo = 2.6f; g.widthHi = 1.0f; };
    switch (i)
    {
        case 1: away(); g.points = { { 0, 1 }, { 1, 1 } }; break;                                   // natural bounces
        case 2: away(); g.points = { { 0, 1 }, { 0.1f, 1 }, { 1, -1 } }; break;                     // bounce away (downward dash)
        case 3: g.points = { { 0, -1 }, { 1, 1 } }; g.pitchLo = -7; g.pitchHi = 5; g.distLo = 18; g.distHi = 1;
                g.sendLo = 3.0f; g.sendHi = 0.8f; g.widthLo = 2.6f; g.widthHi = 0.8f; break;        // about to impact (upward dash)
        case 4: g.points = { { 0, 1 }, { 0.1f, 1 }, { 1, -1 } }; g.pitchLo = 9; g.pitchHi = 0; g.distLo = 18; g.distHi = 1;
                g.sendLo = 3.0f; g.sendHi = 1.0f; g.widthLo = 3.5f; g.widthHi = 1.0f; break;        // impossible: recedes while rising
        case 5: away(); g.points = { { 0, 1 }, { 1, -1 } }; break;                                  // impossible: gains energy (with e = 1.12)
        default: break;                                                                             // 0: no gesture
    }
    return g;
}

struct Bounces
{
    std::vector<float> times, speeds;
};

inline Bounces bounceTimes (float firstFlight, float restitution, int count, float start = 0.05f)
{
    Bounces b;
    float t = start, flight = firstFlight, v = 1.0f;
    for (int i = 0; i < count; ++i)
    {
        b.times.push_back (t); b.speeds.push_back (v);
        flight *= restitution; v *= restitution; t += flight;
    }
    return b;
}

// ---------------------------------------------------------------------------
// Per voice: where the object is (the direct path) and what it sends to the room
// ---------------------------------------------------------------------------
class ObjectPlacer
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        size = 1; while (size < (int) (0.25f * fs) + 8) size <<= 1;     // up to ~85 m of travel
        dl.assign ((size_t) size, 0.0f); dr.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() noexcept
    {
        std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f);
        w = 0; lpL = lpR = lpM = 0.0f;
    }
    // static place (spatial.place) or the start of a gesture
    void begin (const PlaceSpec& sp, float referenceDistance) noexcept
    {
        distance = std::max (sp.distance, 0.2f); r0 = std::max (referenceDistance, 0.2f);
        width = 1.0f; send = 1.0f; pan = sp.pan; airOnWet = true;
        curDelay = delayFor (distance);
        reset();
        setAir();
    }
    // live controls (Matrixxxer effects / modulation matrix destinations), set once per block
    void setDistance (float m) noexcept { distance = std::max (m, 0.2f); setAir(); }
    void setWidth (float x) noexcept { width = x; }
    void setSend (float x) noexcept { send = x; }
    void setPan (float x) noexcept { pan = std::min (1.0f, std::max (-1.0f, x)); }
    void setReference (float m) noexcept { r0 = std::max (m, 0.2f); }

    // dry object in; direct stereo out; mono room send out
    void process (float inL, float inR, float& outL, float& outR, float& roomSend) noexcept
    {
        // width: the side signal of the two listening points
        const float mid = 0.5f * (inL + inR), side = 0.5f * (inL - inR) * width;
        float l = mid + side, r = mid - side;
        // travel time: Doppler while moving
        dl[(size_t) w] = l; dr[(size_t) w] = r;
        const float want = delayFor (distance);
        curDelay += 0.0025f * (want - curDelay);
        l = readFrac (dl, curDelay); r = readFrac (dr, curDelay);
        w = (w + 1) & (size - 1);
        // 1/r and air
        const float rr = std::max (distance, 0.2f);
        lpL += airCoef * (l - lpL); lpR += airCoef * (r - lpR);
        const float al = airOn ? lpL : l, ar = airOn ? lpR : r;
        const float gl = std::sqrt (std::max (0.0f, 1.0f - pan)), gr = std::sqrt (std::max (0.0f, 1.0f + pan));
        outL = al / rr * gl; outR = ar / rr * gr;
        // the room hears the source whatever its distance: its share grows as the direct sound fades
        const float m = 0.5f * (l + r);
        lpM += airCoef * (m - lpM);
        roomSend = (airOn && airOnWet ? lpM : m) * send / std::sqrt (rr) * 0.5f * (gl + gr);
    }

private:
    float delayFor (float m) const noexcept { return 2.0f + std::max (0.0f, m - r0) / kSpeedOfSound * fs; }
    void setAir() noexcept
    {
        airOn = distance > 1.0f;
        const float fc = std::min (0.45f * fs, std::max (1500.0f, 20000.0f / (1.0f + (distance - 1.0f) / 12.0f)));
        // first-order Butterworth low-pass as a one-pole (matched at the cutoff)
        airCoef = 1.0f - std::exp (-2.0f * 3.14159265f * fc / fs);
    }
    float readFrac (const std::vector<float>& b, float d) const noexcept
    {
        const float p = (float) w - d;
        const int i0 = (int) std::floor (p);
        const float fr = p - (float) i0;
        const float a = b[(size_t) (i0 & (size - 1))], c = b[(size_t) ((i0 + 1) & (size - 1))];
        return a + (c - a) * fr;
    }

    float fs = 48000.0f, distance = 1.0f, r0 = 1.0f, width = 1.0f, send = 1.0f, pan = 0.0f, curDelay = 2.0f, airCoef = 1.0f;
    float lpL = 0, lpR = 0, lpM = 0;
    bool airOn = false, airOnWet = true;
    std::vector<float> dl, dr;
    int size = 1, w = 0;
};
} // namespace nb
