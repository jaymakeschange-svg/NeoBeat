// NeoBeat 2.0 — the player pool: plays rendered audio (One Shot cells, pad inserts, window slots,
// zone effects) and samples (grid rows, key-map sample zones), with pitch, pan, chokes, the voice's
// continuous modulation and the pad's real-time algorithm.
//
// Voices hold shared pointers to what they play, so a buffer the render worker replaces stays alive
// until the voice ends. (The worker also keeps replaced buffers for two minutes, so the audio thread
// is never the one that frees them in practice.)
#pragma once

#include "ModSystem.h"
#include "Rt.h"
#include "Sources.h"

namespace nb
{
struct PlayRequest
{
    RenderedPtr rendered;
    std::shared_ptr<const SampleData> sample;
    float gain = 1.0f, pan = 0.0f, semis = 0.0f;
    int pad = -1;           // for the tap, chokes and per-pad polyphony (-1: none)
    int choke = 0;          // choke group (0 none)
    int bus = 0;            // 0 kit, 1 grid, 2 slots (level groups)
    const PadAlgo* algo = nullptr; float algoOffset = 0.0f; float pitchHz = 200.0f;
    float fadeInMs = 0.0f;
    double startPos = 0.0;   // start this many source samples in (a render joined late)
    VoiceMod mod;            // continuous modulation lanes (state/frame pointers set by the caller)
};

class PlayerVoice
{
public:
    bool active = false;
    int pad = -1, choke = 0, bus = 0;
    uint64_t order = 0;
    VoiceMod mod;

    void start (const PlayRequest& r, float engineSr, uint64_t ord) noexcept
    {
        rendered = r.rendered; sample = r.sample;
        if (rendered) { L = rendered->l.data(); R = rendered->r.data(); len = rendered->size(); srcRate = rendered->sampleRate; }
        else if (sample) { L = sample->left.data(); R = sample->right.empty() ? sample->left.data() : sample->right.data(); len = (int) sample->left.size(); srcRate = sample->sampleRate; }
        else { active = false; return; }
        if (len <= 1) { active = false; return; }
        sr = engineSr; pad = r.pad; choke = r.choke; bus = r.bus; order = ord;
        baseInc = (double) srcRate / sr * std::exp2 (r.semis / 12.0);
        pos = std::max (0.0, std::min (r.startPos, (double) (len - 2))); gain = r.gain; basePan = r.pan;
        chokeGain = 1.0f; choking = false; chokeStep = 1.0f / std::max (1.0f, 0.006f * sr);
        fade = r.fadeInMs > 0.0f ? 0.0f : 1.0f; fadeStep = r.fadeInMs > 0.0f ? 1.0f / (r.fadeInMs * 0.001f * sr) : 1.0f;
        algoOn = false;
        if (r.algo != nullptr && r.algo->type > 0) { algo.start (*r.algo, sr, r.pitchHz, r.algoOffset); algoOn = algo.on(); }
        tick = 0;
        mod.t = 0.0;
        mod.update (0, sr);
        applyMod();
        active = true;
    }

    void stopNow() noexcept { active = false; rendered.reset(); sample.reset(); }
    void startChoke() noexcept { choking = true; }

    // adds into the outputs
    void render (float* outL, float* outR, int n, float* tap, int tapPad) noexcept
    {
        if (! active) return;
        for (int i = 0; i < n; ++i)
        {
            if (tick-- <= 0) { tick = 31; if (mod.active()) { mod.update (32, sr); applyMod(); } }
            const int i0 = (int) pos;
            if (i0 >= len - 1) { finish(); return; }
            const float f = (float) (pos - i0);
            float l = L[i0] + f * (L[i0 + 1] - L[i0]);
            float r = R[i0] + f * (R[i0 + 1] - R[i0]);
            pos += inc;
            if (algoOn) algo.process (l, r);
            if (fade < 1.0f) { fade = std::min (1.0f, fade + fadeStep); l *= fade; r *= fade; }
            if (choking)
            {
                chokeGain -= chokeStep;
                if (chokeGain <= 0.0f) { finish(); return; }
                l *= chokeGain; r *= chokeGain;
            }
            const float ol = l * gL, orr = r * gR;
            outL[i] += ol; outR[i] += orr;
            if (tap != nullptr && pad == tapPad) tap[i] += 0.5f * (ol + orr);
        }
    }

private:
    void finish() noexcept { active = false; rendered.reset(); sample.reset(); }
    void applyMod() noexcept
    {
        const float pan = std::max (-1.0f, std::min (1.0f, basePan + (mod.active() ? mod.pan : 0.0f)));
        const float g = gain * (mod.active() ? mod.levelMul : 1.0f);
        gL = g * (pan > 0.0f ? 1.0f - pan : 1.0f);
        gR = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
        inc = baseInc * (mod.active() ? std::exp2 (mod.pitch / 12.0) : 1.0);
    }
    RenderedPtr rendered;
    std::shared_ptr<const SampleData> sample;
    const float* L = nullptr; const float* R = nullptr;
    int len = 0, tick = 0;
    float sr = 48000.0f, srcRate = 48000.0f, gain = 1.0f, basePan = 0.0f, gL = 1.0f, gR = 1.0f;
    float chokeGain = 1.0f, chokeStep = 0.01f, fade = 1.0f, fadeStep = 1.0f;
    bool choking = false, algoOn = false;
    double pos = 0.0, inc = 1.0, baseInc = 1.0;
    AlgoState algo;
};

class PlayerPool
{
public:
    static constexpr int kVoices = 64;

    void prepare (float sampleRate) noexcept { sr = sampleRate; for (auto& v : voices) v.stopNow(); }

    PlayerVoice* play (const PlayRequest& r) noexcept
    {
        if (! r.rendered && ! r.sample) return nullptr;
        if (r.choke > 0) chokeGroup (r.choke);
        PlayerVoice* target = nullptr;
        for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr)
            for (auto& v : voices) if (target == nullptr || v.order < target->order) target = &v;
        target->mod = r.mod;
        target->start (r, sr, ++counter);
        return target->active ? target : nullptr;
    }

    void chokeGroup (int g) noexcept { if (g > 0) for (auto& v : voices) if (v.active && v.choke == g) v.startChoke(); }
    void chokePad (int pad) noexcept { for (auto& v : voices) if (v.active && v.pad == pad) v.startChoke(); }
    void allOff() noexcept { for (auto& v : voices) if (v.active) v.startChoke(); }

    void render (float* L, float* R, int n, float* tap, int tapPad) noexcept
    {
        for (auto& v : voices) if (v.active) v.render (L, R, n, tap, tapPad);
    }
    int activeCount() const noexcept { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    std::array<PlayerVoice, kVoices> voices;
    float sr = 48000.0f;
    uint64_t counter = 0;
};
} // namespace nb
