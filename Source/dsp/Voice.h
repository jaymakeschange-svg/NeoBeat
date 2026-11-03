// Liquid Swing 4 — one sounding hit.
//
// Layers, in the order the plan describes them:
//   strike  : an impulse shaped by mallet hardness (velocity makes it brighter),
//             optional noise that rings the partials, an optional dry click
//   body    : up to three groups of partials (harmonic, odd-only, inharmonic,
//             clustered or explicit ratios), each in its own bank with its own
//             decay law, stereo spread, beating partners, bloom and wobble
//   motion  : a pitch gesture (sweep, drift, stepped flip, energy tension,
//             recoil); each group follows it by its own amount, so fans of
//             partials can rise, fall and cross
//   texture : bright decorrelated fizz, ring/AM sidebands
//   weight  : a clean mono sub with its own pitch drop
//   shape   : attack, gate (hold then cut) and release
//   couple  : (round 5) two groups exchange energy both ways at a junction,
//             as Chromaphone's coupled resonators do
//   metal   : (round 5) a damped feedback delay network beside the body,
//             as in IronHead Bonk's "metal" layer
// Nothing here clips or saturates; levels are managed by the mixer. (The metal
// network has a soft safety bend inside its loop that normal levels never reach.)
#pragma once

#include "Core.h"
#include "PartialBank.h"
#include "Space.h"
#include "Metal.h"
#include <array>

namespace ls
{
enum class GroupKind { Harmonic, Cluster, Explicit };

struct GroupSpec
{
    GroupKind kind = GroupKind::Harmonic;
    int count = 8;               // partials generated (before beating partners)
    int firstHarmonic = 1;       // Harmonic: lowest harmonic number used
    float baseHz = 200.0f;       // fundamental, or centre of a cluster
    float levelDb = 0.0f;
    float tiltDbPerOctave = -6.0f;
    float evenGain = 1.0f;       // Harmonic: level of even partials (0 = odd only, hollow)
    float stretch = 0.0f;        // Harmonic: ratio = k^(1 + stretch); > 0 sounds metallic
    float spacingHz = 60.0f;     // Cluster: average distance between partials
    float spacingJitter = 0.4f;  // Cluster: irregularity of that distance (0..1)
    float formantHz = 0.0f;      // spectral emphasis centre (0 = none)
    float formantDb = 0.0f;      // boost (or cut) at the centre
    float formantOctaves = 1.0f; // width of the emphasis
    float t60 = 1.0f;            // decay of the group's lowest partial
    float decayTilt = 0.3f;      // > 0 highs die faster, < 0 highs ring longer
    float ringHz = 0.0f;         // ring focus: partials near here ring longer (0 = none)
    float ringOctaves = 0.3f;    // width of the ring focus
    float ringBoost = 1.0f;      // decay-time multiplier at the centre of the focus
    float width = 0.5f;          // stereo spread of partials above the mono cutoff
    float phaseWidth = 0.0f;     // 0..1: left/right phase offsets per partial (a wider, 3D image; mono-safe)
    float pitchFollow = 1.0f;    // share of the pitch gesture the group follows (may be negative)
    float pairHz = 0.0f;         // add a beating partner this many Hz above each partial
    float pairLevel = 0.0f;      // partner level relative to its partial
    float strike = 1.0f;         // share of the impulse strike this group receives
    float noise = 1.0f;          // share of the noise excitation this group receives
    float bloom = 0.0f;          // seconds for the group to swell in after the strike (energy cascade)
    float bloomDelay = 0.0f;     // seconds before the swell starts
    float vibratoSemis = 0.0f;   // slow common wobble of the whole group
    float vibratoHz = 0.0f;
    float jitterCents = 0.0f;    // every partial wanders on its own by this much ("liquid")
    float jitterHz = 2.0f;
    float lag = 0.0f;            // elastic stretch: seconds per octave the upper partials trail the sweep
    float fan = 0.0f;            // per octave above the lowest partial, how much further a partial sweeps
                                 // (> 0: the spectrum starts squeezed together and fans out)
    float phaseSpread = 0.0f;    // 0 = partials start together (sharp strike), 1 = random start phases (smooth, lower peaks)
    float pan = 0.0f;            // where the group sits, -1 left .. +1 right (a level cue)
    float itdMs = 0.0f;          // right channel late by this many ms (-1..1): a time cue that places the group
    float spaceBloom = 0.0f;     // seconds for the image to open from a centred strike to full width (0 = open at once)
    float spaceDrift = 0.0f;     // 0..1: how far each partial keeps wandering in the image while it rings
    float spaceDriftHz = 0.7f;
    float flutterDb = 0.0f;      // each partial's level wanders by up to this much while it rings
    float flutterHz = 3.0f;
    float am = 1.0f;             // share of the patch's ring/AM texture this group receives
    // Resonant peaks (research: damping contrast is the main material cue; a few
    // prominent resonances fed by narrowband noise; strike/pickup geometry).
    float peakFocus = 0.0f;      // 0..1: the strongest modes ring longer and louder, the filler shorter
    float peakFromHz = 0.0f;     // peak focus and halo act only on modes above this
    float peakSpan = 0.25f;      // octaves: a mode is a peak if nothing within this distance carries more energy
    float halo = 0.0f;           // 0..1: narrowband noise fed into the peaks (1 = as much energy as the strike)
    float strikePos = 0.0f;      // 0 = off; else where the object is struck (0..0.5 of its length)
    float pickupPos = 0.0f;      // 0 = off; else where it is heard from
    float positionDepth = 0.0f;  // 0..1: how much strike/pickup geometry shapes the mode levels
    // Buoyancy (research: rising bubbles climb in pitch as they decay, van den
    // Doel; bending waves are dispersive, highs arrive first).
    float buoyancy = 0.0f;       // xi: each partial rises by f0 * xi * beta * t (0.1 = a free bubble)
    float disperseMs = 0.0f;     // the lowest partial is struck this much later than the highest
    int numRatios = 0;           // Explicit: ratios relative to baseHz
    std::array<float, 16> ratios {};
    std::array<float, 16> ratioLevelsDb {};
};

// Two-way coupling between two groups (Chromaphone-style). Group `a` is the
// struck object; group `b` is coupled to it at a junction, so energy flows into
// b and back again. Usually b gets no strike of its own (g<b>.strike = 0).
struct CoupleSpec
{
    int a = -1, b = -1;          // group indices; -1 = off
    float rateHz = 0.0f;         // how fast energy can cross the junction (0 = off)
    float balance = 0.0f;        // impedance ratio, -1 .. +1: + = b is stiff (you hear a), - = b is soft (you hear b)
    float posA = 0.0f;           // junction position on a (0 = every mode equally; else 0..0.5 of its length)
    float posB = 0.3f;           // junction position on b
    float morphTime = 0.0f;      // > 0: rate and balance glide to their end values over this time
    float endRateHz = -1.0f;     // -1 = stay at rateHz
    float endBalance = -9.0f;    // -9 = stay at balance
    float partnerDb = 0.0f;      // how loud b is heard
};

struct Patch
{
    static constexpr int kMaxGroups = 4;
    std::array<GroupSpec, kMaxGroups> groups {};
    int numGroups = 1;
    float lowMonoHz = 180.0f;     // partials below this stay centred

