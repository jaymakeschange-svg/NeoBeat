#pragma once

// The stereo room: one stage, with every element of the preset standing somewhere on it.
//
// This is deliberately not a spatial audio engine. The output is ordinary stereo played on
// ordinary speakers, and for that the useful cues are few and cheap: where a thing sits left to
// right, how wide it is, how far away it sounds, and how much of the one shared room you hear
// around it. Binaural and ambisonic rendering buy nothing here - they are built for headphones or
// for speaker rigs, and on a stereo pair they mostly cost clarity.
//
// Three things it is careful about, because each is a way for a room like this to quietly ruin a
// mix rather than improve it:
//
//   * Moving something must not change how loud it is. A pan law that keeps one channel at unity
//     drops three decibels of power when it reaches the side, so anything travelling across the
//     stage pumps in time with its own movement. The pan here is equal power, and what is smoothed
//     is the angle rather than the two gains, so the power stays put all the way across.
//
//   * Width must survive being summed to mono. Width here is mid/side, where the mid is untouched,
//     so a mono listener hears exactly what they heard before the width was applied. Widening by
//     delaying one side would be stronger on speakers and would comb the mono sum to pieces.
//
//   * The bottom of the mix has to stay still. A kick that wanders is a kick with no weight, so an
//     element can be locked: pinned to the middle, in mono, and refusing to move however its path
//     is set. Under all of it the stage sums everything below a crossover to mono, which is the
//     safety net the lock does not cover - a wide hat still has low content of its own.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nsr
{

inline constexpr int kMaxPlaces = 16;       // as many elements as a kit has lanes

// ------------------------------------------------------------------ what a path does
enum class Travel : uint8_t
{
    Still = 0,   // stays where it is put
    Sway,        // left and right, smoothly
    Swing,       // left and right, evenly
    Circle,      // round the stage: across and back to front together
    Drift,       // somewhere new each time, smoothed
    Count
};

inline const char* travelName (Travel t) noexcept
{
    switch (t)
    {
        case Travel::Still:  return "STILL";
        case Travel::Sway:   return "SWAY";
        case Travel::Swing:  return "SWING";
        case Travel::Circle: return "CIRCLE";
        case Travel::Drift:  return "DRIFT";
        default:             return "STILL";
    }
}

// ------------------------------------------------------------------ one element's place
struct Place
{
    bool on = false;             // off: the element goes straight out, untouched

    float x = 0.0f;              // -1 hard left .. +1 hard right
    float y = 0.0f;              // 0 at the front of the stage .. 1 at the back
    float width = 0.0f;          // 0 a point .. 1 as wide as the stage

    // Locked: it sits in the middle, in mono, and no path moves it. On by default for whatever
    // holds the bottom of the mix, because that is what a wandering low end costs you.
    bool locked = false;

    // how it travels
    Travel travel = Travel::Still;
    float bars = 4.0f;           // one lap
    float depth = 0.0f;          // 0 still .. 1 the whole stage
    float phase = 0.0f;          // 0..1 round the lap, so two elements need not move together
    uint32_t seed = 1u;          // for Drift

    bool moves() const noexcept { return on && ! locked && travel != Travel::Still && depth > 0.0001f; }

    bool operator== (const Place& o) const noexcept
    {
        return on == o.on && x == o.x && y == o.y && width == o.width && locked == o.locked
            && travel == o.travel && bars == o.bars && depth == o.depth && phase == o.phase && seed == o.seed;
    }
    bool operator!= (const Place& o) const noexcept { return ! (*this == o); }
};

// ------------------------------------------------------------------ the stage itself
struct Stage
{
    bool on = false;
    float size = 0.5f;           // how big the room is
    float decay = 0.5f;          // how long it rings
    float damp = 0.5f;           // how dark the ring is
    float level = 0.35f;         // how much of the room is in the mix at all
    float monoBelowHz = 120.0f;  // everything under this is summed, whatever the places say

    bool operator== (const Stage& o) const noexcept
    {
        return on == o.on && size == o.size && decay == o.decay && damp == o.damp
            && level == o.level && monoBelowHz == o.monoBelowHz;
    }
};

// ------------------------------------------------------------------ where a travelling place is now
// `beats` is the song position, so two elements an eighth apart in phase stay an eighth apart
// however the transport jumps about.
inline void placeAt (const Place& p, double beats, float& outX, float& outY) noexcept
{
    outX = p.x; outY = p.y;
    if (p.locked) { outX = 0.0f; outY = p.y; return; }
    if (! p.moves()) return;

    const double lap = std::max (0.0625, (double) p.bars) * 4.0;          // bars to beats
    double t = beats / lap + (double) p.phase;
    t -= std::floor (t);                                                  // 0..1 round the lap

    const float d = std::max (0.0f, std::min (1.0f, p.depth));
    const double turn = 6.283185307179586 * t;

    switch (p.travel)
    {
        case Travel::Sway:
            outX = p.x + d * (float) std::sin (turn);
            break;

        case Travel::Swing:
        {
            // even speed there and back, rather than lingering at the edges
            const double saw = t < 0.5 ? (t * 4.0 - 1.0) : (3.0 - t * 4.0);
            outX = p.x + d * (float) saw;
            break;
        }

        case Travel::Circle:
            // across the stage and front to back at once: it walks a ring around the listener
            outX = p.x + d * (float) std::sin (turn);
            outY = p.y + 0.5f * d * (float) (1.0 - std::cos (turn));
            break;

        case Travel::Drift:
        {
            // a new place every lap, eased into rather than jumped to
            const uint32_t step = (uint32_t) std::floor (beats / lap + (double) p.phase);
            auto pick = [&] (uint32_t n) noexcept
            {
                uint32_t h = n * 2654435761u ^ (p.seed * 2246822519u + 374761393u);
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                return (float) ((double) (h >> 8) / 8388608.0 - 1.0);     // -1..1
            };
            const float a = pick (step), b = pick (step + 1u);
            const float u = (float) (t * t * (3.0 - 2.0 * t));            // smoothstep
            outX = p.x + d * (a + (b - a) * u);
            break;
        }

        default: break;
    }

    outX = std::max (-1.0f, std::min (1.0f, outX));
    outY = std::max (0.0f, std::min (1.0f, outY));
}

// ------------------------------------------------------------------ what a place does to the sound
struct Placement
{
    float angle = 0.785398163f;   // 0 hard left .. pi/2 hard right; pi/4 is the middle
    float side = 1.0f;            // how much of the side signal survives: 0 a point, 1 as recorded, more is wider
    float gain = 1.0f;            // nearer is louder
    float cutoff = 20000.0f;      // further is duller
    float send = 0.0f;            // how much of it goes to the room
    bool  mono = false;           // summed before anything else happens to it
};

// The law. Distance is deliberately gentle on level - a far thing that is merely quiet reads as
// turned down rather than as far away. What actually sells distance is that you hear more room
// around it and less of its own top end, so most of the work is in `send` and `cutoff`.
inline Placement placementFor (float x, float y, float width, bool locked) noexcept
{
    Placement out;
    if (locked)
    {
        out.mono = true;
        out.angle = 0.785398163f;
        out.side = 0.0f;
        return out;
    }

    x = std::max (-1.0f, std::min (1.0f, x));
    y = std::max (0.0f, std::min (1.0f, y));
    width = std::max (0.0f, std::min (1.0f, width));

    // equal power: the two gains are cos and sin of one angle, so their powers always sum to one
    out.angle = 0.785398163f * (x + 1.0f);

    // Side content narrows with distance - things far off arrive from one direction - and `width`
    // opens it back up. At width 0.5 the element is as wide as it was recorded.
    const float narrow = 1.0f - 0.45f * y;
    out.side = 2.0f * width * narrow;

    out.gain = std::pow (1.0f / (1.0f + 1.6f * y), 0.5f);         // about -4 dB at the back wall
    out.cutoff = 20000.0f * std::pow (0.16f, y);                  // about 3.2 kHz at the back wall
    out.send = 0.08f + 0.62f * y * y;                             // the real distance cue
    return out;
}

// ------------------------------------------------------------------ smoothing that actually arrives
// A ramp that is restarted every block never finishes: re-aimed five times in its own length, it
// degrades into a lag of about that length, which is why a moved control can feel like it is
// chasing the mouse. This one is told how long a block is and lands exactly as the next one starts.
class Glide
{
public:
    void reset (float value, int blockSamples) noexcept
    {
        current = target = value;
        steps = std::max (1, blockSamples);
        step = 0.0f;
        left = 0;
    }
    void setBlock (int blockSamples) noexcept { steps = std::max (1, blockSamples); }

    void aim (float value) noexcept
    {
        if (value == target) return;
        target = value;
        step = (target - current) / (float) steps;
        left = steps;
    }
    float next() noexcept
    {
        if (left <= 0) { current = target; return current; }
        current += step;
        --left;
        return current;
    }
    float now() const noexcept { return current; }
    bool moving() const noexcept { return left > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int steps = 64, left = 0;
};

// ------------------------------------------------------------------ one element's strip on the stage
class Strip
{
public:
    void prepare (double sampleRate, int blockSamples) noexcept
    {
        sr = (float) std::max (8000.0, sampleRate);
        angle.reset (0.785398163f, blockSamples);
        side.reset (0.0f, blockSamples);
        gain.reset (1.0f, blockSamples);
        sendAmt.reset (0.0f, blockSamples);
        cut = 20000.0f;
        zl = zr = 0.0f;
    }
    void setBlock (int n) noexcept { angle.setBlock (n); side.setBlock (n); gain.setBlock (n); sendAmt.setBlock (n); }

    void aim (const Placement& p) noexcept
    {
        angle.aim (p.angle);
        side.aim (p.side);
        gain.aim (p.gain);
        sendAmt.aim (p.send);
        cut = p.cutoff;
        mono = p.mono;
    }

    // Takes the element's own stereo, writes it into the mix, and adds what it sends to the room.
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  float* sendL, float* sendR, int n) noexcept
    {
        const float a = 1.0f - std::exp (-6.283185307179586f * std::max (20.0f, cut) / sr);
        for (int i = 0; i < n; ++i)
        {
            float l = inL[i], r = inR[i];

            if (mono) { const float m = 0.5f * (l + r); l = m; r = m; }
            else
            {
                // mid/side: the mid is left alone, so the mono sum is unchanged by any width
                const float mid = 0.5f * (l + r), sd = 0.5f * (l - r) * side.next();
                l = mid + sd; r = mid - sd;
            }

            // further away is duller
            zl += a * (l - zl); zr += a * (r - zr);
            l = zl; r = zr;

            // Equal power, referred to the middle rather than to the edges: cos and sin alone put
            // the middle three decibels below where the sound started, so merely switching the room
            // on would duck the whole mix. The root-two takes the middle back to unity, which keeps
            // the power constant as it travels and leaves an untouched element untouched.
            const float th = angle.next(), g = gain.next();
            const float gl = 1.41421356f * std::cos (th) * g, gr = 1.41421356f * std::sin (th) * g;
            const float ol = l * gl, orr = r * gr;

            outL[i] += ol; outR[i] += orr;
            if (sendL != nullptr)
            {
                const float s = sendAmt.next();
                sendL[i] += ol * s; sendR[i] += orr * s;
            }
            else sendAmt.next();
        }
    }

private:
    Glide angle, side, gain, sendAmt;
    float sr = 48000.0f, cut = 20000.0f, zl = 0.0f, zr = 0.0f;
    bool mono = false;
};

// ------------------------------------------------------------------ the room itself
// What `Placement.send` is for. Distance is not mainly gain and dullness - those two alone just
// sound like a quiet, muffled copy in the same spot. What tells the ear how far away something is
// is how much of the room comes back with it: near the front you hear the sound, at the back you
// hear the sound plus the room it is in. So the strip sends more of a far element here, and this
// is the one place that send goes.
//
// A feedback delay network of four lines, mixed by a Hadamard matrix. The matrix is orthogonal, so
// it redistributes energy without creating or destroying it: the decay is set by the loop gain
// alone and the thing cannot run away. Four lines of coprime length so the early pattern does not
// comb, a one-pole damper in each loop so the tail darkens as it dies the way a real room does,
// and the lines paired across the output so what comes back is wide rather than a copy in the
// middle.
//
// It takes the send in stereo rather than summing it. A chop on the left should have its room
// arriving from the left - that is what makes a placed element sound placed rather than dry-left
// with a wash behind it.
class RoomVerb
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = (float) std::max (8000.0, sampleRate);
        // Coprime lengths, scaled from the rate. The ratios are the usual spread: nothing here is
        // a multiple of anything else, so the four lines never line up and re-strike together.
        static constexpr float seconds[kLines] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
        for (int i = 0; i < kLines; ++i)
        {
            const int n = std::max (16, (int) (seconds[i] * sr));
            line[i].assign ((size_t) n, 0.0f);
            write[i] = 0;
            damp[i] = 0.0f;
        }
        set (0.45f, 1.4f, 4200.0f);
    }

    // size: how big the room reads, 0..1 (it shortens the lines rather than resampling them)
    // seconds: how long the tail takes to fall 60 dB
    // dampHz: where the tail starts losing its top
    void set (float size, float seconds, float dampHz) noexcept
    {
        size = std::max (0.05f, std::min (1.0f, size));
        for (int i = 0; i < kLines; ++i)
            use[i] = std::max (16, (int) ((float) line[i].size() * size));

        // The loop gain that gives the asked-for RT60 for this line's own length: a line of length
        // L seconds goes round 1/L times a second, so g = 10^(-3L/T60).
        const float t60 = std::max (0.05f, seconds);
        for (int i = 0; i < kLines; ++i)
        {
            const float len = (float) use[i] / sr;
            g[i] = std::pow (10.0f, -3.0f * len / t60);
        }
        const float f = std::max (200.0f, std::min (dampHz, 0.45f * sr));
        a = 1.0f - std::exp (-6.283185307179586f * f / sr);
    }

    void clear() noexcept
    {
        for (int i = 0; i < kLines; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); damp[i] = 0.0f; }
    }

    // `sendL/sendR` is what the strips put in; `outL/outR` is the mix, which this adds to.
    void process (const float* sendL, const float* sendR, float* outL, float* outR, int n) noexcept
    {
        if (use[0] <= 0) return;
        for (int i = 0; i < n; ++i)
        {
            float d[kLines];
            for (int k = 0; k < kLines; ++k) d[k] = line[k][(size_t) write[k]];

            // Hadamard, normalised: orthogonal, so the tail decays only by the loop gain above.
            const float s0 = d[0] + d[1], s1 = d[0] - d[1], s2 = d[2] + d[3], s3 = d[2] - d[3];
            float m[kLines] = { 0.5f * (s0 + s2), 0.5f * (s1 + s3), 0.5f * (s0 - s2), 0.5f * (s1 - s3) };

            // the two input channels split across the lines, so left stays left-ish on the way back
            const float il = sendL != nullptr ? sendL[i] : 0.0f;
            const float ir = sendR != nullptr ? sendR[i] : 0.0f;
            m[0] += il; m[2] += il;
            m[1] += ir; m[3] += ir;

            for (int k = 0; k < kLines; ++k)
            {
                damp[k] += a * (m[k] * g[k] - damp[k]);          // darkens as it circulates
                line[k][(size_t) write[k]] = damp[k];
                if (++write[k] >= use[k]) write[k] = 0;
            }

            outL[i] += 0.5f * (d[0] + d[2]);
            outR[i] += 0.5f * (d[1] + d[3]);
        }
    }

