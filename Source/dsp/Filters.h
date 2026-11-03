// NeoBeat — small filters, envelopes and noise used by the city models, the
// Wave/Cycle-style voices and the hollow chain. Plain C++17, no framework.
//
// The Round 6 lab (Python, 48 kHz) is the reference for every model here.
// Time constants written for 48 kHz are converted to the running rate, so a
// sound keeps its character at 44.1 or 96 kHz.
#pragma once

#include "Core.h"
#include <cmath>

namespace nb
{
using ls::kPi;
using ls::kTwoPi;
using ls::clampf;
using ls::Rng;
constexpr double kPiD = 3.14159265358979323846;   // MSVC has no M_PI without _USE_MATH_DEFINES

// Approximately unit-variance Gaussian noise from four uniforms (cheap and
// smooth enough for every noise source in the models).
inline float gauss (Rng& r) noexcept
{
    return (r.uniform() + r.uniform() + r.uniform() + r.uniform() - 2.0f) * 1.7320508f;
}

// A one-pole coefficient written for 48 kHz, moved to another rate.
inline float poleAt (float pole48, float sampleRate) noexcept
{
    return std::pow (pole48, 48000.0f / sampleRate);
}

// y = g*x + p*y driven by noise: keeps the output's spread and bandwidth when
// the sample rate changes (scipy lfilter([g], [1, -p]) at 48 kHz).
struct NoiseLowpass
{
    float p = 0.0f, g = 0.0f, y = 0.0f;
    void set (float g48, float p48, float sampleRate) noexcept
    {
        p = poleAt (p48, sampleRate);
        const float v48 = g48 * g48 / std::max (1.0e-9f, 1.0f - p48 * p48);
        g = std::sqrt (v48 * (1.0f - p * p));
        y = 0.0f;
    }
    float process (float x) noexcept { y = g * x + p * y; return y; }
};

// Leaky integrator for pulse trains: y = x + p*y (decay time kept across rates).
struct Leak
{
    float p = 0.0f, y = 0.0f;
    void set (float p48, float sampleRate) noexcept { p = poleAt (p48, sampleRate); y = 0.0f; }
    float process (float x) noexcept { y = x + p * y; return y; }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // The lab's resonant_peak: a 2-pole resonator with unity peak gain.
    void setResonator (float fc, float q, float gain, float sampleRate) noexcept
    {
        fc = clampf (fc, 5.0f, 0.49f * sampleRate);
        const float w = kTwoPi * fc / sampleRate;
        const float r = std::exp (-w / (2.0f * std::max (q, 0.05f)));
        b0 = (1.0f - r * r) * 0.5f * gain; b1 = 0.0f; b2 = -b0;
        a1 = -2.0f * r * std::cos (w); a2 = r * r;
    }

    void setLowpass (float fc, float q, float sampleRate) noexcept
    {
        fc = clampf (fc, 5.0f, 0.45f * sampleRate);
        const float w = kTwoPi * fc / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
    }

    void setHighpass (float fc, float q, float sampleRate) noexcept
    {
        fc = clampf (fc, 5.0f, 0.45f * sampleRate);
        const float w = kTwoPi * fc / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
    }

    // Constant 0 dB peak band-pass.
    void setBandpass (float fc, float q, float sampleRate) noexcept
    {
        fc = clampf (fc, 5.0f, 0.45f * sampleRate);
        const float w = kTwoPi * fc / sampleRate, c = std::cos (w), alpha = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0; b1 = 0.0f; b2 = -alpha / a0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
    }
};

// The lab's bp(): a 2nd-order Butterworth band-pass between fc -/+ bw/2
// (a 4-pole response), approximated by two band-pass sections at the band's
// geometric centre.
struct BandPass4
{
    Biquad s1, s2;
    void set (float fc, float q, float sampleRate) noexcept
    {
        const float bw = fc / std::max (q, 0.05f);
        const float lo = std::max (fc - bw * 0.5f, 10.0f), hi = std::min (fc + bw * 0.5f, 0.45f * sampleRate);
        const float centre = std::sqrt (lo * hi);
        const float qq = 0.644f * centre / std::max (hi - lo, 1.0f);
        s1.setBandpass (centre, qq, sampleRate);
        s2 = s1;
        s1.reset(); s2.reset();
    }
    float process (float x) noexcept { return s2.process (s1.process (x)); }
};

// The lab's adsr(): exponential-ish, gate in seconds, curve 3 by default.
struct Adsr
{
    float a = 0.01f, d = 0.1f, s = 1.0f, r = 0.1f, gate = 1.0f, curve = 3.0f;
    float sr = 48000.0f, decayFactor = 1.0f, releaseFactor = 1.0f, e = 1.0f, er = 1.0f, levelAtGate = 1.0f;
    int64_t n = 0;

    void start (float attack, float decay, float sustain, float release, float gateSeconds, float sampleRate, float curveIn = 3.0f) noexcept
    {
        a = std::max (attack, 1.0e-4f); d = std::max (decay, 1.0e-4f); s = sustain; r = std::max (release, 1.0e-4f);
        gate = gateSeconds; sr = sampleRate; curve = curveIn; n = 0;
        decayFactor = std::exp (-curve / (d * sr));
        releaseFactor = std::exp (-curve / (r * sr));
        e = 1.0f; er = 1.0f;
        levelAtGate = gate > a ? s + (1.0f - s) * std::exp (-(gate - a) / d * curve) : std::pow (std::max (gate, 0.0f) / a, 0.8f);
    }

    float next() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= gate) { const float v = levelAtGate * er; er *= releaseFactor; return v; }
        if (t < a) return std::pow (t / a, 0.8f);
        const float v = s + (1.0f - s) * e;
        e *= decayFactor;
        return v;
    }
};

// Constant-power pan, -1 .. +1 (the lab's pan_mono).
inline void panGains (float pan, float& l, float& r) noexcept
{
    const float a = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (a); r = std::sin (a);
}

// A short linear fade over the last `ms` of a model's length (the lab's _end).
inline float endFade (float t, float dur, float ms = 15.0f) noexcept
{
    const float k = ms * 0.001f;
    if (t >= dur) return 0.0f;
    return t > dur - k ? (dur - t) / k : 1.0f;
}

// sin(h * phase) for h = 1..H by the Chebyshev recurrence, weighted and summed.
inline float harmonicSum (float phaseRadians, const float* weights, int count) noexcept
{
    const float s1 = std::sin (phaseRadians), twoCos = 2.0f * std::cos (phaseRadians);
    float prev = 0.0f, cur = s1, sum = 0.0f;
    for (int h = 0; h < count; ++h)
    {
        sum += weights[h] * cur;
        const float nxt = twoCos * cur - prev;
        prev = cur; cur = nxt;
    }
    return sum;
}
} // namespace nb
