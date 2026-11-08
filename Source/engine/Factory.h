// NeoBeat 2.0 — factory presets: 32 (plus INIT) for each Matrixxxer view, extra main-sequencer
// patterns, key-map layouts and mod algorithm presets.
//
// One Shot presets are recipes: "the last snare of each bar gets a Freezing cell", so they land on
// the hits of whatever pattern is loaded. Sample presets are box layouts on the target pad's hit.
// Sequencer presets are drawn with a seeded hand: the same preset always draws the same marks.
#pragma once

#include "Presets.h"
#include "Project.h"
#include <random>

namespace nb
{
// pad roles shared by every kit
namespace role { enum : int { Kick = 0, Kick2 = 1, Snare = 2, Snare2 = 3, Hat = 4, OpenHat = 5, Hat3 = 6, Perc1 = 7, Perc2 = 8, Clank1 = 9, Clank2 = 10, Drop = 11, Horn = 12, Stab = 13, Bell = 14, Door = 15 }; }

// ------------------------------------------------------------------ One Shot
struct OneShotRule
{
    int pad;
    int which;        // 0 last hit of each bar, 1 first, 2 every hit, 3 every 4th hit, 4 ghost hits, 5 second hit, 6 hits from step 24 of each bar
    int span;
    MatrixFx fx;      // ignored when kind is FS/BD
    int kind;         // 0 library, 1 Friction Stretch, 2 Bounce Delay
    float height;
};

struct OneShotRecipe { const char* name; std::vector<OneShotRule> rules; };

inline const std::vector<OneShotRecipe>& oneShotRecipes()
{
    using F = MatrixFx;
    static const std::vector<OneShotRecipe> r {
        { "Clap Space", { { role::Snare, 0, 4, F::Freezing, 0, 0.7f } } },
        { "Hat Stretch", { { role::Hat, 0, 6, F::Freezing, 1, 0.55f } } },
        { "Kick Drop", { { role::Kick, 1, 3, F::PitchShift, 0, 0.5f } } },
        { "Snare Bounce", { { role::Snare, 5, 8, F::Freezing, 2, 0.6f } } },
        { "Glass Shimmer", { { role::Bell, 2, 3, F::Shimmer, 0, 0.6f } } },
        { "Ghost Weave", { { role::Snare, 4, 2, F::Weave, 0, 0.5f } } },
        { "Gate Hold Stabs", { { role::Stab, 2, 4, F::GateHold, 0, 0.6f } } },
        { "Drone Under", { { role::Kick, 0, 8, F::DroneMaker, 0, 0.6f } } },
        { "Bin Shift Hats", { { role::Hat, 3, 2, F::BinShift, 0, 0.35f } } },
        { "Inharmonic Kick", { { role::Kick, 1, 3, F::Stretch, 0, 0.45f } } },
        { "Traced Snare", { { role::Snare, 2, 2, F::Tracing, 0, 0.3f } } },
        { "Filterbank Perc", { { role::Perc1, 2, 3, F::Filterbank, 0, 0.5f }, { role::Perc2, 2, 3, F::Filterbank, 0, 0.4f } } },
        { "Gliding Clank", { { role::Clank1, 2, 4, F::GlidingFilters, 0, 0.5f } } },
        { "Grain Drops", { { role::Drop, 2, 4, F::Granulation, 0, 0.5f } } },
        { "Emergence", { { role::Clank2, 2, 4, F::Emergence, 0, 0.7f } } },
        { "Partial Slide Horn", { { role::Horn, 2, 4, F::PartialGlide, 0, 0.5f } } },
        { "Idee Cloud", { { role::Snare, 0, 8, F::IdeeFixer, 0, 0.4f } } },
        { "Chorus Hats", { { role::OpenHat, 2, 3, F::Chorus, 0, 0.5f } } },
        { "Comb Clank", { { role::Clank1, 2, 3, F::CombFilterbank, 0, 0.6f }, { role::Clank2, 2, 3, F::CombFilterbank, 0, 0.5f } } },
        { "Duct Tape Accent", { { role::Snare, 0, 8, F::Freezing, 1, 0.8f } } },
        { "Rubber Yank", { { role::Kick, 0, 4, F::Freezing, 1, 0.5f } } },
        { "Streamed Hats", { { role::Hat, 6, 2, F::GrainStreamer, 0, 0.45f } } },
        { "Super Filter Kick", { { role::Kick, 2, 3, F::SuperFilterbank, 0, 0.5f } } },
        { "Harmonized Horn", { { role::Horn, 2, 4, F::Harmonizer, 0, 0.5f } } },
        { "Averaged Snares", { { role::Snare, 2, 2, F::Averaging, 0, 0.5f } } },
        { "Blur Tail", { { role::Snare, 0, 6, F::Blurring, 0, 0.6f } } },
        { "Pulse Perc", { { role::Perc1, 2, 4, F::Pulsing, 0, 0.5f }, { role::Perc2, 2, 4, F::Pulsing, 0, 0.6f } } },
        { "Shuffled Hats", { { role::Hat, 2, 2, F::Shuffle, 0, 0.4f } } },
        { "Frozen Kick Tail", { { role::Kick, 5, 4, F::Freezing, 0, 0.6f } } },
        { "Bounce Accents", { { role::Snare, 0, 8, F::Freezing, 2, 0.75f }, { role::Bell, 1, 6, F::Freezing, 2, 0.5f } } },
        { "Metal Ding", { { role::Bell, 2, 4, F::Harmonizer, 0, 0.8f }, { role::Kick, 1, 3, F::CombFilterbank, 0, 0.5f } } },
        { "Full Mangle", { { role::Kick, 1, 3, F::PitchShift, 0, 0.4f }, { role::Snare, 0, 6, F::Freezing, 1, 0.6f }, { role::Hat, 3, 2, F::Shimmer, 0, 0.5f },
                           { role::OpenHat, 2, 3, F::Chorus, 0, 0.5f }, { role::Clank1, 2, 4, F::GlidingFilters, 0, 0.5f }, { role::Snare, 5, 6, F::Freezing, 2, 0.6f } } },
    };
    return r;
}

inline OneShotPreset makeOneShot (int index, const Pattern& pat)
{
    OneShotPreset p;
    const auto& rs = oneShotRecipes();
    if (index <= 0 || index > (int) rs.size()) return p;
    const auto& rec = rs[(size_t) index - 1];
    p.name = rec.name;
    const int len = std::max (1, std::min (pat.length, Pattern::kMaxSteps));
    for (const auto& rule : rec.rules)
    {
        CellFx fx = rule.kind == 1 ? CellFx::friction (StretchParams()) : rule.kind == 2 ? CellFx::bouncing (BounceParams()) : CellFx::library (rule.fx);
        if (rule.kind == 0 && rule.fx == MatrixFx::PitchShift && ! fx.settings.p.empty()) fx.settings.p[0] = -7.0f;   // a drop
        std::vector<int> hits;
        for (int s = 0; s < len; ++s) if (pat.steps[(size_t) rule.pad][(size_t) s].vel > 0) hits.push_back (s);
        std::vector<int> chosen;
        const int bars = (len + 15) / 16;
        auto inBar = [&] (int b) { std::vector<int> v; for (int s : hits) if (s / 16 == b) v.push_back (s); return v; };
        switch (rule.which)
        {
            case 0: for (int b = 0; b < bars; ++b) { auto v = inBar (b); if (! v.empty()) chosen.push_back (v.back()); } break;
            case 1: for (int b = 0; b < bars; ++b) { auto v = inBar (b); if (! v.empty()) chosen.push_back (v.front()); } break;
            case 2: chosen = hits; break;
            case 3: for (size_t i = 0; i < hits.size(); i += 4) chosen.push_back (hits[i]); break;
            case 4: for (int s : hits) if (pat.steps[(size_t) rule.pad][(size_t) s].vel < 70) chosen.push_back (s); break;
            case 5: for (int b = 0; b < bars; ++b) { auto v = inBar (b); if (v.size() > 1) chosen.push_back (v[1]); } break;
            case 6: for (int s : hits) if (s % 16 >= 12) chosen.push_back (s); break;
            default: break;
        }
        if (chosen.empty() && ! hits.empty()) chosen.push_back (hits.back());
        if (chosen.empty()) chosen.push_back (rule.pad == role::Kick ? 0 : 12);   // waits for a hit there
        for (int s : chosen)
        {
            if ((int) p.cells.size() >= 48) break;
            OneShotCell c; c.pad = rule.pad; c.step = s; c.span = rule.span; c.height = rule.height; c.fx = fx;
            p.cells.push_back (c);
        }
    }
    return p;
}

inline std::vector<std::string> oneShotNames()
{
    std::vector<std::string> n { "INIT" };
    for (const auto& r : oneShotRecipes()) n.push_back (r.name);
    return n;
}

// ------------------------------------------------------------------ Sample
inline std::vector<std::string> sampleNames()
{
    return { "INIT", "Shatter Stretch", "Top Freeze", "Low Drone", "Band Shuffle", "Comb Middle", "Glide Away", "Granular Tail",
             "Pitch Ladder", "Bounce Highs", "Duct Tape Body", "Emerging Air", "Traced Core", "Weave Mids", "Gate Hold Highs", "Blurred Bottom",
             "Shimmer Crown", "Harmonic Stack", "Bin Wreck", "Super Bands", "Pulse Grid", "Chorus Room", "Idee Smear", "Streamed Grit",
             "Partial Rise", "Filterbank Choir", "Average Out", "Inharmonic Bell", "Split Freeze", "Low Bounce", "Fractured", "Total Mangle" };
}

inline SamplePreset makeSample (int index)
{
    using F = MatrixFx;
    SamplePreset p;
    const auto names = sampleNames();
    if (index <= 0 || index >= (int) names.size()) return p;
    p.name = names[(size_t) index];
    p.pad = role::Door;
    auto op = [&] (int c0, int c1, float lo, float hi, CellFx fx) { SampleOp o; o.c0 = c0; o.c1 = c1; o.fLo = lo; o.fHi = hi; o.fx = fx; p.ops.push_back (o); };
    auto L = [] (F f) { return CellFx::library (f); };
    switch (index)
    {
        case 1: op (8, 14, 6000, 16000, CellFx::friction (StretchParams())); op (0, 4, 30, 400, L (F::Blurring)); break;
        case 2: op (0, 32, 3000, 16000, L (F::Freezing)); break;
        case 3: op (0, 32, 30, 300, L (F::DroneMaker)); break;
        case 4: op (4, 20, 500, 8000, L (F::Shuffle)); break;
        case 5: op (0, 16, 300, 3000, L (F::CombFilterbank)); break;
        case 6: op (6, 26, 200, 12000, L (F::GlidingFilters)); break;
        case 7: op (12, 32, 100, 16000, L (F::Granulation)); break;
        case 8: op (0, 8, 100, 4000, L (F::PitchShift)); op (8, 16, 100, 4000, L (F::PitchShift)); op (16, 24, 100, 4000, L (F::Harmonizer)); break;
        case 9: op (2, 30, 4000, 16000, CellFx::bouncing (BounceParams())); break;
        case 10: op (0, 24, 80, 2000, CellFx::friction (StretchParams())); break;
        case 11: op (4, 32, 5000, 16000, L (F::Emergence)); break;
        case 12: op (0, 32, 400, 2500, L (F::Tracing)); break;
        case 13: op (6, 26, 600, 5000, L (F::Weave)); break;
        case 14: op (0, 32, 4000, 16000, L (F::GateHold)); break;
        case 15: op (0, 32, 30, 250, L (F::Blurring)); break;
        case 16: op (0, 20, 6000, 16000, L (F::Shimmer)); break;
        case 17: op (0, 32, 100, 3000, L (F::Harmonizer)); break;
        case 18: op (2, 18, 200, 9000, L (F::BinShift)); break;
        case 19: op (0, 32, 60, 12000, L (F::SuperFilterbank)); break;
        case 20: op (0, 32, 200, 10000, L (F::Pulsing)); break;
        case 21: op (0, 32, 400, 9000, L (F::Chorus)); break;
        case 22: op (4, 28, 100, 12000, L (F::IdeeFixer)); break;
        case 23: op (8, 32, 1500, 12000, L (F::GrainStreamer)); break;
        case 24: op (0, 32, 200, 6000, L (F::PartialGlide)); break;
        case 25: op (0, 32, 100, 5000, L (F::Filterbank)); break;
        case 26: op (0, 32, 60, 16000, L (F::Averaging)); break;
        case 27: op (0, 32, 300, 9000, L (F::Stretch)); break;
        case 28: op (0, 16, 30, 600, L (F::Freezing)); op (16, 32, 3000, 16000, L (F::Freezing)); break;
        case 29: op (0, 12, 30, 400, CellFx::bouncing (BounceParams())); break;
        case 30: op (0, 8, 30, 1000, L (F::Shuffle)); op (8, 16, 1000, 6000, L (F::BinShift)); op (16, 24, 6000, 16000, L (F::Shimmer)); op (24, 32, 30, 16000, L (F::Blurring)); break;
        case 31: op (0, 10, 30, 400, L (F::DroneMaker)); op (6, 20, 400, 4000, CellFx::friction (StretchParams())); op (14, 32, 4000, 16000, CellFx::bouncing (BounceParams())); op (20, 32, 100, 3000, L (F::GlidingFilters)); break;
        default: break;
    }
    return p;
}

// ------------------------------------------------------------------ Sequencer (draw with sound)
inline std::vector<std::string> seqNames()
{
    return { "INIT", "Glass Sparkle Rain", "Warble Drone", "FM Bell Steps", "Spray Cloud", "Additive Choir", "Tick Machine", "Dorian Drift",
             "Minor Pent Sparks", "Lydian Glow", "FFT Whistles", "Grain Chords", "Phrygian Warble", "Blues Ticks", "Metal Rain", "Slow Dashes",
             "Triplet Sparkle", "Mixolydian Bounce", "Aeon Warble", "Chromatic Swarm", "Major Pent Drops", "FM Drone Bed", "Spray Rise", "Glass Ladder",
             "Ticks and Drones", "Warble Pairs", "Thin Air", "Grain Arp", "Dorian Bells", "Night Sparks", "Wide Warble", "Everything Drawn" };
}

inline SeqPreset makeSeq (int index)
{
    SeqPreset p;
    const auto names = seqNames();
    if (index <= 0 || index >= (int) names.size()) return p;
    p.name = names[(size_t) index];
    std::mt19937 rng ((uint32_t) index * 7919u + 13u);
    auto U = [&] (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng); };
    auto I = [&] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (rng); };
    const MarkEngine engines[4] = { MarkEngine::Add, MarkEngine::FM, MarkEngine::FFT, MarkEngine::Smp };
    p.engine = engines[(size_t) (index % 4)];
    p.scale = index % (int) scaleNames().size();
    p.root = (index * 5) % 12;
    p.grid = index % 3 == 0 ? 0 : 1;
    p.bars = 2 + (index % 3) * 2;
    p.mirror = index % 5 == 0;
    p.baseOctave = 3 + (index % 2);
    p.level = 0.55f;
    const double steps = p.bars * 4.0 / gridBeats (p.grid);
    // a character per preset: how many of each mark
    int dots = 0, dashes = 0, warbles = 0, ticks = 0, sprays = 0;
    switch (index % 8)
    {
        case 0: dots = 10; dashes = 2; warbles = 1; ticks = 4; sprays = 1; break;
        case 1: dots = 18; ticks = 2; break;
        case 2: dashes = 2; warbles = 2; dots = 3; break;
        case 3: dots = 12; dashes = 1; break;
        case 4: sprays = 3; dots = 4; break;
        case 5: dashes = 3; dots = 6; break;
        case 6: ticks = 16; dots = 4; break;
        default: warbles = 3; dots = 8; ticks = 4; break;
    }
    auto add = [&] (MarkType t, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            Mark m; m.type = t;
            const bool longMark = t == MarkType::Dash || t == MarkType::Warble || t == MarkType::Spray;
            m.length = longMark ? (float) I (8, (int) std::max (9.0, steps * 0.5)) : 1.0f;
            m.start = (float) I (0, (int) std::max (1.0, steps - (longMark ? m.length : 1.0)));
            m.row = longMark ? I (2, 12) : I (8, SeqPreset::kRows - 4);
            m.intensity = U (0.3f, 0.9f); m.inertia = longMark ? U (0.05f, 0.3f) : 0.0f;
            m.level = longMark ? U (0.35f, 0.55f) : U (0.5f, 0.9f);
            m.depth = t == MarkType::Spray ? U (40.0f, 90.0f) : U (20.0f, 60.0f);
            m.rate = U (3.0f, 8.0f); m.pan = U (-0.6f, 0.6f);
            p.marks.push_back (m);
        }
    };
    add (MarkType::Dash, dashes); add (MarkType::Warble, warbles); add (MarkType::Spray, sprays);
    add (MarkType::Dot, dots); add (MarkType::Tick, ticks);
    return p;
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

