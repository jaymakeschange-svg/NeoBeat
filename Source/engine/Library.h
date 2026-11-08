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
// Depth and fatness defaults for a sound (see dsp/Character.h).
struct CharacterSpec
{
    float body = 0.4f, fat = 0.4f, space = 0.2f;   // macro defaults 0..1
    float bodyT60 = 0.3f, bodyDampHz = 4000.0f;    // the resonant body network
    float bodyMaxHz = 400.0f;                      // the body is tuned to the sound's pitch, folded down to this
    float bodyHz = 0.0f;                           // > 0: tune the body here instead (hats: up in the shimmer)
    bool waveLayer = true;                         // a Wave-style tone under the hit
    float wavePos = 14.0f, waveSemis = 0.0f, waveDecay = 0.25f, waveCutoff = 600.0f;
};

struct SoundDef
{
    std::string id, name, group;
    HitSource::Kind kind = HitSource::Kind::Patch;
    int patchIndex = -1;
    Recipe recipe = Recipe::HornBonk;
    HollowSpec hollow;
    CharacterSpec character;
    float defaultHollow = 0.0f;
    float pitchHz = 200.0f;
    // calibration (tools/neobeat_cli calibrate): dry trim, extra trim for the
    // hollow path, and the dry hit's RMS before trimming (the shaper's reference)
    float trimDb = 0.0f, hollowTrimDb = 0.0f, levelRms = 0.05f, peakRef = 0.3f;
    // MaterialLab sounds: which learned material, how it is played, where it is heard
    int material = -1;
    BodyPlay bodyPlay;
    StretchPlay stretchPlay;
    int defaultPlace = -1;     // -1: NeoBeat's own short room (character Space); 0..7: a MaterialLab place
    float preroll = 0.0f;      // stretch sounds: seconds from note-on to the snap (the sequencer starts them early)
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

        addMaterialLab();

