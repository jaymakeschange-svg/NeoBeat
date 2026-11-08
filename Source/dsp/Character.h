// NeoBeat — depth and fatness ("character") stages for every pad voice.
//
//   waveClip     the Waldorf Wave's drive, fitted to measurements of the
//                open-source emulator's CEM3387 stage: clean to about 1.0x,
//                a sharp knee, a ceiling near 1.33x (odd harmonics only).
//                Driving into it lowers the crest factor and fills the gaps
//                between partials -- the "fat" quality. (Our own curve,
//                x / (1 + |x/L|^12)^(1/12); no emulator code.)
//   BodyNetwork  a small damped feedback delay network tuned around the
//                sound's pitch and fed by the hit itself: dense, short modal
//                body under the partials (IronHead feeds its oscillators into
//                FDNs the same way; that is where its depth comes from).
//   KitBus       glue compression and a touch of saturation on the whole kit.
#pragma once

#include "Filters.h"
#include <array>
#include <vector>

namespace nb
{
inline float waveClip (float x) noexcept
{
    constexpr float invL = 1.0f / 1.325f;
    const float r = x * invL;
    const float r2 = r * r, r4 = r2 * r2, r12 = r4 * r4 * r4;
    return x / std::pow (1.0f + r12, 1.0f / 12.0f);
}

class BodyNetwork
{
public:
    static constexpr int kLines = 4;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        size = 1;
        while (size < (int) (sr / 30.0f * 2.2f) + 8) size <<= 1;   // lowest body pitch 30 Hz
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& l : lp) l = 0.0f;
        w = 0; dcL = dcR = dcInL = dcInR = 0.0f;
    }

    // Irregular line ratios give a dense, chord-free body (like a struck shell).
    void configure (float f0, float t60, float dampHz) noexcept
    {
        static constexpr float ratios[kLines] = { 1.0f, 1.31f, 1.62f, 2.11f };
        const float period = sr / clampf (f0, 30.0f, 2000.0f);
        for (int i = 0; i < kLines; ++i)
        {
            d[(size_t) i] = clampf (period * ratios[i], 2.0f, (float) size - 4.0f);
            g[(size_t) i] = std::pow (10.0f, -3.0f * d[(size_t) i] / (sr * std::max (t60, 0.02f)));
        }
        damp = 1.0f - std::exp (-kTwoPi * clampf (dampHz, 200.0f, 0.45f * sr) / sr);
        dcCoef = 1.0f - std::exp (-kTwoPi * 25.0f / sr);
        // keep the body's loudness roughly independent of its ring time
        inGain = std::sqrt (1.0f - g[0] * g[0]) * 1.6f;
    }

    void process (float in, float& outL, float& outR) noexcept
    {
        std::array<float, kLines> y {};
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float pos = (float) w - d[(size_t) i];
            const float fl = std::floor (pos);
            const float fr = pos - fl;
            const int i0 = (int) ((int64_t) fl & (size - 1)), i1 = (i0 + 1) & (size - 1);
            const float rd = buf[(size_t) i][(size_t) i0] + fr * (buf[(size_t) i][(size_t) i1] - buf[(size_t) i][(size_t) i0]);
            lp[(size_t) i] += damp * (rd - lp[(size_t) i]);
            y[(size_t) i] = lp[(size_t) i];
            sum += y[(size_t) i];
        }
        const float h = sum * (2.0f / kLines);   // Householder reflection
        for (int i = 0; i < kLines; ++i)
            buf[(size_t) i][(size_t) (w & (size - 1))] = in * inGain + g[(size_t) i] * (y[(size_t) i] - h) * (i % 2 ? -1.0f : 1.0f);
        ++w;
        float l = y[0] + 0.7f * y[2], r = y[1] + 0.7f * y[3];
        // remove any DC the loop builds up
        dcInL += dcCoef * (l - dcInL); dcInR += dcCoef * (r - dcInR);
        outL = l - dcInL; outR = r - dcInR;
    }

private:
    std::array<std::vector<float>, kLines> buf;
    std::array<float, kLines> d {}, g {}, lp {};
    float sr = 48000.0f, damp = 0.3f, inGain = 0.5f, dcCoef = 0.003f, dcL = 0, dcR = 0, dcInL = 0, dcInR = 0;
    int size = 4096;
    int64_t w = 0;
};

// Glue compression (stereo-linked, RMS detector) and gentle saturation.
class KitBus
{
public:
    void prepare (float sampleRate) noexcept
    {
        sr = sampleRate;
        env = 0.0f; gainDb = 0.0f;
        att = std::exp (-1.0f / (0.008f * sr));
        rel = std::exp (-1.0f / (0.140f * sr));
        rmsCoef = 1.0f - std::exp (-1.0f / (0.005f * sr));
    }

    // amount 0..1: 0 = bypass; 0.5 = about 3 dB of glue on a busy pattern.
    void process (float& l, float& r, float amount) noexcept
    {
        if (amount <= 0.001f) return;
        const float x2 = 0.5f * (l * l + r * r);
        ms += rmsCoef * (x2 - ms);
        const float levelDb = 10.0f * std::log10 (ms + 1.0e-12f);
        const float threshold = -22.0f + 8.0f * (1.0f - amount), ratio = 1.0f + 3.0f * amount;
        const float over = levelDb - threshold;
        const float target = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
        gainDb = target < gainDb ? att * gainDb + (1.0f - att) * target : rel * gainDb + (1.0f - rel) * target;
        const float g = ls::dbToGain (gainDb + 2.5f * amount);
        l = sat (l * g, amount);
        r = sat (r * g, amount);
    }

private:
    static float sat (float x, float amount) noexcept
    {
        const float k = 1.0f + 1.5f * amount;
        return std::tanh (k * x) / k;
    }

    float sr = 48000.0f, env = 0.0f, gainDb = 0.0f, att = 0.99f, rel = 0.999f, ms = 0.0f, rmsCoef = 0.01f;
};
} // namespace nb
