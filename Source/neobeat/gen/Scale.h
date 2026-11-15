// Keys and scales for the generator library: what "in key" means in LOOPS' melody layers, in KEYS and in
// MidiJaQ. A scale is the set of its twelve pitch classes; a note is a MIDI note number.
// Framework-free.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace gen
{
struct Scale
{
    const char* name;
    uint16_t mask;        // bit k: the scale holds the note k semitones above the root
    int size() const noexcept { int n = 0; for (int k = 0; k < 12; ++k) n += (mask >> k) & 1; return n; }
    bool has (int pc) const noexcept { return (mask >> (((pc % 12) + 12) % 12)) & 1; }
};

namespace detail
{
constexpr uint16_t m (std::initializer_list<int> steps)
{
    uint16_t r = 0; for (int s : steps) r = (uint16_t) (r | (1u << s)); return r;
}
}

// the scales, Chromatic first (the same as no scale)
inline const std::array<Scale, 32>& scales()
{
    using detail::m;
    static const std::array<Scale, 32> s { {
        { "Chromatic",          m ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }) },
        { "Major",              m ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "Minor",              m ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "Dorian",             m ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "Phrygian",           m ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "Lydian",             m ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "Mixolydian",         m ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "Locrian",            m ({ 0, 1, 3, 5, 6, 8, 10 }) },
        { "Harmonic minor",     m ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "Melodic minor",      m ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "Minor pentatonic",   m ({ 0, 3, 5, 7, 10 }) },
        { "Major pentatonic",   m ({ 0, 2, 4, 7, 9 }) },
        { "Blues",              m ({ 0, 3, 5, 6, 7, 10 }) },
        { "Major blues",        m ({ 0, 2, 3, 4, 7, 9 }) },
        { "Dorian #4",          m ({ 0, 2, 3, 6, 7, 9, 10 }) },
        { "Phrygian dominant",  m ({ 0, 1, 4, 5, 7, 8, 10 }) },
        { "Lydian dominant",    m ({ 0, 2, 4, 6, 7, 9, 10 }) },
        { "Altered",            m ({ 0, 1, 3, 4, 6, 8, 10 }) },
        { "Whole tone",         m ({ 0, 2, 4, 6, 8, 10 }) },
        { "Diminished (H-W)",   m ({ 0, 1, 3, 4, 6, 7, 9, 10 }) },
        { "Diminished (W-H)",   m ({ 0, 2, 3, 5, 6, 8, 9, 11 }) },
        { "Bebop dominant",     m ({ 0, 2, 4, 5, 7, 9, 10, 11 }) },
        { "Bebop minor",        m ({ 0, 2, 3, 4, 5, 7, 9, 10 }) },
        { "Hungarian minor",    m ({ 0, 2, 3, 6, 7, 8, 11 }) },
        { "Double harmonic",    m ({ 0, 1, 4, 5, 7, 8, 11 }) },
        { "Neapolitan minor",   m ({ 0, 1, 3, 5, 7, 8, 11 }) },
        { "Hirajoshi",          m ({ 0, 2, 3, 7, 8 }) },
        { "In-sen",             m ({ 0, 1, 5, 7, 10 }) },
        { "Iwato",              m ({ 0, 1, 5, 6, 10 }) },
        { "Pelog",              m ({ 0, 1, 3, 7, 8 }) },
        { "Egyptian",           m ({ 0, 2, 5, 7, 10 }) },
        { "Prometheus",         m ({ 0, 2, 4, 6, 9, 10 }) },
    } };
    return s;
}
inline int numScales() noexcept { return (int) scales().size(); }
inline const Scale& scale (int i) noexcept { return scales()[(size_t) std::max (0, std::min (i, numScales() - 1))]; }

inline const char* pitchName (int pc) noexcept
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}
// "C3" is MIDI note 60, as in Ableton Live and most DAWs
inline std::string noteName (int note) { return std::string (pitchName (note)) + std::to_string ((int) std::floor ((double) note / 12.0) - 2); }

// The nearest note of the key (root + scale). Halfway between two, the lower one.
inline int quantize (int note, int root, const Scale& s) noexcept
{
    if (s.mask == 0) return note;
    for (int d = 0; d <= 12; ++d)
    {
        if (s.has (note - d - root)) return note - d;
        if (s.has (note + d - root)) return note + d;
    }
    return note;
}

// The degree-th note of the key counted from `base` (degree 0 = the first note of the key at or above
// base; negative degrees go down). Degrees walk the scale, so degree 7 of a major key is an octave up.
inline int degreeNote (int degree, int base, int root, const Scale& s) noexcept
{
    if (s.mask == 0) return base + degree;
    int n = base; while (! s.has (n - root)) ++n;
    for (int d = 0; d < degree; ++d) { ++n; while (! s.has (n - root)) ++n; }
    for (int d = 0; d > degree; --d) { --n; while (! s.has (n - root)) --n; }
    return n;
}
} // namespace gen
