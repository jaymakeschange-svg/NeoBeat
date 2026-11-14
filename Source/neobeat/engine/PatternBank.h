// NeoBeat — the pattern bank: the 128 patterns of the sequencer (50 written by hand in Presets.h and 78
// variations of them), and the lanes' roles. Depends on nothing but the sequencer, so Batchcut's Kit
// room uses the same file to audition a kit.
#pragma once

#include "Presets.h"
#include <algorithm>
#include <random>

namespace nb
{
// what each of the 16 lanes is for
namespace role { enum : int { Kick = 0, Kick2 = 1, Snare = 2, Snare2 = 3, Hat = 4, OpenHat = 5, Hat3 = 6, Perc1 = 7, Perc2 = 8, Clank1 = 9, Clank2 = 10, Drop = 11, Horn = 12, Stab = 13, Bell = 14, Door = 15 }; }
inline const char* roleName (int lane)
{
    static const char* n[] = { "Kick", "Kick 2", "Snare", "Snare 2", "Closed hat", "Open hat", "Hat 3", "Rim", "Thud", "Clank", "Junk", "Drop", "Horn", "Stab", "Bell", "FX" };
    return n[std::max (0, std::min (lane, 15))];
}

// ------------------------------------------------------------------ main sequencer: 128 patterns
// The 50 hand-written presets, then 78 variations of them (ghost-note fills, triplet hat rolls,
// pushed kicks, open-hat lifts), named after their parents.
inline int numPatterns() { return 128; }
inline Preset patternPreset (int index)
{
    const auto& base = presets();
    if (index < (int) base.size()) return base[(size_t) std::max (0, index)];
    const int v = index - (int) base.size();
    Preset p = base[(size_t) (v % (int) base.size())];
    const int kind = v / (int) base.size() == 0 ? v % 4 : (v + 1) % 4;
    std::mt19937 rng ((uint32_t) index * 2654435761u);
    const int len = p.pattern.length;
    auto& S = p.pattern.steps;
    static const char* suffix[4] = { "Ghosts", "Rolls", "Push", "Lift" };
    p.name = p.name + " " + suffix[kind];
    switch (kind)
    {
        case 0:   // ghost snares around the backbeat
            for (int s = 1; s < len; s += 2) if (S[role::Snare][(size_t) s].vel == 0 && (rng() % 3) == 0) S[role::Snare][(size_t) s].vel = 38;
            break;
        case 1:   // triplet hat rolls at the end of each bar
            for (int b = 0; b * 16 < len; ++b) { const int s = b * 16 + 14; if (s < len) { S[role::Hat][(size_t) s].vel = 92; S[role::Hat][(size_t) s].roll = 5; } }
            break;
        case 2:   // a pushed kick before the bar
            for (int b = 1; b * 16 < len; ++b) { const int s = b * 16 - 1; S[role::Kick][(size_t) s].vel = 100; S[role::Kick][(size_t) s].nudge = -20; }
            break;
        default:  // open-hat lifts on the offbeats
            for (int s = 2; s < len; s += 8) if (S[role::OpenHat][(size_t) s].vel == 0) S[role::OpenHat][(size_t) s].vel = 84;
            break;
    }
    p.tempo = std::min (126.0f, p.tempo + (float) (v % 3));
    return p;
}

} // namespace nb
