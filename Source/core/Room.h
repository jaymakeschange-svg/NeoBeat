// The stereo room: where each voice of the multi stands, and how it moves while it plays.
//
// A voice is put somewhere in a stereo picture - left to right, and close to far - rather than given a pan
// knob. Close and far is not a volume control: a sound further away is quieter, duller, and wider in the
// room than it is at the ear, because air takes the top off and the walls answer. Those three move together,
// which is what makes a far voice sit behind a near one instead of just under it.
//
// On top of that each voice carries a drawn curve: a shape over a bar or several, read in time with the
// music, that moves the voice as it plays. Drawn rather than an LFO, because the useful movements are not
// sine waves - a voice that steps left on the off-beat, or swings wide once a bar and comes back.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mjq
{
static constexpr int kRoomCurve = 32;      // points in a drawn curve

// What the curve moves.
enum class RoomMove : uint8_t { None = 0, LeftRight, NearFar, Both };

struct Place
{
    // Switching the room on must not change what you hear. A voice starts at the front and centre, as a
    // point, which is exactly where it was before: you then move it, and what you hear is the moving. An
    // earlier version started it half way back and half wide, so turning the room on dropped the voice by
    // 15 dB and halved its pan before you had touched anything, which felt like a broken switch.
    bool on = false;                       // off: the voice goes straight out, as it always did
    float x = 0.0f;                        // -1 hard left .. +1 hard right
    float y = 0.0f;                        // 0 at the ear .. 1 at the back of the room
    float width = 0.0f;                    // how wide it sits: 0 a point, 1 spread across the picture

    RoomMove move = RoomMove::None;
    float depth = 0.5f;                    // how far the curve pushes it
    int bars = 1;                          // how many bars the curve is read over
    std::array<float, kRoomCurve> curve {};// -1 .. +1, drawn; flat by default

    Place() { curve.fill (0.0f); }

    bool moves() const noexcept { return on && move != RoomMove::None && depth > 0.0f; }
    bool operator== (const Place& o) const noexcept
    {
        return on == o.on && x == o.x && y == o.y && width == o.width && move == o.move
            && depth == o.depth && bars == o.bars && curve == o.curve;
    }
};

// The curve read at a point in the bar, with the ends joined so it loops without a step.
inline float roomCurveAt (const Place& p, double beats, int perBar) noexcept
{
    (void) perBar;                                   // the curve is read over bars, not steps
    const double span = std::max (1.0e-6, (double) std::max (1, p.bars) * 4.0);
    double phase = std::fmod (beats, span);
    if (phase < 0.0) phase += span;

    const double pos = phase / span * (double) kRoomCurve;
    const int a = ((int) pos) % kRoomCurve;
    const int b = (a + 1) % kRoomCurve;
    const float f = (float) (pos - std::floor (pos));
    return p.curve[(size_t) a] * (1.0f - f) + p.curve[(size_t) b] * f;
}

// Where the voice actually is at this moment, once the curve has had its say.
inline void roomPlaceAt (const Place& p, double beats, int perBar, float& x, float& y) noexcept
{
    x = p.x;
    y = p.y;
    if (! p.moves()) return;

    const float c = roomCurveAt (p, beats, perBar) * p.depth;
    if (p.move == RoomMove::LeftRight || p.move == RoomMove::Both) x += c;
    if (p.move == RoomMove::NearFar || p.move == RoomMove::Both) y += c * 0.5f;
    x = std::max (-1.0f, std::min (1.0f, x));
    y = std::max (0.0f, std::min (1.0f, y));
}

// What a place does to a sound: a gain each side, and how much of the top is gone.
//
// Distance runs from a third of a metre to ten. The direct sound falls as 1/r, which is the honest law and
// steeper than most pan-and-volume controls; air takes the top off with distance, gently at first and then
// quickly; and the picture narrows as a source goes away, because a thing far off arrives at both ears from
// nearly the same direction. The whole thing is levelled so that moving a voice back does not simply make it
// vanish - you hear it go away rather than go quiet.
struct RoomGain
{
    float l = 1.0f, r = 1.0f;
    float cutoff = 20000.0f;               // one-pole, hertz
};

inline RoomGain roomGain (float x, float y, float width) noexcept
{
    RoomGain g;
    y = std::max (0.0f, std::min (1.0f, y));

    // Distance, from one metre at the front to eight at the back. The honest 1/r would put the back wall
    // 18 dB down on its own, and the air takes more off on top of that, which together is far enough to
    // lose a voice rather than place it. The law is flattened so the whole depth of the room is about 10 dB
    // of level, and what tells you something is far away is mostly that it is duller and narrower - which
    // is how distance actually reads, and leaves the voice still in the mix at the back.
    const float d = 1.0f + 7.0f * y;
    const float amp = std::pow (1.0f / d, 0.55f);

    // a thing far away is heard from nearly one direction: the picture narrows with distance, but not so
    // much that a voice at the back cannot be placed at all
    const float narrow = 1.0f - 0.35f * y;
    const float spread = std::max (0.0f, std::min (1.0f, width)) * narrow;
    const float pos = std::max (-1.0f, std::min (1.0f, x)) * narrow;

    // constant power, so a voice swung across the picture keeps its weight
    const float a = (pos * 0.5f + 0.5f) * 1.57079633f;
    float gl = std::cos (a), gr = std::sin (a);

    // width pulls the two sides back towards each other
    const float mid = 0.70710678f;
    gl = gl * (1.0f - spread) + mid * spread;
    gr = gr * (1.0f - spread) + mid * spread;

    g.l = amp * gl * 1.41421356f;
    g.r = amp * gr * 1.41421356f;

    // Air: nothing at all close by, and by the back wall enough to hear without gutting the sound. This is
    // most of what says "far away", so it does the work the level used to do too much of.
    g.cutoff = 20000.0f * std::pow (0.22f, y);
    return g;
}

inline RoomGain roomGain (const Place& p, double beats, int perBar) noexcept
{
    float x = 0.0f, y = 0.0f;
    roomPlaceAt (p, beats, perBar, x, y);
    return roomGain (x, y, p.width);
}

// One-pole low-pass, per channel, for the air.
struct RoomAir
{
    void prepare (double sampleRate) noexcept { sr = sampleRate > 0.0 ? (float) sampleRate : 48000.0f; zl = zr = 0.0f; }
    void set (float cutoffHz) noexcept
    {
        const float f = std::max (40.0f, std::min (cutoffHz, sr * 0.45f));
        a = 1.0f - std::exp (-6.28318531f * f / sr);
    }
    void process (float& l, float& r) noexcept
    {
        zl += a * (l - zl); l = zl;
        zr += a * (r - zr); r = zr;
    }
    float sr = 48000.0f, a = 1.0f, zl = 0.0f, zr = 0.0f;
};
} // namespace mjq
