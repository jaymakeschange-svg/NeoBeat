// The Orb: eight corners, a joystick among them, and a stack of effects on what comes out.
//
// A corner holds a whole way of playing - how the line behaves, and the effects stacked on it. The joystick
// sits somewhere in the ring of eight and the Orb reads out a blend.
//
// Blending the controls is easy and means something: they are numbers, and a weighted sum of two gate lengths
// is a gate length. Blending the material is neither. A weighted sum of two different notes is a third note
// that is in neither corner, and a weighted sum of "this step fires" and "it does not" is nonsense. So the
// material is never averaged. Each step belongs to one corner at a time, and the weights decide which: with
// the joystick parked on a corner every step is that corner's, and as it travels the steps change hands a few
// at a time, in an order fixed per step rather than at random, so the same journey always sounds the same.
// Moving the joystick therefore migrates a figure from one corner's into another's instead of smearing them
// into mush - which is the difference between a morph you would use and one you would not.
#pragma once

#include "Jaq.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace mjq
{
static constexpr int kOrbCorners = 8;      // the ring
static constexpr int kOrbStack = 4;        // effects stacked on the Orb's output
static constexpr int kOrbFxParams = 8;     // what one effect takes, in matrixFxInfo() order

// One effect in the stack. `fx` indexes NeoBeat's effect library; -1 is an empty slot.
struct OrbFx
{
    int fx = -1;
    float mix = 1.0f;
    std::array<float, kOrbFxParams> p {};

    bool on() const noexcept { return fx >= 0 && mix > 0.0f; }
    bool operator== (const OrbFx& o) const noexcept { return fx == o.fx && mix == o.mix && p == o.p; }
};

// One corner of the ring: a way of playing, kept whole so the joystick can travel back to it.
struct OrbCorner
{
    bool set = false;                      // nothing has been put here yet
    std::string name;

    float dialect = 0.5f, traffic = 0.5f, accent = 0.5f, gate = 1.0f, swing = 0.5f;
    int transpose = 0;
    uint64_t seed = 1;                     // the material this corner writes

    std::array<OrbFx, kOrbStack> stack {};

    bool operator== (const OrbCorner& o) const noexcept
    {
        return set == o.set && name == o.name && dialect == o.dialect && traffic == o.traffic && accent == o.accent
            && gate == o.gate && swing == o.swing && transpose == o.transpose && seed == o.seed && stack == o.stack;
    }
};

// What the Orb reads out at the joystick's position.
struct OrbBlend
{
    float dialect = 0.5f, traffic = 0.5f, accent = 0.5f, gate = 1.0f, swing = 0.5f;
    float transpose = 0.0f;
    std::array<OrbFx, kOrbStack> stack {};
    std::array<float, kOrbCorners> weight {};
    int nearest = 0;                       // the corner with the most say, for the display
};

struct Orb
{
    std::array<OrbCorner, kOrbCorners> corners {};
    float x = 0.0f, y = 0.0f;              // the joystick, in a circle of radius 1
    bool on = false;
    // How sharply the nearest corner takes over. Low and the Orb is always a wash of all eight; high and it
    // snaps from one to the next. At 3 a corner you have parked on is clearly itself - about two thirds of
    // the say, its neighbours audible behind it - while the middle is still a blend of everything.
    float grip = 3.0f;
    uint64_t salt = 0x9E3779B97F4A7C15ull; // fixes the order steps change hands in

    bool anySet() const noexcept { for (const auto& c : corners) if (c.set) return true; return false; }
};

// Where a corner sits in the ring. Corner 0 is at the top and they run clockwise, so the picture on screen
// and the numbers here agree.
inline void orbCornerAt (int i, float& cx, float& cy) noexcept
{
    const float a = 6.28318530718f * (float) ((i % kOrbCorners + kOrbCorners) % kOrbCorners) / (float) kOrbCorners;
    cx = std::sin (a);
    cy = -std::cos (a);
}

// How much say each corner has at a point. A corner that has nothing in it has none, and if the joystick is
// somewhere no corner reaches, the nearest one keeps it rather than the Orb falling silent.
inline void orbWeights (const Orb& orb, float x, float y, float* w) noexcept
{
    const float reach = 1.9f;              // a little over the ring's diameter, so opposite corners still meet
    float total = 0.0f;
    int best = -1;
    float bestDistance = 1.0e9f;

    for (int i = 0; i < kOrbCorners; ++i)
    {
        w[i] = 0.0f;
        if (! orb.corners[(size_t) i].set) continue;

        float cx = 0.0f, cy = 0.0f;
        orbCornerAt (i, cx, cy);
        const float d = std::sqrt ((x - cx) * (x - cx) + (y - cy) * (y - cy));
        if (d < bestDistance) { bestDistance = d; best = i; }

        const float t = 1.0f - d / reach;
        if (t > 0.0f) { w[i] = std::pow (t, std::max (0.05f, orb.grip)); total += w[i]; }
    }

    if (total > 1.0e-9f) { for (int i = 0; i < kOrbCorners; ++i) w[i] /= total; return; }
    for (int i = 0; i < kOrbCorners; ++i) w[i] = 0.0f;
    if (best >= 0) w[best] = 1.0f;
}

// Read the Orb out: the controls blended, the effect stack blended slot by slot.
//
// An effect slot blends only where both corners hold the same effect - two different effects have different
// parameters, and halfway between a filter sweep and a pitch shift is not a sound, it is a mistake. Where the
// corners disagree the slot takes whichever has the most say, and its mix fades with that say, so an effect
// arrives and leaves instead of jumping in at full strength.
inline OrbBlend orbRead (const Orb& orb, float x, float y) noexcept
{
    OrbBlend b;
    orbWeights (orb, x, y, b.weight.data());

    float most = -1.0f;
    for (int i = 0; i < kOrbCorners; ++i) if (b.weight[(size_t) i] > most) { most = b.weight[(size_t) i]; b.nearest = i; }

    b.dialect = b.traffic = b.accent = b.gate = b.swing = b.transpose = 0.0f;
    for (int i = 0; i < kOrbCorners; ++i)
    {
        const float wi = b.weight[(size_t) i];
        if (wi <= 0.0f) continue;
        const auto& c = orb.corners[(size_t) i];
        b.dialect   += wi * c.dialect;
        b.traffic   += wi * c.traffic;
        b.accent    += wi * c.accent;
        b.gate      += wi * c.gate;
        b.swing     += wi * c.swing;
        b.transpose += wi * (float) c.transpose;
    }

    for (int s = 0; s < kOrbStack; ++s)
    {
        // which effect this slot holds: the one with the most say among the corners that have the slot filled
        int holder = -1;
        float holderWeight = 0.0f;
        for (int i = 0; i < kOrbCorners; ++i)
        {
            const float wi = b.weight[(size_t) i];
            if (wi <= 0.0f) continue;
            const auto& slot = orb.corners[(size_t) i].stack[(size_t) s];
            if (slot.fx < 0) continue;
            if (wi > holderWeight) { holderWeight = wi; holder = orb.corners[(size_t) i].stack[(size_t) s].fx; }
        }
        if (holder < 0) { b.stack[(size_t) s] = OrbFx {}; continue; }

        // every corner holding that same effect has a say in its settings
        OrbFx out;
        out.fx = holder;
        float share = 0.0f, mix = 0.0f;
        std::array<float, kOrbFxParams> p {};
        for (int i = 0; i < kOrbCorners; ++i)
        {
            const float wi = b.weight[(size_t) i];
            if (wi <= 0.0f) continue;
            const auto& slot = orb.corners[(size_t) i].stack[(size_t) s];
            if (slot.fx != holder) continue;
            share += wi;
            mix += wi * slot.mix;
            for (int k = 0; k < kOrbFxParams; ++k) p[(size_t) k] += wi * slot.p[(size_t) k];
        }
        if (share > 1.0e-9f)
            for (int k = 0; k < kOrbFxParams; ++k) out.p[(size_t) k] = p[(size_t) k] / share;
        out.mix = mix;                      // not divided: an effect only some corners hold fades with their say
        b.stack[(size_t) s] = out;
    }

    return b;
}

inline OrbBlend orbRead (const Orb& orb) noexcept { return orbRead (orb, orb.x, orb.y); }

// Which corner a step belongs to. Each step gets a fixed place in the queue from its index and the Orb's
// salt; as the weights change, a step changes hands when its place crosses the boundary between corners.
// That makes the migration gradual, repeatable, and the same every time the joystick takes the same path.
inline int orbCornerForStep (const float* w, int step, uint64_t salt) noexcept;

// The line the Orb reads out at a position: the controls are the blend, and every step comes from whichever
// corner owns it, written with that corner's own seed.
//
// JAQ writes a family rather than one line - each step keeps the note it takes at DIALECT 0, the note it
// takes at DIALECT 1, and how heavy it is - and DIALECT, TRAFFIC, ACCENT and GATE are read at playing time.
// That is what makes this work: a corner's material and a corner's controls can be taken apart, so the Orb
// can play one corner's steps under another corner's controls, and everything in between.
inline void orbLine (const Orb& orb, float x, float y, const Line& shape, Line& out)
{
    const auto b = orbRead (orb, x, y);

    out = shape;
    out.dialect = b.dialect;
    out.traffic = b.traffic;
    out.accent = b.accent;
    out.gate = b.gate;
    out.swing = b.swing;
    out.transpose = shape.transpose + (int) std::lround (b.transpose);

    // each corner's own material, written once
    std::array<Line, kOrbCorners> written;
    std::array<bool, kOrbCorners> have {};
    for (int i = 0; i < kOrbCorners; ++i)
    {
        if (! orb.corners[(size_t) i].set || b.weight[(size_t) i] <= 0.0f) continue;
        written[(size_t) i] = shape;
        generate (written[(size_t) i], (uint32_t) orb.corners[(size_t) i].seed);
        have[(size_t) i] = true;
    }

    const int n = std::max (1, std::min (out.numSteps, kMaxSteps));
    for (int s = 0; s < n; ++s)
    {
        const int owner = orbCornerForStep (b.weight.data(), s, orb.salt);
        if (owner >= 0 && owner < kOrbCorners && have[(size_t) owner])
            out.steps[(size_t) s] = written[(size_t) owner].steps[(size_t) s];
    }
}

inline int orbCornerForStep (const float* w, int step, uint64_t salt) noexcept
{
    uint64_t h = (uint64_t) (uint32_t) step * 0xD6E8FEB86659FD93ull + salt;
    h ^= h >> 32; h *= 0xD6E8FEB86659FD93ull; h ^= h >> 29;
    const float u = (float) ((h >> 11) & 0x1FFFFFull) / (float) 0x200000ull;   // 0 to 1

    float run = 0.0f;
    for (int i = 0; i < kOrbCorners; ++i)
    {
        run += w[i];
        if (u < run) return i;
    }
    for (int i = kOrbCorners - 1; i >= 0; --i) if (w[i] > 0.0f) return i;
    return 0;
}
} // namespace mjq
