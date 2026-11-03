// NeoBeat — the sound library: every approved sound so far.
//   Round 4   24 engine patches: Clank (A), Latex (B), Metal Tail (C) variations
//   Round 5   10 engine hits with coupling and the metal network
//   Round 6    8 city one-shots, rebuilt as live recipes (Wave/Cycle/Chromaphone-
//              style sources, city models and engine attacks)
//   IronHead   the Bonk-style knock and Ding-style ping from the Round 6 demos
// Each sound carries the hollow-chain settings it was demoed with (Round 6) or
// a general hollow recipe tuned to its pitch (everything else).
#pragma once

#include "PatchData.h"
#include "Sources.h"
#include "Calibration.h"
#include <string>
#include <vector>

namespace nb
{
struct SoundDef
{
    std::string id, name, group;
    HitSource::Kind kind = HitSource::Kind::Patch;
    int patchIndex = -1;
    Recipe recipe = Recipe::HornBonk;
    HollowSpec hollow;
    float defaultHollow = 0.0f;
    float pitchHz = 200.0f;
    // calibration (tools/neobeat_cli calibrate): dry trim, extra trim for the
    // hollow path, and the dry hit's RMS before trimming (the shaper's reference)
    float trimDb = 0.0f, hollowTrimDb = 0.0f, levelRms = 0.05f;
};

class Library
{
public:
    void build()
    {
        patches.clear();
        sounds.clear();
        for (int i = 0; i < kNumPatchTexts; ++i)
        {
            ls::Patch p;
            ls::parsePatchText (kPatchTexts[i].text, p);
            patches.push_back (p);
        }
        auto patchById = [&] (const std::string& id) -> int
        {
            for (int i = 0; i < kNumPatchTexts; ++i) if (id == kPatchTexts[i].id) return i;
            return -1;
        };
        tables.hollow = tableHollow();
        tables.city = tableCity();
        auto P = [&] (const char* id) { const int i = patchById (id); return i >= 0 ? patches[(size_t) i] : ls::Patch(); };
        tables.innerTubeBonk = P ("round5/R5_03_inner_tube_bonk");
        tables.junkyardClank = P ("round5/R5_05_junkyard_clank");
        tables.metalHat = P ("round5/R5_09_metal_hat");
        tables.rubberSnare = P ("round5/R5_08_rubber_snare");
        tables.drumheadKick = P ("round5/R5_02_rubber_drumhead_kick");
        tables.thudRubber = P ("round5/R5_04_thud_rubber");
        tables.ding = P ("lab/ih_ding");

        // Round 5 first: it is the heart of the default kit.
        const char* r5[][2] = {
            { "R5_01_clank_coupled_metal", "Clank Coupled Metal" }, { "R5_02_rubber_drumhead_kick", "Rubber Drumhead Kick" },
            { "R5_03_inner_tube_bonk", "Inner Tube Bonk" }, { "R5_04_thud_rubber", "Thud Rubber" },
            { "R5_05_junkyard_clank", "Junkyard Clank" }, { "R5_06_liquid_metal_drop", "Liquid Metal Drop" },
            { "R5_07_impedance_swap", "Impedance Swap" }, { "R5_08_rubber_snare", "Rubber Snare" },
            { "R5_09_metal_hat", "Metal Hat" }, { "R5_10_metal_hat_open", "Metal Hat Open" } };
        for (auto& e : r5) addPatch (std::string ("round5/") + e[0], e[1], "Round 5", patchById (std::string ("round5/") + e[0]));

        const char* r6[] = { "Horn Bonk", "Bell Clank", "Drill Hat", "Screech Snare", "Bark Kick", "Train Door", "Brake Stab", "Jet Riser" };
        const char* r6ids[] = { "r6/horn_bonk", "r6/bell_clank", "r6/drill_hat", "r6/screech_snare", "r6/bark_kick", "r6/train_door", "r6/brake_stab", "r6/jet_riser" };
        for (int i = 0; i < kNumRecipes; ++i) addRecipe (r6ids[i], r6[i], (Recipe) i);

        const char* r4[][2] = {
            { "A0_round3_core", "Clank Round 3 Core" }, { "A1_buoyant_core", "Clank Buoyant" }, { "A2_long_ring", "Clank Long Ring" },
            { "A3_tight", "Clank Tight" }, { "A4_float", "Clank Float" }, { "A5_bounce", "Clank Bounce" }, { "A6_spring", "Clank Spring" },
            { "A7_big_room", "Clank Big Room" },
            { "B1_core", "Latex Core" }, { "B2_snap", "Latex Snap" }, { "B3_deep", "Latex Deep" }, { "B4_liquid", "Latex Liquid" },
            { "B5_pure", "Latex Pure" }, { "B6_drop", "Latex Drop" }, { "B7_bounce", "Latex Bounce" }, { "B8_stutter", "Latex Stutter" },
            { "C1_core", "Metal Tail Core" }, { "C2_short", "Metal Tail Short" }, { "C3_bright", "Metal Tail Bright" },
            { "C4_dark_bell", "Metal Tail Dark Bell" }, { "C5_pure_metal", "Metal Tail Pure" }, { "C6_shimmer", "Metal Tail Shimmer" },
            { "C7_gong", "Metal Tail Gong" }, { "C8_cluster_hit", "Metal Tail Cluster Hit" } };
        for (auto& e : r4) addPatch (std::string ("round4/") + e[0], e[1], "Round 4", patchById (std::string ("round4/") + e[0]));

        addPatch ("lab/ih_knock", "IronHead Knock", "IronHead", patchById ("lab/ih_knock"));
        addPatch ("lab/ih_ding", "IronHead Ding", "IronHead", patchById ("lab/ih_ding"));

        for (auto& s : sounds)
            for (const auto& c : kCalibration)
                if (s.id == c.id) { s.trimDb = c.trimDb; s.hollowTrimDb = c.hollowTrimDb; s.levelRms = c.levelRms; }
    }

