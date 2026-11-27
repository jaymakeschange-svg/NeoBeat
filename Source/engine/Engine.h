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
//   Body    resonant body network + Wave-style tone under the hit (depth)
//   Fat     Wave-style drive into a sharp-knee clipper (fatness)
//   Space   a short room of early reflections
#pragma once

#include "Library.h"
#include "../dsp/Character.h"
#include "../dsp/MaterialSpace.h"
#include "ModSystem.h"
#include "Rt.h"
#include "../shared/Ceiling.h"
#include <atomic>

namespace nb
{
struct PadSettings
{
    int sound = 0;
    float tune = 0.0f, decay = 1.0f, tone = 0.0f, drive = 0.0f, hollow = 0.0f, evolve = 0.5f, pan = 0.0f, levelDb = 0.0f;
    int choke = 0;
    float body = -1.0f, fat = -1.0f, space = -1.0f;   // < 0: the sound's own default
    bool character = true;                             // false: build-1 voicing (for A/B)
    bool useSample = false;
    std::shared_ptr<const SampleData> sample;
    float sampleRms = 0.05f;
    // MaterialLab space and gestures (the Matrixxxer effects and modulation-matrix destinations)
    int place = -2;                // -2: the sound's own; -1: NeoBeat's short room (Space macro); 0..7: a MaterialLab place
    int variation = -1;            // learned bodies: -1 the sound's own, else a learner variation (0 = as learned)
    int gesture = 0;               // 0 none, 1..5 the MaterialLab strokes (gesturePreset)
    float gestureSeconds = 3.0f;   // how long one stroke takes
    int bounces = 0;               // > 1: one hit becomes a bouncing object (bounce physics)
    float bounceFlight = 0.35f;    // first flight (s)
    float restitution = 0.79f;     // each flight and impact speed times this (measured: 0.79; > 1 gains energy)
    float width = 1.0f;            // stereo: 0 mono, 1 as the sound is, 2 wide (the widening cancels in mono)
    float bands = 0.0f;            // BANDS send, 0..1: how much of this lane goes through the spectral delay
};

// A sound's stereo width. Narrowing scales the side; widening adds a decorrelated copy of the middle
// (above the bass) to the side only, so the mono sum is untouched.
struct Widener
{
    void start (float width, float sr) noexcept
    {
        w = std::max (0.0f, std::min (2.0f, width));
        on = std::fabs (w - 1.0f) > 1.0e-3f;
        if (w > 1.0f)
        {
            a.fill (0.0f); b.fill (0.0f); pos = 0; lp = 0.0f;
            lenA = std::max (8, std::min (1023, (int) (0.0031f * sr))); lenB = std::max (8, std::min (1023, (int) (0.0053f * sr)));
            lpCoef = 1.0f - std::exp (-6.2831853f * 250.0f / sr);
        }
    }
    void process (float& l, float& r) noexcept
    {
        if (! on) return;
        const float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        if (w <= 1.0f) side *= w;
        else
        {
            lp += lpCoef * (mid - lp);
            const float x = mid - lp;
            const float ya = a[(size_t) ((pos - lenA) & 1023)];
            const float oa = -0.6f * x + ya; a[(size_t) pos] = x + 0.6f * oa;
            const float yb = b[(size_t) ((pos - lenB) & 1023)];
            const float ob = -0.6f * oa + yb; b[(size_t) pos] = oa + 0.6f * ob;
            pos = (pos + 1) & 1023;
            side = side * w + (w - 1.0f) * 0.7f * ob;
        }
        l = mid + side; r = mid - side;
    }
    bool on = false;
    float w = 1.0f, lp = 0.0f, lpCoef = 0.03f;
    int pos = 0, lenA = 149, lenB = 254;
    std::array<float, 1024> a {}, b {};
};

// Where a voice is heard, and the stroke that moves it (set by the engine at the trigger).
struct VoicePlacement
{
    int place = -1;                // MaterialLab place, or -1 for none
    const Gesture* gesture = nullptr;
    float u0 = 0.0f, du = 0.0f;    // stroke position at the start, and per sample
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
        body.prepare (sr);
        layer.prepare (sr);
        room.prepare (sr);
        placer.prepare (sr);
        active = false;
    }

    bool isActive() const noexcept { return active; }

    VoiceMod mod;                 // continuous modulation (set before start)
    const PadAlgo* algoSpec = nullptr; float algoOffset = 0.0f;

