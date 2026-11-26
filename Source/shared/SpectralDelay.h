#pragma once

// Sixteen bands, each with its own delay, and a few ways for them to talk to each other.
//
// Two separate pieces of research arrive at the same module from opposite directions, so it is
// built once rather than twice.
//
// The first is a spectral delay: split the signal into bands and give each its own delay time,
// feedback and level. The part worth copying is not the splitting, which is obvious, but what the
// bands do to each other. A band can be fed from its neighbour rather than only from itself, so
// energy walks up or down the spectrum with each repeat. A sound treated this way does not echo,
// it migrates: every repetition arrives brighter or darker than the one before, and eventually
// leaves the register it started in. That is the whole character, and it costs one signed offset.
//
// The second is interiority. A delay of two or three milliseconds between the ears is too long to
// be any real head position - the most a head can produce is about 690 microseconds, going around
// the skull - and far too short to hear as an echo, which needs thirty-odd. The ear is handed an
// unambiguous direction cue that maps nowhere, and what it does with it is place the sound inside
// the listener rather than anywhere in the room. Modulate that offset and it never settles. Doing
// it per band is what the broadband version cannot do: the metallic top can sit inside the head
// while the bottom stays external and mono, which is what BassMono is already there to guarantee.
//
// Sixteen bands of anything is ninety-six controls, which nobody will use. So every per-band value
// is interpolated between a start and a finish: two knobs drive sixteen delay lines. That idea is
// stolen outright and it is the reason the original was playable.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nsr
{

// The window that produces inside-the-head rather than a position or an echo. Below the lower
// bound the brain finds a real place for it; above the upper it starts to come apart into two
// arrivals. Clamped rather than trusted to a caller, because getting it wrong does not sound
// like a bug, it sounds like a worse version of the effect.
//
// Out here at namespace scope rather than inside the class because the band plan below needs
// them too, and two copies of a psychoacoustic bound is how one of them quietly goes stale.
inline constexpr float kInterMinMs = 0.7f;
inline constexpr float kInterMaxMs = 12.0f;

struct SpectralDelayParams
{
    // where the bands sit. Log spaced, because that is how we hear.
    float startHz = 90.0f, finishHz = 9000.0f;
    float q = 2.6f;                       // shared bandwidth; low Q leaks between bands on purpose

    // per band, interpolated first to last
    float startTimeMs = 45.0f,  finishTimeMs = 320.0f;
    float startFeedback = 0.30f, finishFeedback = 0.58f;
    float startLevel = 1.0f,     finishLevel = 0.55f;
    float startLfoHz = 0.07f,    finishLfoHz = 0.9f;
    float startLfoMs = 0.0f,     finishLfoMs = 5.0f;    // depth of the delay-time wobble

    // the neighbour feed. +1 means band 1 takes from band 2, and energy climbs.
    int   sideOffset = 0;                 // -15..15
    float sideFeedback = 0.0f;            // 0..1

    // interiority: the left/right split, per band, in milliseconds
    float startInterMs = 0.0f, finishInterMs = 0.0f;
    float interLfoHz = 0.23f;             // keeps it from settling
    float interLfoAmt = 0.5f;             // 0..1 of the offset that wobbles

    float dry = 1.0f, wet = 0.8f;
};

// ------------------------------------------------------------------ what the params come to, band by band
//
// An interface showing a spectral delay has to say what each band is actually doing, and the only
// honest way to do that is to read the numbers the engine runs on. Working them out a second time
// in the display is how a readout ends up disagreeing with what you hear: the feedback here is
// trimmed when the neighbour feed is up, by an amount no caller could arrive at on its own, and
// the interaural split is clamped into the window that produces inside-the-head rather than a
// position. So the arithmetic lives here once, and setParams below is one of its two callers.
struct BandPlan
{
    float hz = 0.0f;
    float timeMs = 0.0f;
    float feedback = 0.0f;        // after the pair trim
    float level = 0.0f;
    float lfoHz = 0.0f, lfoMs = 0.0f;
    float interMs = 0.0f;         // 0 when off; otherwise inside [kInterMinMs, kInterMaxMs]
};

inline BandPlan bandPlan (const SpectralDelayParams& p, int band, int bands,
                          float sampleRate = 48000.0f) noexcept
{
    const float sr = std::max (8000.0f, sampleRate);
    const float lo = std::max (20.0f, std::min (p.startHz, p.finishHz));
    const float hi = std::min (0.45f * sr, std::max (p.startHz, p.finishHz));

    const float fbMax = 0.96f;
    const float sumHi = std::max (std::abs (p.startFeedback), std::abs (p.finishFeedback))
                      + std::abs (p.sideFeedback);
    const float trim = sumHi > fbMax ? fbMax / sumHi : 1.0f;

    const int b = std::max (0, std::min (bands - 1, band));
    const float t = bands > 1 ? (float) b / (float) (bands - 1) : 0.0f;
    auto lerp = [t] (float a, float z) noexcept { return a + (z - a) * t; };

    BandPlan r;
    r.hz       = lo * std::pow (hi / lo, t);
    r.timeMs   = lerp (p.startTimeMs, p.finishTimeMs);
    r.feedback = lerp (p.startFeedback, p.finishFeedback) * trim;
    r.level    = lerp (p.startLevel, p.finishLevel);
    r.lfoHz    = lerp (p.startLfoHz, p.finishLfoHz);
    r.lfoMs    = lerp (p.startLfoMs, p.finishLfoMs);

    const float im = lerp (p.startInterMs, p.finishInterMs);
    r.interMs = im <= 0.0f ? 0.0f : std::min (kInterMaxMs, std::max (kInterMinMs, im));
    return r;
}

class SpectralDelay
{
public:
    static constexpr int kBands = 16;
    static constexpr float kMaxDelayMs = 2000.0f;

    // The interaural window, named here as well so a caller that has the class has the bounds.
    // One definition, up at namespace scope.
    static constexpr float kInterMinMs = nsr::kInterMinMs;
    static constexpr float kInterMaxMs = nsr::kInterMaxMs;

    void prepare (double sampleRate, int maxBlock = 0)
    {
        sr = (float) std::max (8000.0, sampleRate);
        const int n = (int) (kMaxDelayMs * 0.001f * sr) + 4;
        const int blk = maxBlock > 0 ? maxBlock : 2048;
        for (int b = 0; b < kBands; ++b)
            for (int c = 0; c < 2; ++c)
            {
                line[b][c].assign ((size_t) n, 0.0f);
                // Sized here so the per-band path never allocates on the audio thread. It still
                // grows if a host hands over a bigger block than it promised, which some do.
                scratch[b][c].assign ((size_t) blk, 0.0f);
                write[b][c] = 0;
            }
        reset();
        setParams (p);
    }

    void reset() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            for (int c = 0; c < 2; ++c)
            {
                std::fill (line[b][c].begin(), line[b][c].end(), 0.0f);
                ic1[b][c] = ic2[b][c] = 0.0f;
                tap[b][c] = 0.0f;
            }
            lfoPhase[b] = (float) b / (float) kBands;     // spread, so they do not pulse together
            interPhase[b] = (float) b / (float) kBands * 0.5f;
        }
    }

    void setParams (const SpectralDelayParams& np)
    {
        p = np;
        const float qq = std::max (0.5f, p.q);

        for (int b = 0; b < kBands; ++b)
        {
            // Every per-band number comes from bandPlan, so the interface can read the same ones
            // without a second copy of the arithmetic to fall out of step. The filter
            // coefficients stay here: a display has no use for them.
            const auto pl = bandPlan (p, b, kBands, sr);

            hz[b] = pl.hz;
            const float g = std::tan (3.14159265358979f * std::min (hz[b], 0.45f * sr) / sr);
            k[b] = 1.0f / qq;
            a1[b] = 1.0f / (1.0f + g * (g + k[b]));
            a2[b] = g * a1[b];
            a3[b] = g * a2[b];

            timeS[b]  = pl.timeMs * 0.001f;
            fb[b]     = pl.feedback;
            level[b]  = pl.level;
            lfoInc[b] = pl.lfoHz / sr;
            lfoS[b]   = pl.lfoMs * 0.001f;
            interS[b] = pl.interMs * 0.001f;
        }

        // Total loop gain has to stay under one however the two feedbacks are set. Clamping the
        // pair together rather than each alone is the difference between a control that gets wild
        // near the top and one that runs away there. bandPlan applies the same trim to the per-band
        // feedback above; this is the neighbour feed's share of it.
        const float fbMax = 0.96f;
        const float sumHi = std::max (std::abs (p.startFeedback), std::abs (p.finishFeedback))
                          + std::abs (p.sideFeedback);
        const float trim = sumHi > fbMax ? fbMax / sumHi : 1.0f;
        sideFb = p.sideFeedback * trim;
        sideOff = std::max (-(kBands - 1), std::min (kBands - 1, p.sideOffset));
        interInc = std::max (0.0f, p.interLfoHz) / sr;
        interAmt = std::min (1.0f, std::max (0.0f, p.interLfoAmt));
    }

    const SpectralDelayParams& params() const noexcept { return p; }
    float bandHz (int b) const noexcept { return hz[(size_t) std::max (0, std::min (kBands - 1, b))]; }

    // Render the bands without mixing them down, so a caller can do something with each one.
    //
    // This is the reason the engine does not own a Stereoroom of its own. Placement already lives
    // in StereoRoom.h and rooms already hold Places and Strips; a second copy inside here would be
    // a worse one that nothing else could reach. So the engine makes sixteen band signals and the
    // room decides where each of them stands - which means a band can take any Travel the rest of
    // the family can, for free, and anything added to the Stereoroom later arrives here too.
    void processSplit (const float* inL, const float* inR, int n) noexcept
    {
        if (inL == nullptr || inR == nullptr || n <= 0) return;
        if ((int) scratch[0][0].size() < n)
            for (auto& band : scratch) for (auto& ch : band) ch.assign ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
            step (inL[i], inR[i], i, true);
        lastSplit = n;
    }

    // One band's rendered output, level already applied. Valid until the next call.
    const float* bandOut (int b, int ch) const noexcept
    {
        b = std::max (0, std::min (kBands - 1, b));
        ch = ch != 0 ? 1 : 0;
        return scratch[b][ch].empty() ? nullptr : scratch[b][ch].data();
    }
    int splitLength() const noexcept { return lastSplit; }

    void process (float* l, float* r, int n) noexcept
    {
        if (l == nullptr || r == nullptr) return;

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            float wetL = 0.0f, wetR = 0.0f;
            step (inL, inR, i, false, &wetL, &wetR);
            l[i] = inL * p.dry + wetL * p.wet;
            r[i] = inR * p.dry + wetR * p.wet;
        }
    }

