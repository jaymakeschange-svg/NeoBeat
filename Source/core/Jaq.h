#pragma once
// MidiJaQ's sequencer: one line of up to 32 steps.
//
// JAQ writes the line. The line is not one pattern but a family of them: every step holds what it would
// play in each direction, so DIALECT and TRAFFIC move through the family without rewriting it. Turning a
// knob therefore never loses the idea, and the same seed always gives the same family, on every computer.
//
//   TRAFFIC  how many steps play. Each step has a weight; TRAFFIC opens them in that order, so a busier
//            line keeps every note of a sparser one and adds to it: the groove grows instead of changing.
//   DIALECT  what they play. Each step holds a free note and an acid note and the point at which it
//            changes over, so the line moves from wide intervals to the root and its octave a note at a
//            time. Nothing but the pitches moves: the rhythm, the accents and the lengths stay.
//
// The lanes (Rhythm, Note, Accent, Gate, Octave, Roll, Slide) are written apart and locked apart, so a
// groove can be kept while the notes change, or the other way round.
//
// No JUCE here: the tests build this file on its own.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "../neobeat/gen/Generators.h"
#include "../neobeat/gen/Rng.h"
#include "../neobeat/gen/Scale.h"

namespace mjq
{
inline constexpr int kMaxSteps = 32;

enum class Lane : uint8_t { Rhythm = 0, Note, Accent, Gate, Octave, Roll, Slide, numLanes };
inline constexpr int numLanes = (int) Lane::numLanes;

inline const char* laneName (Lane l) noexcept
{
    switch (l)
    {
        case Lane::Rhythm: return "Rhythm";
        case Lane::Note:   return "Note";
        case Lane::Accent: return "Accent";
        case Lane::Gate:   return "Gate";
        case Lane::Octave: return "Octave";
        case Lane::Roll:   return "Roll";
        case Lane::Slide:  return "Slide";
        default:           return "?";
    }
}

enum class Direction : uint8_t { Forward = 0, Backward, Pendulum };

// Where the pitches come from. Free writes its own line; Follow takes them from the keys held down (an
// arpeggio that keeps the line's rhythm); Bass reads the held chord and plays under it.
// How a held key steers the line.
//   Free    nothing is held: the line plays as written
//   Follow  the degrees walk the keys held, so the chord chooses the notes
//   Bass    a bassline under the lowest key held
//   Parts   one voice of the chord each, by partIndex
//   Keys    the line keeps its own shape and moves to the key held: every key held plays a copy of it,
//           transposed, so holding a triad gives three copies of the same figure. This is the one that
//           makes MidiJaQ something you play rather than something you set going.
enum class Mode : uint8_t { Free = 0, Follow, Bass, Parts, Keys };

// What one step holds. Two notes and a changeover point, not one note: that is what lets DIALECT move
// without a reroll.
struct Step
{
    uint16_t weight = 0;        // TRAFFIC opens the heaviest steps first
    int16_t freeDegree = 0;     // the note at DIALECT 0: any degree of the scale
    int16_t acidDegree = 0;     // the note at DIALECT 1: the root, its octave, the fifth
    uint16_t dialectAt = 500;   // out of 1000: above this the step plays its acid note
    int8_t octave = 0;          // -1 to +2, on top of the degree
    uint8_t accent = 1;         // 0 low, 1 middle, 2 high
    uint8_t gate = 1;           // 0 short, 1 middle, 2 long
    uint8_t roll = 1;           // 1 to 4 hits in the step
    bool slide = false;         // runs into the next step
};

struct Line
{
    std::array<Step, kMaxSteps> steps {};
    std::array<bool, numLanes> locked {};   // a locked lane survives the next JAQ

    uint32_t seed = 1;
    int numSteps = 16;
    int offset = 0;                 // turns the pattern, in steps
    Direction direction = Direction::Forward;

    float dialect = 0.5f;           // 0 free intervals .. 1 the root and its octave
    float traffic = 0.5f;           // 0 silent .. 1 every step
    float accent = 0.5f;            // 0 one level .. 1 the widest spread
    float gate = 1.0f;              // 0.1 to 2: scales all three lengths (1 = as written)

    int root = 0;                   // 0 = C
    int scaleIndex = 3;             // gen::scales(): 3 = Dorian
    int transpose = 0;              // semitones, on the whole line
    int octLow = -1, octHigh = 2;   // the octave lane is kept between these
    int base = 48;                  // the middle octave, as MIDI (C2)
    bool tie = true;                // long notes run into the next step

