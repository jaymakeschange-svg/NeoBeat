// Liquid Swing 4 — shared DSP helpers.
// Plain C++17, no framework dependencies, so every sound can be rendered and
// measured offline exactly as the plugin will play it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ls
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float clampf (float x, float lo, float hi) noexcept { return std::min (hi, std::max (lo, x)); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
inline float semitonesToRatio (float st) noexcept { return std::exp2 (st / 12.0f); }

// Per-sample decay factor that makes a level fall by 60 dB in t60 seconds.
inline float t60ToRadius (float t60Seconds, float sampleRate) noexcept
{
    return std::exp (-6.907755f / (std::max (t60Seconds, 1.0e-4f) * sampleRate));
}

// One-pole smoothing coefficient for a given time constant.
inline float smoothingCoefficient (float seconds, float sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / (std::max (seconds, 1.0e-5f) * sampleRate));
}

inline float smoothstep (float x) noexcept
{
    x = clampf (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Small, fast, deterministic noise source. The same seed always gives the
// same sound, so renders and bounces are repeatable.
struct Rng
{
    uint32_t state = 1;
    explicit Rng (uint32_t seed = 1) noexcept : state (seed ? seed : 1) {}
    uint32_t next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
};

// Zero-delay-feedback state-variable filter (Zavalishin topology).
// Stable under fast modulation, which matters for swept fizz and tone layers.
struct StateVariableFilter
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;

    void set (float cutoffHz, float q, float sampleRate) noexcept
    {
        cutoffHz = clampf (cutoffHz, 10.0f, sampleRate * 0.49f);
        g = std::tan (kPi * cutoffHz / sampleRate);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = 0; }

    // Returns low, band and high outputs through references.
    void process (float x, float& low, float& band, float& high) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2; band = v1; high = x - k * v1 - v2;
    }
};

// Flush values that have become too small to matter, avoiding slow denormals.
inline float flushTiny (float x) noexcept { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }
} // namespace ls
