// NeoBeat — a Waldorf Wave-style voice (the method, reimplemented from the
// Round 6 analysis; no code or ROM data from the emulator):
//   * 64-wave tables of 128-sample, 8-bit, odd-symmetric waves (pure sine
//     series); positions between key waves are 8-bit blends; 61-63 hold the
//     standard triangle, square and saw
//   * drop-sample playback on a fast internal clock (about 250 kHz), two
//     oscillators summed in an 8-bit accumulator (optional overflow "mix bug")
//   * wave position updated on a 1 kHz control tick from an 8-point,
//     optionally looping wave envelope
//   * a resonant 4-pole low-pass with a tanh input stage
#pragma once

#include "Filters.h"
#include "City.h"
#include <array>
#include <map>
#include <vector>

namespace nb
{
struct WaveTable
{
    std::array<std::array<float, 128>, 64> w {};   // integer values -128..127, stored as float
};

// A 128-sample wave with the Wave's odd symmetry x(1-t) = -x(t): a pure sine series.
inline std::array<float, 128> sineSeriesWave (const std::vector<float>& amps)
{
    std::array<float, 128> x {};
    for (int i = 0; i < 128; ++i)
    {
        const double t = (i + 0.5) / 128.0;
        double v = 0.0;
        for (size_t h = 1; h <= amps.size() && h < 64; ++h)
            if (amps[h - 1] != 0.0f) v += amps[h - 1] * std::sin (2.0 * kPiD * (double) h * t);
        x[(size_t) i] = (float) v;
    }
    float pk = 1.0e-12f;
    for (float v : x) pk = std::max (pk, std::fabs (v));
    for (auto& v : x) v = clampf (std::round (v / pk * 127.0f), -128.0f, 127.0f);
    return x;
}

inline WaveTable buildTable (const std::map<int, std::array<float, 128>>& keys)
{
    WaveTable tab;
    const int first = keys.begin()->first, last = keys.rbegin()->first;
    for (int p = 0; p < 61; ++p)
    {
        if (p <= first) { tab.w[(size_t) p] = keys.begin()->second; continue; }
        if (p >= last) { tab.w[(size_t) p] = keys.rbegin()->second; continue; }
        auto hi = keys.upper_bound (p);
        auto lo = std::prev (hi);
        if (lo->first == p) { tab.w[(size_t) p] = lo->second; continue; }
        const float u = (float) (p - lo->first) / (float) (hi->first - lo->first);
        for (int i = 0; i < 128; ++i)
            tab.w[(size_t) p][(size_t) i] = std::round (lo->second[(size_t) i] * (1.0f - u) + hi->second[(size_t) i] * u);
    }
    std::vector<float> tri (63), sq (63), saw (63);
    for (int h = 1; h <= 63; ++h)
    {
        const bool odd = h % 2 == 1;
        tri[(size_t) h - 1] = odd ? (float) (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (float) (h * h) : 0.0f;
        sq[(size_t) h - 1] = odd ? 1.0f / (float) h : 0.0f;
        saw[(size_t) h - 1] = 1.0f / (float) h;
    }
    tab.w[61] = sineSeriesWave (tri);
    tab.w[62] = sineSeriesWave (sq);
    tab.w[63] = sineSeriesWave (saw);
    return tab;
}

inline std::vector<float> formantAmps (float f0Ref, const std::vector<std::array<float, 3>>& formants, int n = 60, float tilt = -3.0f, bool oddOnly = false)
{
    std::vector<float> amps;
    for (int h = 1; h <= n; ++h)
    {
        const float f = (float) h * f0Ref;
        float a = std::pow (10.0f, tilt * std::log2 ((float) h) / 20.0f);
        float g = 0.0f;
        for (const auto& fm : formants)
        {
            const float d = (f - fm[0]) / fm[1];
            g += fm[2] * std::exp (-0.5f * d * d);
        }
        a *= 0.15f + g;
        if (oddOnly && h % 2 == 0) a *= 0.05f;
        amps.push_back (a);
    }
    return amps;
}

// Odd-harmonic table: soft -> nasal -> hollow comb -> sparse glassy.
inline WaveTable tableHollow()
{
    std::map<int, std::array<float, 128>> k;
    std::vector<float> a14, a42, a56, a60;
    for (int h = 1; h < 64; ++h)
    {
        a14.push_back (h % 2 == 1 ? 1.0f / std::pow ((float) h, 1.2f) : 0.0f);
        a42.push_back (h % 2 == 1 && h % 3 != 0 ? 1.0f / std::pow ((float) h, 0.6f) : 0.0f);
        a60.push_back (h % 2 == 1 ? 1.0f / std::pow ((float) h, 0.3f) : 0.6f / std::pow ((float) h, 0.3f));
    }
    for (int h = 1; h < 40; ++h) a56.push_back (h == 1 || h == 9 || h == 11 || h == 13 || h == 21 ? 1.0f : 0.0f);
    k[0] = sineSeriesWave ({ 1.0f, 0.0f, 0.25f });
    k[14] = sineSeriesWave (a14);
    k[28] = sineSeriesWave (formantAmps (110.0f, { { 900.0f, 250.0f, 2.0f }, { 2600.0f, 500.0f, 1.2f } }, 60, -3.0f, true));
    k[42] = sineSeriesWave (a42);
    k[56] = sineSeriesWave (a56);
    k[60] = sineSeriesWave (a60);
    return buildTable (k);
}

// TurboSynth "Convert to Oscillator" into a Wave-format wave: keep the
// magnitude of each harmonic of one extracted cycle, sine phase.
inline std::array<float, 128> cycleToWave (const std::vector<float>& cycle, int maxH = 48)
{
    const int P = (int) cycle.size();
    std::vector<float> mags;
    for (int h = 1; h <= maxH; ++h)
    {
        if (2 * h >= P) { mags.push_back (0.0f); continue; }
        double re = 0.0, im = 0.0;
        for (int i = 0; i < P; ++i)
        {
            const double a = 2.0 * kPiD * (double) h * i / P;
            re += cycle[(size_t) i] * std::cos (a);
            im -= cycle[(size_t) i] * std::sin (a);
        }
        mags.push_back ((float) std::sqrt (re * re + im * im));
    }
    return sineSeriesWave (mags);
}

// The jet riser's city table: single cycles of a bus engine, a car horn and a
// brake squeal, morphing from one to the next (built at 48 kHz, like the lab).
inline WaveTable tableCity()
{
    auto oneCycle = [] (auto&& model, float hz)
    {
        const int P = (int) (48000.0f / hz), at = (int) (0.3f * 48000.0f);
        std::vector<float> c;
        for (int i = 0; i < at + P; ++i)
        {
            const float v = model.process();
            if (i >= at) c.push_back (v);
        }
        return c;
    };
    BusEngine bus; bus.start (110.0f, 1.0f, 0.9f, 2u, 48000.0f);
    Horn horn; horn.start (220.0f, 1.0f, 0.85f, 1u, 48000.0f);
    BrakeSqueal brake; brake.startSteady (440.0f, 1.0f, 5u, 48000.0f);
    std::map<int, std::array<float, 128>> k;
    k[0] = cycleToWave (oneCycle (bus, 110.0f));
    k[30] = cycleToWave (oneCycle (horn, 220.0f));
    k[60] = cycleToWave (oneCycle (brake, 440.0f));
    return buildTable (k);
}

// 8-point Wave envelope at the 1 kHz control rate (times in seconds, levels 0..1).
struct WaveEnvelope
{
    std::array<float, 8> times {}, levels {};
    int keyoff = 1, loopStart = 0;
    bool loop = false;
    float v = 0.0f, inc = 0.0f;
    int seg = 0, rem = 0;
    bool need = true, sustain = false, done = false, released = false;

    void start() noexcept { v = 0.0f; inc = 0.0f; seg = 0; rem = 0; need = true; sustain = done = released = false; }

    float tick (bool key) noexcept
    {
        if (! key && ! released)
        {
            released = true;
            if (seg <= keyoff || sustain) { seg = keyoff + 1; need = true; sustain = false; }
        }
        if (! done && ! sustain)
        {
            if (seg >= 8) done = true;
            else
            {
                if (need)
                {
                    rem = std::max (1, (int) std::lround (times[(size_t) seg] * 1000.0f));
                    inc = (levels[(size_t) seg] - v) / (float) rem;
                    need = false;
                }
                v += inc;
                if (--rem <= 0)
                {
                    v = levels[(size_t) seg];
                    need = true;
                    if (seg == keyoff && key)
                    {
                        if (loop && loopStart < keyoff) { v = levels[(size_t) loopStart]; seg = loopStart + 1; }
                        else sustain = true;
                    }
                    else ++seg;
                }
            }
        }
        return v;
    }
};

// 4-pole resonant low-pass with a tanh input stage and soft feedback.
struct Ladder
{
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0, G = 0.5f, res = 0.2f, drive = 1.2f;
    void reset() noexcept { s1 = s2 = s3 = s4 = 0.0f; }
    void setCutoff (float fc, float sampleRate) noexcept
    {
        fc = std::min (fc, 0.42f * sampleRate);
        const float g = std::tan (kPi * fc / sampleRate);
        G = g / (1.0f + g);
    }
    float process (float x) noexcept
    {
        const float k = 4.0f * res;
        const float u = std::tanh (drive * x - k * std::tanh (s4));
        float v = (u - s1) * G; const float o1 = v + s1; s1 = o1 + v;
        v = (o1 - s2) * G; const float o2 = v + s2; s2 = o2 + v;
        v = (o2 - s3) * G; const float o3 = v + s3; s3 = o3 + v;
        v = (o3 - s4) * G; const float o4 = v + s4; s4 = o4 + v;
        return o4 * (1.0f + 0.6f * res) / std::max (drive, 1.0e-3f);
    }
};

struct WaveVoiceSpec
{
    float pos = 0.0f, scan = 0.6f, pos2 = -1.0f, scan2 = -99.0f;   // pos2/scan2 < 0: same as osc 1
    float detuneCents = 7.0f, osc2Semis = 0.0f, level1 = 0.55f, level2 = 0.45f;
    bool mixBug = false;
    WaveEnvelope wenv;
    float cutoff = 2500.0f, res = 0.2f, fenvAmount = 2.0f;
    float fa = 0.005f, fd = 0.4f, fs = 0.3f, fr = 0.4f;     // filter envelope
    float aa = 0.005f, ad = 0.3f, as = 0.8f, ar = 0.4f;     // amp envelope
    float drive = 1.2f;
};

class WaveVoice
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        factor = std::max (1, (int) std::lround (250000.0f / sr));
        internalRate = sr * (float) factor;
        // Decimating low-pass (Blackman-windowed sinc), cutoff 0.45 of the host rate.
        const float fc = 0.45f / (float) factor;
        for (int i = 0; i < kTaps; ++i)
        {
            const float m = (float) i - 0.5f * (float) (kTaps - 1);
            const float sinc = m == 0.0f ? 2.0f * fc : std::sin (kTwoPi * fc * m) / (kPi * m);
            const float w = 0.42f - 0.5f * std::cos (kTwoPi * (float) i / (kTaps - 1)) + 0.08f * std::cos (2.0f * kTwoPi * (float) i / (kTaps - 1));
            taps[(size_t) i] = sinc * w;
        }
        float sum = 0.0f;
        for (float t : taps) sum += t;
        for (auto& t : taps) t /= sum;
    }

