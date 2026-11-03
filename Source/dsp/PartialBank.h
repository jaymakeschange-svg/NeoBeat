// Liquid Swing 4 — bank of up to 128 decaying partials.
//
// Each partial is a damped complex rotation (a resonator). Struck by an
// impulse it rings as a decaying sine; struck by noise it rings as filtered
// "metallic noise". Its frequency can move continuously (pitch gestures)
// without clicks, because the state keeps its phase while the rotation
// angle changes. Every partial decays on its own (radius < 1), so the bank
// cannot run away, and partials approaching Nyquist fade out instead of
// aliasing.
#pragma once

#include "Core.h"
#include <array>
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
 #include <emmintrin.h>
 #define LS_HAS_SSE2 1
#endif

namespace ls
{
struct Partial
{
    float ratio = 1.0f;        // frequency relative to the bank's base frequency
    float gain = 1.0f;         // how strongly the strike excites this partial
    float t60 = 1.0f;          // seconds for this partial to fall by 60 dB
    float pan = 0.0f;          // -1 (left) .. +1 (right)
    float pitchFollow = 1.0f;  // share of the global pitch gesture this partial follows
    float phase = 0.0f;        // start phase in radians (0 = every partial starts together)
    float lag = 0.0f;          // seconds this partial trails the pitch sweep (elastic stretch)
    float stereoPhase = 0.0f;  // right channel hears this partial shifted by this many radians (width without panning)
    float sweepScale = 1.0f;   // how far this partial travels in the sweep, relative to the gesture (fan)
    float halo = 0.0f;         // how strongly the resonance halo noise feeds this partial (relative to gain)
    bool isPair = false;       // a beating partner added after its main partial
    float arrival = 0.0f;      // seconds after the strike that the strike reaches this partial (dispersion)
    float junction = 1.0f;     // how much this mode moves at the coupling point (mode shape there)
};

// The sweep part of a pitch gesture: starts `semis` away and arrives at 0
// after `time`; `curve` > 0 arrives late and fast, < 0 arrives early.
struct Sweep
{
    float semis = 0.0f, time = 0.2f, curve = 0.0f;
    float at (float t) const noexcept
    {
        if (semis == 0.0f) return 0.0f;
        const float u = clampf (t / std::max (time, 1.0e-3f), 0.0f, 1.0f);
        const float shape = std::fabs (curve) < 1.0e-3f ? u : (std::exp (curve * u) - 1.0f) / (std::exp (curve) - 1.0f);
        return semis * (1.0f - shape);
    }
};

// Control-rate movement of a group in space and level ("automodulation"):
// the strike starts centred and blooms out to each partial's place, the
// image keeps drifting, small time offsets place the group physically, and
// every partial's level flutters slowly on its own.
struct Motion
{
    float bloom = 1.0f;        // 0 = all partials centred (in phase), 1 = at their stereo targets
    float itdSeconds = 0.0f;   // right channel late by this much (negative = early): placement
    float driftRadians = 0.0f; // how far each partial's stereo phase wanders
    float driftHz = 0.0f;
    float flutter = 0.0f;      // level wander of each partial (0..0.9 as a gain fraction)
    float flutterHz = 0.0f;
    float buoyancy = 0.0f;     // xi in f(t) = f0 (1 + xi * beta * t): every partial rises as it decays (van den Doel)
    float lift = 0.0f;         // level change of the 6-9 kHz elevation band (fraction, +- while bobbing)
};

class PartialBank
{
public:
    static constexpr int kMaxPartials = 128;

    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        clear();
    }

    void clear() noexcept
    {
        re.fill (0.0f);
        im.fill (0.0f);
    }