    void start (const Library& lib, int padIndex, const PadSettings& ps, float velocity, uint32_t seed, uint64_t startOrder,
                const VoicePlacement& vp = {}) noexcept
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
        CharacterSpec cs;
        cs.body = 0.35f; cs.fat = 0.35f; cs.space = 0.2f; cs.waveLayer = false; cs.bodyT60 = 0.25f;
        float pitch = 130.8f, peakRef = ps.sampleRms * 4.0f;
        levelComp = 1.0f;
        if (! (ps.useSample && ps.sample))
        {
            if (ps.sound < 0 || ps.sound >= (int) lib.sounds.size()) { active = false; return; }
            const auto& sd = lib.sounds[(size_t) ps.sound];
            trim = ls::dbToGain (sd.trimDb);
            hollowTrim = ls::dbToGain (sd.hollowTrimDb);
            cs = sd.character;
            pitch = sd.pitchHz;
            peakRef = sd.peakRef;
            // Timbre velocity: every step sounds like the strong strikes the
            // sounds were designed around; the level keeps the step's dynamics.
            const float curve = sd.kind == HitSource::Kind::Recipe ? 1.3f
                              : (sd.patchIndex >= 0 ? lib.patches[(size_t) sd.patchIndex].velCurve : 1.0f);
            const bool material = sd.kind == HitSource::Kind::Body || sd.kind == HitSource::Kind::Stretch;
            const float vt = ps.character && ! material ? 0.55f + 0.45f * v : v;
            levelComp = material ? 1.0f : std::pow (std::max (v, 0.02f) / std::max (vt, 0.02f), curve);
            // MaterialLab: the place decides how wide the two listening points are
            placeIndex = vp.place;
            const PlaceSpec spc = placeIndex >= 0 ? placePreset (placeIndex) : placePreset (0);
            auto begin = [&] (HitSource& s)
            {
                if (sd.kind == HitSource::Kind::Recipe) s.startRecipe (sd.recipe, lib.tables, vt, ps.tune, ts, ps.tone, seed);
                else if (sd.kind == HitSource::Kind::Body)
                {
                    BodyPlay bp = sd.bodyPlay;
                    if (ps.variation >= 0) bp.variation = (Variation) std::min (ps.variation, (int) Variation::Count - 1);
                    bp.speed = std::max (v, 0.02f) / 0.8f;      // the strike law: faster impacts are louder and brighter
                    bp.pitch = tuneRatio;
                    bp.decay = ts;
                    bp.width = placeIndex >= 0 ? spc.width : 1.0f; bp.fd = spc.f_d; bp.q = spc.q;
                    bp.seed = seed;
                    s.startBody (lib.tables.bodies[(size_t) sd.material], bp, lib.tables.materialNoise);
                }
                else if (sd.kind == HitSource::Kind::Stretch)
                {
                    StretchPlay sp = sd.stretchPlay;
                    sp.pitch = tuneRatio;
                    sp.speed = sd.stretchPlay.speed / ts;        // Decay: a slower (longer) or quicker pull
                    sp.level = std::max (v, 0.02f) / 0.8f;
                    sp.width = placeIndex >= 0 ? spc.width : 1.0f; sp.fd = spc.f_d; sp.q = spc.q;
                    sp.seed = seed;
                    const auto& prepTable = lib.tables.rubberPrep;
                    s.startStretch (lib.tables.rubbers[(size_t) sd.material], sp,
                                    (size_t) sd.material < prepTable.size() ? &prepTable[(size_t) sd.material] : nullptr);
                }
                else s.startPatch (lib.patches[(size_t) sd.patchIndex], vt, ps.tune, ts, ps.tone, seed);
            };
            if (dryGain > 1.0e-3f) begin (dry);
            if (wetGain > 1.0e-3f)
            {
                begin (feed);
                const float velLevel = std::pow (std::max (vt, 0.02f) / 0.8f, curve);
                chain.start (sd.hollow, &feed, tuneRatio, ts, ps.evolve, sd.levelRms * velLevel);
            }
        }
        // ---- character: body network, Wave-style tone, Wave drive, room
        const float toneRatio = std::exp2 (1.2f * ps.tone);
        bodyAmt = ps.character ? clampf (ps.body >= 0.0f ? ps.body : cs.body, 0.0f, 1.0f) : 0.0f;
        fatAmt = ps.character ? clampf (ps.fat >= 0.0f ? ps.fat : cs.fat, 0.0f, 1.0f) : 0.0f;
        spaceAmt = ps.character ? clampf (ps.space >= 0.0f ? ps.space : cs.space, 0.0f, 1.0f) : 0.0f;
        if (placeIndex >= 0) spaceAmt = 0.0f;     // the MaterialLab place replaces the short room
        if (placeIndex >= 0)
        {
            const PlaceSpec spc = placePreset (placeIndex);
            gesture = vp.gesture; gu = vp.u0; gdu = vp.du;
            // Doppler is measured from the nearest point of the stroke, so the delay never runs negative
            placer.begin (spc, gesture != nullptr ? std::min (gesture->distLo, gesture->distHi) : spc.distance);
            placeGain = gesture != nullptr ? 1.0f : placeMakeup (spc);
            if (gesture != nullptr) applyGesture();
            placeTail = (int64_t) ((spc.rt60 * 0.2f + 0.1f) * sr);   // the room itself rings on the shared bus
        }
        float bp = cs.bodyHz > 0.0f ? cs.bodyHz : pitch;
        while (bp > cs.bodyMaxHz) bp *= 0.5f;
        while (bp < 40.0f) bp *= 2.0f;
        bp *= tuneRatio;
        if (bodyAmt > 0.0f)
        {
            body.reset();
            body.configure (bp, cs.bodyT60 * ts, cs.bodyDampHz * toneRatio);
        }
        bodyGain = bodyAmt * 0.9f;
        waveOn = bodyAmt > 0.0f && cs.waveLayer;
        if (waveOn)
        {
            WaveVoiceSpec w;
            w.pos = cs.wavePos; w.scan = 0.12f;
            w.wenv.times = { cs.waveDecay * ts, 0, 0, 0, 0, 0, 0, 0 }; w.wenv.levels = { 0, 0, 0, 0, 0, 0, 0, 0 };
            w.wenv.keyoff = 0; w.wenv.loop = false;
            w.detuneCents = 9.0f; w.level1 = 0.55f; w.level2 = 0.45f;
            w.cutoff = cs.waveCutoff * toneRatio; w.res = 0.25f; w.fenvAmount = 2.5f; w.drive = 1.6f;
            w.fa = 0.001f; w.fd = cs.waveDecay * 0.5f * ts; w.fs = 0.1f; w.fr = 0.1f;
            w.aa = 0.001f; w.ad = cs.waveDecay * ts; w.as = 0.0f; w.ar = 0.05f;
            const float f = bp * std::exp2 (cs.waveSemis / 12.0f);
            layer.start (&lib.tables.hollow, w, f, f, 0.0f, cs.waveDecay * ts * 3.0f + 0.05f, cs.waveDecay * ts, seed * 5u + 1u);
            waveGain = bodyAmt * peakRef * 1.1f * std::pow (std::max (v, 0.02f) / 0.8f, 1.2f);
        }
        preGain = ls::dbToGain (14.0f * fatAmt) / std::max (peakRef, 1.0e-4f);
        postGain = std::max (peakRef, 1.0e-4f) / ls::dbToGain (7.0f * fatAmt);
        if (spaceAmt > 0.0f)
        {
            room.reset(); room.configure (0.7f, 5200.0f * toneRatio, 260.0f);
            roomHp1.setHighpass (240.0f, 0.7071f, sr); roomHp1.reset();
            roomHp2 = roomHp1;
        }
        widener.start (ps.width, sr);
        tailSamples = (int64_t) ((cs.bodyT60 * ts * 1.3f + (spaceAmt > 0.0f ? 0.12f : 0.0f) + (ps.width > 1.0f ? 0.03f : 0.0f) + 0.02f) * sr);
        sideHpCoef = 1.0f - std::exp (-kTwoPi * 220.0f / sr);
        sideHp1.setHighpass (260.0f, 0.7071f, sr); sideHp1.reset();
        sideHp2 = sideHp1;
        sampleCount = 0;
        clean0 = (int64_t) (0.002f * sr);
        cleanRamp = 1.0f / std::max (1.0f, 0.012f * sr);
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
        level = ls::dbToGain (ps.levelDb) * levelComp;
        tailLeft = -1;
        chokeGain = 1.0f; choking = false;
        chokeStep = 1.0f / std::max (1.0f, 0.005f * sr);
        baseTone = ps.tone; baseDrive = ps.drive; basePan = pan;
        algoOn = false;
        if (algoSpec != nullptr && algoSpec->type > 0) { algo.start (*algoSpec, sr, pitch * tuneRatio, algoOffset); algoOn = algo.on(); }
        modTick = 0;
        if (mod.active()) { mod.t = 0.0; mod.update (0, sr); applyMod(); }
        active = true;
    }

    void applyMod() noexcept
    {
        const float tone = clampf (baseTone + mod.tone, -1.0f, 1.0f);
        tiltLow = ls::dbToGain (-9.0f * tone);
        tiltHigh = ls::dbToGain (9.0f * tone);
        const float dr = clampf (baseDrive + mod.drive, 0.0f, 1.0f);
        driveAmount = 1.0f + 15.0f * dr * dr; driveOn = dr > 1.0e-3f; driveMakeup = 1.0f / std::pow (driveAmount, 0.6f);
        const float pan = clampf (basePan + mod.pan, -1.0f, 1.0f);
        panL = pan > 0.0f ? 1.0f - pan : 1.0f;
        panR = pan < 0.0f ? 1.0f + pan : 1.0f;
        modLevel = mod.levelMul;
    }

    void choke() noexcept { choking = true; }
    void kill() noexcept { active = false; }

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
        // character
        if (bodyAmt > 0.0f)
        {
            float bl, br;
            body.process (0.5f * (l + r), bl, br);
            // keep the body's low end in the middle: only its upper part spreads
            const float mid = 0.5f * (bl + br), side = 0.5f * (bl - br);
            const float sideHigh = sideHp2.process (sideHp1.process (side));
            l += (mid + sideHigh) * bodyGain; r += (mid - sideHigh) * bodyGain;
        }
        if (waveOn)
        {
            const float w = layer.process() * waveGain;
            l += w; r += w;
        }
        if (fatAmt > 0.0f)
        {
            // the first milliseconds pass clean, so the strike keeps its snap;
            // the drive takes over as the body speaks
            const float k = sampleCount < clean0 ? 0.0f : std::min (1.0f, (float) (sampleCount - clean0) * cleanRamp);
            const float k2 = k * k * (3.0f - 2.0f * k);
            // drive the mid only: the side passes clean, so the low end stays mono
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            const float m2 = mid + k2 * (waveClip (mid * preGain) * postGain - mid);
            l = m2 + side; r = m2 - side;
        }
        ++sampleCount;
        if (spaceAmt > 0.0f)
        {
            float rl, rr;
            room.process (roomHp2.process (roomHp1.process (0.5f * (l + r))), rl, rr);
            l += rl * spaceAmt * 0.8f; r += rr * spaceAmt * 0.8f;
        }
        // let the body and room ring out after the sources end
        if (! any)
        {
            if (tailLeft < 0) tailLeft = tailSamples;
            any = tailLeft-- > 0;
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
        if (algoOn) algo.process (l, r);
        if (mod.active() && --modTick <= 0) { modTick = 32; mod.update (32, sr); applyMod(); }
        if (choking)
        {
            chokeGain -= chokeStep;
            if (chokeGain <= 0.0f) { chokeGain = 0.0f; any = false; }
        }
        const float g = level * chokeGain * modLevel;
        left = l * panL * g;
        right = r * panR * g;
        roomSend = 0.0f;
        if (placeIndex >= 0)
        {
            if (gesture != nullptr && ++gestureTick >= 32) { gestureTick = 0; gu += gdu * 32.0f; applyGesture(); }
            float ol, orr, snd;
            placer.process (left * placeGain, right * placeGain, ol, orr, snd);
            left = ol; right = orr; roomSend = snd;
            // the delay line (distance) must empty before the voice ends
            if (! any && placeTail-- > 0) any = true;
        }
        widener.process (left, right);      // last, so the mono sum is exactly what it was
        if (! any) active = false;
    }

    // the room bus this voice sends to (-1: none) and what it sends this sample
    int bus() const noexcept { return placeIndex; }
    float roomSend = 0.0f;

    int pad = -1, chokeGroup = 0;
    int fxRoute = -1;             // >= 0: this voice feeds a live effect instead of the mix
    uint64_t order = 0;

