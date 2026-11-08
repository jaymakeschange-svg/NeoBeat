// NeoBeat — what a pad plays before its effects: an engine patch (Rounds 4/5 and
// the IronHead-style knock/ding), a live Round 6 city recipe, or the user's own
// sample. All three render stereo frames on demand, so the hollow chain can pull
// them at its own (stretched) pace.
#pragma once

#include "../dsp/PatchText.h"
#include "../dsp/City.h"
#include "../dsp/WaveVoice.h"
#include "../dsp/CycleVoice.h"
#include "../dsp/Modal.h"
#include "../dsp/Hollow.h"
#include "../dsp/MaterialBody.h"
#include "../dsp/Friction2.h"
#include "MaterialData.h"
#include <memory>
#include <vector>

namespace nb
{
// Scale every time constant of an engine patch (the pad's Decay knob).
inline void scalePatchTime (ls::Patch& p, float ts) noexcept
{
    if (std::fabs (ts - 1.0f) < 1.0e-4f) return;
    for (auto& g : p.groups) { g.t60 *= ts; g.bloom *= std::sqrt (ts); }
    p.metal.t60 *= ts;
    p.subT60 *= ts; p.subGrowlT60 *= ts; p.fizzT60 *= ts; p.haloT60 *= ts;
    p.noiseExciteT60 *= std::sqrt (ts);
    if (p.gate > 0.0f) p.gate *= ts;
    p.release *= ts;
    p.maxLength = std::max (0.05f, p.maxLength * ts);
}

// The fundamental the hollow chain tunes to, folded into a musical range.
inline float patchPitch (const ls::Patch& p) noexcept
{
    const auto& g = p.groups[0];
    float f = g.baseHz;
    if (g.kind == ls::GroupKind::Explicit && g.numRatios > 0) f *= g.ratios[0];
    while (f > 800.0f) f *= 0.5f;
    while (f < 40.0f) f *= 2.0f;
    return f;
}

struct SampleData
{
    std::vector<float> left, right;
    float sampleRate = 48000.0f;
    std::string path;
};

enum class Recipe { HornBonk, BellClank, DrillHat, ScreechSnare, BarkKick, TrainDoor, BrakeStab, JetRiser };
constexpr int kNumRecipes = 8;

// Shared, built once: the Wave-style tables and the parsed engine patches the recipes use.
struct SharedTables
{
    WaveTable hollow, city;
    ls::Patch innerTubeBonk, junkyardClank, metalHat, rubberSnare, drumheadKick, thudRubber, ding;
    // MaterialLab: the learned bodies and rubbers, and the shared band noise of the living body
    std::vector<Engine2Material> bodies;
    std::vector<FrictionMaterial> rubbers;
    std::vector<StretchPrep> rubberPrep;   // per rubber, at the engine's rate (Engine::prepare)
    std::shared_ptr<MaterialNoise> materialNoise = std::make_shared<MaterialNoise>();
};

class HitSource final : public StereoSource
{
public:
    enum class Kind { None, Patch, Recipe, Sample, Body, Stretch };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        voice.prepare (sr);
        layerVoice.prepare (sr);
        wave.prepare (sr);
        cycle.prepare (sr);
        modal.prepare (sr);
        stretch.prepare (sr);
        kind = Kind::None;
    }

    // MaterialLab: a learned body struck (engine v3), or a rubber pulled off it (friction v2 + stretch)
    void startBody (const Engine2Material& m, const BodyPlay& play, std::shared_ptr<const MaterialNoise> noise)
    {
        if (bodyNoise != noise) { body.prepare (sr, noise); bodyNoise = noise; }
        kind = Kind::Body;
        body.start (m, play);
    }
    void startStretch (const FrictionMaterial& m, const StretchPlay& play, const StretchPrep* prep = nullptr)
    {
        kind = Kind::Stretch;
        stretch.start (m, play, prep);
    }

    // --- start one hit ------------------------------------------------------
    void startPatch (const ls::Patch& patch, float velocity, float semis, float timeScale, float toneShift, uint32_t seed) noexcept
    {
        kind = Kind::Patch;
        scratch = patch;
        scalePatchTime (scratch, timeScale);
        scratch.hardness = clampf (scratch.hardness + 0.3f * toneShift, 0.0f, 1.0f);
        voice.start (scratch, velocity, semis, seed);
    }