        for (auto& s : sounds)
            for (const auto& c : kCalibration)
                if (s.id == c.id) { s.trimDb = c.trimDb; s.hollowTrimDb = c.hollowTrimDb; s.levelRms = c.levelRms; s.peakRef = c.peakRef; }
        for (auto& s : sounds) s.character = characterFor (s.id);
    }

    // MaterialLab (by Marbelite): the seven learned impacts with their variations, and the
    // Stretch & Snap family built on the learned rubber (friction v2 with the stretched strip).
    void addMaterialLab()
    {
        tables.bodies.clear(); tables.rubbers.clear();
        std::vector<std::string> bodyIds, rubberIds;
        for (int i = 0; i < kNumMaterialTexts; ++i)
        {
            const auto& mt = kMaterialTexts[i];
            if (std::string (mt.kind) == "body")
            {
                Engine2Material m;
                if (parseEngine2Material (mt.join(), m).empty()) { tables.bodies.push_back (std::move (m)); bodyIds.push_back (mt.id); }
            }
            else
            {
                FrictionMaterial m;
                if (parseFrictionMaterial (mt.join(), m).empty()) { tables.rubbers.push_back (std::move (m)); rubberIds.push_back (mt.id); }
            }
        }
        auto bodyIndex = [&] (const char* id) { for (size_t i = 0; i < bodyIds.size(); ++i) if (bodyIds[i] == id) return (int) i; return -1; };
        auto rubberIndex = [&] (const char* id) { for (size_t i = 0; i < rubberIds.size(); ++i) if (rubberIds[i] == id) return (int) i; return -1; };
        auto body = [&] (const char* id, const char* name, const char* mat, Variation v, float pitchHz, int place = 1)
        {
            const int mi = bodyIndex (mat);
            if (mi < 0) return;
            SoundDef d; d.id = id; d.name = name; d.group = "MaterialLab"; d.kind = HitSource::Kind::Body;
            d.material = mi; d.bodyPlay.variation = v; d.pitchHz = pitchHz; d.defaultPlace = place;
            d.hollow = mlHollow (pitchHz);
            sounds.push_back (d);
        };
        body ("ml/clank", "Clank", "clank", Variation::AsLearned, 400.0f);
        body ("ml/tube_clank", "Tube Clank", "tube_clank_01", Variation::AsLearned, 180.0f);
        body ("ml/hollow_pan", "Hollow Pan", "pan_02_01", Variation::AsLearned, 95.0f);
        body ("ml/tin_can", "Tin Can", "can_12_01", Variation::AsLearned, 370.0f);
        body ("ml/pvc_pipe", "PVC Pipe", "pvc_pipe_01", Variation::AsLearned, 140.0f);
        body ("ml/bucket", "Bucket", "bucket_03_01", Variation::AsLearned, 190.0f);
        body ("ml/basketball", "Basketball", "basketball_a_02", Variation::AsLearned, 120.0f, 2);
        for (int v = 1; v < (int) Variation::Count; ++v)
        {
            static const char* ids[] = { "", "higher", "bigger", "longer", "warble", "frozen", "bloom", "soft", "buoyant", "drop", "elsewhere" };
            const std::string id = std::string ("ml/clank_") + ids[v];
            const std::string nm = std::string ("Clank ") + variationName ((Variation) v);
            body (id.c_str(), nm.c_str(), "clank", (Variation) v, 400.0f);
        }
        body ("ml/tin_can_warble", "Tin Can more warble", "can_12_01", Variation::MoreWarble, 370.0f);
        body ("ml/tin_can_soft", "Tin Can soft striker", "can_12_01", Variation::SoftStriker, 370.0f);
        body ("ml/bucket_bloom", "Bucket bloom", "bucket_03_01", Variation::Bloom, 190.0f);
        body ("ml/basketball_soft", "Basketball soft striker", "basketball_a_02", Variation::SoftStriker, 120.0f, 2);
        body ("ml/hollow_pan_bigger", "Hollow Pan bigger body", "pan_02_01", Variation::BiggerBody, 70.0f, 2);
        body ("ml/tube_clank_higher", "Tube Clank higher", "tube_clank_01", Variation::Higher, 240.0f);
        body ("ml/pvc_pipe_buoyant", "PVC Pipe buoyant rise", "pvc_pipe_01", Variation::BuoyantRise, 140.0f);

        // Stretch & Snap: the learned pull, played whole, from part-way through, faster, slower, and with the learner's variations
        auto rubber = [&] (const char* id, const char* name, const char* mat, StretchPlay play, int place = 1)
        {
            const int mi = rubberIndex (mat);
            if (mi < 0) return;
            SoundDef d; d.id = id; d.name = name; d.group = "Stretch & Snap"; d.kind = HitSource::Kind::Stretch;
            d.material = mi; d.stretchPlay = play; d.pitchHz = 1240.0f; d.defaultPlace = place;
            d.preroll = std::max (0.0f, stretchSnapSeconds (tables.rubbers[(size_t) mi], play));
            d.hollow = mlHollow (300.0f);
            sounds.push_back (d);
        };
        auto P = [] (float from, float speed = 1.0f) { StretchPlay p; p.from = from; p.speed = speed; return p; };
        rubber ("snap/full", "Snap Full Pull", "rubber_tabs_01", P (0.0f));
        rubber ("snap/half", "Snap Half Pull", "rubber_tabs_01", P (0.5f));
        rubber ("snap/short", "Snap Short", "rubber_tabs_01", P (0.8f));
        rubber ("snap/flick", "Snap Flick", "rubber_tabs_01", P (0.93f));
        rubber ("snap/zip", "Snap Zip", "rubber_tabs_01", P (0.6f, 3.0f));
        rubber ("snap/slow", "Snap Slow Pull", "rubber_tabs_01", P (0.6f, 0.6f));
        { auto p = P (0.7f); p.tight = true; rubber ("snap/tight", "Snap Tight Rubber", "rubber_tabs_01", p); }
        { auto p = P (0.6f); p.slack = true; rubber ("snap/slack", "Snap Slack Rubber", "rubber_tabs_01", p); }
        { auto p = P (0.7f); p.oneContact = true; rubber ("snap/one", "Snap One Contact", "rubber_tabs_01", p); }
        { auto p = P (0.7f); p.lightBody = true; rubber ("snap/squeal", "Snap Squeal (light body)", "rubber_tabs_01", p); }
        { auto p = P (0.6f); p.climbs = true; rubber ("snap/climb", "Snap Climb (impossible)", "rubber_tabs_01", p); }
        { auto p = P (0.75f); p.metalBody = true; rubber ("snap/metal", "Snap Metal Body (impossible)", "rubber_tabs_01", p); }
        { auto p = P (0.93f); p.metalBody = true; rubber ("snap/metal_flick", "Snap Metal Flick (impossible)", "rubber_tabs_01", p); }
        { auto p = P (0.55f); p.snap = false; rubber ("snap/held", "Stretch Held (no release)", "rubber_tabs_01", p); }
        rubber ("snap/rub", "Rubber Rub", "rubber_tabs_05", P (0.6f));
        rubber ("snap/rub_zip", "Rubber Rub Zip", "rubber_tabs_05", P (0.5f, 2.5f));
    }

    static HollowSpec mlHollow (float pitchHz)
    {
        HollowSpec hs;
        hs.f0 = pitchHz; hs.length = 0.8f; hs.stretch = 0.35f; hs.resFb = 0.82f; hs.resMix = 0.6f;
        hs.inv = Breakpoints::make ({ { 0.0f, 0.0f }, { 0.4f, 0.3f }, { 1.0f, 0.15f } });
        hs.cut = Breakpoints::make ({ { 0.0f, 10000.0f }, { 1.0f, 3000.0f } });
        return hs;
    }

    int find (const std::string& id) const noexcept
    {
        for (size_t i = 0; i < sounds.size(); ++i) if (sounds[i].id == id) return (int) i;
        return -1;
    }

    std::vector<SoundDef> sounds;
    std::vector<ls::Patch> patches;
    SharedTables tables;

    // Family voicing: rubber gets a short dark body and a hollow Wave tone,
    // metal a longer bright body and a glassy tone, hats only a whisper of body.
    static CharacterSpec characterFor (const std::string& id)
    {
        CharacterSpec c;
        auto has = [&] (const char* k) { return id.find (k) != std::string::npos; };
        if (has ("ml/") || has ("snap/"))
        {   // the learned bodies have their own depth and space: only a touch of Wave fatness
            c.body = 0.0f; c.fat = has ("snap/") ? 0.1f : 0.15f; c.space = 0.0f; c.waveLayer = false;
            return c;
        }
        if (has ("R5_02") || has ("R5_03") || has ("R5_04") || has ("R5_08"))
        { c.body = 0.5f; c.fat = 0.5f; c.space = 0.2f; c.bodyT60 = 0.22f; c.bodyDampHz = 3000.0f; c.wavePos = 14.0f; c.waveDecay = 0.22f; c.waveCutoff = 500.0f; }
        else if (has ("R5_09") || has ("R5_10"))
        { c.body = 0.25f; c.fat = 0.3f; c.space = 0.25f; c.bodyT60 = 0.15f; c.bodyDampHz = 9000.0f; c.bodyMaxHz = 6000.0f; c.bodyHz = 3200.0f; c.waveLayer = false; }
        else if (has ("round5/"))
        { c.body = 0.45f; c.fat = 0.4f; c.space = 0.25f; c.bodyT60 = 0.45f; c.bodyDampHz = 6000.0f; c.bodyMaxHz = 1200.0f; c.wavePos = 42.0f; c.waveSemis = 12.0f; c.waveDecay = 0.3f; c.waveCutoff = 1200.0f; }
        else if (has ("round4/A"))
        { c.body = 0.5f; c.fat = 0.45f; c.space = 0.25f; c.bodyT60 = 0.35f; c.bodyDampHz = 4500.0f; c.wavePos = 28.0f; c.waveDecay = 0.3f; c.waveCutoff = 700.0f; }
        else if (has ("round4/B"))
        { c.body = 0.45f; c.fat = 0.4f; c.space = 0.25f; c.bodyT60 = 0.3f; c.bodyDampHz = 4000.0f; c.bodyMaxHz = 800.0f; c.wavePos = 0.0f; c.waveSemis = -12.0f; c.waveDecay = 0.35f; c.waveCutoff = 500.0f; }
        else if (has ("round4/C"))
        { c.body = 0.4f; c.fat = 0.35f; c.space = 0.3f; c.bodyT60 = 0.8f; c.bodyDampHz = 7000.0f; c.bodyMaxHz = 1200.0f; c.wavePos = 56.0f; c.waveDecay = 0.5f; c.waveCutoff = 1500.0f; }
        else if (has ("r6/"))
        { c.body = 0.35f; c.fat = 0.35f; c.space = 0.2f; c.bodyT60 = 0.3f; c.bodyDampHz = 4000.0f; c.bodyMaxHz = 600.0f; c.waveLayer = false; }
        else
        { c.body = 0.45f; c.fat = 0.4f; c.space = 0.2f; c.bodyT60 = 0.25f; c.bodyDampHz = 4000.0f; c.wavePos = 14.0f; c.waveDecay = 0.2f; c.waveCutoff = 800.0f; }
        return c;
    }

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
