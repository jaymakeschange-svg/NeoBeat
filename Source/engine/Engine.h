// NeoBeat — the kit engine: 16 pads, a pool of voices, chokes and the per-pad
// macro controls. No framework code: the plugin and the command-line renderer
// drive exactly the same engine.
//
// Pad macros (each makes a clearly audible change):
//   Tune    -24..+24 semitones (pitch of every layer, and the hollow tuning)
//   Decay   x0.25..x4 (every time constant of the sound)
//   Tone    dark..bright (mallet hardness / filter, plus a +-9 dB tilt)
//   Drive   soft saturation after the voice
//   Hollow  dry -> TurboSynth-style hollow chain (equal-power crossfade)
//   Evolve  how far the chain moves: stretch, wobble, inversion, modulation
//   Pan, Level, Choke group
#pragma once

#include "Library.h"
#include <atomic>

namespace nb
{
struct PadSettings
{
    int sound = 0;
    float tune = 0.0f, decay = 1.0f, tone = 0.0f, drive = 0.0f, hollow = 0.0f, evolve = 0.5f, pan = 0.0f, levelDb = 0.0f;
    int choke = 0;
    bool useSample = false;
    std::shared_ptr<const SampleData> sample;
    float sampleRms = 0.05f;
};

class PadVoice
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        dry.prepare (sr);
        feed.prepare (sr);
        chain.prepare (sr);
        active = false;
    }

    bool isActive() const noexcept { return active; }

    void start (const Library& lib, int padIndex, const PadSettings& ps, float velocity, uint32_t seed, uint64_t startOrder) noexcept
    {
        pad = padIndex; order = startOrder; chokeGroup = ps.choke;
        const float v = clampf (velocity, 0.0f, 1.0f);
        const float h = clampf (ps.hollow, 0.0f, 1.0f);
        dryGain = std::cos (h * 0.5f * kPi);
        wetGain = std::sin (h * 0.5f * kPi);
        const float ts = clampf (ps.decay, 0.1f, 8.0f);
        const float tuneRatio = std::exp2 (ps.tune / 12.0f);
        dry.stop(); feed.stop();
        trim = 1.0f; hollowTrim = 1.0f;

        if (ps.useSample && ps.sample)
        {
            if (dryGain > 1.0e-3f) dry.startSample (ps.sample, v, ps.tune, ts);
            if (wetGain > 1.0e-3f)
            {
                feed.startSample (ps.sample, v, ps.tune, ts);
                HollowSpec hs;
                hs.f0 = 130.8f;
                hs.length = clampf ((float) ps.sample->left.size() / ps.sample->sampleRate / tuneRatio, 0.2f, 4.0f);
                hs.stretch = 0.35f; hs.resFb = 0.82f; hs.resMix = 0.6f;
                hs.inv = Breakpoints::make ({ { 0.0f, 0.0f }, { 0.4f, 0.3f }, { 1.0f, 0.15f } });
                hs.cut = Breakpoints::make ({ { 0.0f, 10000.0f }, { 1.0f, 3000.0f } });
                chain.start (hs, &feed, tuneRatio, ts, ps.evolve, ps.sampleRms * std::pow (v, 1.2f));
            }
        }
        else
        {
            if (ps.sound < 0 || ps.sound >= (int) lib.sounds.size()) { active = false; return; }
            const auto& sd = lib.sounds[(size_t) ps.sound];
            trim = ls::dbToGain (sd.trimDb);
            hollowTrim = ls::dbToGain (sd.hollowTrimDb);
            auto begin = [&] (HitSource& s)
            {
                if (sd.kind == HitSource::Kind::Recipe) s.startRecipe (sd.recipe, lib.tables, v, ps.tune, ts, ps.tone, seed);
                else s.startPatch (lib.patches[(size_t) sd.patchIndex], v, ps.tune, ts, ps.tone, seed);
            };
            if (dryGain > 1.0e-3f) begin (dry);
            if (wetGain > 1.0e-3f)
            {
                begin (feed);
                const float velLevel = sd.kind == HitSource::Kind::Recipe ? std::pow (std::max (v, 0.02f) / 0.8f, 1.3f)
                                                                            : std::pow (std::max (v, 0.02f) / 0.8f, 1.4f);
                chain.start (sd.hollow, &feed, tuneRatio, ts, ps.evolve, sd.levelRms * velLevel);
            }
        }
        if (dryGain <= 1.0e-3f) dry.stop();
        // Tone tilt: +-9 dB around 900 Hz.
        tiltCoef = 1.0f - std::exp (-kTwoPi * 900.0f / sr);
        tiltLow = ls::dbToGain (-9.0f * ps.tone);
        tiltHigh = ls::dbToGain (9.0f * ps.tone);
        lowL = lowR = 0.0f;
        driveAmount = 1.0f + 15.0f * ps.drive * ps.drive;
        driveOn = ps.drive > 1.0e-3f;
        driveMakeup = 1.0f / std::pow (driveAmount, 0.6f);
        const float pan = clampf (ps.pan, -1.0f, 1.0f);
        panL = pan > 0.0f ? 1.0f - pan : 1.0f;
        panR = pan < 0.0f ? 1.0f + pan : 1.0f;
        level = ls::dbToGain (ps.levelDb);
        chokeGain = 1.0f; choking = false;
        chokeStep = 1.0f / std::max (1.0f, 0.005f * sr);
        active = true;
    }

    void choke() noexcept { choking = true; }

    void render (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        if (! active) return;
        float l = 0.0f, r = 0.0f;
        bool any = false;
        if (dry.isActive())
        {
            float dl, dr;
            dry.renderFrame (dl, dr);
            l += dl * dryGain; r += dr * dryGain;
            any = true;
        }
        if (chain.isActive())
        {
            float wl, wr;
            chain.render (wl, wr);
            l += wl * wetGain * hollowTrim; r += wr * wetGain * hollowTrim;
            any = true;
        }
        l *= trim; r *= trim;
        lowL += tiltCoef * (l - lowL);
        lowR += tiltCoef * (r - lowR);
        l = lowL * tiltLow + (l - lowL) * tiltHigh;
        r = lowR * tiltLow + (r - lowR) * tiltHigh;
        if (driveOn)
        {
            constexpr float ref = 0.3f;
            l = ref * std::tanh (driveAmount * l / ref) * driveMakeup;
            r = ref * std::tanh (driveAmount * r / ref) * driveMakeup;
        }
        if (choking)
        {
            chokeGain -= chokeStep;
            if (chokeGain <= 0.0f) { chokeGain = 0.0f; any = false; }
        }
        const float g = level * chokeGain;
        left = l * panL * g;
        right = r * panR * g;
        if (! any) active = false;
    }

    int pad = -1, chokeGroup = 0;
    uint64_t order = 0;