    // Strike
    float hardness = 0.7f;        // 0 = soft and dark, 1 = hard and bright
    float noiseExcite = 0.0f;     // noise that rings the partials (metallic noise); 1 = as much energy as the strike
    float noiseExciteT60 = 0.03f;
    float haloAmount = 1.0f;      // scales every group's halo
    float haloT60 = 0.3f;         // how long the halo noise keeps feeding the peaks
    float rebound = 0.0f;         // 0..1: a second, lighter contact after the strike
    float reboundMs = 25.0f;      // its delay at the reference velocity
    float airHz = 0.0f;           // 0 = off; else partials roll off gently above this
    int bounces = 1;              // contacts after the strike (rebound sets the first)
    float restitution = 0.5f;     // each bounce comes sooner and lighter by this factor

    // Float: the struck object bobs after the hit (a damped buoyant heave).
    float bobSemis = 0.0f;        // pitch excursion while bobbing
    float bobHz = 2.5f;
    float bobTime = 0.15f;        // the bob dies away with this time constant
    float bobLevelDb = 0.0f;      // level change while bobbing
    float bobLiftDb = 0.0f;       // 6-9 kHz (height) band change while bobbing
    float liftDb = 0.0f;          // static emphasis of the 6-9 kHz height band

    // Space: early lateral reflections around each hit.
    float roomLevel = 0.0f;       // 0 = off
    float roomSize = 1.0f;
    float roomDampHz = 5000.0f;
    float roomLowCutHz = 150.0f;
    float clickLevel = 0.0f;      // dry transient that skips the body
    float clickHz = 4000.0f;
    float clickQ = 1.2f;
    float clickT60 = 0.006f;

    // Weight
    float subLevel = 0.0f;
    float subHz = 55.0f;
    float subT60 = 0.6f;
    float subDropSemis = 12.0f;   // starts this far above subHz and falls
    float subDropTime = 0.025f;
    float subGrowl = 0.0f;        // level of the sub's 2nd harmonic (added cleanly, no waveshaping)
    float subGrowlTilt = 0.6f;    // each further harmonic is this much quieter (up to the 12th)
    float subGrowlT60 = 0.5f;     // the harmonics fade faster than the sub itself

    // Texture
    float fizzLevel = 0.0f;
    float fizzHz = 7000.0f;
    float fizzQ = 0.7f;
    float fizzT60 = 0.4f;
    float fizzWidth = 0.8f;       // 0 mono .. 1 fully decorrelated
    float fizzDelay = 0.0f;       // grain can enter late...
    float fizzAttack = 0.0f;      // ...and swell in
    float fizzChopHz = 0.0f;      // rhythmic chopping of the grain (0 = off)
    float fizzChopDuty = 0.5f;    // share of each chop cycle the grain is open
    float amHz = 0.0f;            // ring/AM sidebands this far from each partial
    float amDepth = 0.0f;

    // Motion (semitones)
    float sweepSemis = 0.0f;      // pitch starts this far away and arrives at 0
    float sweepTime = 0.2f;
    float sweepCurve = 0.0f;      // > 0 arrives late and fast (stretch), < 0 arrives early (drop)
    float driftSemis = 0.0f;
    float driftTime = 1.0f;
    float flipSemis = 0.0f;       // total jump, taken in flipSteps equal steps
    float flipTime = 0.0f;        // 0 = no flip
    int flipSteps = 1;
    float flipStepTime = 0.04f;
    float tensionSemis = 0.0f;    // extra pitch while the hit is loud, relaxing as it rings
    float recoilSemis = 0.0f;
    float recoilHz = 14.0f;
    float recoilTime = 0.06f;

    // Shape
    float attack = 0.002f;
    float gate = 0.0f;            // 0 = ring freely; otherwise hold this long, then release
    float release = 0.012f;
    float maxLength = 4.0f;
    float gainDb = 0.0f;

    // Velocity: how much a harder hit changes the sound, beyond getting louder.
    float velCurve = 1.4f;        // loudness = velocity ^ velCurve
    float velBright = 0.35f;      // mallet hardness change across the velocity range
    float velPitch = 0.25f;       // share of the pitch gesture that depends on velocity
    float velDecay = 0.0f;        // harder hits ring longer (+) or shorter (-)
    float velNoise = 0.0f;        // harder hits get more click, fizz and ringing noise
    float velAm = 0.0f;           // harder hits get deeper ring/AM texture
    float velWidth = 0.0f;        // harder hits open wider in the stereo image
    float velTension = 0.0f;      // hard hits start sharp and relax (semitones at full velocity)

    // Variation: every hit differs a little, like a real object struck twice.
    float variation = 0.0f;       // 0..1

    // Round 5
    CoupleSpec couple;
    MetalSpec metal;
};

class Voice
{
public:
    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        for (auto& b : banks)
            b.prepare (sampleRate);
        room.prepare (sampleRate);
        metal.prepare (sampleRate);
        active = false;
    }

    bool isActive() const noexcept { return active; }