    void startSample (std::shared_ptr<const SampleData> s, float velocity, float semis, float timeScale) noexcept
    {
        kind = s ? Kind::Sample : Kind::None;
        sample = std::move (s);
        if (! sample) return;
        pos = 0.0;
        step = (double) std::exp2 (semis / 12.0f) * sample->sampleRate / sr;
        gain = std::pow (clampf (velocity, 0.0f, 1.0f), 1.2f);
        len = (double) sample->left.size();
        // Decay < 1 fades the sample out early; > 1 leaves it as recorded.
        fadeStart = timeScale < 1.0f ? len * timeScale * 0.5 : len;
        fadeLen = timeScale < 1.0f ? len * timeScale * 0.5 : 1.0;
    }

    void startRecipe (Recipe r, const SharedTables& tables, float velocity, float semis, float ts, float toneShift, uint32_t seed) noexcept
    {
        kind = Kind::Recipe;
        recipe = r;
        n = 0;
        recipeLen = recipeLength (r) * ts;
        const float k = std::exp2 (semis / 12.0f);
        const float v = clampf (velocity, 0.02f, 1.0f);
        gain = std::pow (v / 0.8f, 1.3f);
        auto lsVel = [&] (float base) { return clampf (base + 0.6f * (v - 0.8f), 0.05f, 1.0f); };
        auto startLayer = [&] (const ls::Patch& p, float base, float noteSemis, float delaySeconds, float mix)
        {
            scratch = p;
            scalePatchTime (scratch, ts);
            scratch.hardness = clampf (scratch.hardness + 0.3f * toneShift, 0.0f, 1.0f);
            layerVoice.start (scratch, lsVel (base), noteSemis + semis, seed);
            layerDelay = (int64_t) (delaySeconds * sr);
            layerMix = mix;
            layerOn = true;
        };
        layerOn = hornOn = bellOn = drillOn = screechOn = barkOn = brakeOn = jetOn = hissOn = waveOn = cycleOn = modalOn = false;
        switch (r)
        {
            case Recipe::HornBonk:
            {
                horn.start (415.0f * k, 0.35f * ts, 0.22f * ts, seed, sr); hornOn = true; hornMix = 0.5f; panGains (-0.1f, hornL, hornR);
                startLayer (tables.innerTubeBonk, 0.85f, 7.0f, 0.0f, 1.2f);
                WaveVoiceSpec w;
                w.pos = 30.0f; w.scan = -0.45f;
                w.wenv.times = { 0.002f, 0.3f * ts, 0, 0, 0, 0, 0, 0 }; w.wenv.levels = { 1, 0, 0, 0, 0, 0, 0, 0 };
                w.wenv.keyoff = 1; w.wenv.loopStart = 0; w.wenv.loop = false;
                w.cutoff = 1200.0f * toneRatio (toneShift); w.res = 0.3f;
                w.aa = 0.002f; w.ad = 0.25f * ts; w.as = 0.0f; w.ar = 0.2f * ts;
                wave.start (&tables.hollow, w, 207.6f * k, 207.6f * k, 0.0f, 1.6f * ts, 0.2f * ts, seed);
                waveOn = true; waveMix = 0.6f; panGains (0.1f, waveL, waveR);
                break;
            }
            case Recipe::BellClank:
            {
                bell.start (311.1f * k, 3.0f * ts, 3.0f * ts, 1.0f, seed + 4u, sr); bellOn = true; bellMix = 0.8f; panGains (0.2f, bellL, bellR);
                startLayer (tables.junkyardClank, 0.85f, 0.0f, 0.0f, 0.9f);
                BodySpec b; b.kind = BodyKind::Bar; b.t60 = 1.2f * ts; b.material = -0.3f; b.tone = -2.0f + 2.0f * toneShift; b.hit = 0.22f; b.width = 0.7f; b.seed = 3;
                modal.start (b, 622.2f * k, 3.0f * ts, 3000.0f, 0.0f); modalOn = true; modalMix = 0.35f;
                break;
            }
            case Recipe::DrillHat:
            {
                drill.start (330.0f * k, 0.4f * ts, 38.0f, 0.15f * ts, seed, sr); drillOn = true; drillMix = 1.2f; panGains (-0.2f, drillL, drillR);
                startLayer (tables.metalHat, 0.85f, 0.0f, 0.0f, 1.0f);
                break;
            }
            case Recipe::ScreechSnare:
            {
                screech.start (2100.0f * k, 1500.0f * k, 0.6f * ts, 0.26f * ts, seed, sr); screechOn = true; screechMix = 0.5f; panGains (0.25f, screechL, screechR);
                startLayer (tables.rubberSnare, 0.9f, 0.0f, 0.0f, 1.4f);
                break;
            }
            case Recipe::BarkKick:
            {
                bark.start (420.0f * k, 0.3f * ts, seed, sr); barkOn = true; barkMix = 0.45f; panGains (-0.15f, barkL, barkR);
                startLayer (tables.drumheadKick, 0.95f, 0.0f, 0.0f, 1.3f);
                break;
            }
            case Recipe::TrainDoor:
            {
                hiss.start (1.6f * ts, 0.35f * ts, seed, sr); hissOn = true; panGains (0.3f, hissL, hissR);
                BodySpec b; b.kind = BodyKind::Bar; b.t60 = 0.4f * ts; b.material = -0.2f; b.tone = -1.0f + 2.0f * toneShift; b.hit = 0.17f; b.width = 0.6f; b.seed = 3;
                modal.start (b, 410.0f * k, 1.6f * ts, 4000.0f, 0.36f); modalOn = true; modalMix = 0.5f;
                startLayer (tables.thudRubber, 0.85f, -2.0f, 0.34f, 1.0f);
                break;
            }
            case Recipe::BrakeStab:
            {
                CycleSpec c;
                c.numNodes = 4;
                c.start = { CurveNode { 0.0f, 0.0f, 0.2f }, { 0.2f, 1.0f, 0.8f }, { 0.5f, 0.0f, 0.4f }, { 0.7f, -1.0f, 0.8f } };
                c.end = { CurveNode { 0.0f, 0.0f, 0.1f }, { 0.25f, 0.7f, 0.1f }, { 0.5f, 0.0f, 0.1f }, { 0.75f, -0.7f, 0.1f } };
                c.scratchTime = 0.25f * ts; c.deform = 0.6f; c.deformHz = 5.0f; c.bright = clampf (1.0f + 0.5f * toneShift, 0.3f, 1.5f);
                c.aa = 0.002f; c.ad = 0.2f * ts; c.as = 0.3f; c.ar = 0.3f * ts;
                cycle.start (c, 440.0f * k, 1.4f * ts, 0.18f * ts, seed); cycleOn = true; cycleMix = 0.6f * 0.70710678f;
                brake.start (1900.0f * k, 1700.0f * k, 1.4f * ts, 0.7f * ts, seed, sr); brakeOn = true; brakeMix = 0.25f; panGains (0.3f, brakeL, brakeR);
                startLayer (tables.ding, 0.85f, 0.0f, 0.0f, 0.8f);
                break;
            }
            case Recipe::JetRiser:
            {
                jet.start (3.5f * ts, seed, sr); jetOn = true; jetMix = 0.7f * 0.70710678f;
                WaveVoiceSpec w;
                w.pos = 0.0f; w.scan = 0.95f;
                w.wenv.times = { 3.0f * ts, 0.4f * ts, 0, 0, 0, 0, 0, 0 }; w.wenv.levels = { 1.0f, 0.6f, 0, 0, 0, 0, 0, 0 };
                w.wenv.keyoff = 1; w.wenv.loopStart = 0; w.wenv.loop = false;
                w.cutoff = 600.0f * toneRatio (toneShift); w.res = 0.3f; w.fenvAmount = 3.0f;
                w.fa = 2.8f * ts; w.fd = 0.5f * ts; w.fs = 0.8f; w.fr = 0.4f * ts;
                w.aa = 1.5f * ts; w.ad = 0.5f * ts; w.as = 0.9f; w.ar = 0.4f * ts;
                wave.start (&tables.city, w, 110.0f * k, 220.0f * k, 3.0f * ts, 3.5f * ts, 3.0f * ts, seed);
                waveOn = true; waveMix = 0.6f; waveL = waveR = 0.70710678f;
                break;
            }
        }
    }