    // f0 glides exponentially from f0Start to f0End over glideSeconds (0 = steady).
    void start (const WaveTable* t, const WaveVoiceSpec& s, float f0Start, float f0End, float glideSeconds,
                float durSeconds, float gateSeconds, uint32_t seed) noexcept
    {
        table = t; spec = s; fStart = f0Start; fEnd = f0End; glide = glideSeconds; dur = durSeconds; gate = gateSeconds;
        Rng r (ls::hash32 (seed * 37u + 11u) | 1u);
        ph1 = r.uniform(); ph2 = r.uniform();
        det = std::exp2 (spec.detuneCents / 2400.0f);
        lv1 = (int) clampf (spec.level1 * 112.0f, 0.0f, 127.0f);
        lv2 = (int) clampf (spec.level2 * 112.0f, 0.0f, 127.0f);
        spec.wenv.start();
        filter.reset();
        filter.res = spec.res; filter.drive = spec.drive;
        fenv.start (spec.fa, spec.fd, spec.fs, spec.fr, gate, sr);
        aenv.start (spec.aa, spec.ad, spec.as, spec.ar, gate, sr);
        std::fill (hist.begin(), hist.end(), 0.0f);
        histPos = 0; n = 0; ctrlCount = 0; lastMs = 0; fenvHold = 0.0f;
        P1 = P2 = 0.0f;
        controlTick();
    }