private:
    Widener widener;
    void applyGesture() noexcept
    {
        const float u = std::min (gu, 1.0f);
        placer.setDistance (gesture->distance (u));
        placer.setWidth (gesture->width (u));
        placer.setSend (gesture->send (u));
        placer.setPan (gesture->pan (u));
    }
    ObjectPlacer placer;
    const Gesture* gesture = nullptr;
    float gu = 0.0f, gdu = 0.0f, placeGain = 1.0f;
    int gestureTick = 0, placeIndex = -1;
    int64_t placeTail = 0;

    AlgoState algo;
    bool algoOn = false;
    int modTick = 0;
    float baseTone = 0.0f, baseDrive = 0.0f, basePan = 0.0f, modLevel = 1.0f;
    HitSource dry, feed;
    HollowChain chain;
    float sr = 48000.0f, dryGain = 1.0f, wetGain = 0.0f, trim = 1.0f, hollowTrim = 1.0f;
    float tiltCoef = 0.1f, tiltLow = 1.0f, tiltHigh = 1.0f, lowL = 0.0f, lowR = 0.0f;
    float driveAmount = 1.0f, driveMakeup = 1.0f, panL = 1.0f, panR = 1.0f, level = 1.0f;
    float chokeGain = 1.0f, chokeStep = 0.01f;
    bool active = false, choking = false, driveOn = false;
    // character
    BodyNetwork body;
    WaveVoice layer;
    ls::EarlyReflections room;
    Biquad roomHp1, roomHp2, sideHp1, sideHp2;
    float bodyAmt = 0.0f, fatAmt = 0.0f, spaceAmt = 0.0f, bodyGain = 0.0f, waveGain = 0.0f, preGain = 1.0f, postGain = 1.0f, levelComp = 1.0f;
    bool waveOn = false;
    int64_t tailSamples = 0, tailLeft = -1, sampleCount = 0, clean0 = 96;
    float sideHpCoef = 0.03f, sideLow = 0.0f, sideLow2 = 0.0f, cleanRamp = 0.002f;
};