    int perBar = 16;                // steps in a bar: 16 = 16ths, 12 = 8th triplets
    float swing = 0.5f;             // as NeoBeat's: 0.5 straight, 0.66 swung
    Mode mode = Mode::Free;
    int partIndex = 0;               // Parts: which voice of the chord this line carries
};

// ------------------------------------------------------------------ writing the line
namespace jaqdetail
{
// How strongly a step asks to be played. A step on the bar is heavier than one on the beat, which is
// heavier than an offbeat; the jitter is several classes wide, so the strong beats are favoured (about
// twice as often as chance) without every line falling on the same steps.
inline uint16_t weightOf (int s, int perBar, gen::Rng& rng) noexcept
{
    int w = 1;
    for (int d = std::max (1, perBar); d >= 1; d /= 2)
        if (s % d == 0) ++w;
    return (uint16_t) std::min (65535, w * 250 + (int) rng.below (2200u));
}

// The acid vocabulary: the root, then the degree under the octave (the leading note), then the fifth.
inline int16_t acidDegreeOf (int scaleSize, gen::Rng& rng) noexcept
{
    const double u = rng.uni();
    if (u < 0.62) return 0;
    if (u < 0.82) return (int16_t) std::max (1, scaleSize - 1);
    return (int16_t) std::min (scaleSize - 1, 4);
}
} // namespace jaqdetail

// Writes one lane from its own seed, so rerolling the notes leaves the groove and the accents alone.
inline void writeLane (Line& L, Lane lane, uint32_t seed)
{
    gen::Rng rng ((uint32_t) gen::Rng::mix (seed, (uint64_t) lane + 1u));
    const int scaleSize = std::max (1, gen::scale (L.scaleIndex).size());

    for (int s = 0; s < kMaxSteps; ++s)
    {
        Step& st = L.steps[(size_t) s];
        switch (lane)
        {
            case Lane::Rhythm:
                st.weight = jaqdetail::weightOf (s, L.perBar, rng);
                break;
            case Lane::Note:
                st.freeDegree = (int16_t) rng.below ((uint32_t) scaleSize);
                st.acidDegree = jaqdetail::acidDegreeOf (scaleSize, rng);
                st.dialectAt = (uint16_t) rng.below (1000u);
                break;
            case Lane::Accent:
                // mostly the middle, with a few of each edge: the line has shape before the knob is touched
                st.accent = (uint8_t) (rng.uni() < 0.3 ? 2 : rng.uni() < 0.3 ? 0 : 1);
                break;
            case Lane::Gate:
                st.gate = (uint8_t) (rng.uni() < 0.25 ? 2 : rng.uni() < 0.35 ? 0 : 1);
                break;
            case Lane::Octave:
            {
                const double u = rng.uni();
                st.octave = (int8_t) (u < 0.55 ? 0 : u < 0.8 ? 1 : u < 0.93 ? -1 : 2);
                break;
            }
            case Lane::Roll:
                st.roll = (uint8_t) (rng.uni() < 0.12 ? 2 + (int) rng.below (3u) : 1);
                break;
            case Lane::Slide:
                st.slide = rng.uni() < 0.2;
                break;
            default: break;
        }
    }
}

// JAQ: one press writes every lane that is not locked.
inline void generate (Line& L, uint32_t seed)
{
    L.seed = seed;
    for (int i = 0; i < numLanes; ++i)
        if (! L.locked[(size_t) i])
            writeLane (L, (Lane) i, seed);
}

// ------------------------------------------------------------------ reading it back
// How many steps TRAFFIC opens.
inline int playing (const Line& L) noexcept
{
    const int n = std::max (1, std::min (L.numSteps, kMaxSteps));
    return std::max (0, std::min (n, (int) std::lround ((double) std::max (0.0f, std::min (1.0f, L.traffic)) * n)));
}

// Whether a step is one of them: the heaviest `playing` steps of the loop, the earlier step first when
// two weigh the same.
inline bool stepOn (const Line& L, int step) noexcept
{
    const int n = std::max (1, std::min (L.numSteps, kMaxSteps));
    if (step < 0 || step >= n) return false;
    const int open = playing (L);
    if (open >= n) return true;
    const uint16_t w = L.steps[(size_t) step].weight;
    int ahead = 0;                                  // steps that come in before this one
    for (int i = 0; i < n; ++i)
    {
        if (i == step) continue;
        const uint16_t wi = L.steps[(size_t) i].weight;
        if (wi > w || (wi == w && i < step)) ++ahead;
    }
    return ahead < open;
}

// The step's note as MIDI, DIALECT deciding which of its two notes it plays.
inline int noteOf (const Line& L, int step) noexcept
{
    const Step& st = L.steps[(size_t) std::max (0, std::min (step, kMaxSteps - 1))];
    const auto& sc = gen::scale (L.scaleIndex);
    const bool acid = (double) st.dialectAt < (double) std::max (0.0f, std::min (1.0f, L.dialect)) * 1000.0;
    const int degree = acid ? st.acidDegree : st.freeDegree;
    const int oct = std::max (L.octLow, std::min (L.octHigh, (int) st.octave));
    const int note = gen::degreeNote (degree, L.base, L.root, sc) + 12 * oct + L.transpose;
    return std::max (0, std::min (127, note));
}

// The three velocities, as Sting's accent knob scales them: at 0 every note is 100, and the low and high
// layers open out from there (at full, 40 and 127).
inline int velocityOf (const Line& L, int step) noexcept
{
    const double a = 100.0 * (double) std::max (0.0f, std::min (1.0f, L.accent));
    const int level = (int) L.steps[(size_t) std::max (0, std::min (step, kMaxSteps - 1))].accent;
    const double v = level == 0 ? 100.0 - 0.60 * a : level == 2 ? 100.0 + 0.27 * a : 100.0;
    return std::max (1, std::min (127, (int) std::lround (v)));
}

// One note on its way out. It carries its degree and octave as well as its pitch, because in Follow and
// Bass the keys held down decide the pitch, and those change while the line plays.
struct Event
{
    int step = 0;       // which step of the loop wrote it
    int note = 60;      // MIDI, as Free plays it
    int degree = 0;     // the step's degree of the scale
    int octave = 0;     // the octave lane, already kept between the limits
    int velocity = 100;
    double start = 0.0; // beats from the start of the pass, swing included
    double length = 0.0;// beats
    bool slide = false; // runs into the note after it
};

// The length of a step's note, in steps: a quarter of a step, five ninths of it, or the whole of it. A
// long note that runs into the next one overlaps it (a slide), or lasts exactly a step when the pitch is
// the same (a tie). GATE scales all of them.
inline double lengthInSteps (const Line& L, int step, bool intoNote, bool samePitch) noexcept
{
    const int g = (int) L.steps[(size_t) std::max (0, std::min (step, kMaxSteps - 1))].gate;
    double len = g == 0 ? 0.25 : g == 1 ? 5.0 / 9.0 : 0.99;
    if (g == 2 && intoNote && L.tie && L.steps[(size_t) step].slide)
        len = samePitch ? 1.0 : 1.089;              // a tie holds the step; a slide leans into the next
    return len * (double) std::max (0.01f, std::min (4.0f, L.gate));
}

// The order the steps play in: forward, backward, or there and back without repeating the ends.
inline std::vector<int> walk (const Line& L)
{
    const int n = std::max (1, std::min (L.numSteps, kMaxSteps));
    std::vector<int> out;
    if (L.direction == Direction::Forward)       { for (int i = 0; i < n; ++i) out.push_back (i); }
    else if (L.direction == Direction::Backward) { for (int i = n - 1; i >= 0; --i) out.push_back (i); }
    else
    {
        for (int i = 0; i < n; ++i) out.push_back (i);
        for (int i = n - 2; i >= 1; --i) out.push_back (i);
    }
    return out;
}

// One time through the line: its notes, and how long it takes. A pendulum pass is longer than the loop,
// because it walks back as well.
struct Pass
{
    std::vector<Event> events;
    double beats = 4.0;
    float gate = 1.0f;      // the GATE this pass was written with, so a cycling one can be scaled from it
};

// The line as it plays: the steps TRAFFIC opens, turned by OFFSET, walked in the chosen direction, swung
// as NeoBeat swings, with rolls spread inside their step.
inline Pass readPass (const Line& L)
{
    const int n = std::max (1, std::min (L.numSteps, kMaxSteps));
    const double stepBeats = 4.0 / (double) std::max (1, L.perBar);
    const auto order = walk (L);
    Pass pass;
    pass.beats = (double) order.size() * stepBeats;
    pass.gate = L.gate;
    pass.events.reserve (order.size());

    auto stored = [&] (int pos) { return ((pos + L.offset) % n + n) % n; };   // OFFSET turns the pattern

    for (size_t i = 0; i < order.size(); ++i)
    {
        const int s = stored (order[i]);
        if (! stepOn (L, s)) continue;

        const int nextPos = order[(i + 1) % order.size()];
        const int next = stored (nextPos);
        const bool intoNote = order.size() > 1 && stepOn (L, next);
        const int note = noteOf (L, s);
        const Step& st = L.steps[(size_t) s];

        Event e;
        e.step = s;
        e.note = note;
        e.degree = (double) st.dialectAt < (double) std::max (0.0f, std::min (1.0f, L.dialect)) * 1000.0 ? st.acidDegree : st.freeDegree;
        e.octave = std::max (L.octLow, std::min (L.octHigh, (int) st.octave));
        e.velocity = velocityOf (L, s);
        e.start = gen::swung ((int) i, L.perBar, L.swing);
        e.length = lengthInSteps (L, s, intoNote, intoNote && noteOf (L, next) == note) * stepBeats;
        e.slide = intoNote && L.tie && st.slide && st.gate == 2;

        const int hits = std::max (1, std::min (4, (int) st.roll));
        if (hits == 1) { pass.events.push_back (e); continue; }
        const double space = stepBeats / (double) hits;          // a ratchet: the step cut into equal hits
        for (int h = 0; h < hits; ++h)
        {
            Event r = e;
            r.start = e.start + space * (double) h;
            r.length = std::min (e.length, space * 0.9);
            r.slide = false;
            r.velocity = h == 0 ? e.velocity : std::max (1, e.velocity - 8 * h);
            pass.events.push_back (r);
        }
    }
    std::stable_sort (pass.events.begin(), pass.events.end(), [] (const Event& a, const Event& b) { return a.start < b.start; });
    return pass;
}

// The loop forward, for the editor and the tests.
inline std::vector<Event> readLine (const Line& L)
{
    Line f = L;
    f.direction = Direction::Forward;
    return readPass (f).events;
}

// How long the loop is, in beats (one way round, whatever the direction).
inline double loopBeats (const Line& L) noexcept
{
    return (double) std::max (1, std::min (L.numSteps, kMaxSteps)) * 4.0 / (double) std::max (1, L.perBar);
}

// The pitch a note comes out at. In Free that is what the line wrote; in Follow the held keys give the
// pitches and the line gives the rhythm; in Bass the lowest held note becomes the root it plays under.
// How far a line moves when a key is held in Keys mode: the distance from the line's own home note. Holding
// the home note plays it exactly as written.
inline int keysShift (const Line& L, int heldNote) noexcept { return heldNote - L.base; }

inline int notePlayed (const Line& L, const Event& e, const int* held, int numHeld) noexcept
{
    if (L.mode == Mode::Free || held == nullptr || numHeld <= 0) return e.note;

    if (L.mode == Mode::Keys)
    {
        // the written note, moved to the lowest key held. The Player emits a copy for every key held;
        // this is what a single copy sounds, and what a caller asking for one note gets.
        int low = held[0];
        for (int i = 1; i < numHeld; ++i) low = std::min (low, held[i]);
        return std::max (0, std::min (127, e.note + keysShift (L, low) + L.transpose));
    }

    if (L.mode == Mode::Follow)
    {
        const int i = ((e.degree % numHeld) + numHeld) % numHeld;            // the degree walks the held keys
        return std::max (0, std::min (127, held[i] + 12 * e.octave + L.transpose));
    }
    if (L.mode == Mode::Parts)
    {
        const int voice = std::max (0, std::min (numHeld - 1, L.partIndex));
        return std::max (0, std::min (127, held[voice] + 12 * e.octave + L.transpose));
    }
    int low = held[0];
    for (int i = 1; i < numHeld; ++i) low = std::min (low, held[i]);
    const auto& sc = gen::scale (L.scaleIndex);
    const int root = ((low % 12) + 12) % 12;
    const int base = low - 12;                                               // a bassline sits under the chord
    return std::max (0, std::min (127, gen::degreeNote (e.degree, base, root, sc) + 12 * e.octave + L.transpose));
}
} // namespace mjq