    void stop() noexcept { kind = Kind::None; }

    bool isActive() const noexcept override
    {
        switch (kind)
        {
            case Kind::Patch: return voice.isActive();
            case Kind::Sample: return sample != nullptr && pos < std::min (len, fadeStart + fadeLen);
            case Kind::Recipe: return (float) n / sr < recipeLen;
            case Kind::Body: return body.isActive();
            case Kind::Stretch: return stretch.isActive();
            default: return false;
        }
    }

    void renderFrame (float& l, float& r) noexcept override
    {
        l = r = 0.0f;
        switch (kind)
        {
            case Kind::Patch: voice.render (l, r); break;
            case Kind::Sample: renderSample (l, r); break;
            case Kind::Recipe: renderRecipe (l, r); break;
            case Kind::Body: body.process (l, r); break;
            case Kind::Stretch: stretch.process (l, r); break;
            default: break;
        }
    }

    // Nominal length of a recipe (seconds, at Decay 1): the lab's buffer length.
    static float recipeLength (Recipe r) noexcept
    {
        switch (r)
        {
            case Recipe::HornBonk: return 1.6f;
            case Recipe::BellClank: return 3.0f;
            case Recipe::DrillHat: return 0.8f;
            case Recipe::ScreechSnare: return 1.0f;
            case Recipe::BarkKick: return 1.0f;
            case Recipe::TrainDoor: return 1.6f;
            case Recipe::BrakeStab: return 1.4f;
            case Recipe::JetRiser: return 3.5f;
        }
        return 1.0f;
    }

private:
    static float toneRatio (float toneShift) noexcept { return std::exp2 (1.5f * toneShift); }

