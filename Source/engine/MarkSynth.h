// NeoBeat 2.0 — the Matrixxxer's Sequencer view: drawn marks played as sound.
//   Dot     a sparkle: a bright strike that decays
//   Dash    a drone: holds for its length (swells in over Inertia)
//   Warble  a drone with the MetaSynth warble: pitch wobble at Rate, Depth cents, a little flutter
//   Tick    a click of noise and tone
//   Spray   a grain cloud: sparks at random pitches (Depth spreads them) and places in the field
// Engines: ADD (stretched additive partials, metallic as Intensity rises), FM (two operators, an
// inharmonic ratio above half Intensity), FFT (noise through a bank of razor-thin harmonic bands),
// SMP (grains of a pad's hit, pitched to the mark).
#pragma once

#include "Project.h"
#include "Sources.h"
#include <cmath>
#include <memory>

namespace nb
{
class MarkVoice
{
public:
    bool active = false;
    uint64_t order = 0;

    void start (const Mark& m, const SeqPreset& p, double durSec, float sampleRate, std::shared_ptr<const SampleData> grainSrc,
                uint32_t seed, float levelMul, float panAdd, float pitchAdd) noexcept
    {
        sr = sampleRate; type = m.type; engine = p.engine;
        if (engine == MarkEngine::Smp && ! grainSrc) engine = MarkEngine::Add;
        grains = engine == MarkEngine::Smp ? std::move (grainSrc) : nullptr;
        note = (float) rowNote (p, m.row) + pitchAdd;
        intensity = clamp01 (m.intensity); inertia = std::max (0.0f, m.inertia);
        depthCents = m.depth; rateHz = std::max (0.05f, m.rate);
        dur = std::max (0.01, durSec);
        level = clamp01 (m.level) * levelMul;
        pan = std::max (-1.0f, std::min (1.0f, m.pan + panAdd));
        rng = seed * 747796405u + 2891336453u;
        t = 0.0; ph.fill (0.0); phM = 0.0; noiseState = 0.0f;
        grainMs = p.grainMs; spray = p.spray;
        for (auto& g : gr) g.on = false;
        grainClock = 0.0;
        for (auto& b : bands) b = {};
        sparkT = 1e9; sparkPitch = 0.0f; sparkPan = 0.0f; nextSpark = 0.0;
        driftPh = rand01() * 6.28f;
        // the overall shape
        switch (type)
        {
            case MarkType::Dot:   attack = 0.0015; decay = 0.08 + 0.3 * intensity; hold = 0.0; release = 0.0; break;
            case MarkType::Tick:  attack = 0.0004; decay = 0.014; hold = 0.0; release = 0.0; break;
            case MarkType::Spray: attack = 0.002; decay = 0.0; hold = dur; release = 0.06; break;
            default:              attack = std::max (0.004, (double) inertia); decay = 0.0; hold = dur; release = 0.03 + inertia; break;
        }
        length = decay > 0.0 ? attack + decay * 7.0 : hold + release;
        f0 = 440.0 * std::exp2 ((note - 69.0) / 12.0);
        if (engine == MarkEngine::FFT)
        {
            const float q = 30.0f + 170.0f * intensity;
            for (int k = 0; k < kBands; ++k)
            {
                const double f = f0 * (k + 1);
                bands[(size_t) k].on = f < 0.45 * sr;
                if (! bands[(size_t) k].on) continue;
                bands[(size_t) k].g = (float) std::tan (3.14159265358979 * f / sr);
                bands[(size_t) k].k = 1.0f / q;
                bands[(size_t) k].gain = 0.35f * std::sqrt ((float) (sr * q / (2.0 * f))) / (float) (k + 1) * 0.35f;
            }
        }
        active = true;
    }

    void stop() noexcept { if (active && t < attack + hold) { hold = std::max (0.0, t - attack); release = std::max (release, 0.02); length = attack + hold + release; } }

