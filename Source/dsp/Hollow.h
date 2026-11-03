// NeoBeat — the TurboSynth-style "hollow" chain, running live on one hit.
//
// The Round 6 lab ran these modules offline over whole buffers. Here each pad
// voice gets its own chain, and the chain *pulls* its source: the Stretcher's
// read point moves slower than real time, so the source is rendered on demand
// (a little ahead of the read point) instead of buffering the whole hit.
//
//   stretcher   pitch-synchronous grains (2 periods, Hann) read from a slowly
//               moving, wobbling point, snapped to whole periods so periodic
//               sound joins without phase jumps
//   resonator   feedback comb, polarity -1 at half the period: only odd
//               harmonics ring (the stopped-tube colour); wet level follows dry
//   inverter    each harmonic's level is measured live (heterodyne analysis)
//               and handed to its mirror partner k -> H+1-k; compressed, tilted
//               and lift-limited so a ringing fundamental can't turn into a
//               steady whistle; a drawn envelope mixes it in
//   modulator   AM, or PM as a modulated short delay
//   waveshaper  soft knee with a little asymmetry, normalised to the sound's level
//   filter env  drawn low-pass sweep
#pragma once

#include "Filters.h"
#include <array>
#include <vector>

namespace nb
{
struct StereoSource
{
    virtual ~StereoSource() = default;
    virtual void renderFrame (float& left, float& right) noexcept = 0;
    virtual bool isActive() const noexcept = 0;
};

// A drawn envelope: up to 4 (time 0..1, value) points, linear between them.
struct Breakpoints
{
    int count = 2;
    std::array<float, 4> t { 0.0f, 1.0f, 1.0f, 1.0f }, v { 0.0f, 0.0f, 0.0f, 0.0f };

    static Breakpoints make (std::initializer_list<std::pair<float, float>> pts) noexcept
    {
        Breakpoints b; b.count = 0;
        for (const auto& p : pts)
            if (b.count < 4) { b.t[(size_t) b.count] = p.first; b.v[(size_t) b.count] = p.second; ++b.count; }
        return b;
    }