    void renderSample (float& l, float& r) noexcept
    {
        const int64_t i = (int64_t) pos;
        const int64_t sz = (int64_t) sample->left.size();
        if (i + 1 >= sz) { pos = len; return; }
        const float fr = (float) (pos - (double) i);
        l = sample->left[(size_t) i] + fr * (sample->left[(size_t) i + 1] - sample->left[(size_t) i]);
        r = sample->right[(size_t) i] + fr * (sample->right[(size_t) i + 1] - sample->right[(size_t) i]);
        float g = gain;
        if (pos > fadeStart) g *= (float) std::max (0.0, 1.0 - (pos - fadeStart) / fadeLen);
        l *= g; r *= g;
        pos += step;
    }

    void renderRecipe (float& l, float& r) noexcept
    {
        float L = 0.0f, R = 0.0f;
        auto mono = [&] (bool on, float v, float mix, float gl, float gr) { if (on) { L += v * mix * gl; R += v * mix * gr; } };
        if (hornOn) mono (true, horn.process(), hornMix, hornL, hornR);
        if (bellOn) mono (true, bell.process(), bellMix, bellL, bellR);
        if (drillOn) mono (true, drill.process(), drillMix, drillL, drillR);
        if (screechOn) mono (true, screech.process(), screechMix, screechL, screechR);
        if (barkOn) mono (true, bark.process(), barkMix, barkL, barkR);
        if (brakeOn) mono (true, brake.process(), brakeMix, brakeL, brakeR);
        if (jetOn) { const float v = jet.process() * jetMix; L += v; R += v; }
        if (hissOn) mono (true, hiss.process(), 1.0f, hissL, hissR);
        if (waveOn) mono (true, wave.process(), waveMix, waveL, waveR);
        if (cycleOn) { const float v = cycle.process() * cycleMix; L += v; R += v; }
        if (modalOn) { float ml, mr; modal.process (ml, mr); L += ml * modalMix; R += mr * modalMix; }
        if (layerOn && n >= layerDelay)
        {
            float vl, vr;
            layerVoice.render (vl, vr);
            L += vl * layerMix; R += vr * layerMix;
        }
        // the lab trimmed every layer to the recipe's length, with a short fade
        const float t = (float) n / sr;
        const float fade = t > recipeLen - 0.05f ? clampf ((recipeLen - t) / 0.05f, 0.0f, 1.0f) : 1.0f;
        ++n;
        l = L * gain * fade; r = R * gain * fade;
    }

    Kind kind = Kind::None;
    float sr = 48000.0f;
    ls::Patch scratch;
    ls::Voice voice, layerVoice;
    // sample
    std::shared_ptr<const SampleData> sample;
    double pos = 0.0, step = 1.0, len = 0.0, fadeStart = 0.0, fadeLen = 1.0;
    float gain = 1.0f;
    // recipe layers
    Recipe recipe = Recipe::HornBonk;
    int64_t n = 0, layerDelay = 0;
    float layerMix = 1.0f, recipeLen = 1.0f;
    bool layerOn = false, hornOn = false, bellOn = false, drillOn = false, screechOn = false, barkOn = false, brakeOn = false;
    bool jetOn = false, hissOn = false, waveOn = false, cycleOn = false, modalOn = false;
    Horn horn; Bell bell; Drill drill; TireScreech screech; DogBark bark; BrakeSqueal brake; Jet jet; DoorHiss hiss;
    WaveVoice wave; CycleVoice cycle; ModalBody modal;
    MaterialBody body; StretchVoice stretch;
    std::shared_ptr<const MaterialNoise> bodyNoise;
    float hornMix = 0, hornL = 0, hornR = 0, bellMix = 0, bellL = 0, bellR = 0, drillMix = 0, drillL = 0, drillR = 0;
    float screechMix = 0, screechL = 0, screechR = 0, barkMix = 0, barkL = 0, barkR = 0, brakeMix = 0, brakeL = 0, brakeR = 0;
    float jetMix = 0, hissL = 0, hissR = 0, waveMix = 0, waveL = 0, waveR = 0, cycleMix = 0, modalMix = 0;
};
} // namespace nb