class Engine
{
public:
    static constexpr int kPads = 16;
    static constexpr int kVoices = 48;
    static constexpr int kVoicesPerPad = 4;
    static constexpr int kRoutes = 24, kMixBlock = 2048;

    Engine() { library.build(); }

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        mixL.assign ((size_t) kMixBlock, 0.0f); mixR.assign ((size_t) kMixBlock, 0.0f);
        routeL.fill (nullptr); routeR.fill (nullptr);
        limiter.prepare (sr, 120.0f);
        for (auto& v : voices) v.prepare (sr);
        bus.prepare (sr);
        library.tables.materialNoise->prepare (sr);
        if (library.tables.rubberPrep.size() != library.tables.rubbers.size()
            || (! library.tables.rubberPrep.empty() && library.tables.rubberPrep[0].fs != sr))
        {
            library.tables.rubberPrep.clear();
            for (const auto& m : library.tables.rubbers) library.tables.rubberPrep.push_back (StretchVoice::makePrep (m, sr));
        }
        // one shared room per MaterialLab place (spatial.room_ir), convolved on a bus
        for (int i = 0; i < kNumPlaces; ++i)
        {
            const PlaceSpec sp = placePreset (i);
            roomOn[(size_t) i] = sp.wet > 0.0f || sp.early > 0.0f;
            if (roomOn[(size_t) i]) rooms[(size_t) i].configure (sr, sp, i * 64);   // staggered block boundaries
        }
        for (int i = 0; i <= kNumPlaces; ++i) gestures[(size_t) i] = gesturePreset (i);
        for (auto& g : gestureStart) g = -1;
        sampleClock = 0; pendingCount = 0;
    }

    // seconds from a pad's note-on to its snap (Stretch & Snap sounds): the sequencer starts those pads early
    float prerollSeconds (int pad) const noexcept
    {
        if (pad < 0 || pad >= kPads) return 0.0f;
        const auto& ps = pads[(size_t) pad];
        if (ps.useSample || ps.sound < 0 || ps.sound >= (int) library.sounds.size()) return 0.0f;
        const auto& sd = library.sounds[(size_t) ps.sound];
        return sd.kind == HitSource::Kind::Stretch ? sd.preroll * clampf (ps.decay, 0.1f, 8.0f) : 0.0f;
    }

    const Library& lib() const noexcept { return library; }

    // extraSemis: the key-transpose mode shifts every pad together.
    void trigger (int pad, float velocity, float extraSemis = 0.0f) noexcept
    {
        if (pad < 0 || pad >= kPads || velocity <= 0.0f) return;
        nextSettings = nullptr; nextMod = nullptr; nextAlgo = nullptr;
        triggerImpl (pad, velocity, extraSemis);
    }

    // A hit with its own settings (modulated macros, another kit's pad), modulation lanes and algorithm.
    void triggerWith (int pad, const PadSettings& settings, float velocity, float extraSemis, const VoiceMod* vmod,
                      const PadAlgo* algo, float algoOffset) noexcept
    {
        if (pad < 0 || pad >= kPads || velocity <= 0.0f) return;
        nextSettings = &settings; nextMod = vmod; nextAlgo = algo; nextAlgoOffset = algoOffset;
        triggerImpl (pad, velocity, extraSemis);
        nextSettings = nullptr; nextMod = nullptr; nextAlgo = nullptr;
    }

    // The same, but the hit feeds a live effect: its sound goes to route buffer `route` (setRoute), not the mix.
    void triggerRouted (int route, int pad, const PadSettings& settings, float velocity, float extraSemis, const VoiceMod* vmod) noexcept
    {
        if (route < 0 || route >= kRoutes) return;
        nextRoute = route;
        triggerWith (pad, settings, velocity, extraSemis, vmod, nullptr, 0.0f);
        nextRoute = -1;
    }
    // where a route's sound is written during the next renderVoices (null: nowhere)
    void setRoute (int route, float* l, float* r) noexcept { if (route >= 0 && route < kRoutes) { routeL[(size_t) route] = l; routeR[(size_t) route] = r; } }
    bool routeBusy (int route) const noexcept
    {
        for (const auto& v : voices) if (v.isActive() && v.fxRoute == route) return true;
        for (int k = 0; k < pendingCount; ++k) if (pending[(size_t) k].route == route) return true;
        return false;
    }
    void killRoute (int route) noexcept
    {
        for (auto& v : voices) if (v.isActive() && v.fxRoute == route) v.kill();
        for (int k = 0; k < pendingCount; ) { if (pending[(size_t) k].route == route) pending[(size_t) k] = pending[(size_t) --pendingCount]; else ++k; }
    }
    void chokeRoute (int route) noexcept { for (auto& v : voices) if (v.isActive() && v.fxRoute == route) v.choke(); }

    // ---- offline: one hit of a pad, dry (no master, no kit bus), until it has died away
    void renderHit (const PadSettings& settings, float velocity, float maxSeconds, Rendered& out)
    {
        for (auto& v : voices) { v.kill(); v.fxRoute = -1; }
        for (int b = 0; b < kNumPlaces; ++b)
        {
            if (roomUsed[(size_t) b]) { rooms[(size_t) b].configure (sr, placePreset (b), b * 64); roomUsed[(size_t) b] = false; }
            roomIdle[(size_t) b] = (int64_t) 1 << 40;   // idle until a voice sends to it
        }
        pendingCount = 0; sampleClock = 0;
        for (auto& g : gestureStart) g = -1;
        const auto saved = pads[0];
        pads[0] = settings;
        nextSettings = nullptr; nextMod = nullptr; nextAlgo = nullptr;
        triggerImpl (0, velocity, 0.0f);
        const int maxN = (int) (std::max (0.05f, maxSeconds) * sr);
        out.sampleRate = sr; out.l.assign ((size_t) maxN, 0.0f); out.r.assign ((size_t) maxN, 0.0f);
        int quiet = 0, n = 0;
        constexpr int B = 256;
        while (n < maxN)
        {
            const int m = std::min (B, maxN - n);
            renderCore (out.l.data() + n, out.r.data() + n, m, nullptr, -1, nullptr, nullptr, false);
            float pk = 0.0f;
            for (int i = 0; i < m; ++i) pk = std::max (pk, std::max (std::fabs (out.l[(size_t) (n + i)]), std::fabs (out.r[(size_t) (n + i)])));
            n += m;
            const bool anyVoice = activeVoices() > 0 || pendingCount > 0;
            quiet = (! anyVoice && pk < 1.0e-4f) ? quiet + m : 0;
            if (quiet > (int) (0.05f * sr)) break;
        }
        pads[0] = saved;
        // trim the silent tail
        int end = n;
        while (end > 1 && std::fabs (out.l[(size_t) end - 1]) < 2.0e-5f && std::fabs (out.r[(size_t) end - 1]) < 2.0e-5f) --end;
        end = std::max (end, std::min (n, 64));
        out.l.resize ((size_t) end); out.r.resize ((size_t) end);
        for (auto& v : voices) v.kill();
    }