    void start (const Patch& newPatch, float newVelocity, float noteSemitones, uint32_t seed) noexcept
    {
        patch = newPatch;
        patch.numGroups = std::min (std::max (patch.numGroups, 1), Patch::kMaxGroups);
        velocity = clampf (newVelocity, 0.0f, 1.0f);
        noteOffset = noteSemitones;
        voiceSeed = seed;
        rng = Rng (hash32 (seed) | 1u);
        noiseLeft = Rng (hash32 (seed * 3u + 17u) | 1u);
        noiseRight = Rng (hash32 (seed * 7u + 91u) | 1u);
        initialEnergy = -1.0f;
        tension = 1.0f;
        coupleOn = metalOn = false;
        applyVelocityAndVariation();
        buildPartials();
        sampleIndex = 0;
        controlCountdown = 0;
        envelope = 0.0f;
        subPhase = 0.0f;
        subAmp = 1.0f;
        subRadius = t60ToRadius (patch.subT60, sampleRate);
        growlAmp = 1.0f;
        growlRadius = t60ToRadius (patch.subGrowlT60, sampleRate);
        fizzAmp = 1.0f;
        fizzRadius = t60ToRadius (patch.fizzT60, sampleRate);
        exciteAmp = 1.0f;
        exciteRadius = t60ToRadius (patch.noiseExciteT60, sampleRate);
        // Scale the noise so that noiseExcite = 1 puts as much energy into each
        // partial as the strike itself, however long the noise lasts. Then 0.3
        // means "a breath of noise under the strike" on every patch, and hits
        // stay consistent instead of randomly losing a harmonic.
        noiseScale = std::sqrt (3.0f * (1.0f - exciteRadius * exciteRadius));
        // Filtered noise keeps the same loudness at any sample rate.
        noiseRateScale = std::sqrt (sampleRate / 48000.0f);
        haloNoise = Rng (hash32 (seed * 11u + 5u) | 1u);
        haloAmp = 1.0f;
        haloRadius = t60ToRadius (patch.haloT60, sampleRate);
        haloScale = std::sqrt (3.0f * (1.0f - haloRadius * haloRadius));
        haloActive = false;
        for (int gi = 0; gi < patch.numGroups; ++gi)
            haloActive = haloActive || patch.groups[(size_t) gi].halo > 0.0f;
        haloActive = haloActive && patch.haloAmount > 0.0f;
        clickAmp = 1.0f;
        clickRadius = t60ToRadius (patch.clickT60, sampleRate);
        clickFilter.reset();
        clickFilter.set (patch.clickHz, patch.clickQ, sampleRate);
        fizzFilterLeft.reset();
        fizzFilterRight.reset();
        fizzFilterLeft.set (patch.fizzHz, patch.fizzQ, sampleRate);
        fizzFilterRight.set (patch.fizzHz, patch.fizzQ, sampleRate);
        amPhase = 0.0f;
        chopPhase = 0.0f;
        chopGain = 1.0f;
        chopSmoothing = smoothingCoefficient (0.002f, sampleRate);
        attackStep = 1.0f / std::max (1.0f, patch.attack * sampleRate);
        releaseFactor = t60ToRadius (std::max (patch.release, 0.001f), sampleRate);
        gain = dbToGain (patch.gainDb) * std::pow (velocity, patch.velCurve);
        released = choking = false;
        dryFinished = false;
        tailLeft = 0;
        bobGain = 1.0f;
        if (patch.roomLevel > 0.0f)
        {
            room.reset();
            room.configure (patch.roomSize, patch.roomDampHz, patch.roomLowCutHz);
        }
        // Coupling
        const auto& c = patch.couple;
        coupleOn = c.rateHz > 0.0f && c.a >= 0 && c.b >= 0 && c.a != c.b && c.a < patch.numGroups && c.b < patch.numGroups;
        // Metal network
        metalOn = patch.metal.amount > 0.0f;
        if (metalOn)
        {
            metal.configure (patch.metal, seed * 13u + 7u);
            metalStrikeLp = 0.0f;
            // Contact time: 0.25 ms for the hardest mallet, about 3 ms for the softest.
            metalContactLen = std::max (2, (int) (sampleRate * 0.001f * (0.25f + 2.75f * (1.0f - clampf (patch.hardness, 0.0f, 1.0f)))));
            metalStrikeCoef = smoothingCoefficient (1.0f / (kTwoPi * std::min (malletCorner, 0.4f * sampleRate)), sampleRate);
            metalNoise = Rng (hash32 (seed * 19u + 3u) | 1u);
            metalNoiseAmp = 1.0f;
            metalNoiseRadius = t60ToRadius (patch.metal.noiseT60, sampleRate);
        }
        updateControl (0.0f);
        active = true;
    }

    // Fast fade used when the voice is stolen or choked (about 3 ms).
    void choke() noexcept { choking = true; chokeFactor = t60ToRadius (0.02f, sampleRate); }

    void render (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        if (! active)
            return;

        const float t = (float) sampleIndex / sampleRate;
        if (controlCountdown-- <= 0)
        {
            controlCountdown = kControlInterval - 1;
            updateControl (t);
        }

        // Strike: one impulse (and an optional lighter rebound contact), plus optional ringing noise.
        float impulse = sampleIndex == 0 ? 1.0f : 0.0f;
        if (bounceIndex < numBounces && sampleIndex == bounceSample[(size_t) bounceIndex])
        {
            impulse = bounceAmp[(size_t) bounceIndex];
            clickAmp = std::max (clickAmp, 0.5f * impulse);
            ++bounceIndex;
        }
        float halo = 0.0f;
        if (haloActive && haloAmp > 1.0e-6f)
        {
            halo = patch.haloAmount * haloAmp * haloNoise.bipolar() * haloScale;
            haloAmp *= haloRadius;
        }
        float noise = 0.0f;
        if (patch.noiseExcite > 0.0f && exciteAmp > 1.0e-6f)
        {
            noise = patch.noiseExcite * exciteAmp * rng.bipolar() * noiseScale;
            exciteAmp *= exciteRadius;
        }

        // Ring/AM texture.
        float am = 1.0f;
        if (patch.amDepth > 0.0f)
        {
            am = 1.0f - patch.amDepth + patch.amDepth * std::cos (amPhase);
            amPhase += kTwoPi * patch.amHz / sampleRate;
            if (amPhase > kTwoPi) amPhase -= kTwoPi;
        }

        float bodyLeft = 0.0f, bodyRight = 0.0f;
        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            const auto& g = patch.groups[(size_t) gi];
            float l, r;
            const float groupAm = g.am >= 1.0f ? am : 1.0f + g.am * (am - 1.0f);
            // Dispersive groups receive their first strike through arrive() instead.
            const float strikeHere = (g.disperseMs > 0.0f && sampleIndex == 0) ? 0.0f : impulse;
            banks[(size_t) gi].process (strikeHere * g.strike + noise * g.noise, halo, groupAm, l, r);
            float level = 1.0f;
            if (g.bloom > 0.0f || g.bloomDelay > 0.0f)
                level = smoothstep ((t - g.bloomDelay) / std::max (g.bloom, 1.0e-4f));
            if (coupleOn && gi == patch.couple.b)
                level *= partnerGain;
            bodyLeft += l * level;
            bodyRight += r * level;
        }

