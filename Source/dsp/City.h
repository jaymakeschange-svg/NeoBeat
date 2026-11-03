// NeoBeat — city sounds as live, playable models (ported from the Round 6 lab's
// city.py). No recordings: each is a small physical sketch that takes a pitch
// and a length, so the event behaves like an instrument and still sounds like
// the event. Every model is mono; the recipes place them in the stereo image.
//
//   Horn         dual-tone car horn: buzzing diaphragm through horn formants
//   BusEngine    diesel idle: firing pulses at the played pitch, exhaust resonances
//   Bell         church bell: hum/prime/tierce/quint/nominal partials, beating doublets
//   BrakeSqueal  stick-slip tone with pitch jitter and flutter
//   Drill        hammer drill: motor whine spinning up, hammer blows ringing chuck and wall
//   TireScreech  narrow-band rubber noise with stick-slip chatter
//   DogBark      glottal pulses with a fast pitch fall through formants
//   Jet          flyover: pink roar, turbine whine, Doppler
//   DoorHiss     pneumatic door hiss
#pragma once

#include "Filters.h"
#include <array>

namespace nb
{
// Band-limited pulse-rich buzz with cycle-to-cycle jitter (the lab's _pulse_source).
struct PulseSource
{
    std::array<float, 64> weights {};
    int harmonics = 40;
    float phase = 0.0f, jitter = 0.004f, sr = 48000.0f;
    NoiseLowpass drift;
    Rng rng;

    void start (int numHarmonics, float rolloff, float jitterAmount, uint32_t seed, float sampleRate) noexcept
    {
        harmonics = std::min (numHarmonics, 64);
        for (int h = 0; h < harmonics; ++h)
            weights[(size_t) h] = 1.0f / std::pow ((float) (h + 1), rolloff);
        jitter = jitterAmount; sr = sampleRate; phase = 0.0f;
        rng = Rng (ls::hash32 (seed) | 1u);
        drift.set (0.002f, 0.998f, sr);
    }

    float process (float hz) noexcept
    {
        const float d = drift.process (gauss (rng)) * 20.0f;
        phase += hz * (1.0f + jitter * d) / sr;
        phase -= std::floor (phase);
        const int count = std::min (harmonics, (int) (0.45f * sr / std::max (hz, 1.0f)));
        return count > 0 ? harmonicSum (kTwoPi * phase, weights.data(), count) : 0.0f;
    }
};

struct Horn
{
    PulseSource a, b;
    Biquad r1, r2, r3;
    Adsr env;
    float f0 = 415.0f, dur = 0.35f, sr = 48000.0f;
    int64_t n = 0;

    void start (float hz, float duration, float gate, uint32_t seed, float sampleRate) noexcept
    {
        f0 = hz; dur = duration; sr = sampleRate; n = 0;
        a.start (36, 0.55f, 0.006f, seed * 2u + 1u, sr);
        b.start (36, 0.55f, 0.006f, seed * 2u + 2u, sr);
        r1.setResonator (420.0f, 2.5f, 1.0f, sr); r1.reset();
        r2.setResonator (2700.0f, 4.0f, 1.4f, sr); r2.reset();
        r3.setResonator (3600.0f, 5.0f, 0.7f, sr); r3.reset();
        env.start (0.012f, 0.08f, 0.85f, 0.05f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float scoop = std::exp2 (-1.2f * std::exp (-t / 0.03f) / 12.0f);
        const float sa = a.process (f0 * scoop) * (1.0f + 0.15f * std::sin (kTwoPi * 40.0f * t));
        const float sb = b.process (f0 * 1.26f * scoop) * (1.0f + 0.15f * std::sin (kTwoPi * (31.0f + 9.0f * 1.26f) * t));
        const float x = sa + 0.8f * sb;
        const float y = r1.process (x) + r2.process (x) + r3.process (x) + 0.15f * x;
        return std::tanh (1.5f * y * env.next()) * 0.5f * endFade (t, dur);
    }
};

struct BusEngine
{
    Rng rng;
    NoiseLowpass jit;
    Leak shaped, rattleEnv;
    Biquad e1, e2, e3, body, rattleHp, rumbleLp;
    Adsr env;
    float f0 = 110.0f, dur = 1.0f, sr = 48000.0f, phase = 0.0f;
    int64_t n = 0;