private:
    void triggerImpl (int pad, float velocity, float extraSemis) noexcept
    {
        const auto& p0 = nextSettings != nullptr ? *nextSettings : pads[(size_t) pad];
        if (p0.bounces > 1)
        {   // a bouncing object: each flight lasts e times the last, each impact is e times slower
            const Bounces b = bounceTimes (p0.bounceFlight, p0.restitution, std::min (p0.bounces, 32), 0.0f);
            for (size_t i = 1; i < b.times.size() && pendingCount < kPending; ++i)
                pending[(size_t) pendingCount++] = { sampleClock + (int64_t) (b.times[i] * sr), pad, velocity * std::min (b.speeds[i], 1.6f), extraSemis, nextRoute };
        }
        fire (pad, velocity, extraSemis, nextRoute);
    }

    void fire (int pad, float velocity, float extraSemis, int route = -1) noexcept
    {
        PadSettings ps = nextSettings != nullptr ? *nextSettings : pads[(size_t) pad];
        ps.tune += extraSemis;
        // where it is heard and the stroke that moves it
        VoicePlacement vp;
        const auto& sd0 = library.sounds[(size_t) std::max (0, std::min (ps.sound, (int) library.sounds.size() - 1))];
        vp.place = ps.place >= -1 ? ps.place : (ps.useSample ? -1 : sd0.defaultPlace);
        if (ps.gesture > 0 && ps.gesture <= kNumPlaces)
        {
            if (vp.place < 0) vp.place = 1;
            const int64_t len = (int64_t) (std::max (ps.gestureSeconds, 0.05f) * sr);
            auto& g0 = gestureStart[(size_t) pad];
            if (g0 < 0 || sampleClock - g0 >= len) g0 = sampleClock;           // a new stroke
            vp.gesture = &gestures[(size_t) ps.gesture];
            vp.u0 = (float) (sampleClock - g0) / (float) len;
            vp.du = 1.0f / (float) len;
            ps.tune += vp.gesture->pitch (std::min (vp.u0, 1.0f));               // pitch applies to each strike
        }
        // choke: a hit in a group fades every voice in that group (open/closed hats)
        if (ps.choke > 0 && route < 0)
            for (auto& v : voices)
                if (v.isActive() && v.chokeGroup == ps.choke && v.fxRoute < 0) v.choke();
        // per-pad polyphony
        int count = 0; PadVoice* oldestOfPad = nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.pad == pad && v.fxRoute < 0)
            {
                ++count;
                if (oldestOfPad == nullptr || v.order < oldestOfPad->order) oldestOfPad = &v;
            }
        if (route < 0 && count >= kVoicesPerPad && oldestOfPad != nullptr) oldestOfPad->choke();
        PadVoice* target = nullptr;
        for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
        if (target == nullptr)
        {
            for (auto& v : voices) if (target == nullptr || v.order < target->order) target = &v;
        }
        target->mod = nextMod != nullptr ? *nextMod : VoiceMod();
        target->algoSpec = nextAlgo; target->algoOffset = nextAlgoOffset;
        target->fxRoute = route;
        target->start (library, pad, ps, velocity, seedCounter = ls::hash32 (seedCounter + 0x9e3779b9u), ++orderCounter, vp);
        hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
    }