        // Coupling: an energy-preserving rotation between the two groups' motion
        // at the junction (weighted by the impedance ratio). Each bank still
        // decays on its own, so the pair cannot run away.
        if (coupleOn)
        {
            auto& A = banks[(size_t) patch.couple.a];
            auto& B = banks[(size_t) patch.couple.b];
            const float a = A.junctionVelocity();
            const float bs = B.junctionVelocity() * coupleScale;
            const float a2 = a * coupleCos - bs * coupleSin;
            const float b2 = a * coupleSin + bs * coupleCos;
            A.pushAtJunction (a2 - a);
            B.pushAtJunction ((b2 - bs) / coupleScale);
        }

        // Metal network: struck by a mallet-softened impulse, a short rattle of
        // noise and, optionally, the body's own ring.
        if (metalOn)
        {
            // The strike reaches the network as a short raised-cosine contact
            // (shorter for a harder mallet), so the lines ring instead of clicking.
            float contact = 0.0f;
            if (sampleIndex < metalContactLen)
                contact = (1.0f - std::cos (kTwoPi * ((float) sampleIndex + 0.5f) / (float) metalContactLen)) / (float) metalContactLen;
            metalStrikeLp += metalStrikeCoef * (contact - metalStrikeLp);
            float in = patch.metal.strike * metalStrikeLp * kMetalStrikeGain;
            if (patch.metal.noise > 0.0f && metalNoiseAmp > 1.0e-6f)
            {
                in += patch.metal.noise * metalNoiseAmp * metalNoise.bipolar() * kMetalNoiseGain;
                metalNoiseAmp *= metalNoiseRadius;
            }
            if (patch.metal.feed > 0.0f)
                in += patch.metal.feed * 0.5f * (bodyLeft + bodyRight) * kMetalFeedGain;
            float ml, mr;
            metal.process (in, ml, mr);
            const float duck = 1.0f - clampf (patch.metal.duck, 0.0f, 1.0f) * clampf (patch.metal.amount, 0.0f, 1.0f);
            bodyLeft = bodyLeft * duck + patch.metal.amount * ml;
            bodyRight = bodyRight * duck + patch.metal.amount * mr;
        }

        // Fizz: two independent noises, so the highs are genuinely wide. It can
        // enter late, swell in and be chopped rhythmically.
        float fizzL = 0.0f, fizzR = 0.0f;
        if (patch.fizzLevel > 0.0f && fizzAmp > 1.0e-5f && t >= patch.fizzDelay)
        {
            const float nl = noiseLeft.bipolar();
            const float nr = lerp (nl, noiseRight.bipolar(), patch.fizzWidth);
            float lo, bandL, hi, bandR;
            fizzFilterLeft.process (nl, lo, bandL, hi);
            fizzFilterRight.process (nr, lo, bandR, hi);
            float shape = fizzAmp * patch.fizzLevel;
            if (patch.fizzAttack > 0.0f)
                shape *= smoothstep ((t - patch.fizzDelay) / patch.fizzAttack);
            if (patch.fizzChopHz > 0.0f)
            {
                chopPhase += patch.fizzChopHz / sampleRate;
                if (chopPhase >= 1.0f) chopPhase -= 1.0f;
                const float target = chopPhase < patch.fizzChopDuty ? 1.0f : 0.0f;
                chopGain += (target - chopGain) * chopSmoothing;
                shape *= chopGain;
            }
            fizzL = bandL * shape * noiseRateScale;
            fizzR = bandR * shape * noiseRateScale;
            fizzAmp *= fizzRadius;
        }

        // Dry click that skips the body.
        float click = 0.0f;
        if (patch.clickLevel > 0.0f && clickAmp > 1.0e-5f)
        {
            float lo, band, hi;
            clickFilter.process (rng.bipolar() * clickAmp, lo, band, hi);
            click = band * patch.clickLevel * 2.0f * noiseRateScale;
            clickAmp *= clickRadius;
        }

        // Clean mono sub with its own drop, plus optional harmonics for weight
        // on small speakers (synthesised additively, so nothing aliases or
        // intermodulates).
        float sub = 0.0f;
        if (patch.subLevel > 0.0f)
        {
            const float s1 = std::sin (subPhase);
            float wave = s1;
            if (patch.subGrowl > 0.0f && growlAmp > 1.0e-5f)
            {
                // sin(h*phase) by the Chebyshev recurrence: cheap and exact.
                const float twoCos = 2.0f * std::cos (subPhase);
                float previous = s1, current = twoCos * s1; // sin(2*phase)
                float level = patch.subGrowl * growlAmp;
                for (int h = 2; h <= 12 && level > 1.0e-4f; ++h)
                {
                    if ((float) h * subHz > 0.45f * sampleRate) break;
                    wave += level * current;
                    const float next = twoCos * current - previous;
                    previous = current;
                    current = next;
                    level *= patch.subGrowlTilt;
                }
                growlAmp *= growlRadius;
            }
            sub = wave * subAmp * patch.subLevel;
            subPhase += kTwoPi * subHz / sampleRate;
            if (subPhase > kTwoPi) subPhase -= kTwoPi;
            subAmp *= subRadius;
        }

        // Envelope: attack ramp, optional gate, release.
        if (! released)
        {
            envelope = std::min (1.0f, envelope + attackStep);
            if (patch.gate > 0.0f && t >= patch.gate)
                released = true;
        }
        else
        {
            envelope *= releaseFactor;
        }
        if (choking)
            envelope *= chokeFactor;