    void start (float hz, float duration, float gate, uint32_t seed, float sampleRate) noexcept
    {
        f0 = hz; dur = duration; sr = sampleRate; n = 0; phase = 0.0f;
        rng = Rng (ls::hash32 (seed * 5u + 3u) | 1u);
        jit.set (0.01f, 0.99f, sr);
        shaped.set (0.93f, sr); rattleEnv.set (0.97f, sr);
        e1.setResonator (95.0f, 3.0f, 1.2f, sr); e2.setResonator (240.0f, 4.0f, 0.9f, sr); e3.setResonator (520.0f, 5.0f, 0.5f, sr);
        body.setLowpass (1800.0f, 0.7071f, sr); rattleHp.setHighpass (1500.0f, 0.7071f, sr); rumbleLp.setLowpass (120.0f, 0.7071f, sr);
        for (auto* f : { &e1, &e2, &e3, &body, &rattleHp, &rumbleLp }) f->reset();
        env.start (0.05f, 0.2f, 0.9f, 0.12f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float j = 1.0f + 0.3f * jit.process (gauss (rng));
        const float before = phase;
        phase += f0 * j / sr;
        const bool fire = std::floor (phase) > std::floor (before);
        const float pulse = fire ? 0.7f + 0.3f * rng.uniform() : 0.0f;
        const float s = shaped.process (pulse);
        const float y = e1.process (s) + e2.process (s) + e3.process (s) + 0.3f * body.process (s);
        const float rattle = rattleHp.process (gauss (rng)) * rattleEnv.process (pulse) * 0.05f;
        const float rumble = rumbleLp.process (gauss (rng)) * 0.15f;
        return (y + rattle + rumble) * env.next() * 0.35f * endFade (t, dur);
    }
};

struct Bell
{
    static constexpr int kPartials = 11;
    static constexpr float kRatios[kPartials] = { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f };
    static constexpr float kAmps[kPartials] = { 1.0f, 0.9f, 0.7f, 0.45f, 0.8f, 0.35f, 0.3f, 0.25f, 0.18f, 0.1f, 0.06f };
    static constexpr float kT60s[kPartials] = { 1.0f, 0.75f, 0.6f, 0.45f, 0.5f, 0.3f, 0.28f, 0.22f, 0.15f, 0.1f, 0.08f };

    // Each partial and its beating partner: a decaying rotating phasor.
    std::array<float, 2 * kPartials> re {}, im {}, c {}, s {}, amp {};
    int count = 0;
    Rng rng;
    BandPass4 clapBp;
    float clapAmp = 0.0f, clapRadius = 0.99f, strike = 1.0f, dur = 3.0f, sr = 48000.0f;
    int64_t n = 0;

    void start (float hz, float duration, float t60, float strikeAmount, uint32_t seed, float sampleRate) noexcept
    {
        sr = sampleRate; dur = duration; strike = strikeAmount; n = 0; count = 0;
        rng = Rng (ls::hash32 (seed * 7u + 1u) | 1u);
        for (int k = 0; k < kPartials; ++k)
        {
            const float f = hz * kRatios[k];
            if (f > 0.45f * sr) continue;
            const float beat = 0.4f + 0.6f * rng.uniform();
            const float radius = ls::t60ToRadius (t60 * kT60s[k], sr);
            for (int p = 0; p < 2; ++p)
            {
                const float fp = f + (p == 1 ? beat : 0.0f);
                const float w = kTwoPi * fp / sr, ph = kTwoPi * rng.uniform();
                re[(size_t) count] = std::cos (ph); im[(size_t) count] = std::sin (ph);
                c[(size_t) count] = radius * std::cos (w); s[(size_t) count] = radius * std::sin (w);
                amp[(size_t) count] = kAmps[k] * (p == 1 ? 0.5f : 1.0f);
                ++count;
            }
        }
        clapBp.set (3500.0f, 1.5f, sr);
        clapAmp = 1.0f; clapRadius = ls::t60ToRadius (0.03f, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        float x = 0.0f;
        for (int k = 0; k < count; ++k)
        {
            const float a = re[(size_t) k], b = im[(size_t) k];
            re[(size_t) k] = a * c[(size_t) k] - b * s[(size_t) k];
            im[(size_t) k] = a * s[(size_t) k] + b * c[(size_t) k];
            x += amp[(size_t) k] * im[(size_t) k];
        }
        const float attack = std::min (1.0f, t / 0.002f);
        const float clap = clapBp.process (gauss (rng)) * clapAmp * attack * 0.6f * strike;
        clapAmp *= clapRadius;
        return (x * 0.25f + clap * 0.2f) * endFade (t, dur);
    }
};

struct BrakeSqueal
{
    Rng rng;
    NoiseLowpass stick, flutter;
    BandPass4 hissBp;
    Adsr env;
    float fStart = 1900.0f, fEnd = 1700.0f, dur = 1.4f, sr = 48000.0f, phase = 0.0f;
    int64_t n = 0;

