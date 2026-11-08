// NeoBeat 2.0 — the project: everything the editor edits apart from the pad macros and the main
// pattern (those stay host parameters / the PatternStore). Plain data, no framework code: the
// plugin serialises it, the render worker copies it, the tests build it by hand.
//
//   Pad FX       3 inserts per pad (rendered ahead)
//   Windows      Friction Stretch and Bounce Delay, 8 slots each; a slot listens to a pad, a grid row
//                or a One Shot cell and adds (or replaces with) its rendered version
//   Key map      zones on a key x velocity grid, in layers: every note lands here
//   Matrixxxer   Sequencer (drawn marks), One Shot (an effect on a hit), Sample (effect boxes on a sample)
//   Grid         the Blip-style panel: a folder of samples, 64 x 64 painted cells, 16 patterns
//   Mod          the dock's sources, the routes from them to knobs, and per-pad algorithms
#pragma once

#include "MatrixxxerFx.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace nb
{
constexpr int kNumPads = 16;
constexpr int kNumSlots = 8;
constexpr int kNumInserts = 3;

// ------------------------------------------------------------------ windows
enum class SourceKind : uint8_t { None, Pad, GridRow, Cell };
struct SlotSource { SourceKind kind = SourceKind::None; int index = 0; };

struct FsSlot
{
    bool on = false;
    SlotSource src;
    StretchParams p;
    float level = 1.0f;      // the rendered version's level
    bool replace = true;     // true: the stretch replaces the dry hit (an accent); false: added on top
};

struct BdSlot
{
    bool on = false;
    SlotSource src;
    BounceParams p;
    float level = 0.7f;
    bool replace = false;    // a delay: the dry hit stays
};

struct PadFx { std::array<CellFx, kNumInserts> inserts {}; };

// ------------------------------------------------------------------ key map
enum class ZoneSource : uint8_t { Pad, Sample, Kit, OneShot, GridPattern };
struct Zone
{
    std::string name;
    int layer = 0;
    int loKey = 36, hiKey = 36, loVel = 1, hiVel = 127, root = 36;
    ZoneSource src = ZoneSource::Pad;
    int index = 0;            // pad, kit, One Shot preset or grid pattern
    std::string file;         // Sample zones
    float tune = 0.0f, gainDb = 0.0f, pan = 0.0f;
    bool keytrack = false;    // pitch follows the key (from the root)
    int rrGroup = 0;          // > 0: zones of a group take turns
    int choke = 0;
    CellFx fx;                // an effect on this zone's hits (rendered ahead)
};

struct ZoneLayer { std::string name; uint32_t colour = 0xff7fe0d0; bool on = true; };

struct KeyMapState
{
    std::vector<ZoneLayer> layers;
    std::vector<Zone> zones;
    static KeyMapState drumMap()
    {
        KeyMapState k;
        k.layers.push_back ({ "Drums", 0xff7fe0d0, true });
        for (int p = 0; p < kNumPads; ++p)
        {
            Zone z; z.name = "Pad " + std::to_string (p + 1); z.loKey = z.hiKey = z.root = 36 + p; z.index = p;
            k.zones.push_back (z);
        }
        return k;
    }
};

// ------------------------------------------------------------------ Matrixxxer: One Shot
struct OneShotCell
{
    int pad = 0, step = 0, span = 2;   // sits on the hit at (pad, step); reaches `span` steps
    float height = 0.5f;               // sets the effect's main control
    CellFx fx;
};
struct OneShotPreset { std::string name = "INIT"; std::vector<OneShotCell> cells; };

// ------------------------------------------------------------------ Matrixxxer: Sample
struct SampleOp
{
    int c0 = 0, c1 = 4;                    // cells (of 32) it spans in time
    float fLo = 200.0f, fHi = 4000.0f;     // the band it acts on; the rest passes untouched
    CellFx fx;
    float mix = 1.0f;
};
struct SamplePreset
{
    std::string name = "INIT";
    int pad = 15;                 // the pad that plays the result
    std::string file;             // empty: the pad's own hit is the sample
    float seconds = 2.0f;         // length when the pad's hit is the sample
    std::vector<SampleOp> ops;
};

// ------------------------------------------------------------------ Matrixxxer: Sequencer (draw with sound)
enum class MarkType : uint8_t { Dot, Dash, Warble, Tick, Spray };
enum class MarkEngine : uint8_t { Add, FM, FFT, Smp };
struct Mark
{
    MarkType type = MarkType::Dot;
    float start = 0.0f, length = 1.0f;   // grid steps
    int row = 12;                        // 0 .. kRows-1 from the bottom: degrees of the song scale
    float intensity = 0.6f;              // brightness / FM index / grain density
    float inertia = 0.08f;               // seconds of glide and swell
    float level = 0.8f;
    float depth = 35.0f, rate = 6.0f;    // warble: cents and Hz
    float pan = 0.0f;
};
struct SeqPreset
{
    static constexpr int kRows = 36;     // three octaves of the scale... capped at 36 rows
    std::string name = "INIT";
    MarkEngine engine = MarkEngine::Add;
    int root = 0;                        // 0 = C
    int scale = 2;                       // index into scaleNames()
    int grid = 1;                        // 0 1/16, 1 1/16T, 2 1/8T, 3 1/32
    int bars = 4;
    bool mirror = false;                 // marks speak only where the main pattern hits
    int baseOctave = 3;                  // row 0 = root in this octave (MIDI octave, C3 = 48)
    float grainMs = 40.0f, spray = 0.35f;
    int samplePad = -1;                  // Smp engine: grains of this pad's hit (-1: the selected kit's pad 1)
    float level = 0.7f;
    std::vector<Mark> marks;
};

inline const std::vector<std::string>& scaleNames()
{
    static const std::vector<std::string> n { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Minor Pent", "Major Pent", "Blues", "Chromatic" };
    return n;
}
inline const std::vector<int>& scaleSteps (int scale)
{
    static const std::vector<std::vector<int>> s {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 6, 7, 10 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return s[(size_t) std::max (0, std::min (scale, (int) s.size() - 1))];
}
// MIDI note of a row of the canvas
inline int rowNote (const SeqPreset& p, int row)
{
    const auto& st = scaleSteps (p.scale);
    const int n = (int) st.size();
    const int oct = row / n, deg = row % n;
    return 12 * (p.baseOctave + 1) + p.root + 12 * oct + st[(size_t) deg];
}
inline double gridBeats (int grid)
{
    switch (grid) { case 0: return 0.25; case 1: return 1.0 / 6.0; case 2: return 1.0 / 3.0; default: return 0.125; }
}

struct MatrixState
{
    int view = 1;   // 0 Sequencer, 1 One Shot, 2 Sample
    bool seqOn = true, oneShotOn = true, sampleOn = true;
    int seqIndex = 0, oneShotIndex = 0, sampleIndex = 0;   // which factory/user preset they came from (display)
    SeqPreset seq;
    OneShotPreset oneShot;
    SamplePreset sample;
};

// ------------------------------------------------------------------ the Blip-style grid
struct GridPattern
{
    static constexpr int kSize = 64;
    int length = 64;
    int rate = 1;        // 0 1/8, 1 1/16, 2 1/16T, 3 1/32
    float swing = -1.0f; // < 0: follow the main swing
    std::array<uint32_t, kSize * kSize> cells {};   // [row * 64 + col]: R vol, G pan, B pitch, A flags
    static constexpr uint32_t kOn = 1u, kFs = 2u, kBd = 4u;
    static uint32_t pack (int r, int g, int b, uint32_t flags) { return ((uint32_t) (r & 255) << 24) | ((uint32_t) (g & 255) << 16) | ((uint32_t) (b & 255) << 8) | (flags & 255u); }
    static int red (uint32_t c) { return (int) (c >> 24) & 255; }
    static int green (uint32_t c) { return (int) (c >> 16) & 255; }
    static int blue (uint32_t c) { return (int) (c >> 8) & 255; }
    static uint32_t flags (uint32_t c) { return c & 255u; }
    bool empty() const { for (auto c : cells) if (c & kOn) return false; return true; }
};
inline double gridRateBeats (int rate)
{
    switch (rate) { case 0: return 0.5; case 1: return 0.25; case 2: return 1.0 / 6.0; default: return 0.125; }
}

struct GridState
{
    std::string folder;
    std::vector<std::string> files;   // one per row (row % count)
    bool useKit = true;               // rows play the kit's 16 pads (until a folder is loaded)
    // 16 patterns of 64 x 64 cells (256 KB) live on the heap, so a Project stays small enough for any
    // thread's stack (Windows gives threads 1 MB). Always 16; copies are deep.
    std::vector<GridPattern> patterns = std::vector<GridPattern> (16);
    int current = 0;
    bool play = false;
    float level = 0.8f;
};

// ------------------------------------------------------------------ modulation
enum class ModSrc : uint8_t { Lfo1, Lfo2, Env1, Random, Velocity, Key, Height, Step, XyX, XyY, Count };
enum class Scope : uint8_t { Kit, Pad, Cell };
inline const char* modSrcName (ModSrc s)
{
    static const char* n[] = { "LFO 1", "LFO 2", "ENV 1", "RANDOM", "VELOCITY", "KEY POSITION", "CELL HEIGHT", "STEP", "XY X", "XY Y" };
    return n[(size_t) std::min ((int) s, (int) ModSrc::Count - 1)];
}

struct LfoSpec
{
    int shape = 0;          // 0 sine, 1 triangle, 2 saw, 3 square, 4 drawn (LFO 2's table)
    bool sync = true;
    float beats = 1.0f;     // one cycle (sync)
    float hz = 2.0f;        // one cycle (free)
    Scope scope = Scope::Kit;
};

struct ModRoute
{
    ModSrc src = ModSrc::Lfo1;
    std::string dest;       // "pad/<n|*>/<macro>", "cell/<i>/main", "fs/<slot>/<param>", "bd/<slot>/<param>",
                            // "seq/<level|pan|pitch>", "grid/<level|pan|pitch>", "master"
    float depth = 0.3f;     // -1..1 of the destination's range
};

// A real-time algorithm on a pad's voices, its depth driven by a source (the old global matrix's ideas:
// ring mod turning a kick into a metallic ding, a comb, a crusher, a sweeping filter, a wavefolder).
struct PadAlgo
{
    int type = 0;           // 0 none, 1 ring, 2 comb, 3 crush, 4 filter, 5 fold
    float amount = 0.5f;    // ratio / tuning / bits / cutoff / fold
    float mix = 0.5f;
    ModSrc src = ModSrc::Env1;
    float srcDepth = 0.0f;  // how far the source moves `amount`
};
inline const char* padAlgoName (int t)
{
    static const char* n[] = { "OFF", "RING MOD", "COMB", "CRUSH", "FILTER", "FOLD" };
    return n[std::max (0, std::min (t, 5))];
}

// The part of the mod state the voices read while they play (no containers: the audio thread copies it).
struct ModParams
{
    LfoSpec lfo1 { 0, true, 1.0f, 2.0f, Scope::Kit };
    LfoSpec lfo2 { 4, true, 0.5f, 2.0f, Scope::Pad };
    std::array<float, 64> table {};     // LFO 2's drawn shape, -1..1
    float envA = 0.004f, envD = 0.35f;  // ENV 1 (restarts on every hit)
    float randBeats = 0.25f; Scope randScope = Scope::Kit;
    float xyX = 0.5f, xyY = 0.5f;
    ModParams()
    {
        for (int i = 0; i < 64; ++i)
        {
            const float t = (float) i / 64.0f;
            table[(size_t) i] = 0.6f * std::sin (6.2831853f * 3.0f * t) + 0.4f * std::sin (6.2831853f * 9.0f * t + 1.0f) * (1.0f - t);
        }
    }
};

struct ModState : ModParams
{
    std::vector<ModRoute> routes;
    std::array<PadAlgo, kNumPads> algos {};
};

// ------------------------------------------------------------------ the whole project
struct Project
{
    std::array<PadFx, kNumPads> padFx {};
    std::array<FsSlot, kNumSlots> fs {};
    std::array<BdSlot, kNumSlots> bd {};
    KeyMapState keymap = KeyMapState::drumMap();
    MatrixState matrix;
    GridState grid;
    ModState mod;
    float glue = 0.4f;
};
} // namespace nb
