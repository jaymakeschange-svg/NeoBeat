// Batchcut 3.0 - LOOPS: the light sample sequencer. Quick loops of up to 8 bars in up to 8 layers, made of
// the sounds already in Batchcut (the pool the Kit room fills), rolled by musical rules, edited, played by
// the transport or from a MIDI keyboard, and exported. Framework-free.
//
//   LoopLayer   where its sounds come from, how it fills (Slice, Pool, Stutter, Melody), its grid, density,
//               deja vu, level, pan, and its hits
//   rollLayer   a layer filled by the rules:
//                 time        every chop starts on the swung grid and is cut to a note value, with short fades
//                 roles       low, heavy chops on the strong beats, short bright ones between (or the rhythm
//                             of a lane of one of NeoBeat's 128 patterns)
//                 pitch       melody chops are tuned into the key
//                 repetition  deja vu: from a bar that repeats for ever to a new bar every time; a seed
//                             rebuilds any roll exactly
//                 taste       the learner (your stars) weighs the choice, or RANDOM deals without ranking
//   LoopPlayer  the loop on the audio thread: the host's beat (or its own), and MIDI (Trigger: a key plays
//               the loop transposed; Slices: each key plays one chop)
//   renderLoop  the loop as sound, its tails wrapped round to the start so the file loops without a click
#pragma once

#include "../StereoRoom.h"