    float at (float u) const noexcept
    {
        if (count <= 0) return 0.0f;
        if (u <= t[0]) return v[0];
        for (int i = 1; i < count; ++i)
            if (u <= t[(size_t) i])
            {
                const float span = std::max (t[(size_t) i] - t[(size_t) i - 1], 1.0e-6f);
                return v[(size_t) i - 1] + (v[(size_t) i] - v[(size_t) i - 1]) * (u - t[(size_t) i - 1]) / span;
            }
        return v[(size_t) count - 1];
    }
};

struct HollowSpec
{
    float stretch = 0.55f, wobble = 0.3f, wobbleHz = 0.3f, mismatch = 0.003f, predelay = 0.03f;
    float resFb = 0.86f, resMix = 0.7f, resOctave = 0.0f, dampHz = 7000.0f;
    Breakpoints inv = Breakpoints::make ({ { 0.0f, 0.0f }, { 0.3f, 0.45f }, { 0.7f, 0.2f }, { 1.0f, 0.35f } });
    bool modPm = true;
    float modRate = 0.35f, modDepth = 0.6f;
    float shape = 1.4f, asym = 0.15f;
    Breakpoints cut = Breakpoints::make ({ { 0.0f, 9000.0f }, { 0.4f, 3500.0f }, { 1.0f, 2000.0f } });
    float cutQ = 0.8f;
    float length = 1.6f;                       // seconds the drawn envelopes span
    float f0 = 200.0f, f0End = 0.0f, f0Glide = 0.0f;   // f0End <= 0: steady pitch
};

class HollowChain
{
public:
    static constexpr int kRing = 1 << 15;
    static constexpr int kComb = 1 << 13;
    static constexpr int kPmDelay = 1 << 11;
    static constexpr int kMaxH = 15;
    static constexpr int kTick = 16;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            ring[(size_t) c].assign (kRing, 0.0f);
            ch[(size_t) c].comb.assign (kComb, 0.0f);
            ch[(size_t) c].pm.assign (kPmDelay, 0.0f);
        }
    }

    bool isActive() const noexcept { return active; }

    // evolve 0..1 (0.5 = the recipe as drawn); timeScale stretches the drawn
    // envelopes (the pad's Decay); levelRef = the hit's RMS level (for the shaper).
    void start (const HollowSpec& s, StereoSource* src, float tuneRatio, float timeScale, float evolve, float levelRef) noexcept
    {
        spec = s; source = src; tune = tuneRatio;
        len = std::max (0.05f, spec.length * std::max (timeScale, 0.1f));
        const float e = clampf (evolve, 0.0f, 1.0f) * 2.0f;
        stretchEff = clampf (spec.stretch * e, 0.0f, 0.92f);
        wobbleEff = spec.wobble * e;
        invScale = e;
        modDepthEff = spec.modDepth * e;
        level = std::max (levelRef, 1.0e-4f);
        n = 0; written = 0; nextCenter = 0.0;
        numGrains = 0;
        for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f);
        // harmonic count from the median pitch: odd, so odd harmonics stay odd
        const float fEnd = spec.f0End > 0.0f ? spec.f0End : spec.f0;
        const float fMed = std::sqrt (spec.f0 * fEnd) * tune;
        H = std::max (5, std::min (kMaxH, (int) (6000.0f / std::max (fMed, 20.0f))));
        if (H % 2 == 0) --H;
        const float fLp = clampf (0.3f * spec.f0 * tune, 8.0f, 50.0f);
        lpCoef = 1.0f - std::exp (-kTwoPi * fLp / sr);
        followCoef = 1.0f - std::exp (-1.0f / (0.25f * sr));
        invFollowCoef = 1.0f - std::exp (-1.0f / (0.03f * sr));
        gainSmooth = 1.0f - std::exp (-1.0f / (0.05f * sr));
        const float fb = clampf (spec.resFb, 0.0f, 0.98f);
        combG0 = std::sqrt ((1.0f + fb) / (1.0f - fb));
        damp = 1.0f - std::exp (-kTwoPi * spec.dampHz / sr);
        for (auto& c : ch)
        {
            std::fill (c.comb.begin(), c.comb.end(), 0.0f);
            std::fill (c.pm.begin(), c.pm.end(), 0.0f);
            c.combW = 0; c.combLp = 0.0f; c.px = c.pw = 0.0f; c.combGain = combG0;
            c.pmW = 0;
            c.invPx = 0.0f; c.globalMax = 0.0f;
            for (int k = 0; k < kMaxH; ++k)
            {
                c.z1[(size_t) k] = c.z2[(size_t) k] = {};
                c.pr[(size_t) k] = 1.0f; c.pi[(size_t) k] = 0.0f;
                c.cur[(size_t) k] = c.step[(size_t) k] = 0.0f;
            }
            c.filter.reset();
        }
        modPhase = 0.0f;
        tickCount = 0;
        controlTick (0.0f);
        active = true;
    }

    void render (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        if (! active) return;
        const float t = (float) n / sr;
        if (t >= len) { active = false; return; }
        if (tickCount-- <= 0) { tickCount = kTick - 1; controlTick (t); }

        // 1. Stretcher
        while (true)
        {
            const float Pc = periodAt ((float) nextCenter / sr);
            if ((int64_t) nextCenter - (int64_t) Pc > n) break;
            addGrain (nextCenter, Pc);
            nextCenter += Pc;
        }
        float x[2] = { 0.0f, 0.0f };
        for (int g = 0; g < numGrains;)
        {
            auto& gr = grains[(size_t) g];
            const int64_t k = n - gr.outStart;
            if (k >= gr.length) { grains[(size_t) g] = grains[(size_t) --numGrains]; continue; }
            if (k >= 0)
            {
                const float w = 0.5f - 0.5f * std::cos (kTwoPi * ((float) k + 0.5f) / (float) gr.length);
                const double s = gr.srcStart + (double) k;
                for (int c = 0; c < 2; ++c) x[c] += w * readRing (c, s);
            }
            ++g;
        }

        const float invMix = clampf (spec.inv.at (t / len) * invScale, 0.0f, 1.0f);
        const float modS = std::sin (modPhase);
        modPhase += kTwoPi * spec.modRate / sr;
        if (modPhase > kTwoPi) modPhase -= kTwoPi;
        const float tail = std::min (0.12f, 0.2f * len);
        const float fade = t > len - tail ? (len - t) / tail : 1.0f;

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& C = ch[(size_t) c];
            float y = x[c];
            // 2. Resonator
            {
                const int64_t wr = C.combW;
                const float pos = (float) wr - combDelay;
                const float fl = std::floor (pos);
                const float fr = pos - fl;
                const int i0 = (int) ((int64_t) fl & (kComb - 1)), i1 = (i0 + 1) & (kComb - 1);
                const float rd = C.comb[(size_t) i0] + fr * (C.comb[(size_t) i1] - C.comb[(size_t) i0]);
                C.combLp += damp * (rd - C.combLp);
                float v = y * (1.0f - spec.resFb) - spec.resFb * C.combLp;
                v = clampf (v, -4.0f, 4.0f);
                C.comb[(size_t) (wr & (kComb - 1))] = v;
                C.combW = wr + 1;
                C.px += followCoef * (y * y - C.px);
                C.pw += followCoef * (v * v - C.pw);
                if (C.px > 1.0e-12f && C.pw > 1.0e-14f)
                {
                    const float target = clampf (std::sqrt (C.px / C.pw), 0.25f * combG0, 4.0f * combG0);
                    C.combGain += gainSmooth * (target - C.combGain);
                }
                y = (1.0f - spec.resMix) * y + spec.resMix * v * C.combGain;
            }
            // 3. Harmonic inverter (heterodyne analysis + mirrored resynthesis)
            {
                C.invPx += invFollowCoef * (y * y - C.invPx);
                float syn = 0.0f;
                for (int k = 0; k < H; ++k)
                {
                    // rotate this harmonic's phasor
                    const float pr = C.pr[(size_t) k], pi = C.pi[(size_t) k];
                    C.pr[(size_t) k] = pr * rotC[(size_t) k] - pi * rotS[(size_t) k];
                    C.pi[(size_t) k] = pr * rotS[(size_t) k] + pi * rotC[(size_t) k];
                    // demodulate: y * conj(phasor), two one-pole low-passes
                    auto& z1 = C.z1[(size_t) k];
                    auto& z2 = C.z2[(size_t) k];
                    z1[0] += lpCoef * (y * pr - z1[0]);
                    z1[1] += lpCoef * (-y * pi - z1[1]);
                    z2[0] += lpCoef * (z1[0] - z2[0]);
                    z2[1] += lpCoef * (z1[1] - z2[1]);
                    C.cur[(size_t) k] += C.step[(size_t) k];
                    syn += C.cur[(size_t) k] * pi;
                }
                y = y * (1.0f - invMix) + syn * invMix;
            }
            // 4. Modulator
            if (spec.modPm)
            {
                C.pm[(size_t) (C.pmW & (kPmDelay - 1))] = y;
                const float d = 0.004f * modDepthEff * sr * (1.0f + modS);
                const float pos = (float) C.pmW - std::min (d, (float) kPmDelay - 4.0f);
                const float fl = std::floor (pos), fr = pos - fl;
                const int i0 = (int) ((int64_t) fl & (kPmDelay - 1)), i1 = (i0 + 1) & (kPmDelay - 1);
                y = C.pm[(size_t) i0] + fr * (C.pm[(size_t) i1] - C.pm[(size_t) i0]);
                ++C.pmW;
            }
            else
            {
                const float dpt = clampf (modDepthEff, 0.0f, 1.0f);
                y *= 1.0f - dpt + dpt * 0.5f * (1.0f + modS);
            }
            // 5. Waveshaper
            if (spec.shape > 0.0f)
            {
                const float g4 = 4.0f * level;
                const float u = y / g4;
                const float d = spec.shape;
                y = (std::tanh (d * (u + spec.asym)) - std::tanh (d * spec.asym)) / std::tanh (d) * g4;
            }
            // 6. Filter envelope
            float lo, band, hi;
            C.filter.process (y, lo, band, hi);
            out[c] = lo * fade;
        }
        left = out[0];
        right = out[1];
        ++n;
        if (! std::isfinite (left) || ! std::isfinite (right)) { left = right = 0.0f; active = false; }
    }