    void render (float* L, float* R, int n) noexcept
    {
        if (! active) return;
        const double dt = 1.0 / sr;
        for (int i = 0; i < n; ++i)
        {
            if (t >= length) { active = false; grains.reset(); return; }
            float env = envelope();
            // pitch: the inertia scoop, the warble, the spray's sparks
            double semis = 0.0;
            if (inertia > 0.0f && t < inertia) semis -= (1.0 - t / inertia) * std::min (2.0f, inertia * 10.0f);
            float panNow = pan;
            if (type == MarkType::Warble)
            {
                const double drift = 1.0 + 0.08 * std::sin (0.7 * t + driftPh);
                semis += depthCents / 100.0 * std::sin (6.283185307 * rateHz * drift * t);
                env *= (float) (0.9 + 0.1 * std::sin (3.14159265 * rateHz * t + 1.3));
            }
            if (type == MarkType::Spray)
            {
                if (t >= nextSpark)
                {
                    sparkT = 0.0;
                    sparkPitch = (rand01() * 2.0f - 1.0f) * depthCents / 100.0f * 12.0f;
                    sparkPan = (rand01() * 2.0f - 1.0f) * 0.7f;
                    nextSpark = t + 1.0 / (8.0 + 70.0 * intensity) * (0.4 + 1.2 * rand01());
                }
                semis += sparkPitch;
                env *= (float) std::exp (-sparkT / 0.025);
                sparkT += dt;
                panNow = std::max (-1.0f, std::min (1.0f, pan + sparkPan));
            }
            const double f = f0 * std::exp2 (semis / 12.0);
            float x = oscillator (f);
            if (type == MarkType::Tick) x = 0.6f * x + 0.7f * noise();
            x *= env * level;
            L[i] += x * (panNow > 0.0f ? 1.0f - panNow : 1.0f);
            R[i] += x * (panNow < 0.0f ? 1.0f + panNow : 1.0f);
            t += dt;
        }
    }

private:
    static float clamp01 (float v) noexcept { return std::max (0.0f, std::min (1.0f, v)); }
    float rand01() noexcept { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; }
    float noise() noexcept { return rand01() * 2.0f - 1.0f; }

    float envelope() const noexcept
    {
        if (t < attack) return (float) (t / attack);
        if (decay > 0.0) return (float) std::exp (-(t - attack) / (decay / 2.3));
        if (t < attack + hold) return 1.0f;
        return (float) std::max (0.0, 1.0 - (t - attack - hold) / std::max (1e-4, release));
    }

    float oscillator (double f) noexcept
    {
        constexpr double tau = 6.283185307179586;
        switch (engine)
        {
            case MarkEngine::Add:
            {
                const double stretch = 1.0 + 0.045 * intensity;
                const double tilt = 1.9 - 1.3 * intensity;
                float s = 0.0f, norm = 0.0f;
                for (int k = 0; k < kPartials; ++k)
                {
                    const double fk = f * std::pow ((double) (k + 1), stretch);
                    if (fk > 0.45 * sr) break;
                    double a = 1.0 / std::pow ((double) (k + 1), tilt);
                    if (type == MarkType::Dot || type == MarkType::Tick) a *= std::exp (-t * (k + 1) * 2.5);
                    ph[(size_t) k] += fk / sr; if (ph[(size_t) k] >= 1.0) ph[(size_t) k] -= 1.0;
                    s += (float) (a * std::sin (tau * ph[(size_t) k]));
                    norm += (float) a;
                }
                return norm > 0.0f ? 0.55f * s / std::sqrt (norm) : 0.0f;
            }
            case MarkEngine::FM:
            {
                const double ratio = intensity < 0.5 ? 2.0 : 3.53;
                double index = 0.8 + 7.0 * intensity;
                index *= (type == MarkType::Dot || type == MarkType::Tick) ? std::exp (-t * 9.0) * 0.8 + 0.2 : 0.6 + 0.4 * std::exp (-t * 2.0);
                phM += f * ratio / sr; if (phM >= 1.0) phM -= 1.0;
                ph[0] += f / sr; if (ph[0] >= 1.0) ph[0] -= 1.0;
                return 0.42f * (float) std::sin (tau * ph[0] + index * std::sin (tau * phM));
            }
            case MarkEngine::FFT:
            {
                const float in = noise();
                float s = 0.0f;
                for (int k = 0; k < kBands; ++k)
                {
                    auto& b = bands[(size_t) k];
                    if (! b.on) continue;
                    // the band follows the pitch (warble, sparks) through its coefficient
                    const float g = (float) std::tan (3.14159265358979 * std::min (f * (k + 1), 0.45 * sr) / sr);
                    const float hp = (in - (2.0f * b.k + g) * b.s1 - b.s2) / (1.0f + 2.0f * b.k * g + g * g);
                    const float bp = g * hp + b.s1; b.s1 = g * hp + bp;
                    const float lp = g * bp + b.s2; b.s2 = g * bp + lp;
                    s += bp * b.gain;
                }
                return 0.5f * s;
            }
            case MarkEngine::Smp:
            default:
            {
                if (! grains) return 0.0f;
                const int len = (int) grains->left.size();
                const double gLen = std::max (0.004, grainMs * 0.001);
                grainClock -= 1.0 / sr;
                if (grainClock <= 0.0)
                {
                    grainClock = gLen * 0.5;
                    for (auto& g : gr)
                        if (! g.on)
                        {
                            g.on = true; g.t = 0.0; g.len = gLen;
                            g.pos = rand01() * spray * 0.7 * len;
                            g.inc = f / 261.6256 * grains->sampleRate / sr;
                            break;
                        }
                }
                float s = 0.0f;
                for (auto& g : gr)
                {
                    if (! g.on) continue;
                    const int i0 = (int) g.pos;
                    if (i0 >= len - 1 || g.t >= g.len) { g.on = false; continue; }
                    const float fr = (float) (g.pos - i0);
                    const float x = grains->left[(size_t) i0] + fr * (grains->left[(size_t) i0 + 1] - grains->left[(size_t) i0]);
                    const float w = 0.5f - 0.5f * (float) std::cos (tau * g.t / g.len);
                    s += x * w;
                    g.pos += g.inc; g.t += 1.0 / sr;
                }
                return 0.8f * s;
            }
        }
    }