        float shaped = envelope * gain * bobGain;
        const float fadeTime = std::min (0.25f, 0.2f * patch.maxLength);
        if (t > patch.maxLength - fadeTime)
            shaped *= smoothstep ((patch.maxLength - t) / fadeTime);
        if (dryFinished)
            bodyLeft = bodyRight = fizzL = fizzR = click = sub = 0.0f;
        left = (bodyLeft + fizzL + click + sub) * shaped;
        right = (bodyRight + fizzR + click + sub) * shaped;
        if (patch.roomLevel > 0.0f)
        {
            float rl, rr;
            room.process (0.5f * (bodyLeft + bodyRight + fizzL + fizzR) * shaped + click * shaped, rl, rr);
            left += patch.roomLevel * rl;
            right += patch.roomLevel * rr;
        }

        ++sampleIndex;
        if (! dryFinished && (((released || choking) && envelope < 1.0e-4f) || t > patch.maxLength))
        {
            dryFinished = true;
            tailLeft = patch.roomLevel > 0.0f ? room.tailSamples() : 0;
        }
        if (dryFinished && tailLeft-- <= 0)
            active = false;
        if (! std::isfinite (left) || ! std::isfinite (right))
        {
            left = right = 0.0f;
            active = false;
        }
    }

private:
    static constexpr int kControlInterval = 16;
    // Calibrated so metal.amount = 1 lands near the body's level (see tools/round5).
    static constexpr float kMetalStrikeGain = 57.0f, kMetalNoiseGain = 3.0f, kMetalFeedGain = 2.5f;

    float velocityDepth() const noexcept { return (1.0f - patch.velPitch) + patch.velPitch * velocity * hitDepth; }

    // Fold velocity and per-hit variation into this voice's copy of the patch.
    void applyVelocityAndVariation() noexcept
    {
        const float d = velocity - 0.7f;  // 0.7 = the patch as written
        auto scale = [] (float amount, float d2, float lo, float hi) { return clampf (1.0f + 2.0f * amount * d2, lo, hi); };
        const float noiseScaleV = scale (patch.velNoise, d, 0.05f, 3.0f);
        patch.clickLevel *= noiseScaleV;
        patch.fizzLevel *= noiseScaleV;
        patch.noiseExcite *= noiseScaleV;
        patch.haloAmount *= noiseScaleV;
        numBounces = 0;
        bounceIndex = 0;
        if (patch.rebound > 0.0f)
        {
            float ms = patch.reboundMs * (0.8f + 0.4f * velocity);
            if (patch.variation > 0.0f) ms *= 1.0f + 0.15f * patch.variation * rng.bipolar();
            float amp = patch.rebound * (0.5f + 0.5f * velocity);
            // A bouncing object: each contact comes sooner and lighter by the
            // restitution factor (heard as bouncing when regular, Warren & Verbrugge).
            float when = ms, gap = ms;
            const int count = std::min (std::max (patch.bounces, 1), kMaxBounces);
            for (int i = 0; i < count; ++i)
            {
                bounceSample[(size_t) i] = (int64_t) (when * 0.001f * sampleRate);
                bounceAmp[(size_t) i] = amp;
                ++numBounces;
                gap *= patch.restitution;
                amp *= patch.restitution;
                when += gap;
            }
        }
        patch.amDepth = clampf (patch.amDepth * scale (patch.velAm, d, 0.0f, 2.5f), 0.0f, 1.0f);
        if (patch.velTension != 0.0f)
            patch.tensionSemis += patch.velTension * clampf (d / 0.3f, -1.0f, 1.0f) * 0.5f + patch.velTension * 0.5f;
        const float decayScale = scale (patch.velDecay, d, 0.3f, 2.5f);
        if (patch.gate > 0.0f)
            patch.gate *= std::sqrt (decayScale);
        const float widthScale = scale (patch.velWidth, d, 0.2f, 1.8f);
        hitDepth = 1.0f;
        const float v = patch.variation;
        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            auto& g = patch.groups[(size_t) gi];
            g.t60 *= decayScale;
            (void) 0;
            g.phaseWidth = clampf (g.phaseWidth * widthScale, 0.0f, 1.5f);
            g.width = clampf (g.width * widthScale, 0.0f, 1.0f);
            if (v > 0.0f)
            {
                g.t60 *= 1.0f + 0.25f * v * rng.bipolar();
                g.levelDb += 1.5f * v * rng.bipolar();
                if (g.ringHz > 0.0f) g.ringHz *= 1.0f + 0.12f * v * rng.bipolar();
                if (g.formantHz > 0.0f) g.formantHz *= 1.0f + 0.12f * v * rng.bipolar();
                g.lag *= 1.0f + 0.3f * v * rng.bipolar();
                g.flutterHz *= 1.0f + 0.3f * v * rng.bipolar();
            }
        }
        patch.metal.t60 *= decayScale;
        patch.metal.noise *= noiseScaleV;
        if (v > 0.0f)
        {
            if (patch.metal.amount > 0.0f)
            {
                // Only patches that use the network draw these, so older patches render exactly as before.
                patch.metal.baseHz *= 1.0f + 0.02f * v * rng.bipolar();
                patch.metal.t60 *= 1.0f + 0.2f * v * rng.bipolar();
            }
            hitDepth = 1.0f + 0.15f * v * rng.bipolar();
            patch.sweepTime *= 1.0f + 0.12f * v * rng.bipolar();
            patch.amHz *= 1.0f + 0.04f * v * rng.bipolar();
            patch.recoilHz *= 1.0f + 0.15f * v * rng.bipolar();
            patch.fizzHz *= 1.0f + 0.15f * v * rng.bipolar();
            patch.hardness = clampf (patch.hardness + 0.08f * v * rng.bipolar(), 0.0f, 1.0f);
        }
    }

    Sweep currentSweep() const noexcept
    {
        Sweep s;
        s.semis = patch.sweepSemis * velocityDepth();
        s.time = patch.sweepTime;
        s.curve = patch.sweepCurve;
        return s;
    }

    // Everything in the pitch gesture except the sweep.
    float gestureSemitones (float t) const noexcept
    {
        float semis = 0.0f;
        const float depth = velocityDepth();
        if (patch.driftSemis != 0.0f)
            semis += patch.driftSemis * clampf (t / std::max (patch.driftTime, 1.0e-3f), 0.0f, 1.0f);
        if (patch.flipTime > 0.0f && patch.flipSemis != 0.0f)
        {
            const int steps = std::max (1, patch.flipSteps);
            for (int i = 0; i < steps; ++i)
                semis += (patch.flipSemis / (float) steps)
                         * smoothstep ((t - patch.flipTime - (float) i * patch.flipStepTime) / 0.006f);
        }
        if (patch.tensionSemis != 0.0f)
            semis += patch.tensionSemis * velocity * velocity * tension;
        if (patch.recoilSemis != 0.0f)
            semis += patch.recoilSemis * depth * std::exp (-t / std::max (patch.recoilTime, 1.0e-3f))
                     * std::sin (kTwoPi * patch.recoilHz * t);
        return semis;
    }

    void updateControl (float t) noexcept
    {
        if (patch.tensionSemis != 0.0f)
        {
            const float e = banks[0].energy();
            if (initialEnergy < 0.0f && sampleIndex > 0)
                initialEnergy = std::max (e, 1.0e-12f);
            tension = initialEnergy > 0.0f ? clampf (std::sqrt (e / initialEnergy), 0.0f, 1.5f) : 1.0f;
        }
        const float gesture = gestureSemitones (t);
        const Sweep sweep = currentSweep();
        // Buoyant bob: a damped heave after the strike, felt as a slight
        // pitch, level and height sway.
        float bob = 0.0f;
        if (patch.bobTime > 0.0f && patch.bobHz > 0.0f && (patch.bobSemis != 0.0f || patch.bobLevelDb != 0.0f || patch.bobLiftDb != 0.0f))
            bob = std::exp (-t / patch.bobTime) * std::sin (kTwoPi * patch.bobHz * t) * velocityDepth();
        bobGain = patch.bobLevelDb != 0.0f ? dbToGain (patch.bobLevelDb * bob) : 1.0f;
        const float lift = (patch.liftDb != 0.0f ? dbToGain (patch.liftDb) - 1.0f : 0.0f)
                           + (patch.bobLiftDb != 0.0f ? dbToGain (patch.bobLiftDb * std::fabs (bob)) * (bob >= 0.0f ? 1.0f : -1.0f) - (bob >= 0.0f ? 1.0f : -1.0f) : 0.0f);
        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            const auto& g = patch.groups[(size_t) gi];
            float groupSemis = noteOffset + patch.bobSemis * bob;
            if (g.vibratoSemis != 0.0f)
                groupSemis += g.vibratoSemis * std::sin (kTwoPi * g.vibratoHz * t);
            Motion motion;
            motion.bloom = g.spaceBloom > 0.0f ? smoothstep (t / g.spaceBloom) : 1.0f;
            motion.itdSeconds = clampf (g.itdMs, -1.0f, 1.0f) * 0.001f;
            motion.driftRadians = g.spaceDrift * 0.5f * kPi;
            motion.driftHz = g.spaceDriftHz;
            motion.flutter = g.flutterDb > 0.0f ? clampf (1.0f - dbToGain (-g.flutterDb), 0.0f, 0.9f) : 0.0f;
            motion.flutterHz = g.flutterHz;
            motion.buoyancy = g.buoyancy;
            motion.lift = lift;
            if (g.disperseMs > 0.0f)
                banks[(size_t) gi].arrive (t, g.strike);
            banks[(size_t) gi].setPitch (gesture, sweep, t, groupSemis, g.jitterCents * 0.01f, g.jitterHz, motion);
            banks[(size_t) gi].flushDenormals();
        }
        const float drop = patch.subDropSemis * std::exp (-t / std::max (patch.subDropTime, 1.0e-3f));
        subHz = patch.subHz * semitonesToRatio (drop + noteOffset);

        if (coupleOn)
        {
            const auto& c = patch.couple;
            float rate = c.rateHz, bal = c.balance;
            if (c.morphTime > 0.0f)
            {
                const float u = smoothstep (t / c.morphTime);
                if (c.endRateHz >= 0.0f) rate = lerp (c.rateHz, c.endRateHz, u);
                if (c.endBalance > -2.0f) bal = lerp (c.balance, c.endBalance, u);
            }
            // rho = impedance of b over a; transmission is greatest when they match.
            const float rho = std::pow (10.0f, 1.5f * clampf (bal, -1.0f, 1.0f));
            const float transmission = 4.0f * rho / ((1.0f + rho) * (1.0f + rho));
            const float theta = kTwoPi * std::max (rate, 0.0f) / sampleRate * transmission;
            coupleCos = std::cos (theta);
            coupleSin = std::sin (theta);
            coupleScale = std::sqrt (rho);
            partnerGain = dbToGain (c.partnerDb);
        }
        if (metalOn)
            metal.setPitch (noteOffset, gesture + currentSweep().at (t), t);
    }

    void buildPartials() noexcept
    {
        std::array<std::array<Partial, PartialBank::kMaxPartials>, Patch::kMaxGroups> partials {};
        std::array<int, Patch::kMaxGroups> counts {};
        // Velocity makes the strike harder: shorter contact, more high partials.
        // The patch's hardness sets the mallet's corner at the reference velocity
        // (0.7); velocity then moves that corner by octaves, so soft hits are
        // clearly darker and hard hits open fully, whatever the patch.
        const float baseCorner = std::min (1500.0f * std::exp2 (8.0f * clampf (patch.hardness, 0.0f, 1.0f)), 20000.0f);
        malletCorner = baseCorner * std::exp2 (12.0f * patch.velBright * (velocity - 0.7f));

        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            const auto& g = patch.groups[(size_t) gi];
            auto& list = partials[(size_t) gi];
            int& n = counts[(size_t) gi];
            const float referenceHz = std::max (g.baseHz, 1.0f);

            float strikeHere = g.strikePos, pickupHere = g.pickupPos;
            if (g.positionDepth > 0.0f && patch.variation > 0.0f && strikeHere > 0.0f)
                strikeHere = clampf (strikeHere + 0.03f * patch.variation * rng.bipolar(), 0.01f, 0.5f);

            auto add = [&] (float hz, float levelDb, float lowestHz)
            {
                if (n >= PartialBank::kMaxPartials || hz <= 0.0f)
                    return;
                Partial p;
                if (patch.variation > 0.0f)
                    hz *= semitonesToRatio (0.06f * patch.variation * rng.bipolar());
                p.ratio = hz / referenceHz;
                const float octaves = std::log2 (std::max (hz, 1.0f) / std::max (lowestHz, 1.0f));
                const float mallet = 1.0f / (1.0f + (hz / malletCorner) * (hz / malletCorner));
                float formant = 0.0f;
                if (g.formantHz > 0.0f && g.formantDb != 0.0f)
                {
                    const float d = std::log2 (std::max (hz, 1.0f) / g.formantHz) / std::max (g.formantOctaves, 0.05f);
                    formant = g.formantDb * std::exp (-0.5f * d * d);
                }
                p.gain = dbToGain (g.levelDb + levelDb + g.tiltDbPerOctave * octaves + formant) * mallet;
                if (g.positionDepth > 0.0f && (strikeHere > 0.0f || pickupHere > 0.0f))
                {
                    // Mode shape of a simple bar/tube at the strike and pickup points
                    // (Fletcher & Rossing); mean of |sin| is 2/pi, so divide it out.
                    const float n = g.kind == GroupKind::Harmonic ? hz / std::max (g.baseHz, 1.0f) : hz / std::max (lowestHz, 1.0f);
                    float w = 1.0f;
                    if (strikeHere > 0.0f) w *= std::fabs (std::sin (kPi * n * strikeHere)) / 0.6366f;
                    if (pickupHere > 0.0f) w *= std::fabs (std::sin (kPi * n * pickupHere)) / 0.6366f;
                    p.gain *= (1.0f - g.positionDepth) + g.positionDepth * std::max (w, 0.08f);
                }
                if (patch.airHz > 0.0f)
                {
                    const float r = hz / patch.airHz;
                    p.gain /= std::sqrt (1.0f + r * r * r * r);
                }
                float ring = 1.0f;
                if (g.ringHz > 0.0f && g.ringBoost != 1.0f)
                {
                    const float d = std::log2 (std::max (hz, 1.0f) / g.ringHz) / std::max (g.ringOctaves, 0.02f);
                    ring = 1.0f + (g.ringBoost - 1.0f) * std::exp (-0.5f * d * d);
                }
                if (patch.variation > 0.0f)
                    p.gain *= 1.0f + 0.25f * patch.variation * rng.bipolar();
                const float decayScatter = patch.variation > 0.0f ? 1.0f + 0.15f * patch.variation * rng.bipolar() : 1.0f;
                p.t60 = clampf (decayScatter * g.t60 * ring * std::pow (std::max (hz, 1.0f) / std::max (lowestHz, 1.0f), -g.decayTilt), 0.005f, 30.0f);
                p.pan = clampf (g.pan + (hz < patch.lowMonoHz ? 0.0f : g.width * rng.bipolar()), -1.0f, 1.0f);
                p.pitchFollow = g.pitchFollow;
                p.phase = g.phaseSpread * kTwoPi * rng.uniform();
                p.lag = g.lag * std::max (0.0f, octaves);
                p.sweepScale = 1.0f + g.fan * std::max (0.0f, octaves);
                p.stereoPhase = hz < patch.lowMonoHz ? 0.0f : g.phaseWidth * 0.5f * kPi * rng.bipolar();
                {
                    // Mode shape at the coupling junction (1 = every mode moves there equally).
                    const float jpos = gi == patch.couple.a ? patch.couple.posA : gi == patch.couple.b ? patch.couple.posB : 0.0f;
                    const float modeN = g.kind == GroupKind::Harmonic ? hz / std::max (g.baseHz, 1.0f) : hz / std::max (lowestHz, 1.0f);
                    p.junction = jpos > 0.0f ? std::max (std::fabs (std::sin (kPi * modeN * jpos)), 0.05f) : 1.0f;
                }
                list[(size_t) n++] = p;
                if (g.pairLevel > 0.0f && g.pairHz != 0.0f && n < PartialBank::kMaxPartials)
                {
                    Partial q = p;
                    q.ratio = (hz + g.pairHz) / referenceHz;
                    q.gain = p.gain * g.pairLevel;
                    q.pan = clampf (g.pan + (hz < patch.lowMonoHz ? 0.0f : g.width * rng.bipolar()), -1.0f, 1.0f);
                    q.phase = g.phaseSpread * kTwoPi * rng.uniform();
                    q.stereoPhase = hz < patch.lowMonoHz ? 0.0f : g.phaseWidth * 0.5f * kPi * rng.bipolar();
                    q.isPair = true;
                    list[(size_t) n++] = q;
                }
            };

            if (g.kind == GroupKind::Harmonic)
            {
                const bool oddOnly = g.evenGain <= 1.0e-4f;
                const float evenDb = oddOnly ? 0.0f : 20.0f * std::log10 (g.evenGain);
                const float lowestHz = g.baseHz * std::pow ((float) std::max (g.firstHarmonic, 1), 1.0f + g.stretch);
                int made = 0;
                for (int k = std::max (g.firstHarmonic, 1); made < g.count && k < 2048; ++k)
                {
                    if (oddOnly && k % 2 == 0)
                        continue;
                    const float hz = g.baseHz * std::pow ((float) k, 1.0f + g.stretch);
                    add (hz, k % 2 == 0 ? evenDb : 0.0f, lowestHz);
                    ++made;
                }
            }
            else if (g.kind == GroupKind::Cluster)
            {
                const float first = g.baseHz - 0.5f * (float) (g.count - 1) * g.spacingHz;
                float hz = first;
                for (int k = 0; k < g.count; ++k)
                {
                    add (hz, -3.0f * rng.uniform(), std::max (first, 20.0f));
                    hz += g.spacingHz * (1.0f + g.spacingJitter * (rng.uniform() - 0.5f));
                }
            }
            else
            {
                for (int k = 0; k < g.numRatios; ++k)
                    add (g.baseHz * g.ratios[(size_t) k], g.ratioLevelsDb[(size_t) k], g.baseHz * g.ratios[0]);
            }
        }

        // Resonant peaks: rank each group's modes by the energy they will carry
        // (level squared times ring time). The strongest ring longer and a little
        // louder, the filler shorter, so the peaks stand out more as the sound
        // rings; the group's strike energy is kept, so the onset balance stays.
        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            const auto& g = patch.groups[(size_t) gi];
            auto& list = partials[(size_t) gi];
            const int n = counts[(size_t) gi];
            if (n == 0 || (g.peakFocus <= 0.0f && g.halo <= 0.0f)) continue;
            // A peak is a mode that carries more energy than every other mode
            // within peakSpan octaves: each region keeps its own strongest line.
            std::array<bool, PartialBank::kMaxPartials> isPeak {};
            int primaries = 0;
            for (int k = 0; k < n; ++k)
            {
                const auto& pk = list[(size_t) k];
                if (pk.isPair) continue;
                ++primaries;
                const float ek = pk.gain * pk.gain * pk.t60;
                bool strongest = true;
                for (int j = 0; j < n && strongest; ++j)
                {
                    const auto& pj = list[(size_t) j];
                    if (j == k || pj.isPair) continue;
                    if (std::fabs (std::log2 (pj.ratio / pk.ratio)) < g.peakSpan && pj.gain * pj.gain * pj.t60 > ek)
                        strongest = false;
                }
                isPeak[(size_t) k] = strongest;
            }
            const bool ranked = primaries >= 4;
            float before = 0.0f, after = 0.0f;
            bool peakNow = false;
            const float f = ranked ? clampf (g.peakFocus, 0.0f, 1.0f) : 0.0f;
            for (int k = 0; k < n; ++k)
            {
                auto& p = list[(size_t) k];
                if (! p.isPair) peakNow = isPeak[(size_t) k] && p.ratio * std::max (g.baseHz, 1.0f) >= g.peakFromHz;   // a partner follows its main partial
                before += p.gain * p.gain;
                if (f > 0.0f)
                {
                    p.t60 = clampf (p.t60 * (peakNow ? 1.0f + 0.7f * f : 1.0f - 0.25f * f), 0.005f, 30.0f);
                    p.gain *= peakNow ? 1.0f + 0.25f * f : 1.0f - 0.1f * f;
                }
                p.halo = peakNow ? g.halo : 0.0f;
                after += p.gain * p.gain;
            }
            if (after > 0.0f)
            {
                const float keep = std::sqrt (before / after);
                for (int k = 0; k < n; ++k) list[(size_t) k].gain *= keep;
            }
        }

        // Dispersion: bending waves travel faster at higher frequency, so the
        // highest partial is struck first and the lowest disperseMs later.
        for (int gi = 0; gi < patch.numGroups; ++gi)
        {
            const auto& g = patch.groups[(size_t) gi];
            const int n = counts[(size_t) gi];
            if (g.disperseMs <= 0.0f || n == 0) continue;
            auto& list = partials[(size_t) gi];
            float lo = 1.0e9f, hi = 0.0f;
            for (int k = 0; k < n; ++k) { lo = std::min (lo, list[(size_t) k].ratio); hi = std::max (hi, list[(size_t) k].ratio); }
            const float span = std::sqrt (hi / std::max (lo, 1.0e-6f)) - 1.0f;
            for (int k = 0; k < n; ++k)
            {
                const float x = span > 0.0f ? (std::sqrt (hi / std::max (list[(size_t) k].ratio, 1.0e-6f)) - 1.0f) / span : 0.0f;
                list[(size_t) k].arrival = 0.001f * g.disperseMs * x;
            }
        }

        // Normalise so a patch's loudness doesn't depend on how many partials it has.
        float sumSquares = 0.0f;
        for (int gi = 0; gi < patch.numGroups; ++gi)
            for (int k = 0; k < counts[(size_t) gi]; ++k)
                sumSquares += partials[(size_t) gi][(size_t) k].gain * partials[(size_t) gi][(size_t) k].gain;
        const float norm = sumSquares > 0.0f ? 0.5f / std::sqrt (sumSquares) : 0.0f;
        for (int gi = 0; gi < Patch::kMaxGroups; ++gi)
        {
            for (int k = 0; k < counts[(size_t) gi]; ++k)
                partials[(size_t) gi][(size_t) k].gain *= norm;
            banks[(size_t) gi].configure (partials[(size_t) gi].data(), gi < patch.numGroups ? counts[(size_t) gi] : 0,
                                          std::max (patch.groups[(size_t) gi].baseHz, 1.0f), voiceSeed * 31u + (uint32_t) gi);
        }
    }

    std::array<PartialBank, Patch::kMaxGroups> banks;
    Patch patch;
    Rng rng, noiseLeft, noiseRight;
    StateVariableFilter clickFilter, fizzFilterLeft, fizzFilterRight;
    float sampleRate = 48000.0f, velocity = 1.0f, noteOffset = 0.0f, gain = 1.0f, hitDepth = 1.0f;
    float envelope = 0.0f, attackStep = 1.0f, releaseFactor = 0.99f;
    float subPhase = 0.0f, subHz = 55.0f, subAmp = 1.0f, subRadius = 0.999f, growlAmp = 1.0f, growlRadius = 0.999f;
    float fizzAmp = 1.0f, fizzRadius = 0.999f, exciteAmp = 1.0f, exciteRadius = 0.99f, noiseScale = 0.05f, noiseRateScale = 1.0f;
    float clickAmp = 1.0f, clickRadius = 0.99f, amPhase = 0.0f;
    float chopPhase = 0.0f, chopGain = 1.0f, chopSmoothing = 0.01f;
    float initialEnergy = -1.0f, tension = 1.0f, chokeFactor = 0.99f;
    uint32_t voiceSeed = 1u;
    Rng haloNoise;
    float haloAmp = 1.0f, haloRadius = 0.99f, haloScale = 0.0f, reboundAmp = 0.0f;
    bool haloActive = false;
    static constexpr int kMaxBounces = 8;
    std::array<int64_t, kMaxBounces> bounceSample {};
    std::array<float, kMaxBounces> bounceAmp {};
    int numBounces = 0, bounceIndex = 0, tailLeft = 0;
    bool dryFinished = false;
    float bobGain = 1.0f;
    EarlyReflections room;
    // Round 5
    MetalNetwork metal;
    Rng metalNoise;
    bool coupleOn = false, metalOn = false;
    float coupleCos = 1.0f, coupleSin = 0.0f, coupleScale = 1.0f, partnerGain = 1.0f;
    int metalContactLen = 12;
    float malletCorner = 8000.0f, metalStrikeLp = 0.0f, metalStrikeCoef = 0.5f, metalNoiseAmp = 1.0f, metalNoiseRadius = 0.99f;
    int64_t sampleIndex = 0;
    int controlCountdown = 0;
    bool active = false, released = false, choking = false;
};
} // namespace ls