public:

    // Adds into left/right. tap (optional) receives the mono sum of `tapPad`. addL/addR (optional): other
    // sources (players, marks, grid) mixed in before the master and the kit bus.
    void render (float* left, float* right, int numSamples, float* tap = nullptr, int tapPad = -1,
                 const float* addL = nullptr, const float* addR = nullptr) noexcept
    {
        renderCore (left, right, numSamples, tap, tapPad, addL, addR, true);
    }

    void renderCore (float* left, float* right, int numSamples, float* tap, int tapPad,
                     const float* addL, const float* addR, bool withMaster) noexcept
    {
        for (int done = 0; done < numSamples; )
        {
            const int n = std::min (kMixBlock, numSamples - done);
            renderVoices (n, tap != nullptr ? tap + done : nullptr, tapPad, withMaster);
            if (withMaster) finishMix (left + done, right + done, n, addL != nullptr ? addL + done : nullptr, addR != nullptr ? addR + done : nullptr);
            else for (int i = 0; i < n; ++i) { left[done + i] += mixL[(size_t) i]; right[done + i] += mixR[(size_t) i]; }
            done += n;
        }
    }

    // The two halves of a block (n <= kMixBlock), so live effects can sit between them: the voices and
    // rooms into the engine's own mix (routed voices into their route buffers) ...
    void renderVoices (int numSamples, float* tap, int tapPad, bool tapAdds = true) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // scheduled bounces
            for (int k = 0; k < pendingCount; )
            {
                if (pending[(size_t) k].at <= sampleClock)
                {
                    const auto e = pending[(size_t) k];
                    pending[(size_t) k] = pending[(size_t) --pendingCount];
                    fire (e.pad, e.velocity, e.semis, e.route);
                    continue;
                }
                ++k;
            }
            float L = 0.0f, R = 0.0f, T = 0.0f;
            std::array<float, kNumPlaces> sends {};
            for (auto& v : voices)
            {
                if (! v.isActive()) continue;
                float l, r;
                v.render (l, r);
                if (v.fxRoute >= 0)
                {
                    if (float* rl = routeL[(size_t) v.fxRoute]) { rl[i] += l; routeR[(size_t) v.fxRoute][i] += r; }
                    continue;
                }
                if (band.to (v.pad))       // through its effects first, if it has any: then the unit sends
                {
                    const float s = band.send[v.pad], d = band.duck[v.pad];
                    band.L[i] += l * s; band.R[i] += r * s;
                    if (v.pad == tapPad) T += 0.5f * (l + r) * (1.0f - d);   // the tap hears the whole lane
                    l *= d; r *= d;
                }
                L += l; R += r;
                if (v.bus() >= 0) sends[(size_t) v.bus()] += v.roomSend;
                if (v.pad == tapPad) T += 0.5f * (l + r);
            }
            // the MaterialLab rooms
            for (int b = 0; b < kNumPlaces; ++b)
            {
                if (! roomOn[(size_t) b]) continue;
                if (sends[(size_t) b] != 0.0f) { roomIdle[(size_t) b] = 0; roomUsed[(size_t) b] = true; }
                else if (roomIdle[(size_t) b] > roomTail (b)) continue;    // nothing in it: skip the convolution
                ++roomIdle[(size_t) b];
                float wl, wr;
                rooms[(size_t) b].process (sends[(size_t) b], wl, wr);
                L += wl; R += wr;
            }
            ++sampleClock;
            mixL[(size_t) i] = L; mixR[(size_t) i] = R;
            if (tap != nullptr) { if (tapAdds) tap[i] += T; else tap[i] = T; }
        }
    }
    // ... then everything else (players, marks, live effects: addL/addR), the master, the kit bus and the safety knee.
    void finishMix (float* left, float* right, int numSamples, const float* addL, const float* addR) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float L = mixL[(size_t) i], R = mixR[(size_t) i];
            if (addL != nullptr) { L += addL[i]; R += addR[i]; }
            L *= master; R *= master;
            bus.process (L, R, glue);
            // IDM Jack can push the kit well over: seizures stack. While it is working, a limiter
            // takes the level down ahead of the knee, so a seizure keeps its shape instead of
            // being squashed into the knee's tanh. Off, this line does nothing and nothing else in
            // NeoBeat changes. Zero latency, so nothing in the mix is delayed for it.
            if (limiting) { const float g = limiter.gain (std::max (std::fabs (L), std::fabs (R))); L *= g; R *= g; }
            left[i] += safety (L);
            right[i] += safety (R);
        }
    }

    void allNotesOff() noexcept { pendingCount = 0; for (auto& v : voices) if (v.isActive()) v.choke(); }
    void chokeGroup (int g) noexcept { if (g > 0) for (auto& v : voices) if (v.isActive() && v.chokeGroup == g) v.choke(); }
    float sampleRate() const noexcept { return sr; }

    int activeVoices() const noexcept { int c = 0; for (auto& v : voices) c += v.isActive() ? 1 : 0; return c; }

    // Soft safety knee above -1 dBFS: normal levels pass untouched.
    //
    // The curve lives in shared/Ceiling.h as nsr::safetyKnee, so every program in the family bends
    // the same way rather than each carrying its own copy. The arithmetic is unchanged, so a mix
    // that passed through the version that used to be written out here passes identically.
    //
    // Note that Metal.h has a function of the same name doing a different job - it bends toward
    // 1.8, not 1.0 - and is deliberately left alone.
    static float safety (float x) noexcept { return nsr::safetyKnee (x); }

    std::array<PadSettings, kPads> pads {};
    std::array<std::atomic<uint32_t>, kPads> hitCounter {};
    std::array<Gesture, kNumPlaces + 1> gestures {};   // the strokes the pads can follow (editable: the Matrixxxer draws them)
    float master = 0.35f;  // -9 dB: kick, snare and hats landing together stay clear of the knee
    bool limiting = false;         // set by the workstation while IDM Jack is working
    BandSend band;                 // set by the workstation, per segment
    nsr::PeakLimiter limiter;
    float glue = 0.4f;     // kit-bus glue compression and saturation