#include "Kit.h"
#include "../../gen/Generators.h"
#include "../../gen/Scale.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bc
{
constexpr int kLoopLayers = 8, kLoopMaxBars = 8;

enum class LoopMode : uint8_t { Slice = 0, Pool = 1, Stutter = 2, Melody = 3 };
inline const char* loopModeName (LoopMode m)
{
    switch (m) { case LoopMode::Slice: return "Slice"; case LoopMode::Pool: return "Pool"; case LoopMode::Stutter: return "Stutter"; default: return "Melody"; }
}
inline const char* familyName (int f)
{
    static const char* n[12] = { "Kicks", "Snares", "Hats", "Rims", "Thuds", "Clanks", "Junk", "Drops", "Horns", "Stabs", "Bells", "FX" };
    return n[std::max (0, std::min (f, 11))];
}

// where a layer's sounds come from: everything in the pool, one recording (or folder), or one kind of sound
struct LoopSource
{
    enum Kind : uint8_t { Everything = 0, From = 1, Family = 2 } kind = Everything;
    std::string from;
    int family = 0;
    bool takes (const PoolItem& it) const
    {
        if (! it.f.valid) return false;
        if (kind == From) return it.source == from;
        if (kind == Family) return KitModel::familyOf (it.bestRole) == family;
        return true;
    }
    std::string label() const { return kind == From ? from : kind == Family ? std::string (familyName (family)) : std::string ("Whole pool"); }
    bool operator== (const LoopSource& o) const noexcept { return kind == o.kind && from == o.from && family == o.family; }
};

struct LoopHit
{
    int step = 0;                 // on the layer's grid, 0 .. steps-1
    int len = 1;                  // in steps (the chop is cut there)
    int ratchet = 1;              // 1: one hit; 2..4: that many inside the step
    std::shared_ptr<const PoolItem> sound;
    float semis = 0.0f, levelDb = 0.0f, vel = 1.0f;
    bool reverse = false, locked = false;
};
inline bool operator== (const LoopHit& a, const LoopHit& b) noexcept
{
    return a.step == b.step && a.len == b.len && a.ratchet == b.ratchet && a.sound == b.sound && a.semis == b.semis && a.levelDb == b.levelDb
        && a.vel == b.vel && a.reverse == b.reverse && a.locked == b.locked;
}

struct LoopLayer
{
    LoopMode mode = LoopMode::Pool;
    LoopSource source;
    int bars = 2, perBar = 16;            // 8, 12 (8th triplets), 16 or 24 (16th triplets) steps a bar
    float density = 0.5f, dejavu = 0.6f, levelDb = 0.0f, pan = 0.0f;
    bool mute = false, solo = false, random = false;   // random: chops dealt without ranking (as RANDOM in the Kit room)
    int midiMode = 0;                      // 0 Trigger (a key plays the loop, transposed), 1 Slices (each key one chop)
    int rhythm = -1, rhythmLane = 4;       // -1: its own rhythm; else the hits of that lane of NeoBeat pattern `rhythm`
    uint64_t seed = 1;
    std::vector<LoopHit> hits;             // by step
    int steps() const noexcept { return std::max (1, bars) * std::max (1, perBar); }
};
inline bool operator== (const LoopLayer& a, const LoopLayer& b) noexcept
{
    return a.mode == b.mode && a.source == b.source && a.bars == b.bars && a.perBar == b.perBar && a.density == b.density && a.dejavu == b.dejavu
        && a.levelDb == b.levelDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo && a.random == b.random && a.midiMode == b.midiMode
        && a.rhythm == b.rhythm && a.rhythmLane == b.rhythmLane && a.seed == b.seed && a.hits == b.hits;
}

struct LoopSet
{
    std::array<LoopLayer, kLoopLayers> layers {};
    int numLayers = 1;
    double bpm = 100.0;
    float swing = 0.62f;
    int root = 0, scale = 3;               // C dorian
    std::string name = "My loop";

    int bars() const noexcept { int b = 1; for (int i = 0; i < numLayers; ++i) b = std::max (b, layers[(size_t) i].bars); return b; }
    double beats() const noexcept { return 4.0 * bars(); }
    bool anySolo() const noexcept { for (int i = 0; i < numLayers; ++i) if (layers[(size_t) i].solo) return true; return false; }
    bool audible (int i) const noexcept { if (i < 0 || i >= numLayers) return false; const auto& L = layers[(size_t) i]; return anySolo() ? L.solo : ! L.mute; }
};
inline bool operator== (const LoopSet& a, const LoopSet& b) noexcept
{
    if (a.numLayers != b.numLayers || a.bpm != b.bpm || a.swing != b.swing || a.root != b.root || a.scale != b.scale || a.name != b.name) return false;
    for (int i = 0; i < a.numLayers; ++i) if (! (a.layers[(size_t) i] == b.layers[(size_t) i])) return false;
    return true;
}

// A layer's hits kept inside it: a valid grid and length, no chop past its end or without a sound, in order.
inline void tidyLayer (LoopLayer& L)
{
    L.bars = std::max (1, std::min (L.bars, kLoopMaxBars));
    if (L.perBar != 8 && L.perBar != 12 && L.perBar != 16 && L.perBar != 24) L.perBar = 16;
    const int n = L.steps();
    L.hits.erase (std::remove_if (L.hits.begin(), L.hits.end(), [n] (const LoopHit& h) { return ! h.sound || h.step < 0 || h.step >= n; }), L.hits.end());
    for (auto& h : L.hits) { h.len = std::max (1, std::min (h.len, n)); h.ratchet = std::max (1, std::min (h.ratchet, 4)); }
    std::stable_sort (L.hits.begin(), L.hits.end(), [] (const LoopHit& a, const LoopHit& b) { return a.step < b.step; });
    // one chop to a step
    L.hits.erase (std::unique (L.hits.begin(), L.hits.end(), [] (const LoopHit& a, const LoopHit& b) { return a.step == b.step; }), L.hits.end());
}

// A layer moved to another grid: its chops keep their place in time as near as the new grid allows.
inline void regrid (LoopLayer& L, int perBar)
{
    if (perBar == L.perBar || perBar <= 0) return;
    for (auto& h : L.hits)
    {
        h.step = (int) std::lround ((double) h.step * perBar / (double) L.perBar);
        h.len = std::max (1, (int) std::lround ((double) h.len * perBar / (double) L.perBar));
    }
    L.perBar = perBar;
    tidyLayer (L);
}

// ------------------------------------------------------------------ rolling a layer
namespace loopsdetail
{
// how strong a step is: the downbeat, the half bar, the other beats, the 8ths, the rest
inline float weight (int s, int perBar)
{
    const int p = ((s % perBar) + perBar) % perBar;
    if (p == 0) return 1.0f;
    if (perBar % 3 == 0)                                  // triplet grids: the beats, then the rest
    {
        const int beat = perBar / 4;
        return p % beat == 0 ? (p == 2 * beat ? 0.8f : 0.7f) : 0.4f;
    }
    if (p * 2 == perBar) return 0.8f;
    if (p % (perBar / 4) == 0) return 0.7f;
    if (perBar >= 16 && p % (perBar / 8) == 0) return 0.5f;
    return 0.3f;
}
// a sound's pitch as a MIDI note (0: none clear enough)
inline float midiPitch (const PoolItem& it)
{
    if (it.f.pitchStrength < 0.35f || it.f.pitchHz < 30.0f || it.f.pitchHz > 4000.0f) return 0.0f;
    return 69.0f + 12.0f * std::log2 (it.f.pitchHz / 440.0f);
}
// how heavy it is: low, long and full sit on the strong beats
inline float heaviness (const PoolItem& it)
{
    const float c = std::min (1.0f, it.f.centroidHz / 5000.0f);
    return it.f.low * 1.2f + (1.0f - c) * 0.6f + std::min (1.0f, it.f.decayMs / 400.0f) * 0.3f - it.f.high * 0.4f;
}
inline float seconds (const PoolItem& it) { return it.sr > 0 ? (float) ((double) it.l.size() / it.sr) : 0.0f; }

// choosing chops: by taste (a weighted choice the learner leans) or RANDOM (dealt without ranking)
struct Picker
{
    std::vector<std::shared_ptr<const PoolItem>> items;
    std::vector<double> cum;
    gen::Dealer dealer;
    bool random = false;

    void build (std::vector<std::shared_ptr<const PoolItem>> list, nbk::Learner* learner, bool rnd, uint64_t seed)
    {
        items = std::move (list); random = rnd || learner == nullptr;
        cum.clear(); double t = 0.0;
        for (const auto& it : items)
        {
            double w = 1.0;
            if (! random)
            {
                w = 0.6 + (double) learner->preference (it->f, it->bestRole);
                const float stars = learner->ratingOf (it->x);
                if (stars > 0.0f) w *= 0.4 + 0.3 * stars;           // 1 star: 0.7, 5 stars: 1.9
            }
            t += std::max (0.01, w); cum.push_back (t);
        }
        std::vector<int> all; for (int i = 0; i < (int) items.size(); ++i) all.push_back (i);
        dealer.reset (all, (uint32_t) (seed ^ (seed >> 32)));
    }
    bool empty() const noexcept { return items.empty(); }
    // u: a value from the deja vu stream (the same u gives the same chop when tasting)
    int pick (double u)
    {
        if (items.empty()) return -1;
        if (random) return dealer.deal();
        const double x = u * cum.back();
        return (int) std::min<size_t> (items.size() - 1, (size_t) (std::upper_bound (cum.begin(), cum.end(), x) - cum.begin()));
    }
};

// lengths: each chop runs to the next hit (or its own end, if sooner), cut to a note value
inline void cutToNotes (LoopLayer& L, double bpm, bool legato)
{
    auto& H = L.hits;
    std::sort (H.begin(), H.end(), [] (const LoopHit& a, const LoopHit& b) { return a.step < b.step; });
    const int n = L.steps();
    const double stepSec = 60.0 / std::max (30.0, bpm) * 4.0 / (double) L.perBar;
    static const int notes[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
    for (size_t i = 0; i < H.size(); ++i)
    {
        auto& h = H[i];
        if (h.locked) continue;
        const int next = i + 1 < H.size() ? H[i + 1].step : H[0].step + n;
        const int gap = std::max (1, next - h.step);
        const int own = h.sound ? std::max (1, (int) std::ceil (seconds (*h.sound) / stepSec)) : 1;
        const int room = legato ? gap : std::min (gap, own);
        int len = 1; for (int v : notes) if (v <= room) len = v;
        h.len = std::max (1, len);
    }
}
} // namespace loopsdetail

// the pool's sounds this layer may use
inline std::vector<std::shared_ptr<const PoolItem>> loopCandidates (const LoopLayer& L, const std::vector<std::shared_ptr<PoolItem>>& pool)
{
    std::vector<std::shared_ptr<const PoolItem>> out;
    for (const auto& it : pool) if (it && L.source.takes (*it)) out.push_back (it);
    if (L.mode == LoopMode::Slice) std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a->name < b->name; });
    if (L.mode == LoopMode::Melody)
    {
        std::vector<std::shared_ptr<const PoolItem>> pitched;
        for (const auto& it : out) if (loopsdetail::midiPitch (*it) > 0.0f) pitched.push_back (it);
        if (pitched.size() >= 2) out = std::move (pitched);   // tuned sounds, when there are some
    }
    return out;
}

