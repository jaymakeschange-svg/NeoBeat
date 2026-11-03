// Liquid Swing 4 — a small space around each hit.
//
// Spatial impression ("the sound has size and sits in 3D") comes mostly from
// early lateral reflections 5-80 ms after the direct sound, in roughly the
// 125-1500 Hz range (Barron; Bradley). This adds a handful of reflections
// that arrive at different times and polarities in the two ears, so the
// image opens out without smearing the strike. The lows below the cut stay
// dry and centred; the highs are softened as real walls would.
#pragma once

#include "Core.h"
#include <array>
#include <vector>

namespace ls
{
class EarlyReflections
{
public:
    static constexpr int kTaps = 6;

    void prepare (float newSampleRate)
    {
        sampleRate = newSampleRate;
        // Longest tap 43 ms at size 2, with margin, at any sample rate.
        size = 1;
        while (size < (int) (0.1f * sampleRate) + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        write = 0;
        lowState = highState = 0.0f;
    }

    void configure (float roomSize, float dampHz, float lowCutHz) noexcept
    {
        static constexpr float leftMs[kTaps] = { 6.3f, 11.9f, 17.6f, 23.8f, 31.3f, 39.7f };
        static constexpr float rightMs[kTaps] = { 8.1f, 13.4f, 20.2f, 26.9f, 34.6f, 42.8f };
        static constexpr float leftGain[kTaps] = { 0.90f, -0.72f, 0.60f, -0.50f, 0.40f, -0.32f };
        static constexpr float rightGain[kTaps] = { -0.85f, 0.70f, -0.58f, 0.47f, -0.38f, 0.30f };
        const float s = clampf (roomSize, 0.3f, 2.0f);
        for (int i = 0; i < kTaps; ++i)
        {
            leftDelay[(size_t) i] = std::min (size - 2, (int) (leftMs[i] * s * 0.001f * sampleRate));
            rightDelay[(size_t) i] = std::min (size - 2, (int) (rightMs[i] * s * 0.001f * sampleRate));
            gainLeft[(size_t) i] = leftGain[i];
            gainRight[(size_t) i] = rightGain[i];
        }
        longest = std::max (leftDelay[kTaps - 1], rightDelay[kTaps - 1]);
        lowCoef = smoothingCoefficient (1.0f / (kTwoPi * std::max (dampHz, 200.0f)), sampleRate);
        highCoef = smoothingCoefficient (1.0f / (kTwoPi * std::max (lowCutHz, 20.0f)), sampleRate);
    }

    int tailSamples() const noexcept { return longest + 1; }

    void process (float input, float& left, float& right) noexcept
    {
        // Band-limit what reaches the "walls": keep the lows dry, soften the highs.
        highState += highCoef * (input - highState);
        const float banded = input - highState;
        lowState += lowCoef * (banded - lowState);
        buffer[(size_t) write] = flushTiny (lowState);
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kTaps; ++i)
        {
            l += gainLeft[(size_t) i] * buffer[(size_t) ((write - leftDelay[(size_t) i]) & (size - 1))];
            r += gainRight[(size_t) i] * buffer[(size_t) ((write - rightDelay[(size_t) i]) & (size - 1))];
        }
        write = (write + 1) & (size - 1);
        left = l;
        right = r;
    }

private:
    float sampleRate = 48000.0f;
    std::vector<float> buffer;
    int size = 8192, write = 0, longest = 0;
    std::array<int, kTaps> leftDelay {}, rightDelay {};
    std::array<float, kTaps> gainLeft {}, gainRight {};
    float lowState = 0.0f, highState = 0.0f, lowCoef = 0.5f, highCoef = 0.01f;
};
} // namespace ls
