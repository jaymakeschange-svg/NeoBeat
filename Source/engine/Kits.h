// NeoBeat — kits. Every approved sound has a pad in one of the three kits, and
// all three keep the same pad roles, so any pattern plays on any kit:
//   1 kick  2 kick 2  3 snare  4 snare 2  5 closed hat  6 open hat  7 hat 3
//   8 rim/bonk  9 thud  10 clank  11 junk  12 drop  13 horn  14 stab  15 bell  16 fx
#pragma once

#include "Engine.h"
#include <cstdlib>
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
            { "round5/R5_08_rubber_snare", 0, 1, 0, 0, -1, 0.5f, 0.0f, 3.0f },
            { "r6/screech_snare", 0, 1, 0, 0, -1, 0.5f, 0.1f, 0.0f },
            { "round5/R5_09_metal_hat", 0, 1, 0, 0, -1, 0.5f, 0.15f, 4.0f, 1 },
            { "round5/R5_10_metal_hat_open", 0, 1, 0, 0, -1, 0.5f, 0.15f, 1.0f, 1 },
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
            { "round5/R5_02_rubber_drumhead_kick" },
            { "round4/A0_round3_core" },
            { "round4/A3_tight", 0, 1, 0, 0, -1, 0.5f, 0.0f, 0.0f },
            { "round4/A5_bounce", 0, 1, 0, 0, -1, 0.5f, 0.1f, -1.0f },
            { "round4/C2_short", 0, 1, 0, 0, -1, 0.5f, 0.15f, -3.0f, 1 },
            { "round4/C3_bright", 0, 1, 0, 0, -1, 0.5f, 0.15f, -4.0f, 1 },
            { "round4/C8_cluster_hit", 0, 1, 0, 0, -1, 0.5f, -0.25f, -3.0f },
            { "lab/ih_knock", 0, 1, 0, 0, -1, 0.5f, -0.2f, -2.0f },
            { "round4/A4_float", 0, 1, 0, 0, -1, 0.5f, 0.0f, -2.0f },
            { "round4/A1_buoyant_core", 0, 1, 0, 0, -1, 0.5f, 0.2f, -3.0f },
            { "round4/A6_spring", 0, 1, 0, 0, -1, 0.5f, -0.3f, -3.0f },
            { "round4/A2_long_ring", 0, 1, 0, 0, -1, 0.5f, 0.3f, -4.0f },
            { "round4/A7_big_room", 0, 1, 0, 0, -1, 0.5f, -0.1f, -4.0f },
            { "round4/C1_core", 0, 1, 0, 0, -1, 0.5f, 0.1f, -5.0f },
            { "round4/C4_dark_bell", 0, 1, 0, 0, -1, 0.5f, 0.25f, -5.0f },
            { "round4/C7_gong", 0, 1, 0, 0, -1, 0.5f, -0.25f, -6.0f } } } },
        { "Latex Float", { {
            { "round5/R5_02_rubber_drumhead_kick" },
            { "round4/B3_deep", 0, 1, 0, 0, -1, 0.5f, 0.0f, -1.0f },
            { "round5/R5_08_rubber_snare", 0, 1, 0, 0, -1, 0.5f, 0.0f, 3.0f },
            { "round4/B2_snap", 0, 1, 0, 0, -1, 0.5f, 0.1f, -1.0f },
            { "round4/C5_pure_metal", 0, 1, 0, 0, -1, 0.5f, 0.15f, -4.0f, 1 },
            { "round4/C6_shimmer", 0, 1, 0, 0, -1, 0.5f, 0.15f, -5.0f, 1 },
            { "round4/B8_stutter", 0, 1, 0, 0, -1, 0.5f, -0.25f, -3.0f },
            { "lab/ih_ding", 0, 1, 0, 0, -1, 0.5f, -0.2f, -3.0f },
            { "round4/B6_drop", 0, 1, 0, 0, -1, 0.5f, 0.0f, -2.0f },
            { "round5/R5_07_impedance_swap", 0, 1, 0, 0, -1, 0.5f, 0.2f, -3.0f },
            { "round4/B1_core", 0, 1, 0, 0, -1, 0.5f, -0.3f, -3.0f },
            { "round4/B4_liquid", 0, 1, 0, 0, -1, 0.5f, 0.3f, -3.0f },
            { "round4/B5_pure", 0, 1, 0, 0, -1, 0.5f, -0.1f, -4.0f },
            { "round4/B7_bounce", 0, 1, 0, 0, -1, 0.5f, 0.1f, -4.0f },
            { "r6/bell_clank", 0, 1, 0, 0, -1, 0.5f, 0.25f, -6.0f },
            { "r6/jet_riser", 0, 1, 0, 0, -1, 0.5f, 0.0f, -6.0f } } } },
        // MaterialLab (by Marbelite): the learned impacts, same pad roles
        { "MaterialLab", { {
            { "ml/basketball", -5.0f, 1.0f, 0.0f, 0.0f, -1, 0.5f, 0.0f, 0.0f },
            { "ml/hollow_pan_bigger", 0, 1, 0, 0, -1, 0.5f, 0.0f, -1.0f },
            { "ml/tin_can", 0, 1, 0, 0, -1, 0.5f, 0.0f, 0.0f },
            { "ml/bucket", 0, 1, 0, 0, -1, 0.5f, 0.05f, 0.0f },
            { "ml/clank_higher", 7.0f, 0.3f, 0.0f, 0.0f, -1, 0.5f, 0.15f, -6.0f, 1 },
            { "ml/clank", 7.0f, 0.8f, 0.0f, 0.0f, -1, 0.5f, 0.15f, -6.0f, 1 },
            { "ml/tube_clank_higher", 5.0f, 0.4f, 0.0f, 0.0f, -1, 0.5f, -0.25f, -6.0f },
            { "ml/pvc_pipe", 0, 1, 0, 0, -1, 0.5f, -0.2f, -3.0f },
            { "ml/basketball_soft", 0, 1, 0, 0, -1, 0.5f, 0.0f, -2.0f },
            { "ml/clank", 0, 1, 0, 0, -1, 0.5f, 0.2f, -4.0f },
            { "ml/tube_clank", 0, 1, 0, 0, -1, 0.5f, -0.3f, -4.0f },
            { "ml/clank_drop", 0, 1, 0, 0, -1, 0.5f, 0.3f, -4.0f },
            { "ml/hollow_pan", 0, 1, 0, 0, -1, 0.5f, -0.1f, -4.0f },
            { "snap/short", 0, 1, 0, 0, -1, 0.5f, 0.1f, -4.0f },
            { "ml/clank_longer", 0, 1, 0, 0, -1, 0.5f, 0.25f, -6.0f },
            { "snap/full", 0, 1, 0, 0, -1, 0.5f, 0.0f, -4.0f } } } },
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
        if (const char* c = std::getenv ("NB_CHAR")) ps.character = std::atoi (c) != 0;
    }
}
} // namespace nb
