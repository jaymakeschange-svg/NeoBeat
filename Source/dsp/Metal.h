// Liquid Swing 4 — metal layer: a small damped feedback delay network (FDN).
//
// Idea taken from the analysis of UGO IronHead "Bonk": a 3-line damped network
// with mostly polarity-inverted cross-feeds sits beside the drum body, and one
// "warm -> metallic" control crossfades it in. Here the network has 4 to 8
// lines, an orthogonal feedback matrix (a Householder reflection followed by a
// fixed sign pattern), and every line loses energy on each pass:
//   - a gain below 1 chosen so every line falls 60 dB in the same t60,
//   - a one-pole low-pass (|H| <= 1) that makes the highs die first.
// An orthogonal matrix times a contraction is a contraction, so the network
// cannot run away, whatever the settings. A soft limiter inside the loop is a
// safety net only: at normal levels it is a straight line and changes nothing.
//
// Delay lengths come from inharmonic ratios against baseHz, so the network
// rings as a dense, clangy cluster rather than a comb. The lengths can follow
// the hit's pitch gesture (rubbery metal) and wobble slowly (liquid metal).
#pragma once

#include "Core.h"
#include <array>
#include <vector>

namespace ls
{
struct MetalSpec
{
    float amount = 0.0f;      // output level of the network (0 = off)
    int lines = 6;            // 4..8 delay lines
    float baseHz = 220.0f;    // the lowest line's loop frequency
    float spread = 1.0f;      // scales how far apart the line ratios sit (0.5 tight cluster .. 2 wide)
    float t60 = 0.8f;         // every line falls 60 dB in this time
    float brightHz = 6000.0f; // loop low-pass corner: lower = darker, the highs die sooner
    float strike = 1.0f;      // share of the strike impulse that reaches the network
    float noise = 0.3f;       // a short noise burst into the network (rattle)
    float noiseT60 = 0.02f;
    float feed = 0.0f;        // the body's own ring drives the network (Bonk: oscillators into the FDN)
    float follow = 0.0f;      // share of the pitch gesture the line lengths follow
    float modCents = 0.0f;    // slow per-line wobble of the loop pitch (liquid metal)
    float modHz = 0.7f;
    float width = 0.7f;       // 0 = mono, 1 = left and right hear different lines
    float duck = 0.0f;        // the body drops by this share of `amount` (Bonk's warm -> metallic crossfade)
    float invert = 1.0f;      // 1 = sign pattern on the cross-feeds (Bonk's inverted feeds), 0 = plain Householder
};

class MetalNetwork
{
public:
    static constexpr int kMaxLines = 8;

    void prepare (float newSampleRate)
    {
        sampleRate = newSampleRate;
        // Enough for a 12 Hz loop, so the lowest lines can still be swept down.
        size = 1;
        while (size < (int) (sampleRate / 12.0f) + 8) size <<= 1;
        for (auto& b : buffer) b.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
        lp.fill (0.0f);
        write = 0;
        dcIn = dcOutL = dcOutR = 0.0f;
        dcInR = 0.0f;
    }

    void configure (const MetalSpec& s, uint32_t seed) noexcept
    {
        spec = s;
        n = std::min (std::max (s.lines, 4), kMaxLines);
        // Incommensurate ratios (no two lines share a simple interval), widened or
        // squeezed around the lowest line by `spread`.
        static constexpr float base[kMaxLines] = { 1.0f, 1.37f, 1.83f, 2.41f, 2.98f, 3.62f, 4.27f, 5.13f };
        Rng r (hash32 (seed) | 1u);
        for (int i = 0; i < n; ++i)
        {
            const float ratio = 1.0f + (base[i] - 1.0f) * std::max (s.spread, 0.05f);
            const float jitter = 1.0f + 0.01f * r.bipolar();
            const float hz = clampf (s.baseHz * ratio * jitter, 15.0f, 0.45f * sampleRate);
            lengths[(size_t) i] = sampleRate / hz;
            modPhase[(size_t) i] = kTwoPi * r.uniform();
            modRate[(size_t) i] = 0.6f + 0.8f * r.uniform();
        }
        // Input and output spreads: +-1/sqrt(n) patterns; left and right taps differ.
        static constexpr float inSign[kMaxLines] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        static constexpr float leftSign[kMaxLines] = { 1, 1, -1, 1, -1, -1, 1, -1 };
        static constexpr float rightSign[kMaxLines] = { 1, 1, 1, -1, -1, 1, -1, 1 };   // agrees with leftSign on half the lines: wide but mono-safe
        const float norm = 1.0f / std::sqrt ((float) n);
        for (int i = 0; i < n; ++i)
        {
            inGain[(size_t) i] = inSign[i] * norm;
            const float l = leftSign[i] * norm, rr = rightSign[i] * norm;
            outLeft[(size_t) i] = l;
            outRight[(size_t) i] = lerp (l, rr, clampf (s.width, 0.0f, 1.0f));
            mixSign[(size_t) i] = s.invert > 0.5f ? (float) inSign[(i + 3) % kMaxLines] : 1.0f;
        }
        lpCoef = smoothingCoefficient (1.0f / (kTwoPi * clampf (s.brightHz, 200.0f, 0.45f * sampleRate)), sampleRate);
        dcCoef = smoothingCoefficient (1.0f / (kTwoPi * 25.0f), sampleRate);
        setPitch (0.0f, 0.0f, 0.0f);
        reset();
    }