private:
    static constexpr int kLines = 4;
    std::vector<float> line[kLines];
    int write[kLines] {}, use[kLines] {};
    float damp[kLines] {}, g[kLines] {};
    float sr = 48000.0f, a = 1.0f;
};

// ------------------------------------------------------------------ a small space around each hit
// Spatial impression - the sense that a sound has size and sits in three dimensions - comes mostly
// from early lateral reflections arriving 5-80 ms after the direct sound, in roughly the 125-1500 Hz
// range (Barron; Bradley). This is a handful of reflections reaching the two ears at different times
// and opposite polarities, so the image opens out without smearing the strike.
//
// It does a different job from RoomVerb and the two are meant to be used together: these are the
// first few discrete arrivals that say how big the space is, where the FDN is the diffuse tail that
// says how far away the element is. Either alone is half a room.
//
// The lows below the cut stay dry and centred, because a reflected bass arrival only muddies the
// bottom, and the highs are softened the way real walls absorb them. The tap times are coprime-ish
// and deliberately different in each ear, and the gains alternate in polarity, which is what
// decorrelates the two channels rather than producing a wide copy of the same thing.
class EarlyReflections
{
public:
    static constexpr int kTaps = 6;

    void prepare (float newSampleRate)
    {
        sampleRate = newSampleRate;
        // Longest tap 43 ms at size 2, with margin, at any sample rate.
        size = 1;
        while (size < (int) (0.1f * sampleRate) + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        write = 0;
        lowState = highState = 0.0f;
    }

    void configure (float roomSize, float dampHz, float lowCutHz) noexcept
    {
        static constexpr float leftMs[kTaps] = { 6.3f, 11.9f, 17.6f, 23.8f, 31.3f, 39.7f };
        static constexpr float rightMs[kTaps] = { 8.1f, 13.4f, 20.2f, 26.9f, 34.6f, 42.8f };
        static constexpr float leftGain[kTaps] = { 0.90f, -0.72f, 0.60f, -0.50f, 0.40f, -0.32f };
        static constexpr float rightGain[kTaps] = { -0.85f, 0.70f, -0.58f, 0.47f, -0.38f, 0.30f };
        const float s = std::min (2.0f, std::max (0.3f, roomSize));
        for (int i = 0; i < kTaps; ++i)
        {
            leftDelay[(size_t) i] = std::min (size - 2, (int) (leftMs[i] * s * 0.001f * sampleRate));
            rightDelay[(size_t) i] = std::min (size - 2, (int) (rightMs[i] * s * 0.001f * sampleRate));
            gainLeft[(size_t) i] = leftGain[i];
            gainRight[(size_t) i] = rightGain[i];
        }
        longest = std::max (leftDelay[kTaps - 1], rightDelay[kTaps - 1]);

        // One pole each, set from a corner frequency. Written as seconds-then-coefficient rather
        // than folded into one expression, so the arithmetic matches the per-plugin version this
        // replaced to the last bit and no preset changes how it sounds.
        const float lowSeconds = 1.0f / (6.283185307179586f * std::max (dampHz, 200.0f));
        const float highSeconds = 1.0f / (6.283185307179586f * std::max (lowCutHz, 20.0f));
        lowCoef = 1.0f - std::exp (-1.0f / (std::max (lowSeconds, 1.0e-5f) * sampleRate));
        highCoef = 1.0f - std::exp (-1.0f / (std::max (highSeconds, 1.0e-5f) * sampleRate));
    }

    int tailSamples() const noexcept { return longest + 1; }

    void process (float input, float& left, float& right) noexcept
    {
        // Band-limit what reaches the "walls": keep the lows dry, soften the highs.
        highState += highCoef * (input - highState);
        const float banded = input - highState;
        lowState += lowCoef * (banded - lowState);
        buffer[(size_t) write] = std::fabs (lowState) < 1.0e-20f ? 0.0f : lowState;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kTaps; ++i)
        {
            l += gainLeft[(size_t) i] * buffer[(size_t) ((write - leftDelay[(size_t) i]) & (size - 1))];
            r += gainRight[(size_t) i] * buffer[(size_t) ((write - rightDelay[(size_t) i]) & (size - 1))];
        }
        write = (write + 1) & (size - 1);
        left = l;
        right = r;
    }

private:
    float sampleRate = 48000.0f;
    std::vector<float> buffer;
    int size = 8192, write = 0, longest = 0;
    std::array<int, kTaps> leftDelay {}, rightDelay {};
    std::array<float, kTaps> gainLeft {}, gainRight {};
    float lowState = 0.0f, highState = 0.0f, lowCoef = 0.5f, highCoef = 0.01f;
};

// ------------------------------------------------------------------ the safety net
// Everything below the crossover is summed to mono. A locked element is already mono, but a wide
// hat or a widened pad still has low content of its own, and that is what folds badly on a club
// system or a phone. One pole each way, so what is taken out of the sides is exactly what is put
// back into the middle and the sum is unchanged.
class BassMono
{
public:
    void prepare (double sampleRate) noexcept { sr = (float) std::max (8000.0, sampleRate); z = 0.0f; }

    void process (float* l, float* r, int n, float hz) noexcept
    {
        if (hz < 20.0f) return;
        const float a = 1.0f - std::exp (-6.283185307179586f * std::min (hz, 0.45f * sr) / sr);
        for (int i = 0; i < n; ++i)
        {
            const float sd = 0.5f * (l[i] - r[i]);
            z += a * (sd - z);               // the side's low end
            const float keep = sd - z;       // the side, without it
            const float mid = 0.5f * (l[i] + r[i]);
            l[i] = mid + keep; r[i] = mid - keep;
        }
    }

private:
    float sr = 48000.0f, z = 0.0f;
};

} // namespace nsr