private:
    // One sample of the whole bank. Shared by both entry points so the two can never drift: the
    // mixed path and the per-band path are the same arithmetic, differing only in where the
    // sixteen results are put.
    void step (float inL, float inR, int i, bool toScratch,
               float* wetL = nullptr, float* wetR = nullptr) noexcept
    {
        {
            // Read every band before writing any of them. Reading and writing band by band would
            // let a band hear its neighbour's current sample through the side feed, which is a
            // different and much less stable circuit than the one intended.
            for (int b = 0; b < kBands; ++b)
            {
                const float wob = std::sin (6.283185307179586f * lfoPhase[b]);
                lfoPhase[b] += lfoInc[b]; if (lfoPhase[b] >= 1.0f) lfoPhase[b] -= 1.0f;

                float base = (timeS[b] + wob * lfoS[b]) * sr;

                float split = 0.0f;
                if (interS[b] > 0.0f)
                {
                    const float iw = std::sin (6.283185307179586f * interPhase[b]);
                    interPhase[b] += interInc; if (interPhase[b] >= 1.0f) interPhase[b] -= 1.0f;
                    split = interS[b] * sr * (1.0f - interAmt + interAmt * 0.5f * (1.0f + iw));
                }

                const float maxD = (float) line[b][0].size() - 3.0f;
                const float dL = clampf (base - split * 0.5f, 1.0f, maxD);
                const float dR = clampf (base + split * 0.5f, 1.0f, maxD);

                tap[b][0] = read (b, 0, dL);
                tap[b][1] = read (b, 1, dR);

                if (toScratch)
                {
                    scratch[b][0][(size_t) i] = tap[b][0] * level[b];
                    scratch[b][1][(size_t) i] = tap[b][1] * level[b];
                }
                else
                {
                    *wetL += tap[b][0] * level[b];
                    *wetR += tap[b][1] * level[b];
                }
            }

            // Then write. The split happens once, on the dry input, and the delay network that
            // follows is filter-free.
            //
            // This is the whole mechanism and it is easy to get backwards. Putting the bandpass
            // inside the feedback loop seems tidier - every line then carries only its own band -
            // but it recolours anything arriving from a neighbour back to the receiving band's own
            // frequency, which deletes precisely the content that was supposed to move. Measured,
            // a version with the filter in the loop gave +1 and -1 outputs identical to within
            // float noise: the migration had been filtered away in both directions. Split once,
            // then delay, and a band's line genuinely carries its neighbour's frequencies at its
            // own time and level - which is what makes a repeat arrive somewhere else.
            for (int b = 0; b < kBands; ++b)
            {
                // The neighbour, or nothing. The bands are a line, not a ring: energy walks up or
                // down and then leaves at the end, which is the point of it. Wrapping the top band
                // round to the bottom makes a loop that circulates for ever.
                const int s = b + sideOff;
                const bool hasSide = sideFb != 0.0f && s >= 0 && s < kBands;

                const float fbL = tap[b][0] * fb[b] + (hasSide ? tap[s][0] * sideFb : 0.0f);
                const float fbR = tap[b][1] * fb[b] + (hasSide ? tap[s][1] * sideFb : 0.0f);

                const float bL = svf (b, 0, inL) + fbL;
                const float bR = svf (b, 1, inR) + fbR;

                line[b][0][(size_t) write[b][0]] = flush (bL);
                line[b][1][(size_t) write[b][1]] = flush (bR);
                if (++write[b][0] >= (int) line[b][0].size()) write[b][0] = 0;
                if (++write[b][1] >= (int) line[b][1].size()) write[b][1] = 0;
            }
        }
    }

    static float clampf (float v, float lo, float hi) noexcept { return std::max (lo, std::min (hi, v)); }
    static float flush (float v) noexcept { return std::abs (v) < 1.0e-20f ? 0.0f : v; }

    // There is deliberately no wrap() here. The side feed is a line, not a ring: a band past
    // either end has no neighbour, and energy leaves the bank rather than circulating in it
    // forever. An earlier version wrapped, and band 15 fed band 0 until the end of time.

    // Topology-preserving state variable filter, bandpass output normalised to unity at centre.
    // Unity matters because this runs inside a feedback loop: left at its natural peak gain of Q,
    // a feedback of 0.5 with Q of 2.6 would be a loop gain of 1.3 and the band would take off.
    float svf (int b, int c, float in) noexcept
    {
        const float v3 = in - ic2[b][c];
        const float v1 = a1[b] * ic1[b][c] + a2[b] * v3;
        const float v2 = ic2[b][c] + a2[b] * ic1[b][c] + a3[b] * v3;
        ic1[b][c] = 2.0f * v1 - ic1[b][c];
        ic2[b][c] = 2.0f * v2 - ic2[b][c];
        return k[b] * v1;
    }

    float read (int b, int c, float delaySamples) const noexcept
    {
        const auto& buf = line[b][c];
        const int size = (int) buf.size();
        float pos = (float) write[b][c] - delaySamples;
        while (pos < 0.0f) pos += (float) size;
        const int i0 = (int) pos;
        const float fr = pos - (float) i0;
        const int i1 = (i0 + 1) % size;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }

    SpectralDelayParams p;
    float sr = 48000.0f;

    std::vector<float> line[kBands][2];
    std::vector<float> scratch[kBands][2];
    int lastSplit = 0;
    int write[kBands][2] {};
    float ic1[kBands][2] {}, ic2[kBands][2] {}, tap[kBands][2] {};

    float hz[kBands] {}, a1[kBands] {}, a2[kBands] {}, a3[kBands] {}, k[kBands] {};
    float timeS[kBands] {}, fb[kBands] {}, level[kBands] {}, lfoInc[kBands] {}, lfoS[kBands] {};
    float interS[kBands] {}, lfoPhase[kBands] {}, interPhase[kBands] {};
    float sideFb = 0.0f, interInc = 0.0f, interAmt = 0.5f;
    int sideOff = 0;
};

} // namespace nsr