    // Control rate: move the loop lengths with the pitch gesture and the wobble,
    // and keep every line's loss matched to the same t60 at its current length.
    //   noteSemis    : the played note (the network always tracks the keyboard)
    //   gestureSemis : the hit's pitch gesture, followed by `follow`
    void setPitch (float noteSemis, float gestureSemis, float timeSeconds) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float semis = noteSemis + spec.follow * gestureSemis;
            if (spec.modCents != 0.0f)
                semis += 0.01f * spec.modCents * std::sin (modPhase[(size_t) i] + kTwoPi * spec.modHz * modRate[(size_t) i] * timeSeconds);
            const float len = clampf (lengths[(size_t) i] / semitonesToRatio (semis), 2.0f, (float) size - 4.0f);
            current[(size_t) i] = len;
            loss[(size_t) i] = std::exp (-6.907755f * len / (std::max (spec.t60, 0.01f) * sampleRate));
        }
    }

    // Per sample: excite with `input` (mono) and return the stereo ring.
    void process (float input, float& left, float& right) noexcept
    {
        std::array<float, kMaxLines> out {};
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // Fractional read (linear interpolation) so the lengths can move smoothly.
            const float pos = (float) write - current[(size_t) i];
            const float fl = std::floor (pos);
            const float frac = pos - fl;
            const int i0 = ((int) fl) & (size - 1), i1 = (i0 + 1) & (size - 1);
            const auto& b = buffer[(size_t) i];
            const float o = b[(size_t) i0] + frac * (b[(size_t) i1] - b[(size_t) i0]);
            out[(size_t) i] = o;
            lp[(size_t) i] += lpCoef * (o - lp[(size_t) i]);
            const float v = lp[(size_t) i] * loss[(size_t) i];
            sum += v;
        }
        // Householder reflection: y = v - (2/n) * sum(v), then the sign pattern. Orthogonal.
        const float k = 2.0f / (float) n;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float v = lp[(size_t) i] * loss[(size_t) i];
            float y = mixSign[(size_t) i] * (v - k * sum) + inGain[(size_t) i] * input;
            y = safety (y);
            buffer[(size_t) i][(size_t) write] = flushTiny (y);
            l += outLeft[(size_t) i] * out[(size_t) i];
            r += outRight[(size_t) i] * out[(size_t) i];
        }
        write = (write + 1) & (size - 1);
        // Remove any slow offset so the layer sits cleanly over the sub.
        dcOutL += dcCoef * (l - dcOutL);
        dcOutR += dcCoef * (r - dcOutR);
        left = l - dcOutL;
        right = r - dcOutR;
    }

    int lineCount() const noexcept { return n; }

private:
    // Straight line below 0.9; above that it bends smoothly towards 1.8. Never reached at normal levels.
    static float safety (float x) noexcept
    {
        const float a = std::fabs (x);
        if (a <= 0.9f) return x;
        const float over = a - 0.9f;
        return (x > 0.0f ? 1.0f : -1.0f) * (0.9f + over / (1.0f + over / 0.9f));
    }

    MetalSpec spec;
    float sampleRate = 48000.0f;
    int n = 6, size = 4096, write = 0;
    std::array<std::vector<float>, kMaxLines> buffer;
    std::array<float, kMaxLines> lengths {}, current {}, loss {}, lp {}, inGain {}, outLeft {}, outRight {}, mixSign {}, modPhase {}, modRate {};
    float lpCoef = 0.5f, dcCoef = 0.001f, dcIn = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f;
};
} // namespace ls