    void start (float hzStart, float hzEnd, float duration, float gate, uint32_t seed, float sampleRate) noexcept
    {
        fStart = hzStart; fEnd = hzEnd; dur = duration; sr = sampleRate; n = 0; phase = 0.0f;
        rng = Rng (ls::hash32 (seed * 11u + 5u) | 1u);
        stick.set (0.05f, 0.95f, sr);
        flutter.set (0.02f, 0.98f, sr);
        hissBp.set (0.5f * (fStart + fEnd) * 1.5f, 3.0f, sr);
        env.start (0.08f, 0.3f, 0.8f, 0.25f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float f = fStart + (fEnd - fStart) * clampf (t / dur, 0.0f, 1.0f);
        phase += f * (1.0f + 0.016f * stick.process (gauss (rng))) / sr;
        phase -= std::floor (phase);
        const float th = kTwoPi * phase;
        const float x = std::sin (th) + 0.25f * std::sin (2.0f * th + 0.3f) + 0.1f * std::sin (3.0f * th);
        const float fl = clampf (1.0f + 2.1f * flutter.process (gauss (rng)), 0.2f, 1.8f);
        const float hiss = hissBp.process (gauss (rng)) * 0.1f;
        return (x * fl + hiss) * env.next() * 0.25f * endFade (t, dur);
    }

    // Steady pitch, for building single-cycle waves.
    void startSteady (float hz, float duration, uint32_t seed, float sampleRate) noexcept
    {
        start (hz, hz, duration, duration * 0.7f, seed, sampleRate);
    }
};

struct Drill
{
    Rng rng;
    std::array<float, 13> whineWeights {};
    Biquad c1, c2, c3, wall, gritHp;
    Leak gritEnv, whineEnv;
    Adsr env;
    float f0 = 330.0f, hammer = 38.0f, dur = 0.4f, sr = 48000.0f, phase = 0.0f, hammerPhase = 0.0f;
    int64_t n = 0;

    void start (float hz, float duration, float hammerHz, float gate, uint32_t seed, float sampleRate) noexcept
    {
        f0 = hz; dur = duration; hammer = hammerHz; sr = sampleRate; n = 0; phase = hammerPhase = 0.0f;
        rng = Rng (ls::hash32 (seed * 13u + 9u) | 1u);
        for (int h = 0; h < 13; ++h) whineWeights[(size_t) h] = 1.0f / (float) (h + 1);
        c1.setResonator (1830.0f, 25.0f, 1.0f, sr); c2.setResonator (2950.0f, 25.0f, 0.7f, sr); c3.setResonator (4700.0f, 25.0f, 0.5f, sr);
        wall.setResonator (160.0f, 3.0f, 1.2f, sr); gritHp.setHighpass (3000.0f, 0.7071f, sr);
        for (auto* f : { &c1, &c2, &c3, &wall, &gritHp }) f->reset();
        gritEnv.set (0.995f, sr); whineEnv.set (0.99f, sr);
        env.start (0.02f, 0.1f, 0.9f, 0.08f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float spin = f0 * (0.55f + 0.45f * (1.0f - std::exp (-t / 0.12f)));
        phase += spin / sr; phase -= std::floor (phase);
        const int count = std::min (13, (int) (0.45f * sr / std::max (spin, 1.0f)));
        const float whine = harmonicSum (kTwoPi * phase, whineWeights.data(), count);
        const float before = hammerPhase;
        hammerPhase += hammer / sr;
        const float blow = std::floor (hammerPhase) > std::floor (before) ? 0.6f + 0.4f * rng.uniform() : 0.0f;
        const float chuck = c1.process (blow) + c2.process (blow) + c3.process (blow);
        const float w = wall.process (blow);
        const float grit = gritHp.process (gauss (rng)) * gritEnv.process (blow) * 0.02f;
        const float buzz = 0.8f + 0.2f * whineEnv.process (blow);
        return (0.25f * whine * buzz + 0.6f * chuck + 0.6f * w + grit) * env.next() * 0.3f * endFade (t, dur);
    }
};

struct TireScreech
{
    Rng rng;
    Biquad res;
    Adsr env;
    float fStart = 2100.0f, fEnd = 1500.0f, dur = 0.6f, sr = 48000.0f, chatterPhase = 0.0f;
    int64_t n = 0;

    void start (float hzStart, float hzEnd, float duration, float gate, uint32_t seed, float sampleRate) noexcept
    {
        fStart = hzStart; fEnd = hzEnd; dur = duration; sr = sampleRate; n = 0; chatterPhase = 0.0f;
        rng = Rng (ls::hash32 (seed * 17u + 2u) | 1u);
        res.reset();
        env.start (0.03f, 0.2f, 0.85f, 0.15f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n;
        if (t / sr >= dur) { ++n; return 0.0f; }
        if (n % 256 == 0)   // the lab updates the resonator per 256-sample block
        {
            const float f = fStart + (fEnd - fStart) * clampf ((t + 128.0f) / (dur * sr), 0.0f, 1.0f);
            res.setResonator (f, 40.0f, 1.0f, sr);
        }
        ++n;
        const float y = res.process (gauss (rng));
        chatterPhase += (90.0f + 60.0f * rng.uniform()) / sr;
        chatterPhase -= std::floor (chatterPhase);
        const float chatter = 1.0f + (chatterPhase < 0.5f ? 0.6f : -0.6f);
        return y * chatter * env.next() * 2.5f * endFade (t / sr, dur);
    }
};

struct DogBark
{
    PulseSource src;
    Biquad f1, f2, f3;
    BandPass4 breathBp;
    Rng rng;
    float f0 = 420.0f, dur = 0.3f, sr = 48000.0f;
    int64_t n = 0;