    float process() noexcept
    {
        const float t = (float) n / sr;
        if (t >= dur || table == nullptr) { ++n; return 0.0f; }
        // control: wave envelope at 1 kHz, filter envelope held for each millisecond
        const int64_t ms = (int64_t) (t * 1000.0f);
        if (ms != lastMs) { lastMs = ms; controlTick(); }
        const float f = currentHz (t);
        const float inc1 = f / det / internalRate, inc2 = f * det * std::exp2 (spec.osc2Semis / 12.0f) / internalRate;
        for (int k = 0; k < factor; ++k)
        {
            ph1 += inc1; ph1 -= std::floor (ph1);
            ph2 += inc2; ph2 -= std::floor (ph2);
            const int v1 = readWave (P1, (int) (ph1 * 128.0f));
            const int v2 = readWave (P2, (int) (ph2 * 128.0f));
            const int prod = v1 * lv1 + v2 * lv2;
            int sc = prod >= 0 ? prod / 128 : -((-prod + 127) / 128);
            if (spec.mixBug) { const int w = sc & 0xff; sc = w < 128 ? w : w - 256; }
            else sc = std::min (127, std::max (-128, sc));
            hist[(size_t) histPos] = (float) sc / 128.0f;
            histPos = (histPos + 1) % kTaps;
        }
        float host = 0.0f;
        for (int i = 0; i < kTaps; ++i)
            host += taps[(size_t) i] * hist[(size_t) ((histPos + i) % kTaps)];
        ++n;
        return filter.process (host) * aenv.next();
    }

private:
    static constexpr int kTaps = 128;