// Fills a layer by the rules, from its seed (the same seed and settings: the same layer). Locked hits stay.
// Returns false when its source has no sound in the pool.
inline bool rollLayer (LoopLayer& L, const std::vector<std::shared_ptr<PoolItem>>& pool, nbk::Learner* learner, int root, int scaleIndex, double bpm)
{
    using namespace loopsdetail;
    L.bars = std::max (1, std::min (L.bars, kLoopMaxBars));
    if (L.perBar != 8 && L.perBar != 12 && L.perBar != 16 && L.perBar != 24) L.perBar = 16;
    const int n = L.steps(), perBar = L.perBar;
    std::vector<LoopHit> keep;
    for (const auto& h : L.hits) if (h.locked && h.step < n && h.sound) keep.push_back (h);
    std::vector<char> taken ((size_t) n, 0);
    for (const auto& h : keep) taken[(size_t) h.step] = 1;

    auto cand = loopCandidates (L, pool);
    if (cand.empty()) { L.hits = keep; return false; }

    // heavy and light halves (Pool): the strong beats take from the first
    std::vector<std::shared_ptr<const PoolItem>> heavy = cand, light = cand;
    if (L.mode == LoopMode::Pool && cand.size() >= 4)
    {
        auto sorted = cand;
        std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return heaviness (*a) > heaviness (*b); });
        heavy.assign (sorted.begin(), sorted.begin() + (std::ptrdiff_t) (sorted.size() / 2));
        light.assign (sorted.begin() + (std::ptrdiff_t) (sorted.size() / 2), sorted.end());
    }
    const bool slice = L.mode == LoopMode::Slice;
    Picker pHeavy, pLight;
    pHeavy.build (heavy, learner, L.random || slice, gen::Rng::mix (L.seed, 11));
    pLight.build (light, learner, L.random || slice, gen::Rng::mix (L.seed, 12));

    // the deja vu streams: one bar long, so deja vu repeats bars
    gen::DejaVu rhythm, pick, vary, notes;
    rhythm.reset (gen::Rng::mix (L.seed, 1), perBar); pick.reset (gen::Rng::mix (L.seed, 2), perBar);
    vary.reset (gen::Rng::mix (L.seed, 3), perBar);   notes.reset (gen::Rng::mix (L.seed, 4), perBar);
    for (auto* d : { &rhythm, &pick, &vary, &notes }) d->setDejaVu (L.dejavu);
    gen::Rng rng (gen::Rng::mix (L.seed, 5));

    // which steps play
    const float density = std::max (0.0f, std::min (1.0f, L.density));
    std::vector<float> velAt ((size_t) n, 0.0f);
    if (L.rhythm >= 0)
    {
        // borrowed: the hits of one lane of a NeoBeat pattern (16ths), placed on this layer's grid
        const auto preset = nb::patternPreset (std::max (0, std::min (L.rhythm, nb::numPatterns() - 1)));
        const int lane = std::max (0, std::min (L.rhythmLane, 15)), plen = std::max (1, preset.pattern.length);
        for (int s16 = 0; s16 < L.bars * 16; ++s16)
        {
            const auto& st = preset.pattern.steps[(size_t) lane][(size_t) (s16 % plen)];
            if (st.vel == 0) continue;
            const int s = (int) std::lround ((double) s16 * perBar / 16.0);
            if (s >= n || rng.uni() > 0.35 + 0.65 * density / 0.8) continue;   // density thins it (all of it from 0.8)
            velAt[(size_t) s] = std::max (velAt[(size_t) s], (float) st.vel / 127.0f);
        }
    }
    else
    {
        const float scale = L.mode == LoopMode::Melody ? 0.8f : 1.0f;
        for (int s = 0; s < n; ++s)
        {
            const float w = weight (s, perBar);
            const float p = density <= 0.0f ? 0.0f : std::pow (density * scale, slice ? 1.2f - 0.4f * w : 1.5f - w);
            const float v = rhythm.next();
            if (v < p) velAt[(size_t) s] = 0.72f + 0.28f * w;
        }
        if (density >= 0.15f && velAt[0] <= 0.0f && ! taken[0]) velAt[0] = 1.0f;   // a loop starts on its one
    }

    // the chops
    std::vector<LoopHit> made;
    std::vector<int> lastAt ((size_t) perBar, -1);      // RANDOM and Slice: the chop at each bar position, for deja vu
    std::vector<int> stutterSet;                         // Stutter: its few chops
    if (L.mode == LoopMode::Stutter)
    {
        const int k = 1 + (int) rng.below (3);
        for (int i = 0; i < k; ++i) { const int c = pLight.pick (rng.uni()); if (c >= 0) stutterSet.push_back (c); }
    }
    for (int s = 0; s < n; ++s)
    {
        const float vel = velAt[(size_t) s];
        const float u = pick.next(), var = vary.next(), nu = notes.next();
        if (vel <= 0.0f || taken[(size_t) s]) continue;
        const float w = weight (s, perBar);
        auto& pk = (L.mode == LoopMode::Pool && w >= 0.7f) ? pHeavy : pLight;
        int c;
        if (L.mode == LoopMode::Stutter && ! stutterSet.empty()) c = stutterSet[(size_t) std::min<size_t> (stutterSet.size() - 1, (size_t) (u * (float) stutterSet.size()))];
        else if (pk.random)
        {
            // dealt: a bar position keeps its chop as often as deja vu says, otherwise the next card
            const int pos = s % perBar;
            c = (lastAt[(size_t) pos] >= 0 && rng.uni() < std::min (1.0, (double) L.dejavu * 1.12)) ? lastAt[(size_t) pos] : pk.pick (u);
            lastAt[(size_t) pos] = c;
        }
        else c = pk.pick (u);
        if (c < 0) continue;
        LoopHit h; h.step = s; h.sound = pk.items[(size_t) c]; h.vel = std::min (1.0f, vel + (var - 0.5f) * 0.16f);
        if (L.mode == LoopMode::Melody)
        {
            // a degree of the key around the root (C2 .. C4 in Live's names), the chop tuned to it
            const auto& sc = gen::scale (scaleIndex);
            const int degree = (int) (nu * 9.0f) - 2;
            const int target = gen::degreeNote (degree, 48 + root, root, sc);
            float own = midiPitch (*h.sound); if (own <= 0.0f) own = (float) (48 + root);
            float semis = (float) target - own;
            while (semis > 12.0f) semis -= 12.0f;
            while (semis < -12.0f) semis += 12.0f;
            h.semis = std::round (semis * 100.0f) / 100.0f;
        }
        if (L.mode == LoopMode::Stutter)
        {
            if (rng.uni() < 0.25 + 0.4 * density) h.ratchet = 2 + (int) rng.below (3);
            made.push_back (h);
            // a burst: the same chop on the next steps, sometimes climbing
            if (rng.uni() < 0.35)
            {
                const int run = 1 + (int) rng.below (3); const float climb = rng.uni() < 0.4 ? (float) (1 + rng.below (3)) : 0.0f;
                for (int k = 1; k <= run && s + k < n; ++k)
                {
                    if (taken[(size_t) (s + k)] || velAt[(size_t) (s + k)] > 0.0f) break;
                    LoopHit b = h; b.step = s + k; b.ratchet = 1; b.semis = h.semis + climb * (float) k; b.vel = h.vel * 0.85f;
                    made.push_back (b); taken[(size_t) (s + k)] = 1;
                }
            }
            continue;
        }
        made.push_back (h);
    }
    L.hits = keep;
    for (auto& h : made) if (h.step < n) L.hits.push_back (h);
    cutToNotes (L, bpm, L.mode == LoopMode::Slice || L.mode == LoopMode::Melody);
    if (L.mode == LoopMode::Stutter) for (auto& h : L.hits) if (! h.locked) h.len = std::min (h.len, 2);
    return true;
}