    void start (float hz, float duration, uint32_t seed, float sampleRate) noexcept
    {
        f0 = hz; dur = duration; sr = sampleRate; n = 0;
        src.start (30, 1.0f, 0.02f, seed * 3u + 8u, sr);
        f1.setResonator (750.0f, 4.0f, 1.0f, sr); f2.setResonator (1650.0f, 6.0f, 0.7f, sr); f3.setResonator (2700.0f, 8.0f, 0.4f, sr);
        f1.reset(); f2.reset(); f3.reset();
        breathBp.set (2000.0f, 1.2f, sr);
        rng = Rng (ls::hash32 (seed * 23u + 4u) | 1u);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float pitch = f0 * (1.0f - 0.38f * (1.0f - std::exp (-t / 0.07f)));
        const float s = src.process (pitch);
        const float jaw = clampf (1.0f - t / 0.25f, 0.3f, 1.0f);
        const float y = (f1.process (s) + f2.process (s) + f3.process (s)) * (0.6f + 0.4f * jaw);
        const float breath = breathBp.process (gauss (rng)) * 0.12f;
        const float e = clampf (t / 0.008f, 0.0f, 1.0f) * std::exp (-std::max (t - 0.06f, 0.0f) / 0.06f);
        return (y + breath) * e * 0.6f * endFade (t, dur);
    }
};

// Pink noise (the lab's 3-pole filter on white noise, x8).
struct Pink
{
    float x1 = 0, x2 = 0, x3 = 0, y1 = 0, y2 = 0, y3 = 0;
    float process (float w) noexcept
    {
        const float y = 0.049922035f * w - 0.095993537f * x1 + 0.050612699f * x2 - 0.004408786f * x3
                        + 2.494956002f * y1 - 2.017265875f * y2 + 0.522189400f * y3;
        x3 = x2; x2 = x1; x1 = w; y3 = y2; y2 = y1; y1 = y;
        return y * 8.0f;
    }
};

struct Jet
{
    Rng rng;
    Pink p1, p2;
    Biquad roarLp;
    BandPass4 roarBp;
    float dur = 3.5f, sr = 48000.0f, phase = 0.0f, phase2 = 0.0f;
    int64_t n = 0;

    void start (float duration, uint32_t seed, float sampleRate) noexcept
    {
        dur = duration; sr = sampleRate; n = 0; phase = phase2 = 0.0f;
        rng = Rng (ls::hash32 (seed * 29u + 6u) | 1u);
        p1 = Pink(); p2 = Pink();
        roarLp.setLowpass (900.0f, 0.7071f, sr); roarLp.reset();
        roarBp.set (2500.0f, 0.8f, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        const float u = t / dur;
        const float dop = 1.08f - 0.15f / (1.0f + std::exp (-(u - 0.55f) * 12.0f));
        // white noise at the lab's 48 kHz spread; the pink filter is a 48 kHz design
        const float roar = roarLp.process (p1.process (gauss (rng))) * 0.4f + roarBp.process (p2.process (gauss (rng))) * 0.15f;
        phase += 4300.0f * dop / sr; phase -= std::floor (phase);
        phase2 += 1.53f * 4300.0f * dop / sr; phase2 -= std::floor (phase2);   // its own phase: 1.53 x a wrapped phase would click
        const float whine = std::sin (kTwoPi * phase) * 0.05f + std::sin (kTwoPi * phase2) * 0.03f;
        const float d = (u - 0.5f) / 0.22f;
        return (roar + whine) * std::exp (-d * d) * 0.8f * endFade (t, dur);
    }
};

struct DoorHiss
{
    Rng rng;
    BandPass4 bp;
    Adsr env;
    float dur = 1.6f, sr = 48000.0f;
    int64_t n = 0;

    void start (float duration, float gate, uint32_t seed, float sampleRate) noexcept
    {
        dur = duration; sr = sampleRate; n = 0;
        rng = Rng (ls::hash32 (seed * 31u + 7u) | 1u);
        bp.set (3500.0f, 0.8f, sr);
        env.start (0.005f, 0.1f, 0.5f, 0.2f, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n++ / sr;
        if (t >= dur) return 0.0f;
        return bp.process (gauss (rng)) * env.next() * 0.15f;
    }
};
} // namespace nb
