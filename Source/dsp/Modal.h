// NeoBeat — a Chromaphone-style modal body (the method: mallet or noise into a
// bank of decaying modes with material-dependent damping, tone tilt and hit
// position). Used by the bell-clank and train-door recipes.
#pragma once

#include "Filters.h"
#include <array>

namespace nb
{
enum class BodyKind { String, ClosedTube, OpenTube, Membrane, Plate, Bar, Bell };

struct BodySpec
{
    BodyKind kind = BodyKind::Bar;
    float t60 = 1.0f, material = -0.5f, tone = -3.0f, hit = 0.3f, width = 0.6f;
    uint32_t seed = 3;
};

class ModalBody
{
public:
    static constexpr int kMaxModes = 24;

    void prepare (float sampleRate) noexcept { sr = sampleRate; }

    void start (const BodySpec& b, float hz, float durSeconds, float malletStiffnessHz, float strikeDelaySeconds, float strength = 1.0f) noexcept
    {
        dur = durSeconds; n = 0; count = 0; f0 = hz;
        float ratios[kMaxModes]; int nr = 0;
        switch (b.kind)
        {
            case BodyKind::String:     for (int k = 1; k <= 24; ++k) ratios[nr++] = (float) k * (1.0f + 0.0004f * (float) (k * k)); break;
            case BodyKind::ClosedTube: for (int k = 1; k < 24; k += 2) ratios[nr++] = (float) k; break;
            case BodyKind::OpenTube:   for (int k = 1; k < 16; ++k) ratios[nr++] = (float) k; break;
            case BodyKind::Membrane:   for (float r : { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.5f, 3.6f, 4.06f, 4.15f, 4.6f }) ratios[nr++] = r; break;
            case BodyKind::Plate:      for (float r : { 1.0f, 2.5f, 4.0f, 5.0f, 6.5f, 8.5f, 9.0f, 10.0f, 13.0f, 16.0f, 17.0f, 18.5f }) ratios[nr++] = std::sqrt (r); break;
            case BodyKind::Bar:        for (float r : { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f }) ratios[nr++] = r; break;
            case BodyKind::Bell:       for (float r : { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f }) ratios[nr++] = r; break;
        }
        Rng rng (ls::hash32 (b.seed * 43u + 1u) | 1u);
        for (int k = 0; k < nr; ++k)
        {
            const float f = hz * ratios[k];
            const float rel = ratios[k] / ratios[0];
            const float t60 = clampf (b.t60 * std::pow (rel, b.material), 0.005f, 30.0f);
            const float g = std::pow (10.0f, b.tone * std::log2 (rel) / 20.0f) * std::max (std::fabs (std::sin (kPi * (float) (k + 1) * b.hit)), 0.08f);
            const float pan = k == 0 ? 0.0f : clampf (b.width * rng.bipolar(), -1.0f, 1.0f);
            if (f >= 18000.0f || f >= 0.45f * sr) continue;
            const float w = kTwoPi * f / sr, rad = ls::t60ToRadius (t60, sr);
            c[(size_t) count] = rad * std::cos (w); s[(size_t) count] = rad * std::sin (w);
            gain[(size_t) count] = g;
            panGains (pan, gl[(size_t) count], gr[(size_t) count]);
            re[(size_t) count] = im[(size_t) count] = 0.0f;
            ++count;
        }
        malletLen = std::max (2, (int) (48000.0f / std::max (malletStiffnessHz, 20.0f) * sr / 48000.0f));
        strikeAt = (int64_t) (strikeDelaySeconds * sr);
        malletStrength = strength;
    }

    void process (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        if ((float) n / sr >= dur) { ++n; return; }
        float x = 0.0f;
        const int64_t k = n - strikeAt;
        if (k >= 0 && k < malletLen)
            x = malletStrength * (1.0f - std::cos (kTwoPi * ((float) k + 0.5f) / (float) malletLen)) / (float) malletLen;
        for (int i = 0; i < count; ++i)
        {
            const float a = re[(size_t) i], b = im[(size_t) i];
            re[(size_t) i] = a * c[(size_t) i] - b * s[(size_t) i] + x * gain[(size_t) i];
            im[(size_t) i] = a * s[(size_t) i] + b * c[(size_t) i];
            left += im[(size_t) i] * gl[(size_t) i];
            right += im[(size_t) i] * gr[(size_t) i];
        }
        ++n;
    }

private:
    std::array<float, kMaxModes> re {}, im {}, c {}, s {}, gain {}, gl {}, gr {};
    int count = 0, malletLen = 16;
    int64_t n = 0, strikeAt = 0;
    float sr = 48000.0f, dur = 1.0f, f0 = 400.0f, malletStrength = 1.0f;
};
} // namespace nb
