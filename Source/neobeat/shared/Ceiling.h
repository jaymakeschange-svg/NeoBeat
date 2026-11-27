// Keeping the output under one, two ways.
//
// `safetyKnee` is the family's existing net, lifted out of NeoBeat's Engine.h bit-identically so
// there is one definition rather than one per plugin. It is a soft clipper: above -1 dBFS it bends
// into a tanh, and normal levels pass through untouched. Right for a mix that occasionally peaks.
//
// `Limiter` is for IDM Jack, and the difference matters. Jack seizing feedback upward while the
// delays are short stacks sixteen resonators: the first render measured 1.75, about 5 dB over. Put
// that through the knee and it holds the ceiling, but (1.75-0.891)/0.109 is 7.9 into a tanh, which
// is saturated - the knee would be clipping politely rather than limiting, and every seizure would
// arrive as distortion. A limiter rides the gain down instead, so the seizure keeps its character
// and only its level is taken.
//
// Framework-free, like everything else here.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace nsr
{

// Soft safety knee above -1 dBFS. Lifted from NeoBeat's Engine::safety; the arithmetic is
// unchanged, so a mix that passed through that one passes through this identically.
inline float safetyKnee (float x) noexcept
{
    constexpr float knee = 0.891f, room = 1.0f - knee;
    const float a = std::fabs (x);
    if (a <= knee) return x;
    return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
}

// A gain-riding limiter with lookahead.
//
// The lookahead is what makes it a limiter rather than a fast compressor: the gain is already down
// by the time the peak arrives, so a transient is not let through before the reduction starts. A
// few milliseconds is enough for this, and it costs that much latency - which is why it is Jack's
// and not the master bus's.
class Limiter
{
public:
    void prepare (double sampleRate, float lookaheadMs = 3.0f, float releaseMs = 120.0f)
    {
        sr = (float) std::max (8000.0, sampleRate);
        look = std::max (1, (int) (lookaheadMs * 0.001f * sr));
        delayL.assign ((size_t) look + 1, 0.0f);
        delayR.assign ((size_t) look + 1, 0.0f);
        // The envelope must fall at least as fast as the lookahead, or the gain is still coming
        // down when the peak lands and the ceiling is missed.
        attack = std::exp (-1.0f / (0.3f * (float) look));
        release = std::exp (-1.0f / (releaseMs * 0.001f * sr));
        // A peak is held FLAT for the whole lookahead, then decays. Both halves of that matter.
        // Without a hold at all, the gain came from whatever sample was arriving, so a peak
        // landing late in the window had no time left to be reduced. With a hold that decays
        // immediately, the gain chases a target that is rising away from it and never arrives.
        // Flat until the delayed output has passed the peak, and only then released.
        holdDecay = std::exp (-1.0f / (0.5f * (float) look));
        reset();
    }

    void reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        write = 0;
        // Unity, not zero. Starting at zero means the first block fades up from silence through
        // the release time - about 120 ms of everything being quiet - and the "worst reduction"
        // reading is then just that startup ramp rather than anything the music did.
        env = 1.0f;
        gainSeen = 1.0f;
        peakHold = 0.0f;
        holdLeft = 0;
    }

    void setCeiling (float c) noexcept { ceiling = std::max (0.05f, std::min (1.0f, c)); }
    float ceilingValue() const noexcept { return ceiling; }

    // The most gain this has had to take since the last reset, in dB. Zero means it never engaged,
    // which is worth knowing: a limiter that is always working is a level problem upstream.
    float worstReductionDb() const noexcept
    {
        return gainSeen >= 0.999f ? 0.0f : 20.0f * std::log10 (std::max (1.0e-6f, gainSeen));
    }

    void process (float* l, float* r, int n) noexcept
    {
        if (delayL.empty()) return;
        const int size = (int) delayL.size();

        for (int i = 0; i < n; ++i)
        {
            // The loudest thing anywhere in the lookahead window, not just the sample arriving.
            // Instant attack, slow decay: a peak is seen the moment it appears and held until the
            // delayed output has passed it.
            const float peak = std::max (std::fabs (l[i]), std::fabs (r[i]));
            if (peak >= peakHold) { peakHold = peak; holdLeft = look; }
            else if (holdLeft > 0) --holdLeft;              // flat while the peak is still ahead
            else peakHold *= holdDecay;
            const float want = peakHold > ceiling ? ceiling / peakHold : 1.0f;

            // fall fast, recover slowly: the reverse would pump on every transient
            const float coeff = want < env ? attack : release;
            env = want + (env - want) * coeff;
            if (env < gainSeen) gainSeen = env;

            delayL[(size_t) write] = l[i];
            delayR[(size_t) write] = r[i];
            const int readAt = (write + size - look) % size;
            write = (write + 1) % size;

            // the knee still sits underneath, in case the envelope is caught out by something
            // faster than its attack
            l[i] = safetyKnee (delayL[(size_t) readAt] * env);
            r[i] = safetyKnee (delayR[(size_t) readAt] * env);
        }
    }

    int latencySamples() const noexcept { return look; }

private:
    std::vector<float> delayL, delayR;
    float sr = 48000.0f, ceiling = 0.95f;
    float attack = 0.0f, release = 0.0f, env = 1.0f, gainSeen = 1.0f;
    float holdDecay = 0.0f, peakHold = 0.0f;
    int holdLeft = 0;
    int look = 1, write = 0;
};

// Zero latency, one sample at a time, for use inside an engine's mix.
//
// The lookahead Limiter above delays its output, which is fine as the last thing in a chain and
// wrong inside one: everything after it, and everything it is summed with, would be late. This one
// takes the gain down on the very sample that needs it - instant attack, so nothing overshoots the
// ceiling - and lets it back up slowly. What that costs is some distortion on the fastest overs,
// where a lookahead limiter would have eased in. Set its ceiling just under the safety knee, so
// while it is working the knee never has to.
struct PeakLimiter
{
    float ceiling = 0.88f;
    float env = 1.0f, release = 0.9998f, worst = 1.0f;

    void prepare (double sampleRate, float releaseMs = 120.0f) noexcept
    {
        release = std::exp (-1.0f / (releaseMs * 0.001f * (float) std::max (8000.0, sampleRate)));
        env = 1.0f; worst = 1.0f;
    }

    float gain (float peak) noexcept
    {
        const float want = peak > ceiling ? ceiling / peak : 1.0f;
        env = want < env ? want : want + (env - want) * release;
        if (env < worst) worst = env;
        return env;
    }

    float worstReductionDb() const noexcept
    {
        return worst >= 0.999f ? 0.0f : 20.0f * std::log10 (std::max (1.0e-6f, worst));
    }
};

} // namespace nsr