private:
    struct Grain { int64_t outStart = 0; double srcStart = 0.0; int length = 0; };

    struct Channel
    {
        std::vector<float> comb, pm;
        int64_t combW = 0, pmW = 0;
        float combLp = 0.0f, px = 0.0f, pw = 0.0f, combGain = 1.0f;
        float invPx = 0.0f, globalMax = 0.0f;
        std::array<std::array<float, 2>, kMaxH> z1 {}, z2 {};
        std::array<float, kMaxH> pr {}, pi {}, cur {}, step {};
        ls::StateVariableFilter filter;
    };

    float hzAt (float t) const noexcept
    {
        float f = spec.f0;
        if (spec.f0End > 0.0f && spec.f0Glide > 0.0f)
            f = spec.f0 * std::pow (spec.f0End / spec.f0, clampf (t / spec.f0Glide, 0.0f, 1.0f));
        return f * tune;
    }

    float periodAt (float t) const noexcept { return sr / (std::max (hzAt (t), 20.0f) * (1.0f + spec.mismatch)); }

    // Where the Stretcher reads (in source samples) at output sample c.
    double readPosition (double c) const noexcept
    {
        const double pd = spec.predelay * sr;
        const double speed = 1.0 - 0.95 * stretchEff;
        double p = c < pd ? c + 1.0 : pd + (c - pd + 1.0) * speed;
        if (c > pd) p += wobbleEff * sr * 0.05 * std::sin (2.0 * kPiD * spec.wobbleHz * c / sr);
        return std::max (0.0, p);
    }

    void addGrain (double centre, float P) noexcept
    {
        if (numGrains >= (int) grains.size()) return;
        const double d = readPosition (centre);
        const double m = std::floor ((centre - d) / P + 0.5);
        const double r = centre - m * P;
        Grain g;
        g.length = std::max (2, (int) (2.0f * P));
        g.outStart = (int64_t) centre - (int64_t) P;
        g.srcStart = r - P;
        grains[(size_t) numGrains++] = g;
        ensureSource ((int64_t) (g.srcStart + g.length) + 2);
    }

    void ensureSource (int64_t upTo) noexcept
    {
        while (written <= upTo)
        {
            float l = 0.0f, r = 0.0f;
            if (source != nullptr && source->isActive()) source->renderFrame (l, r);
            ring[0][(size_t) (written & (kRing - 1))] = l;
            ring[1][(size_t) (written & (kRing - 1))] = r;
            ++written;
        }
    }

    float readRing (int c, double s) const noexcept
    {
        if (s < 0.0) return 0.0f;
        const int64_t si = (int64_t) s;
        if (si + 1 >= written || si < written - kRing + 1) return 0.0f;
        const float fr = (float) (s - (double) si);
        const auto& r = ring[(size_t) c];
        const float a = r[(size_t) (si & (kRing - 1))], b = r[(size_t) ((si + 1) & (kRing - 1))];
        return a + fr * (b - a);
    }

    void controlTick (float t) noexcept
    {
        const float f = hzAt (t);
        combDelay = clampf (sr / (2.0f * f * std::exp2 (spec.resOctave)), 2.0f, (float) kComb - 4.0f);
        for (int k = 0; k < H; ++k)
        {
            const float w = kTwoPi * (float) (k + 1) * f / sr;
            rotC[(size_t) k] = std::cos (w);
            rotS[(size_t) k] = std::sin (w);
        }
        const float cutHz = spec.cut.at (t / len);
        for (auto& C : ch)
        {
            C.filter.set (cutHz, spec.cutQ, sr);
            // renormalise the phasors
            for (int k = 0; k < H; ++k)
            {
                const float mag = std::sqrt (C.pr[(size_t) k] * C.pr[(size_t) k] + C.pi[(size_t) k] * C.pi[(size_t) k]);
                if (mag > 0.0f) { C.pr[(size_t) k] /= mag; C.pi[(size_t) k] /= mag; }
            }
            // measured harmonic levels -> mirrored, compressed, tilted, lift-limited targets
            std::array<float, kMaxH> A {}, a {}, inv {};
            float frameMax = 0.0f;
            for (int k = 0; k < H; ++k)
            {
                const auto& z = C.z2[(size_t) k];
                A[(size_t) k] = 2.0f * std::sqrt (z[0] * z[0] + z[1] * z[1]);
                frameMax = std::max (frameMax, A[(size_t) k]);
            }
            C.globalMax = std::max (C.globalMax, frameMax);
            float target[kMaxH] {};
            if (C.globalMax > 1.0e-9f)
            {
                float M = 0.0f;
                for (int k = 0; k < H; ++k) { a[(size_t) k] = std::pow (A[(size_t) k] / C.globalMax, 0.8f); M = std::max (M, a[(size_t) k]); }
                float eFree = 0.0f;
                for (int k = 0; k < H; ++k)
                {
                    const float free = a[(size_t) (H - 1 - k)] * std::pow ((float) (k + 1), -0.7f);
                    eFree += free * free;
                    inv[(size_t) k] = std::min (free, 8.0f * a[(size_t) k] + 0.003f * M);
                }
                // power: the uncapped mirror would carry the input's power; the cap's loss stays lost
                const float s = std::sqrt (C.invPx / std::max (0.5f * eFree, 1.0e-20f));
                for (int k = 0; k < H; ++k) target[k] = inv[(size_t) k] * s;
            }
            for (int k = 0; k < H; ++k)
                C.step[(size_t) k] = (target[k] - C.cur[(size_t) k]) / (float) kTick;
        }
    }

    HollowSpec spec;
    StereoSource* source = nullptr;
    std::array<std::vector<float>, 2> ring;
    std::array<Channel, 2> ch;
    std::array<Grain, 8> grains {};
    std::array<float, kMaxH> rotC {}, rotS {};
    int numGrains = 0, H = 15, tickCount = 0;
    int64_t n = 0, written = 0;
    double nextCenter = 0.0;
    float sr = 48000.0f, tune = 1.0f, len = 1.0f, stretchEff = 0.5f, wobbleEff = 0.3f, invScale = 1.0f, modDepthEff = 0.6f;
    float level = 0.05f, lpCoef = 0.001f, followCoef = 0.0001f, invFollowCoef = 0.001f, gainSmooth = 0.001f;
    float combG0 = 1.0f, damp = 0.5f, combDelay = 100.0f, modPhase = 0.0f;
    bool active = false;
};
} // namespace nb