    static constexpr int kPartials = 8, kBands = 6;
    struct Band { bool on = false; float g = 0.1f, k = 0.02f, gain = 1.0f, s1 = 0.0f, s2 = 0.0f; };
    struct Grain { bool on = false; double pos = 0.0, inc = 1.0, t = 0.0, len = 0.04; };
    MarkType type = MarkType::Dot;
    MarkEngine engine = MarkEngine::Add;
    std::shared_ptr<const SampleData> grains;
    float sr = 48000.0f, note = 60.0f, intensity = 0.6f, inertia = 0.0f, depthCents = 35.0f, rateHz = 6.0f, level = 0.8f, pan = 0.0f;
    float grainMs = 40.0f, spray = 0.35f, noiseState = 0.0f, sparkPitch = 0.0f, sparkPan = 0.0f, driftPh = 0.0f;
    double t = 0.0, dur = 0.1, attack = 0.002, decay = 0.1, hold = 0.0, release = 0.0, length = 0.5, f0 = 440.0, phM = 0.0;
    double grainClock = 0.0, sparkT = 1e9, nextSpark = 0.0;
    std::array<double, kPartials> ph {};
    std::array<Band, kBands> bands {};
    std::array<Grain, 8> gr {};
    uint32_t rng = 1;
};

class MarkSynth
{
public:
    static constexpr int kVoices = 32;
    void prepare (float sampleRate) noexcept { sr = sampleRate; for (auto& v : voices) v.active = false; }

    void trigger (const Mark& m, const SeqPreset& p, double durSec, std::shared_ptr<const SampleData> grains, uint32_t seed,
                  float levelMul, float panAdd, float pitchAdd) noexcept
    {
        MarkVoice* target = nullptr;
        for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr) for (auto& v : voices) if (target == nullptr || v.order < target->order) target = &v;
        target->start (m, p, durSec, sr, std::move (grains), seed, levelMul, panAdd, pitchAdd);
        target->order = ++counter;
    }
    void render (float* L, float* R, int n) noexcept { for (auto& v : voices) if (v.active) v.render (L, R, n); }
    void allOff() noexcept { for (auto& v : voices) v.stop(); }
    int activeCount() const noexcept { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    std::array<MarkVoice, kVoices> voices;
    float sr = 48000.0f;
    uint64_t counter = 0;
};
} // namespace nb
