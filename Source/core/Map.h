#pragma once

// The MAP: one grid where loops and keys play together.
//
// A mark carries its own source, and the row it sits on is read according to that source. For a
// keys mark the row is a degree of the scale, so dragging it up moves it through the key and it
// cannot land outside it. For a loop mark the row is which chop. That is what lets a melody and
// chopped loop material interleave anywhere on the one grid, in one pattern, rather than needing
// two sequencers side by side.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mjq
{

enum class MarkSource : uint8_t { Keys = 0, Loop = 1 };

struct Mark
{
    int step = 0;                 // along the grid
    int row = 0;                  // a degree of the scale, or which chop: by source
    int len = 1;                  // in steps
    float velocity = 0.8f;
    MarkSource source = MarkSource::Keys;
    int layer = 0;                // which loop layer, when source is Loop
    int ratchet = 1;              // 1..4 inside the step
    bool glide = false;           // into the next mark, for keys

    bool operator== (const Mark& o) const noexcept
    {
        return step == o.step && row == o.row && len == o.len && velocity == o.velocity
            && source == o.source && layer == o.layer && ratchet == o.ratchet && glide == o.glide;
    }
};

inline constexpr int kMapRows = 14;        // two octaves of a seven-note scale, or 14 chops
inline constexpr int kMapMaxSteps = 64;

struct MapPattern
{
    std::vector<Mark> marks;
    int steps = 16;
    int perBar = 16;
    int root = 2, scale = 3;      // D dorian, as LOOPS keeps its key
    int octave = 4;               // where row 0 sits

    int bars() const noexcept { return std::max (1, steps / std::max (1, perBar)); }

    const Mark* at (int step, int row) const noexcept
    {
        for (const auto& m : marks) if (m.step == step && m.row == row) return &m;
        return nullptr;
    }
    Mark* at (int step, int row) noexcept
    {
        for (auto& m : marks) if (m.step == step && m.row == row) return &m;
        return nullptr;
    }
    void put (const Mark& m)
    {
        if (auto* e = at (m.step, m.row)) { *e = m; return; }
        marks.push_back (m);
    }
    void erase (int step, int row)
    {
        marks.erase (std::remove_if (marks.begin(), marks.end(),
                        [step, row] (const Mark& m) { return m.step == step && m.row == row; }),
                     marks.end());
    }
    void clear() { marks.clear(); }

    bool operator== (const MapPattern& o) const noexcept
    {
        return steps == o.steps && perBar == o.perBar && root == o.root && scale == o.scale
            && octave == o.octave && marks == o.marks;
    }
};

// A keys mark's row is a degree of the scale, so what it plays is always in key: row 0 is the root
// at `octave`, row 1 the next note the scale holds, and so on through the octaves.
inline int noteOfRow (int row, uint16_t scaleMask, int root, int octave) noexcept
{
    int steps[12]; int n = 0;
    for (int k = 0; k < 12; ++k) if ((scaleMask >> k) & 1) steps[n++] = k;
    if (n < 1) { steps[0] = 0; n = 1; }
    int oct = row / n, deg = row % n;
    if (deg < 0) { deg += n; --oct; }
    return std::max (0, std::min (127, 12 * (octave + oct) + root + steps[deg]));
}

inline int degreesIn (uint16_t scaleMask) noexcept
{
    int n = 0; for (int k = 0; k < 12; ++k) n += (scaleMask >> k) & 1; return std::max (1, n);
}

} // namespace mjq