    void configure (const Partial* partials, int count, float newBaseHz, uint32_t seed = 1u) noexcept
    {
        numPartials = std::min (count, kMaxPartials);
        baseHz = newBaseHz;
        Rng random (hash32 (seed) | 1u);
        Rng motionRandom (hash32 (seed * 5u + 3u) | 1u);
        for (int k = 0; k < kMaxPartials; ++k)
        {
            if (k < numPartials)
            {
                const auto& p = partials[k];
                ratio[k] = std::max (p.ratio, 0.0f);
                follow[k] = p.pitchFollow;
                lag[k] = std::max (p.lag, 0.0f);
                sweepScale[k] = p.sweepScale;
                input[k] = p.gain * std::cos (p.phase);
                inputIm[k] = p.gain * std::sin (p.phase);
                haloIn[k] = p.gain * p.halo;
                radius[k] = t60ToRadius (p.t60, sampleRate);
                const float angle = (clampf (p.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
                panLeft[k] = std::cos (angle);
                panRight[k] = std::sin (angle);
                rightIm[k] = std::cos (p.stereoPhase);
                rightRe[k] = std::sin (p.stereoPhase);
                stereoTarget[k] = p.stereoPhase;
                arrival[k] = std::max (p.arrival, 0.0f);
                decayRate[k] = 6.907755f / std::max (p.t60, 1.0e-3f);
                junction[k] = std::max (p.junction, 0.0f);
            }
            else
            {
                ratio[k] = 1.0f; follow[k] = 0.0f; lag[k] = 0.0f; sweepScale[k] = 1.0f; input[k] = inputIm[k] = haloIn[k] = 0.0f; radius[k] = 0.0f;
                panLeft[k] = panRight[k] = 0.0f;
                rightIm[k] = 1.0f;
                rightRe[k] = 0.0f;
                stereoTarget[k] = 0.0f;
                arrival[k] = 0.0f;
                decayRate[k] = 0.0f;
                junction[k] = 0.0f;
            }
            arrived[k] = 0.0f;  // only used by groups struck through arrive()
            // Every partial wanders at its own slow rate and phase.
            jitterRate[k] = 0.6f + 0.8f * random.uniform();
            jitterPhase[k] = kTwoPi * random.uniform();
            driftRate[k] = 0.5f + motionRandom.uniform();
            driftPhase[k] = kTwoPi * motionRandom.uniform();
            flutterRate[k] = 0.5f + motionRandom.uniform();
            flutterPhase[k] = kTwoPi * motionRandom.uniform();
            cosine[k] = sine[k] = outLeft[k] = outRightIm[k] = outRightRe[k] = 0.0f;
        }
        // The coupling point as a unit vector over the modes, so the coupling
        // rotation below is exactly energy-preserving.
        float jj = 0.0f;
        for (int k = 0; k < numPartials; ++k) jj += junction[k] * junction[k];
        const float jn = jj > 0.0f ? 1.0f / std::sqrt (jj) : 0.0f;
        for (int k = 0; k < kMaxPartials; ++k) junction[k] *= jn;
        clear();
    }

    // Coupling (two-way, Chromaphone-style): how fast the bank's modes are
    // moving at the coupling point, and a push along the same mode shape.
    float junctionVelocity() const noexcept
    {
        float v = 0.0f;
        for (int k = 0; k < numPartials; ++k) v += junction[k] * re[k];
        return v;
    }

    void pushAtJunction (float amount) noexcept
    {
        for (int k = 0; k < numPartials; ++k) re[k] += amount * junction[k];
    }

    // Control rate: move every partial to the current pitch.
    //   gestureSemis : the patch's pitch gesture without its sweep, scaled by each partial's follow
    //   sweep        : the sweep, which each partial follows after its own lag
    //   groupSemis   : added to every partial equally (note, group vibrato)
    //   jitterSemis  : depth of each partial's own slow wander, at jitterHz
    void setPitch (float gestureSemis, const Sweep& sweep, float timeSeconds, float groupSemis = 0.0f,
                   float jitterSemis = 0.0f, float jitterHz = 0.0f, const Motion& motion = Motion()) noexcept
    {
        const bool moving = motion.bloom != 1.0f || motion.itdSeconds != 0.0f || motion.driftRadians != 0.0f;
        const float driftOmega = kTwoPi * motion.driftHz * timeSeconds;
        const float flutterOmega = kTwoPi * motion.flutterHz * timeSeconds;
        const float nyquistFadeStart = 0.40f * sampleRate;
        const float nyquistFadeEnd = 0.46f * sampleRate;
        const float jitterOmega = kTwoPi * jitterHz * timeSeconds;
        const float sweepNow = sweep.at (timeSeconds);
        for (int k = 0; k < numPartials; ++k)
        {
            const float sweepHere = lag[k] > 0.0f ? sweep.at (timeSeconds - lag[k]) : sweepNow;
            float semis = follow[k] * (gestureSemis + sweepScale[k] * sweepHere) + groupSemis;
            if (jitterSemis != 0.0f)
                semis += jitterSemis * std::sin (jitterPhase[k] + jitterOmega * jitterRate[k]);
            if (motion.buoyancy > 0.0f)
            {
                // A rising bubble's pitch climbs in proportion to its damping:
                // short-lived (usually high) partials float up fastest.
                const float sinceArrival = std::max (0.0f, timeSeconds - arrival[k]);
                semis += std::min (12.0f, 12.0f * std::log2 (1.0f + motion.buoyancy * decayRate[k] * sinceArrival));
            }
            const float hz = baseHz * ratio[k] * semitonesToRatio (semis);
            float taper = 1.0f;
            if (hz > nyquistFadeStart)
                taper = 1.0f - smoothstep ((hz - nyquistFadeStart) / (nyquistFadeEnd - nyquistFadeStart));
            if (hz < 20.0f)
                taper *= smoothstep ((hz - 12.0f) / 8.0f);
            const float omega = kTwoPi * std::min (hz, nyquistFadeEnd) / sampleRate;
            cosine[k] = radius[k] * std::cos (omega);
            sine[k] = radius[k] * std::sin (omega);
            if (motion.flutter > 0.0f)
                taper *= 1.0f + motion.flutter * std::sin (flutterPhase[k] + flutterOmega * flutterRate[k]);
            if (motion.lift != 0.0f)
            {
                // Energy in the 6-9 kHz pinna-notch region is heard as height (Zonooz et al. 2019).
                const float d = std::log2 (std::max (hz, 1.0f) / 7500.0f) / 0.45f;
                taper *= std::max (0.0f, 1.0f + motion.lift * std::exp (-0.5f * d * d));
            }
            float rIm = rightIm[k], rRe = rightRe[k];
            if (moving)
            {
                float delta = motion.bloom * stereoTarget[k] + kTwoPi * hz * motion.itdSeconds;
                if (motion.driftRadians != 0.0f && stereoTarget[k] != 0.0f)
                    delta += motion.driftRadians * std::sin (driftPhase[k] + driftOmega * driftRate[k]);
                rIm = std::cos (delta);
                rRe = std::sin (delta);
            }
            outLeft[k] = panLeft[k] * taper;
            outRightIm[k] = panRight[k] * rIm * taper;
            outRightRe[k] = panRight[k] * rRe * taper;
        }
    }

    // Dispersion: deliver the strike to every partial whose arrival time has
    // come (called at control rate; arrival 0 partials are struck at once).
    void arrive (float timeSeconds, float amount) noexcept
    {
        for (int k = 0; k < numPartials; ++k)
        {
            if (arrived[k] > 0.5f || arrival[k] > timeSeconds) continue;
            re[k] += amount * input[k];
            im[k] += amount * inputIm[k];
            arrived[k] = 1.0f;
        }
    }

    // Per sample: strike the bank with `excitation`, return the stereo ring.
    // An impulse of 1.0 starts partial k as a sine of amplitude gain[k].
    void process (float excitation, float amModulation, float& left, float& right) noexcept
    {
        process (excitation, 0.0f, amModulation, left, right);
    }

    // As above, plus `halo`: a second (noise) excitation that only reaches the
    // partials chosen as the patch's resonant peaks.
    void process (float excitation, float halo, float amModulation, float& left, float& right) noexcept
    {
        // Partials are processed in groups of four; unused slots have zero
        // gain and zero state, so padding to a multiple of four is harmless.
        const int padded = (numPartials + 3) & ~3;
#if LS_HAS_SSE2
        __m128 accLeft = _mm_setzero_ps(), accRight = _mm_setzero_ps();
        const __m128 x = _mm_set1_ps (excitation);
        const __m128 h = _mm_set1_ps (halo);
        for (int k = 0; k < padded; k += 4)
        {
            const __m128 a = _mm_load_ps (re.data() + k), b = _mm_load_ps (im.data() + k);
            const __m128 c = _mm_load_ps (cosine.data() + k), s = _mm_load_ps (sine.data() + k);
            const __m128 newRe = _mm_add_ps (_mm_add_ps (_mm_sub_ps (_mm_mul_ps (a, c), _mm_mul_ps (b, s)),
                                                         _mm_mul_ps (x, _mm_load_ps (input.data() + k))),
                                             _mm_mul_ps (h, _mm_load_ps (haloIn.data() + k)));
            const __m128 newIm = _mm_add_ps (_mm_add_ps (_mm_mul_ps (a, s), _mm_mul_ps (b, c)),
                                             _mm_mul_ps (x, _mm_load_ps (inputIm.data() + k)));
            _mm_store_ps (re.data() + k, newRe);
            _mm_store_ps (im.data() + k, newIm);
            accLeft = _mm_add_ps (accLeft, _mm_mul_ps (newIm, _mm_load_ps (outLeft.data() + k)));
            accRight = _mm_add_ps (accRight, _mm_add_ps (_mm_mul_ps (newIm, _mm_load_ps (outRightIm.data() + k)),
                                                         _mm_mul_ps (newRe, _mm_load_ps (outRightRe.data() + k))));
        }
        alignas (16) float l4[4], r4[4];
        _mm_store_ps (l4, accLeft);
        _mm_store_ps (r4, accRight);
        left = ((l4[0] + l4[1]) + (l4[2] + l4[3])) * amModulation;
        right = ((r4[0] + r4[1]) + (r4[2] + r4[3])) * amModulation;
#else
        float l4[4] = {}, r4[4] = {};
        for (int k = 0; k < padded; ++k)
        {
            const float a = re[k], b = im[k];
            re[k] = a * cosine[k] - b * sine[k] + excitation * input[k] + halo * haloIn[k];
            im[k] = a * sine[k] + b * cosine[k] + excitation * inputIm[k];
            l4[k & 3] += im[k] * outLeft[k];
            r4[k & 3] += im[k] * outRightIm[k] + re[k] * outRightRe[k];
        }
        left = ((l4[0] + l4[1]) + (l4[2] + l4[3])) * amModulation;
        right = ((r4[0] + r4[1]) + (r4[2] + r4[3])) * amModulation;
#endif
    }

    float energy() const noexcept
    {
        float e = 0.0f;
        for (int k = 0; k < numPartials; ++k)
            e += re[k] * re[k] + im[k] * im[k];
        return e;
    }

    void flushDenormals() noexcept
    {
        for (int k = 0; k < numPartials; ++k)
        {
            re[k] = flushTiny (re[k]);
            im[k] = flushTiny (im[k]);
        }
    }

    int size() const noexcept { return numPartials; }

private:
    float sampleRate = 48000.0f, baseHz = 100.0f;
    int numPartials = 0;
    alignas (16) std::array<float, kMaxPartials> re {}, im {}, cosine {}, sine {}, input {}, inputIm {}, haloIn {}, outLeft {}, outRightIm {}, outRightRe {};
    std::array<float, kMaxPartials> ratio {}, follow {}, radius {}, panLeft {}, panRight {}, jitterRate {}, jitterPhase {}, lag {}, sweepScale {}, rightIm {}, rightRe {},
        stereoTarget {}, driftRate {}, driftPhase {}, flutterRate {}, flutterPhase {},
        arrival {}, arrived {}, decayRate {}, junction {};
};
} // namespace ls
