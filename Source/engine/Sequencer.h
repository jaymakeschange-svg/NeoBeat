// NeoBeat — the step sequencer.
//
// 16 tracks (one per pad) x up to 64 sixteenth steps. Each step has a velocity,
// a roll and a nudge:
//   roll 0     one hit
//   roll 2/3/4 a ratchet: that many hits inside the step (3 = 32nd-note triplets)
//   roll 5     a triplet roll: three hits spread over this step and the next
//              (16th-note triplets, the new jack swing hi-hat roll)
//   nudge      -50..+50 % of a step, for pushing or dragging a hit off the grid
// Rolls rise in velocity toward the written one, the way a drummer plays them.
//
// Swing moves every second 16th later (0.5 straight, 0.66 triplet feel). The
// sequencer follows the host's beat position, so it stays locked to Ableton's
// tempo and loop points; events land on exact sample offsets inside a block.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nb
{
struct Step
{
    uint8_t vel = 0;     // 0 = off, 1..127
    uint8_t roll = 0;    // 0, 2, 3, 4, 5 (triplet over two steps)
    int8_t nudge = 0;    // percent of a step
};

struct Pattern
{
    static constexpr int kTracks = 16, kMaxSteps = 64;
    int length = 32;
    std::array<std::array<Step, kMaxSteps>, kTracks> steps {};
};

struct SeqEvent
{
    int offset = 0;      // sample offset inside the block
    int pad = 0;
    float velocity = 0.0f;
};

class Sequencer
{
public:
    void prepare (float sampleRate) noexcept { sr = sampleRate; }

    // Rebuild the event grid (no allocation).
    void setPattern (const Pattern& p) noexcept
    {
        length = std::max (1, std::min (p.length, Pattern::kMaxSteps));
        count = 0;
        for (int tr = 0; tr < Pattern::kTracks; ++tr)
            for (int s = 0; s < length; ++s)
            {
                const Step& st = p.steps[(size_t) tr][(size_t) s];
                if (st.vel == 0) continue;
                const float v = (float) st.vel / 127.0f;
                const int hits = st.roll == 5 ? 3 : (st.roll >= 2 && st.roll <= 4 ? st.roll : 1);
                const float span = st.roll == 5 ? 2.0f : 1.0f;
                for (int h = 0; h < hits && count < kMaxEvents; ++h)
                {
                    GridEvent& e = grid[(size_t) count++];
                    e.step = s;
                    e.span = (int) span;
                    e.frac = (float) h / (float) hits;
                    e.nudge = (float) st.nudge / 100.0f;
                    e.pad = tr;
                    e.velocity = hits == 1 ? v : v * (0.55f + 0.45f * (float) (h + 1) / (float) hits);
                }
            }
    }

    int patternLength() const noexcept { return length; }

    // Collect the hits inside [ppqStart, ppqStart + block). `semis` is carried
    // through for the key-transpose mode.
    void collect (double ppqStart, double bpm, int numSamples, bool playing, float swing, float semis, std::vector<SeqEvent>& out) noexcept
    {
        (void) semis;
        if (! playing || numSamples <= 0 || bpm <= 0.0) return;
        const double ppqPerSample = bpm / 60.0 / sr;
        const double ppqEnd = ppqStart + ppqPerSample * numSamples;
        const double loop = 0.25 * length;
        const size_t first = out.size();
        for (int i = 0; i < count; ++i)
        {
            const auto& e = grid[(size_t) i];
            const double p = position (e.step, e.span, e.frac, e.nudge, swing);
            const double k = std::ceil ((ppqStart - p) / loop - 1.0e-9);
            const double at = p + k * loop;
            if (at < ppqStart - 1.0e-9 || at >= ppqEnd) continue;
            SeqEvent ev;
            ev.offset = std::max (0, std::min (numSamples - 1, (int) std::floor ((at - ppqStart) / ppqPerSample)));
            ev.pad = e.pad;
            ev.velocity = e.velocity;
            if (out.size() < out.capacity()) out.push_back (ev);
        }
        // stable sort by offset (few events per block: insertion sort)
        for (size_t a = first + 1; a < out.size(); ++a)
        {
            const SeqEvent x = out[a];
            size_t b = a;
            while (b > first && out[b - 1].offset > x.offset) { out[b] = out[b - 1]; --b; }
            out[b] = x;
        }
    }

    // Which step is sounding at a beat position (for the playhead).
    int stepAt (double ppq) const noexcept
    {
        const double loop = 0.25 * length;
        double p = std::fmod (ppq, loop);
        if (p < 0) p += loop;
        return std::min (length - 1, (int) (p / 0.25));
    }

private:
    struct GridEvent { int step = 0, span = 1; float frac = 0.0f, nudge = 0.0f; int pad = 0; float velocity = 0.0f; };
    static constexpr int kMaxEvents = Pattern::kTracks * Pattern::kMaxSteps * 4;

    // Start of step s with swing (in quarter notes); a hit sits a fraction of
    // the way across `span` steps (a triplet roll spans an even, unswung 8th).
    static double stepStart (int s, float swing) noexcept
    {
        const int pair = s >> 1;
        return 0.5 * pair + ((s & 1) ? 0.5 * (double) swing : 0.0);
    }

    double position (int s, int span, float frac, float nudge, float swing) const noexcept
    {
        const double a = stepStart (s, swing), b = stepStart (s + span, swing);
        return a + (b - a) * frac + 0.25 * nudge;
    }

    std::array<GridEvent, kMaxEvents> grid {};
    int count = 0, length = 32;
    float sr = 48000.0f;
};
} // namespace nb