private:
    int64_t roomTail (int b) const noexcept { return (int64_t) ((placePreset (b).rt60 * 1.6f + 0.6f) * sr); }
    struct Pending { int64_t at = 0; int pad = 0; float velocity = 0, semis = 0; int route = -1; };
    std::vector<float> mixL, mixR;
    std::array<float*, kRoutes> routeL {}, routeR {};
    int nextRoute = -1;
    static constexpr int kPending = 128;
    std::array<Pending, kPending> pending {};
    int pendingCount = 0;
    int64_t sampleClock = 0;
    std::array<int64_t, kPads> gestureStart {};
    std::array<RoomBus, kNumPlaces> rooms;
    std::array<bool, kNumPlaces> roomOn {};
    std::array<bool, kNumPlaces> roomUsed {};
    const PadSettings* nextSettings = nullptr;
    const VoiceMod* nextMod = nullptr;
    const PadAlgo* nextAlgo = nullptr;
    float nextAlgoOffset = 0.0f;
    std::array<int64_t, kNumPlaces> roomIdle {};
    Library library;
    std::array<PadVoice, kVoices> voices;
    KitBus bus;
    float sr = 48000.0f;
    uint32_t seedCounter = 12345u;
    uint64_t orderCounter = 0;
};
} // namespace nb
