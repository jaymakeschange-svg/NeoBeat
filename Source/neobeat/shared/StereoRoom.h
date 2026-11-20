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
