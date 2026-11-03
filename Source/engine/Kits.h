// NeoBeat — kits. Every approved sound has a pad in one of the three kits, and
// all three keep the same pad roles, so any pattern plays on any kit:
//   1 kick  2 kick 2  3 snare  4 snare 2  5 closed hat  6 open hat  7 hat 3
//   8 rim/bonk  9 thud  10 clank  11 junk  12 drop  13 horn  14 stab  15 bell  16 fx
#pragma once

#include "Engine.h"
#include <string>
#include <vector>

namespace nb
{
struct KitPad
{
    const char* sound;
    float tune = 0.0f, decay = 1.0f, tone = 0.0f, drive = 0.0f, hollow = -1.0f /* -1: the sound's default */, evolve = 0.5f, pan = 0.0f, levelDb = 0.0f;
    int choke = 0;
};

struct Kit
{
    std::string name;
    std::array<KitPad, 16> pads;
};

inline const std::vector<Kit>& kits()
{
    static const std::vector<Kit> k = {
        { "Rubber City", { {
            { "round5/R5_02_rubber_drumhead_kick" },
            { "r6/bark_kick" },
            { "round5/R5_08_rubber_snare" },
            { "r6/screech_snare", 0, 1, 0, 0, -1, 0.5f, 0.1f, -2.0f },
            { "round5/R5_09_metal_hat", 0, 1, 0, 0, -1, 0.5f, 0.15f, 0.0f, 1 },
            { "round5/R5_10_metal_hat_open", 0, 1, 0, 0, -1, 0.5f, 0.15f, -2.0f, 1 },
            { "r6/drill_hat", 0, 1, 0, 0, -1, 0.5f, -0.25f, -3.0f },
            { "round5/R5_03_inner_tube_bonk", 0, 1, 0, 0, -1, 0.5f, -0.2f, -3.0f },
            { "round5/R5_04_thud_rubber", 0, 1, 0, 0, -1, 0.5f, 0.0f, -2.0f },
            { "round5/R5_01_clank_coupled_metal", 0, 1, 0, 0, -1, 0.5f, 0.2f, -5.0f },
            { "round5/R5_05_junkyard_clank", 0, 1, 0, 0, -1, 0.5f, -0.3f, -6.0f },
            { "round5/R5_06_liquid_metal_drop", 0, 1, 0, 0, -1, 0.5f, 0.3f, -5.0f },
            { "r6/horn_bonk", 0, 1, 0, 0, -1, 0.5f, -0.1f, -5.0f },
            { "r6/brake_stab", 0, 1, 0, 0, -1, 0.5f, 0.1f, -6.0f },
            { "r6/bell_clank", 0, 1, 0, 0, -1, 0.5f, 0.25f, -7.0f },
            { "r6/train_door", 0, 1, 0, 0, -1, 0.5f, -0.25f, -5.0f } } } },
        { "Clank Yard", { {
            { "round5/R5_02_rubber_drumhead_kick", -2.0f },
            { "round4/A0_round3_core", 0, 0.8f },
            { "round4/A3_tight", 5.0f, 0.8f, 0.2f },
            { "round4/A5_bounce", 3.0f, 0.8f, 0.2f, 0, -1, 0.5f, 0.1f, -2.0f },
            { "round4/C2_short", 7.0f, 0.3f, 0.3f, 0, -1, 0.5f, 0.15f, -4.0f, 1 },
            { "round4/C3_bright", 5.0f, 0.6f, 0.2f, 0, -1, 0.5f, 0.15f, -5.0f, 1 },
            { "round4/C8_cluster_hit", 12.0f, 0.5f, 0, 0, -1, 0.5f, -0.25f, -5.0f },
            { "lab/ih_knock", 7.0f, 1, 0, 0, -1, 0.5f, -0.2f, -3.0f },
            { "round4/A4_float", -5.0f, 1, 0, 0, -1, 0.5f, 0.0f, -3.0f },
            { "round4/A1_buoyant_core", 0, 1, 0, 0, -1, 0.5f, 0.2f, -5.0f },
            { "round4/A6_spring", 0, 1, 0, 0, -1, 0.5f, -0.3f, -5.0f },
            { "round4/A2_long_ring", 0, 1, 0, 0, -1, 0.5f, 0.3f, -6.0f },
            { "round4/A7_big_room", 0, 1, 0, 0, -1, 0.5f, -0.1f, -6.0f },
            { "round4/C1_core", 0, 0.7f, 0, 0, -1, 0.5f, 0.1f, -7.0f },
            { "round4/C4_dark_bell", 0, 1, 0, 0, -1, 0.5f, 0.25f, -7.0f },
            { "round4/C7_gong", -5.0f, 1, 0, 0, -1, 0.5f, -0.25f, -8.0f } } } },
        { "Latex Float", { {
            { "round5/R5_02_rubber_drumhead_kick", 1.0f },
            { "round4/B3_deep", -19.0f, 0.6f },
            { "round5/R5_08_rubber_snare", 2.0f },
            { "round4/B2_snap", -7.0f, 1, 0, 0, -1, 0.5f, 0.1f, -2.0f },
            { "round4/C5_pure_metal", 7.0f, 0.3f, 0.3f, 0, -1, 0.5f, 0.15f, -5.0f, 1 },
            { "round4/C6_shimmer", 5.0f, 0.6f, 0.2f, 0, -1, 0.5f, 0.15f, -6.0f, 1 },
            { "round4/B8_stutter", 0, 0.6f, 0.2f, 0, -1, 0.5f, -0.25f, -5.0f },
            { "lab/ih_ding", 0, 1, 0, 0, -1, 0.5f, -0.2f, -5.0f },
            { "round4/B6_drop", -12.0f, 0.8f, 0, 0, -1, 0.5f, 0.0f, -3.0f },
            { "round5/R5_07_impedance_swap", 0, 1, 0, 0, -1, 0.5f, 0.2f, -5.0f },
            { "round4/B1_core", -5.0f, 0.8f, 0, 0, -1, 0.5f, -0.3f, -5.0f },
            { "round4/B4_liquid", -3.0f, 0.8f, 0, 0, -1, 0.5f, 0.3f, -5.0f },
            { "round4/B5_pure", -7.0f, 0.8f, 0, 0, -1, 0.5f, -0.1f, -6.0f },
            { "round4/B7_bounce", -2.0f, 0.8f, 0, 0, -1, 0.5f, 0.1f, -6.0f },
            { "r6/bell_clank", 0, 1, 0, 0, -1, 0.5f, 0.25f, -7.0f },
            { "r6/jet_riser", 0, 1, 0, 0, -1, 0.5f, 0.0f, -7.0f } } } },
    };
    return k;
}

// For the command-line renderer: load a kit straight into the engine's pads.
inline void applyKit (Engine& e, const Kit& kit)
{
    for (int i = 0; i < 16; ++i)
    {
        const auto& kp = kit.pads[(size_t) i];
        auto& ps = e.pads[(size_t) i];
        ps = PadSettings();
        ps.sound = std::max (0, e.lib().find (kp.sound));
        ps.tune = kp.tune; ps.decay = kp.decay; ps.tone = kp.tone; ps.drive = kp.drive;
        ps.hollow = kp.hollow >= 0.0f ? kp.hollow : e.lib().sounds[(size_t) ps.sound].defaultHollow;
        ps.evolve = kp.evolve; ps.pan = kp.pan; ps.levelDb = kp.levelDb; ps.choke = kp.choke;
    }
}
} // namespace nb