// another chop for one hit, of the same kind; its place and length stay
inline bool rerollHit (LoopLayer& L, int index, const std::vector<std::shared_ptr<PoolItem>>& pool, nbk::Learner* learner, uint64_t seed)
{
    if (index < 0 || index >= (int) L.hits.size()) return false;
    auto cand = loopCandidates (L, pool);
    if (cand.size() < 2) return false;
    loopsdetail::Picker pk; pk.build (cand, learner, L.random, seed);
    gen::Rng rng (seed);
    auto& h = L.hits[(size_t) index];
    for (int tries = 0; tries < 8; ++tries)
    {
        const int c = pk.pick (rng.uni());
        if (c >= 0 && pk.items[(size_t) c] != h.sound) { h.sound = pk.items[(size_t) c]; return true; }
    }
    return false;
}

// ------------------------------------------------------------------ the loop as events
struct LoopEvent
{
    double beat = 0.0, lenBeats = 0.25;   // in quarter notes, swung, inside the layer's own loop
    int layer = 0;
    int hit = -1;                         // which chop of that layer, so each can be placed on its own
    const PoolItem* sound = nullptr;
    float semis = 0.0f, gain = 1.0f;      // gain: the hit's level, velocity and loudness match (the layer's own level apart)
    bool reverse = false;
};
struct LoopSlice { const PoolItem* sound = nullptr; float semis = 0.0f, gain = 1.0f; bool reverse = false; int layer = 0; };