private:
    HitSource dry, feed;
    HollowChain chain;
    float sr = 48000.0f, dryGain = 1.0f, wetGain = 0.0f, trim = 1.0f, hollowTrim = 1.0f;
    float tiltCoef = 0.1f, tiltLow = 1.0f, tiltHigh = 1.0f, lowL = 0.0f, lowR = 0.0f;
    float driveAmount = 1.0f, driveMakeup = 1.0f, panL = 1.0f, panR = 1.0f, level = 1.0f;
    float chokeGain = 1.0f, chokeStep = 0.01f;
    bool active = false, choking = false, driveOn = false;
};

class Engine
{
public:
    static constexpr int kPads = 16;
    static constexpr int kVoices = 32;
    static constexpr int kVoicesPerPad = 4;

    Engine() { library.build(); }

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sr);
    }

    const Library& lib() const noexcept { return library; }

    // extraSemis: the key-transpose mode shifts every pad together.
    void trigger (int pad, float velocity, float extraSemis = 0.0f) noexcept
    {
        if (pad < 0 || pad >= kPads || velocity <= 0.0f) return;
        PadSettings ps = pads[(size_t) pad];
        ps.tune += extraSemis;
        // choke: a hit in a group fades every voice in that group (open/closed hats)
        if (ps.choke > 0)
            for (auto& v : voices)
                if (v.isActive() && v.chokeGroup == ps.choke) v.choke();
        // per-pad polyphony
        int count = 0; PadVoice* oldestOfPad = nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.pad == pad)
            {
                ++count;
                if (oldestOfPad == nullptr || v.order < oldestOfPad->order) oldestOfPad = &v;
            }
        if (count >= kVoicesPerPad && oldestOfPad != nullptr) oldestOfPad->choke();
        PadVoice* target = nullptr;
        for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
        if (target == nullptr)
        {
            for (auto& v : voices) if (target == nullptr || v.order < target->order) target = &v;
        }
        target->start (library, pad, ps, velocity, seedCounter = ls::hash32 (seedCounter + 0x9e3779b9u), ++orderCounter);
        hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
    }

    // Adds into left/right. tap (optional) receives the mono sum of `tapPad`.
    void render (float* left, float* right, int numSamples, float* tap = nullptr, int tapPad = -1) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float L = 0.0f, R = 0.0f, T = 0.0f;
            for (auto& v : voices)
            {
                if (! v.isActive()) continue;
                float l, r;
                v.render (l, r);
                L += l; R += r;
                if (v.pad == tapPad) T += 0.5f * (l + r);
            }
            left[i] += safety (L * master);
            right[i] += safety (R * master);
            if (tap != nullptr) tap[i] = T;
        }
    }

    void allNotesOff() noexcept { for (auto& v : voices) if (v.isActive()) v.choke(); }

    int activeVoices() const noexcept { int c = 0; for (auto& v : voices) c += v.isActive() ? 1 : 0; return c; }

    // Soft safety knee above -1 dBFS: normal levels pass untouched.
    static float safety (float x) noexcept
    {
        constexpr float knee = 0.891f, room = 1.0f - knee;
        const float a = std::fabs (x);
        if (a <= knee) return x;
        return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
    }

    std::array<PadSettings, kPads> pads {};
    std::array<std::atomic<uint32_t>, kPads> hitCounter {};
    float master = 0.35f;  // -9 dB: kick, snare and hats landing together stay clear of the knee

private:
    Library library;
    std::array<PadVoice, kVoices> voices;
    float sr = 48000.0f;
    uint32_t seedCounter = 12345u;
    uint64_t orderCounter = 0;
};
} // namespace nb
