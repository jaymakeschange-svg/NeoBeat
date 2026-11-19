#pragma once
// MidiJaQ's session: 128 slots, four separate lines, a chord track, and four alternative ways of
// writing a line. This belongs to MidiJaQ; nothing in NeoBeat or Batchcut needs to change.
// No JUCE in this file: the same decisions are used by the plugin and the tests.

#include "Jaq.h"
#include "Orb.h"
#include "Room.h"
#include "Randomise.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mjq
{
inline constexpr int kSessionSlots = 128;
inline constexpr int kSessionTracks = 4;
inline constexpr int kChordBars = 8;
inline constexpr int kEvolutionRounds = 16; // sixteen evolving rounds, then the cycle repeats

enum class Generator : uint8_t { JAQ, HABIT, DRIFT, ORBIT, CUES };
enum class ChordKind : uint8_t { Major, Minor, Diminished, Augmented, Sus2, Sus4, Dominant7, Minor7, Major7 };

inline const char* generatorName (Generator g) noexcept
{
    switch (g)
    {
        case Generator::JAQ: return "JAQ";
        case Generator::HABIT: return "HABIT";
        case Generator::DRIFT: return "DRIFT";
        case Generator::ORBIT: return "ORBIT";
        case Generator::CUES: return "CUES";
    }
    return "JAQ";
}

inline const char* chordKindName (ChordKind k) noexcept
{
    switch (k)
    {
        case ChordKind::Major: return "Major";
        case ChordKind::Minor: return "Minor";
        case ChordKind::Diminished: return "Diminished";
        case ChordKind::Augmented: return "Augmented";
        case ChordKind::Sus2: return "Sus 2";
        case ChordKind::Sus4: return "Sus 4";
        case ChordKind::Dominant7: return "7th";
        case ChordKind::Minor7: return "Minor 7";
        case ChordKind::Major7: return "Major 7";
    }
    return "Major";
}

struct Chord
{
    int root = 0;                  // C = 0, chromatic pitch class
    ChordKind kind = ChordKind::Minor;
    int bassNote = 48;            // octave of the chord, C2 by default
    int inversion = 0;            // how many of the low voices are lifted an octave
};

struct ChordNotes
{
    std::array<int, 4> notes {};
    int count = 3;
    int at (int voice) const noexcept { return notes[(size_t) std::max (0, std::min (voice, count - 1))]; }
};

inline ChordNotes voiceChord (const Chord& c) noexcept
{
    static constexpr int intervals[9][4] = {
        { 0, 4, 7, 0 }, { 0, 3, 7, 0 }, { 0, 3, 6, 0 }, { 0, 4, 8, 0 },
        { 0, 2, 7, 0 }, { 0, 5, 7, 0 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 }
    };
    const int quality = std::max (0, std::min (8, (int) c.kind));
    ChordNotes out;
    out.count = quality >= 6 ? 4 : 3;
    const int base = c.bassNote + ((c.root - c.bassNote) % 12 + 12) % 12;
    for (int i = 0; i < out.count; ++i)
        out.notes[(size_t) i] = std::max (0, std::min (127, base + intervals[quality][i] + 12 * (i < c.inversion ? 1 : 0)));
    std::sort (out.notes.begin(), out.notes.begin() + out.count);
    return out;
}

struct Progression
{
    std::array<Chord, kChordBars> bars {};
    int length = 4;
    int beatsPerChord = 4;

    const Chord& atBeat (double beat) const noexcept
    {
        const int count = std::max (1, std::min (kChordBars, length));
        const int grid = std::max (1, beatsPerChord);
        const int index = (int) std::floor (std::max (0.0, beat) / (double) grid) % count;
        return bars[(size_t) index];
    }
    ChordNotes notesAt (double beat) const noexcept { return voiceChord (atBeat (beat)); }
};

struct GeneratorSettings
{
    Generator kind = Generator::JAQ;
    uint32_t seed = 1;
    float amount = 0.5f;           // HABIT's mutation or DRIFT's memory
    int hits = 5;                  // ORBIT's number of hits per pass
    int rotate = 0;
    gen::Cue cue {};               // CUES: when a note should be heard
    bool fill = false;
};

// One voice of the multi: a line, the writer behind it, the instrument that sounds it, and the stretch of
// keys it answers to. Ranges may overlap, which is the point: one key can start a piano figure and a
// bassline at once, each on its own instrument.
struct Track
{
    Line line;
    GeneratorSettings generator;
    bool enabled = true;

    Orb orb;                            // the eight corners this voice morphs between
    Place place;                        // where this voice stands in the stereo room
    Randomiser randomiser;              // controls that will not sit still
    std::string instrumentFile;         // the .NJQi (or .NJQ) this voice plays; empty: silent
    int lowKey = 0, highKey = 127;      // the keys that steer it, in Follow, Bass and Parts
    float level = 0.8f;                 // so the layers of a multi can be balanced (the player's own default)

    bool answersTo (int note) const noexcept { return note >= lowKey && note <= highKey; }
};

struct Slot
{
    std::array<Track, kSessionTracks> tracks {};
    Progression chords {};
};

struct Session
{
    std::array<Slot, kSessionSlots> slots {};
    std::array<bool, kSessionSlots> touched {}; // untouched slots are recreated from fixed seeds
    int selected = 0;
    int selectedTrack = 0;
    float orbX = 0.0f, orbY = 0.0f;   // the joystick, shared: one hand moves every voice's Orb at once
    int chainLength = 1;        // one means the selected slot alone; >1 walks forward and wraps at 128

    Session()
    {
        for (int p = 0; p < kSessionSlots; ++p)
            for (int t = 0; t < kSessionTracks; ++t)
            {
                auto& tr = slots[(size_t) p].tracks[(size_t) t];
                tr.enabled = (t == 0);
                const uint32_t s = (uint32_t) gen::Rng::mix (0x4a4151u + (uint64_t) p, (uint64_t) t);
                generate (tr.line, s);
                tr.generator.seed = s;
                tr.line.mode = t == 0 ? Mode::Free : Mode::Free;
                tr.line.base = 48 + 12 * (t > 0 ? 1 : 0);
                tr.line.partIndex = t;    // every Parts line takes its own chord voice
                // the multi starts laid out rather than stacked: a bass end, a middle, and a top that
                // overlaps it, so holding one key in the middle can start two voices at once
                const int bounds[kSessionTracks][2] = { { 0, 127 }, { 0, 59 }, { 48, 83 }, { 72, 127 } };
                tr.lowKey = bounds[t][0];
                tr.highKey = bounds[t][1];
            }
    }

    // the voices a key reaches: enabled, with an instrument, and answering to that key
    int voicesFor (int slotIndex, int note, int* out, int max) const noexcept
    {
        int n = 0;
        const auto& s = slots[(size_t) std::max (0, std::min (kSessionSlots - 1, slotIndex))];
        for (int t = 0; t < kSessionTracks && n < max; ++t)
            if (s.tracks[(size_t) t].enabled && s.tracks[(size_t) t].answersTo (note)) out[n++] = t;
        return n;
    }

    Slot& current() noexcept { return slots[(size_t) std::max (0, std::min (127, selected))]; }
    const Slot& current() const noexcept { return slots[(size_t) std::max (0, std::min (127, selected))]; }
    int slotAtBar (uint64_t bar) const noexcept
    {
        const int count = std::max (1, std::min (128, chainLength));
        return (selected + (int) (bar % (uint64_t) count)) % 128;
    }
};

// The original family is never damaged. A different writer makes a temporary copy before the clock
// reads it, so switching the writer back to JAQ restores exactly the earlier pattern.
inline Pass generatedPass (const Track& track, uint64_t round = 0, float orbX = 0.0f, float orbY = 0.0f)
{
    // Advance from the same seed, outside the audio thread, to preserve each writer's memory.
    round %= (uint64_t) kEvolutionRounds;
    // The Orb comes first: it decides what the line is before a writer gates it. A writer therefore thins
    // out whatever the joystick is currently reading, rather than the two fighting over the same steps.
    Line copy = track.line;
    if (track.orb.on && track.orb.anySet()) orbLine (track.orb, orbX, orbY, track.line, copy);
    const auto& gs = track.generator;
    const int n = std::max (1, std::min (kMaxSteps, copy.numSteps));
    std::array<bool, kMaxSteps> allowed {};
    allowed.fill (true);
    if (gs.kind == Generator::HABIT)
    {
        gen::Habit habit;
        habit.reset (gs.seed, n);
        habit.setChange (gs.amount);
        for (uint64_t i = 0; i < round * (uint64_t) n; ++i) (void) habit.next();
        for (int i = 0; i < n; ++i)
        {
            const auto value = habit.next();
            allowed[(size_t) i] = value.bit;
            copy.steps[(size_t) i].weight = (uint16_t) (value.bit ? 45000 + (int) (value.value * 10000) : (int) (value.value * 10000));
        }
    }
    else if (gs.kind == Generator::DRIFT)
    {
        gen::DejaVu memory;
        memory.reset (gs.seed, n);
        memory.setDejaVu (gs.amount);
        const int degrees = std::max (1, gen::scale (copy.scaleIndex).size());
        for (uint64_t i = 0; i < round * (uint64_t) n; ++i) (void) memory.next();
        for (int i = 0; i < n; ++i)
        {
            const float v = memory.next();
            copy.steps[(size_t) i].freeDegree = (int16_t) std::min (degrees - 1, (int) (v * degrees));
            copy.steps[(size_t) i].acidDegree = (int16_t) std::min (degrees - 1, (int) (v * degrees));
        }
    }
    else if (gs.kind == Generator::ORBIT)
    {
        const auto ring = gen::euclid (gs.hits, n, gs.rotate);
        for (int i = 0; i < n; ++i)
        {
            allowed[(size_t) i] = ring[(size_t) i];
            copy.steps[(size_t) i].weight = (uint16_t) (ring[(size_t) i] ? 60000 - i : i);
        }
    }
    Pass pass = readPass (copy);
    if (gs.kind == Generator::HABIT || gs.kind == Generator::ORBIT)
        pass.events.erase (std::remove_if (pass.events.begin(), pass.events.end(),
            [&allowed] (const Event& e) { return ! allowed[(size_t) e.step]; }), pass.events.end());
    return pass;
}

// Stable decisions for CUES. Evaluated for each round, not only when the editor writes the pattern.
// Supplying the earlier step's result enables Prev/NotPrev without consulting mutable plugin state.
inline bool cueAllows (const GeneratorSettings& g, uint64_t round, int step, bool prev) noexcept
{
    if (g.kind != Generator::CUES) return true;
    gen::Rng rng (gen::Rng::mix (g.seed, gen::Rng::mix (round, (uint64_t) (uint32_t) step)));
    return g.cue.passes ((int) std::min<uint64_t> (round, (uint64_t) 0x7fffffffu), g.fill, prev, rng);
}
} // namespace mjq