// What the player plays: built on the message thread whenever the loop changes, then handed over whole.
struct LoopSnapshot
{
    std::vector<LoopEvent> events;                          // by layer, then beat
    std::array<int, kLoopLayers + 1> first {};              // events of layer i: [first[i], first[i + 1])
    std::array<double, kLoopLayers> layerBeats {};
    std::array<float, kLoopLayers> gainL {}, gainR {};
    std::array<bool, kLoopLayers> audible {};
    std::array<int, kLoopLayers> midiMode {};
    std::vector<LoopSlice> slices;                          // Slices: C3 upwards, each distinct chop of the Slices layers
    std::vector<std::shared_ptr<const PoolItem>> hold;      // keeps every sound alive while this snapshot is in use
    double bpm = 100.0, beats = 8.0;
};

inline float hitGain (const LoopHit& h)
{
    const float match = h.sound ? nbk::matchGainDb ({ h.sound->lufs, h.sound->peakDb }) : 0.0f;
    return std::pow (10.0f, (h.levelDb + match) / 20.0f) * std::max (0.0f, h.vel);
}

inline std::shared_ptr<LoopSnapshot> makeSnapshot (const LoopSet& set)
{
    auto s = std::make_shared<LoopSnapshot>();
    s->bpm = set.bpm; s->beats = set.beats();
    std::map<const PoolItem*, std::shared_ptr<const PoolItem>> held;
    for (int i = 0; i < kLoopLayers; ++i)
    {
        s->first[(size_t) i] = (int) s->events.size();
        if (i >= set.numLayers) continue;
        const auto& L = set.layers[(size_t) i];
        const int perBar = std::max (1, L.perBar);
        s->layerBeats[(size_t) i] = 4.0 * std::max (1, L.bars);
        const float g = std::pow (10.0f, L.levelDb / 20.0f), pan = std::max (-1.0f, std::min (1.0f, L.pan));
        s->gainL[(size_t) i] = g * (pan > 0.0f ? 1.0f - pan : 1.0f); s->gainR[(size_t) i] = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
        s->audible[(size_t) i] = set.audible (i);
        s->midiMode[(size_t) i] = L.midiMode;
        for (int hitIndex = 0; hitIndex < (int) L.hits.size(); ++hitIndex)
        {
            const auto& h = L.hits[(size_t) hitIndex];
            if (! h.sound || h.step < 0 || h.step >= L.steps()) continue;
            held[h.sound.get()] = h.sound;
            const double a = gen::swung (h.step, perBar, set.swing), b = gen::swung (h.step + std::max (1, h.len), perBar, set.swing);
            const int r = std::max (1, std::min (h.ratchet, 4));
            const double each = (gen::swung (h.step + 1, perBar, set.swing) - a) / (double) r;
            for (int k = 0; k < r; ++k)
            {
                LoopEvent e; e.layer = i; e.hit = hitIndex; e.sound = h.sound.get(); e.semis = h.semis; e.reverse = h.reverse;
                e.beat = a + each * k;
                e.lenBeats = r > 1 ? each : b - a;
                // a ratchet rises to the written hit, as a drummer plays a roll
                e.gain = hitGain (h) * (r > 1 ? 0.55f + 0.45f * (float) (k + 1) / (float) r : 1.0f);
                s->events.push_back (e);
            }
        }
        std::stable_sort (s->events.begin() + s->first[(size_t) i], s->events.end(), [] (const LoopEvent& x, const LoopEvent& y) { return x.beat < y.beat; });
        if (L.midiMode == 1)
        {
            // each distinct chop once, in the order they first play
            for (int k = s->first[(size_t) i]; k < (int) s->events.size(); ++k)
            {
                const auto& e = s->events[(size_t) k];
                bool seen = false;
                for (const auto& sl : s->slices) seen = seen || (sl.sound == e.sound && sl.semis == e.semis && sl.reverse == e.reverse && sl.layer == i);
                if (! seen && s->slices.size() < 68) s->slices.push_back ({ e.sound, e.semis, e.gain, e.reverse, i });
            }
        }
    }
    s->first[(size_t) kLoopLayers] = (int) s->events.size();
    for (auto& h : held) s->hold.push_back (h.second);
    return s;
}

// ------------------------------------------------------------------ the player (audio thread)
struct LoopHost { bool valid = false, playing = false; double ppq = 0.0, bpm = 120.0; };
struct LoopNote { int offset = 0, note = 60; float velocity = 0.8f; bool on = true; };