// ------------------------------------------------------------------ key-map layouts
inline std::vector<std::string> keyMapNames() { return { "1 KEY PER PAD", "SPLIT | CLANK / DRUMS / PEARL", "SNARE VELOCITY LAYERS", "KIT ACROSS THE KEYS", "ROUND ROBIN HATS" }; }
inline KeyMapState makeKeyMap (int index)
{
    KeyMapState k = KeyMapState::drumMap();
    switch (index)
    {
        case 1:
        {
            k.layers = { { "Drums", 0xff7fe0d0, true }, { "Clank low", 0xfff5b25a, true }, { "Pearl high", 0xffffd1f3, true } };
            Zone lo; lo.name = "Clank low"; lo.layer = 1; lo.loKey = 0; lo.hiKey = 23; lo.root = 12; lo.index = role::Clank1; lo.keytrack = true;
            lo.fx = CellFx::friction (StretchParams());
            Zone hi; hi.name = "Pearl high"; hi.layer = 2; hi.loKey = 52; hi.hiKey = 95; hi.root = 72; hi.index = role::Bell; hi.keytrack = true;
            hi.fx = CellFx::bouncing (BounceParams());
            k.zones.push_back (lo); k.zones.push_back (hi);
            break;
        }
        case 2:
        {
            k.layers.push_back ({ "Snare velocity", 0xffc7d3ff, true });
            for (auto it = k.zones.begin(); it != k.zones.end(); ++it) if (it->index == role::Snare) { k.zones.erase (it); break; }
            Zone soft; soft.name = "Snare soft"; soft.layer = 1; soft.loKey = soft.hiKey = soft.root = 38; soft.index = role::Snare; soft.hiVel = 90;
            Zone hard = soft; hard.name = "Snare hard"; hard.loVel = 91; hard.hiVel = 127; hard.fx = CellFx::library (MatrixFx::Freezing);
            k.zones.push_back (soft); k.zones.push_back (hard);
            break;
        }
        case 3:
            k.layers = { { "Kit", 0xffc7d3ff, true } };
            k.zones.clear();
            for (int p = 0; p < kNumPads; ++p)
            {
                Zone z; z.name = "Pad " + std::to_string (p + 1); z.loKey = 24 + p * 4; z.hiKey = z.loKey + 3; z.root = z.loKey; z.index = p; z.keytrack = true;
                k.zones.push_back (z);
            }
            break;
        case 4:
        {
            for (auto it = k.zones.begin(); it != k.zones.end(); ++it) if (it->index == role::Hat) { k.zones.erase (it); break; }
            for (int i = 0; i < 3; ++i)
            {
                Zone z; z.name = "Hat " + std::to_string (i + 1); z.loKey = z.hiKey = z.root = 40; z.index = i == 0 ? role::Hat : (i == 1 ? role::Hat3 : role::OpenHat);
                z.rrGroup = 1; z.gainDb = i == 2 ? -6.0f : 0.0f;
                k.zones.push_back (z);
            }
            break;
        }
        default: break;
    }
    return k;
}

// ------------------------------------------------------------------ mod algorithm presets
struct AlgoPreset { const char* name; int pad; PadAlgo algo; };
inline const std::vector<AlgoPreset>& algoPresets()
{
    static const std::vector<AlgoPreset> a {
        { "Kick to Metal Ding", role::Kick, { 1, 0.72f, 0.8f, ModSrc::Env1, 0.0f } },
        { "Clap Comb Falls", role::Snare, { 2, 0.55f, 0.5f, ModSrc::Lfo1, 0.25f } },
        { "Hollow Tube", role::Perc1, { 2, 0.3f, 0.6f, ModSrc::Velocity, 0.2f } },
        { "Wave Crush", role::Hat, { 3, 0.55f, 0.6f, ModSrc::Random, 0.3f } },
        { "Rubber Wah", role::Snare2, { 4, 0.4f, 0.8f, ModSrc::Velocity, 0.5f } },
        { "Folded Horn", role::Horn, { 5, 0.6f, 0.6f, ModSrc::Lfo2, 0.3f } },
    };
    return a;
}
} // namespace nb