    float currentHz (float t) const noexcept
    {
        if (glide <= 0.0f || fStart == fEnd) return fStart;
        const float u = clampf (t / glide, 0.0f, 1.0f);
        return fStart * std::pow (fEnd / fStart, u);
    }

    int readWave (float p, int idx) const noexcept
    {
        const int w0 = (int) p, w1 = std::min (63, w0 + 1);
        const float u = p - (float) w0;
        const float a = table->w[(size_t) w0][(size_t) idx], b = table->w[(size_t) w1][(size_t) idx];
        return (int) clampf (std::floor (a + (b - a) * u + 0.5f), -128.0f, 127.0f);
    }

    void controlTick() noexcept
    {
        const bool key = (float) ctrlCount < gate * 1000.0f;
        const float we = spec.wenv.tick (key);
        ++ctrlCount;
        const float pos2 = spec.pos2 < 0.0f ? spec.pos : spec.pos2;
        const float scan2 = spec.scan2 < -9.0f ? spec.scan : spec.scan2;
        P1 = clampf (spec.pos + 64.0f * spec.scan * we, 0.0f, 63.0f);
        P2 = clampf (pos2 + 64.0f * scan2 * we, 0.0f, 63.0f);
        // filter envelope sampled once per millisecond (the stepped Wave control rate)
        const int steps = std::max (1, (int) (sr / 1000.0f));
        fenvHold = fenv.next();
        for (int i = 1; i < steps; ++i) fenv.next();
        filter.setCutoff (spec.cutoff * std::exp2 (spec.fenvAmount * fenvHold), sr);
    }

    const WaveTable* table = nullptr;
    WaveVoiceSpec spec;
    Ladder filter;
    Adsr fenv, aenv;
    std::array<float, kTaps> taps {}, hist {};
    int histPos = 0, factor = 5, lv1 = 60, lv2 = 50;
    float sr = 48000.0f, internalRate = 240000.0f, ph1 = 0, ph2 = 0, det = 1.0f, P1 = 0, P2 = 0, fenvHold = 0.0f;
    float fStart = 220.0f, fEnd = 220.0f, glide = 0.0f, dur = 1.0f, gate = 0.5f;
    int64_t n = 0, ctrlCount = 0, lastMs = 0;
};
} // namespace nb