// What the Stereoroom gives the player: where each chop stands, and how to find a chop's node.
// Swapped in whole between blocks, so the audio thread never reads a half-written list.
struct LoopPlacing
{
    std::vector<nsr::Place> places;
    std::vector<int> index;              // layer * stride + hit -> node, or -1
    int stride = 0;
    int indexOf (int layer, int hit) const noexcept
    {
        if (layer < 0 || hit < 0 || stride <= 0) return -1;
        const size_t at = (size_t) layer * (size_t) stride + (size_t) hit;
        return at < index.size() ? index[at] : -1;
    }
};

class LoopPlayer
{
public:
    void setPlacing (std::shared_ptr<const LoopPlacing> p) { std::atomic_store (&placing, std::move (p)); }
    // where the song is, taken from the host each block, so a travelling chop is placed where it
    // actually is rather than where it was when the list was last written
    double nowBeats = 0.0;
    float* sendL = nullptr;
    float* sendR = nullptr;
    static constexpr int kVoices = 64;
    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& v : voices) v = {};
        clock = 0.0; trig = Trigger(); limiter = 1.0f;
        release = std::exp (-1.0f / (0.12f * (float) sr));
    }

    // message thread: the loop changed. The caller keeps the snapshot alive (the audio thread never frees one).
    void setSnapshot (std::shared_ptr<const LoopSnapshot> s) { std::atomic_store (&next, std::move (s)); }
    std::atomic<bool> playing { false };                   // the room's PLAY
    std::atomic<float> level { 0.8f };
    std::atomic<double> beatNow { -1.0 };                  // where the loop is (for the playhead), -1: stopped
    void audition (const PoolItem* s, float semis = 0.0f, float gain = 1.0f) { pendingSound.store (s); pendingSemis.store (semis); pendingGain.store (gain); }

    // adds into l/r
    // Where the placed chops send their share of the room. Null (the default) means no send bus,
    // which is Batchcut: nothing is placed there, so nothing is sent.
    void setSendBus (float* l, float* r) noexcept { sendL = l; sendR = r; }

    void process (float* l, float* r, int n, const LoopHost& host, const LoopNote* notes = nullptr, int numNotes = 0) noexcept
    {
        if (auto s = std::atomic_exchange (&next, std::shared_ptr<const LoopSnapshot>())) held = std::move (s);
        nowBeats = host.valid ? host.ppq : nowBeats + (double) n * host.bpm / (60.0 * sr);
        if (const auto* a = pendingSound.exchange (nullptr)) start (a, pendingSemis.load(), pendingGain.load(), 0, -1.0, false, -1);
        const LoopSnapshot* snap = held.get();
        const double bpm = host.valid && host.bpm > 1.0 ? host.bpm : (snap != nullptr ? snap->bpm : 120.0);
        const double beatsPerSample = bpm / 60.0 / sr;
        const bool run = playing.load();
        // the transport clock: the host's beat while it plays, else our own
        double b0 = clock;
        if (run && host.valid && host.playing) b0 = host.ppq;
        else if (run && ! wasRunning) b0 = 0.0;
        wasRunning = run;
        // split the block at the MIDI notes, so each lands on its sample
        int at = 0, ni = 0;
        while (at < n)
        {
            const int end = ni < numNotes ? std::max (at, std::min (n, notes[ni].offset)) : n;
            if (end > at && snap != nullptr)
            {
                const double a = b0 + beatsPerSample * at, b = b0 + beatsPerSample * end;
                if (run) schedule (*snap, a, b, at, beatsPerSample, false);
                if (trig.on)
                {
                    const double ta = trig.beat, tb = trig.beat + beatsPerSample * (end - at);
                    schedule (*snap, ta, tb, at, beatsPerSample, true);
                    trig.beat = tb;
                }
            }
            if (end >= n) break;
            if (snap != nullptr) noteEvent (*snap, notes[ni], end);
            at = end; ++ni;
        }
        clock = b0 + beatsPerSample * n;
        beatNow.store (trig.on ? trig.beat : run ? std::fmod (clock, snap != nullptr ? std::max (0.25, snap->beats) : 8.0) : -1.0);
        render (l, r, n);
    }