    int find (const std::string& id) const noexcept
    {
        for (size_t i = 0; i < sounds.size(); ++i) if (sounds[i].id == id) return (int) i;
        return -1;
    }

    std::vector<SoundDef> sounds;
    std::vector<ls::Patch> patches;
    SharedTables tables;

private:
    void addPatch (const std::string& id, const std::string& name, const std::string& group, int index)
    {
        if (index < 0) return;
        SoundDef s;
        s.id = id; s.name = name; s.group = group; s.kind = HitSource::Kind::Patch; s.patchIndex = index;
        const auto& p = patches[(size_t) index];
        s.pitchHz = patchPitch (p);
        auto& h = s.hollow;
        h.f0 = s.pitchHz;
        h.stretch = 0.3f; h.wobble = 0.25f; h.mismatch = 0.004f;
        h.resFb = 0.82f; h.resMix = 0.6f;
        h.inv = Breakpoints::make ({ { 0.0f, 0.0f }, { 0.4f, 0.3f }, { 1.0f, 0.1f } });
        h.modPm = true; h.modRate = 0.5f; h.modDepth = 0.4f; h.shape = 1.4f;
        h.cut = Breakpoints::make ({ { 0.0f, 9000.0f }, { 1.0f, 2500.0f } });
        h.length = clampf (p.maxLength, 0.4f, 3.0f);
        sounds.push_back (s);
    }

    void addRecipe (const std::string& id, const std::string& name, Recipe r)
    {
        SoundDef s;
        s.id = id; s.name = name; s.group = "City (Round 6)"; s.kind = HitSource::Kind::Recipe; s.recipe = r;
        s.defaultHollow = 1.0f;
        auto& h = s.hollow;
        h = HollowSpec();
        h.length = HitSource::recipeLength (r);
        using B = Breakpoints;
        switch (r)
        {
            case Recipe::HornBonk:     h.f0 = 207.6f; h.stretch = 0.4f; h.resFb = 0.85f; h.inv = B::make ({ { 0, 0 }, { 0.3f, 0.4f }, { 1, 0.2f } }); h.cut = B::make ({ { 0, 9000 }, { 1, 2500 } }); break;
            case Recipe::BellClank:    h.f0 = 311.1f; h.stretch = 0.5f; h.wobble = 0.4f; h.resFb = 0.88f; h.mismatch = 0.006f;
                                       h.inv = B::make ({ { 0, 0.1f }, { 0.5f, 0.5f }, { 1, 0.3f } }); h.cut = B::make ({ { 0, 10000 }, { 1, 4000 } }); break;
            case Recipe::DrillHat:     h.f0 = 330.0f; h.stretch = 0.2f; h.resFb = 0.78f; h.resMix = 0.55f; h.inv = B::make ({ { 0, 0.3f }, { 1, 0.5f } });
                                       h.modPm = false; h.modRate = 38.0f; h.modDepth = 0.3f; h.cut = B::make ({ { 0, 14000 }, { 1, 7000 } }); break;
            case Recipe::ScreechSnare: h.f0 = 200.0f; h.stretch = 0.25f; h.resFb = 0.8f; h.resMix = 0.6f;
                                       h.inv = B::make ({ { 0, 0.2f }, { 0.5f, 0.45f }, { 1, 0.2f } }); h.cut = B::make ({ { 0, 12000 }, { 1, 5000 } }); break;
            case Recipe::BarkKick:     h.f0 = 62.0f; h.stretch = 0.2f; h.resFb = 0.8f; h.resMix = 0.5f;
                                       h.inv = B::make ({ { 0, 0 }, { 0.4f, 0.25f }, { 1, 0.1f } }); h.cut = B::make ({ { 0, 6000 }, { 1, 1500 } }); break;
            case Recipe::TrainDoor:    h.f0 = 55.0f; h.stretch = 0.3f; h.resFb = 0.82f; h.inv = B::make ({ { 0, 0.2f }, { 0.6f, 0.4f }, { 1, 0.2f } });
                                       h.cut = B::make ({ { 0, 9000 }, { 1, 2000 } }); break;
            case Recipe::BrakeStab:    h.f0 = 440.0f; h.stretch = 0.45f; h.wobble = 0.4f; h.resFb = 0.84f; h.inv = B::make ({ { 0, 0 }, { 0.5f, 0.4f }, { 1, 0.25f } });
                                       h.modRate = 6.0f; h.modDepth = 0.4f; h.cut = B::make ({ { 0, 12000 }, { 1, 4000 } }); break;
            case Recipe::JetRiser:     h.f0 = 110.0f; h.f0End = 220.0f; h.f0Glide = 3.0f; h.stretch = 0.5f; h.wobble = 0.5f; h.resFb = 0.8f; h.resMix = 0.5f;
                                       h.inv = B::make ({ { 0, 0 }, { 0.7f, 0.5f }, { 1, 0.3f } }); h.modRate = 0.4f; h.modDepth = 0.5f;
                                       h.cut = B::make ({ { 0, 4000 }, { 0.8f, 12000 }, { 1, 6000 } }); break;
        }
        s.pitchHz = h.f0;
        sounds.push_back (s);
    }
};
} // namespace nb