private:
    struct Voice
    {
        const PoolItem* s = nullptr; double pos = 0.0, inc = 1.0;
        float gl = 1.0f, gr = 1.0f, fade = 1.0f;
        int delay = 0, left = 0; bool reverse = false; int layer = -1; int hit = -1;
        uint64_t order = 0;
        // where this chop stands, when the Stereoroom has placed it. Each voice keeps its own
        // filter state, so the dullness that carries distance is per chop rather than per layer.
        bool placed = false;
        nsr::Placement place;
        float zl = 0.0f, zr = 0.0f;
    };
    struct Trigger { bool on = false; int note = 60; double beat = 0.0; float semis = 0.0f; };

    void schedule (const LoopSnapshot& s, double a, double b, int offset, double beatsPerSample, bool byTrigger) noexcept
    {
        for (int i = 0; i < kLoopLayers; ++i)
        {
            if (! s.audible[(size_t) i]) continue;
            const bool trigLayer = s.midiMode[(size_t) i] == 0;
            if (byTrigger != (trig.on && trigLayer)) continue;      // while a key holds, Trigger layers follow it alone
            const double L = s.layerBeats[(size_t) i];
            const int f = s.first[(size_t) i], e = s.first[(size_t) i + 1];
            if (L <= 0.0 || f >= e) continue;
            // the window inside the layer's own loop (a block is far shorter than a loop: one wrap at most)
            const double wa = a - L * std::floor (a / L), wb = wa + (b - a);
            for (int pass = 0; pass < 2; ++pass)
            {
                const double lo = pass == 0 ? wa : 0.0, hi = pass == 0 ? std::min (wb, L) : wb - L;
                if (hi <= lo) continue;
                const double base = pass == 0 ? 0.0 : L - wa;
                auto it = std::lower_bound (s.events.begin() + f, s.events.begin() + e, lo, [] (const LoopEvent& x, double v) { return x.beat < v; });
                for (; it != s.events.begin() + e && it->beat < hi; ++it)
                {
                    const int off = offset + (int) std::lround ((base + it->beat - lo) / beatsPerSample);
                    start (it->sound, it->semis + (byTrigger ? trig.semis : 0.0f), it->gain, off, it->lenBeats / beatsPerSample, it->reverse, i, &s, it->hit);
                }
            }
        }
    }
    void noteEvent (const LoopSnapshot& s, const LoopNote& nt, int offset) noexcept
    {
        bool anyTrig = false, anySlices = false;
        for (int i = 0; i < kLoopLayers; ++i) { if (! s.audible[(size_t) i] || s.first[(size_t) i] == s.first[(size_t) i + 1]) continue; anyTrig = anyTrig || s.midiMode[(size_t) i] == 0; anySlices = anySlices || s.midiMode[(size_t) i] == 1; }
        if (nt.on)
        {
            const int k = nt.note - 60;
            if (anySlices && k >= 0 && k < (int) s.slices.size())
            {
                const auto& sl = s.slices[(size_t) k];
                start (sl.sound, sl.semis, sl.gain * std::max (0.05f, nt.velocity), offset, -1.0, sl.reverse, sl.layer, &s);
                return;
            }
            if (anyTrig) { trig.on = true; trig.note = nt.note; trig.beat = 0.0; trig.semis = (float) (nt.note - 60); }
        }
        else if (trig.on && nt.note == trig.note) trig.on = false;
    }
    void start (const PoolItem* s, float semis, float gain, int offset, double lenSamples, bool reverse, int layer, const LoopSnapshot* snap = nullptr, int hit = -1) noexcept
    {
        if (s == nullptr || s->l.size() < 2) return;
        Voice* use = nullptr;
        for (auto& v : voices) if (v.s == nullptr) { use = &v; break; }
        if (use == nullptr) { use = &voices[0]; for (auto& v : voices) if (v.order < use->order) use = &v; }
        use->s = s; use->reverse = reverse; use->delay = std::max (0, offset); use->layer = layer; use->hit = hit; use->fade = 1.0f; use->order = ++counter;
        use->zl = use->zr = 0.0f;
        use->placed = false;
        if (auto st = std::atomic_load (&placing))
        {
            // a chop that has been placed on the stage plays from where it stands
            const int idx = st->indexOf (layer, hit);
            if (idx >= 0 && idx < (int) st->places.size())
            {
                float px = 0.0f, py = 0.0f;
                nsr::placeAt (st->places[(size_t) idx], nowBeats, px, py);
                use->place = nsr::placementFor (px, py, st->places[(size_t) idx].width, st->places[(size_t) idx].locked);
                use->placed = true;
            }
        }
        use->inc = s->sr / sr * std::exp2 ((double) semis / 12.0);
        use->pos = reverse ? (double) s->l.size() - 2.0 : 0.0;
        const int own = (int) ((double) (s->l.size() - 1) / use->inc);
        use->left = lenSamples > 0.0 ? std::min (own, (int) lenSamples + (int) (0.004 * sr)) : own;
        const float gl = snap != nullptr && layer >= 0 ? snap->gainL[(size_t) layer] : 1.0f, gr = snap != nullptr && layer >= 0 ? snap->gainR[(size_t) layer] : 1.0f;
        use->gl = gain * gl; use->gr = gain * gr;
    }
    void render (float* l, float* r, int n) noexcept
    {
        const float lv = level.load();
        const int fadeLen = std::max (1, (int) (0.005 * sr)), fadeIn = std::max (1, (int) (0.0015 * sr));
        for (int i = 0; i < n; ++i)
        {
            float L = 0.0f, R = 0.0f, SL = 0.0f, SR = 0.0f;
            for (auto& v : voices)
            {
                if (v.s == nullptr) continue;
                if (v.delay > 0) { --v.delay; continue; }
                const auto& s = *v.s; const int len = (int) s.l.size();
                const int i0 = (int) v.pos;
                if (i0 < 0 || i0 >= len - 1 || v.left <= 0) { v.s = nullptr; continue; }
                const float f = (float) (v.pos - i0);
                const float x = s.l[(size_t) i0] + f * (s.l[(size_t) i0 + 1] - s.l[(size_t) i0]);
                const float y = s.r.size() == s.l.size() ? s.r[(size_t) i0] + f * (s.r[(size_t) i0 + 1] - s.r[(size_t) i0]) : x;
                // short fades: in over 1.5 ms, out over 5 ms where the chop is cut
                const int done = (int) (v.reverse ? ((double) len - 2.0 - v.pos) / v.inc : v.pos / v.inc);
                float env = done < fadeIn ? (float) (done + 1) / (float) fadeIn : 1.0f;
                if (v.left < fadeLen) env *= (float) v.left / (float) fadeLen;
                if (v.placed)
                {
                    // the chop's own place: mid/side width, a one-pole that dulls with distance,
                    // and equal power referred to the middle so moving it does not change its level
                    float a = v.gl * env * x, b = v.gr * env * y;
                    if (v.place.mono) { const float m = 0.5f * (a + b); a = m; b = m; }
                    else { const float mid = 0.5f * (a + b), sd = 0.5f * (a - b) * v.place.side; a = mid + sd; b = mid - sd; }
                    const float k = 1.0f - std::exp (-6.283185307179586f * std::max (20.0f, v.place.cutoff) / (float) sr);
                    v.zl += k * (a - v.zl); v.zr += k * (b - v.zr);
                    const float gl2 = 1.41421356f * std::cos (v.place.angle) * v.place.gain;
                    const float gr2 = 1.41421356f * std::sin (v.place.angle) * v.place.gain;
                    const float pl = v.zl * gl2, pr = v.zr * gr2;
                    L += pl; R += pr;
                    // what this chop sends to the room: the distance cue proper, kept in its own
                    // place so the room comes back from where the chop is
                    SL += pl * v.place.send; SR += pr * v.place.send;
                }
                else { L += v.gl * env * x; R += v.gr * env * y; }
                v.pos += v.reverse ? -v.inc : v.inc;
                --v.left;
            }
            // layers that add up beyond the output: a limiter keeps the sum under it (export is not limited)
            const float pk = std::max (std::fabs (L), std::fabs (R)) * lv;
            const float want = pk > 0.89f ? 0.89f / pk : 1.0f;
            limiter = want < limiter ? want : 1.0f - (1.0f - limiter) * release;
            l[i] += lv * limiter * L; r[i] += lv * limiter * R;
            if (sendL != nullptr) { sendL[i] += lv * limiter * SL; sendR[i] += lv * limiter * SR; }
        }
    }

    double sr = 48000.0, clock = 0.0;
    float release = 0.9999f, limiter = 1.0f;
    bool wasRunning = false;
    Trigger trig;
    std::shared_ptr<const LoopSnapshot> next, held;
    std::array<Voice, kVoices> voices {};
    uint64_t counter = 0;
    std::shared_ptr<const LoopPlacing> placing;
    std::atomic<const PoolItem*> pendingSound { nullptr };
    std::atomic<float> pendingSemis { 0.0f }, pendingGain { 1.0f };
};

// ------------------------------------------------------------------ offline: the loop as sound
// The mix of the audible layers (onlyLayer -1), or one layer as it sounds in the mix (its level and pan,
// heard or not). Length: the loop (the longest layer; shorter ones repeat). Tails past the end wrap round
// to the start, so the file loops without a click. Not limited: the caller decides about peaks.
inline void renderLoop (const LoopSet& set, double sr, std::vector<float>& l, std::vector<float>& r, int onlyLayer = -1, float transpose = 0.0f)
{
    const auto snap = makeSnapshot (set);
    const double beats = set.beats(), spb = 60.0 / std::max (30.0, set.bpm) * sr;
    const int total = std::max (1, (int) std::lround (beats * spb));
    l.assign ((size_t) total, 0.0f); r.assign ((size_t) total, 0.0f);
    const int fadeLen = std::max (1, (int) (0.005 * sr)), fadeIn = std::max (1, (int) (0.0015 * sr));
    for (int i = 0; i < set.numLayers; ++i)
    {
        if (onlyLayer >= 0 ? i != onlyLayer : ! snap->audible[(size_t) i]) continue;
        const double L = snap->layerBeats[(size_t) i];
        for (double rep = 0.0; rep < beats - 1e-9; rep += L)
            for (int k = snap->first[(size_t) i]; k < snap->first[(size_t) i + 1]; ++k)
            {
                const auto& e = snap->events[(size_t) k];
                if (rep + e.beat >= beats) continue;
                const auto& s = *e.sound; const int len = (int) s.l.size();
                if (len < 2) continue;
                const double inc = s.sr / sr * std::exp2 ((double) (e.semis + transpose) / 12.0);
                const int own = (int) ((double) (len - 1) / inc);
                const int cut = std::min (own, (int) (e.lenBeats * spb) + (int) (0.004 * sr));
                const int at = (int) std::lround ((rep + e.beat) * spb);
                const float gl = e.gain * snap->gainL[(size_t) i], gr = e.gain * snap->gainR[(size_t) i];
                for (int j = 0; j < cut; ++j)
                {
                    const double pos = e.reverse ? (double) len - 2.0 - j * inc : j * inc;
                    const int i0 = std::max (0, std::min (len - 2, (int) pos)); const float f = (float) (pos - i0);
                    float env = j < fadeIn ? (float) (j + 1) / (float) fadeIn : 1.0f;
                    if (cut - j < fadeLen) env *= (float) (cut - j) / (float) fadeLen;
                    const float x = s.l[(size_t) i0] + f * (s.l[(size_t) i0 + 1] - s.l[(size_t) i0]);
                    const float y = s.r.size() == s.l.size() ? s.r[(size_t) i0] + f * (s.r[(size_t) i0 + 1] - s.r[(size_t) i0]) : x;
                    const size_t o = (size_t) ((at + j) % total);
                    l[o] += gl * env * x; r[o] += gr * env * y;
                }
            }
    }
}
} // namespace bc
