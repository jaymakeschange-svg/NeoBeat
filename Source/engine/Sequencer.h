// NeoBeat — the step sequencer.
//
// 16 lanes (one per sound) x up to 512 sixteenth steps (32 bars) in each of four scenes, A to D.
// Each step (a node, when it holds a hit) has a velocity, a roll and a nudge:
//   roll 0     one hit
//   roll 2/3/4 a ratchet: that many hits inside the step (3 = 32nd-note triplets)
//   roll 5     a triplet roll: three hits spread over this step and the next
//              (16th-note triplets, the new jack swing hi-hat roll)
//   nudge      -50..+50 % of a step, for pushing or dragging a hit off the grid
// Rolls rise in velocity toward the written one, the way a drummer plays them.
//
// Swing moves every second 16th later (0.5 straight, 0.66 triplet feel). The sequencer follows the
// host's beat position, so it stays locked to the song's tempo and loop points; events land on exact
// sample offsets inside a block.
//
// The song: either one scene looping (switched live, on the bar), or the chain: scenes played one
// after another, each a number of times, up to 128 bars, then round again. Both are a function of the
// song position alone, so the host can jump anywhere and the right bar plays.
#pragma once

#include <algorithm>
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

constexpr int kNumScenes = 4;
constexpr int kStepsPerBar = 16;
constexpr int kMaxBars = 32;

struct Pattern
{
    static constexpr int kTracks = 16, kMaxSteps = kMaxBars * kStepsPerBar;
    int length = 32;
    std::array<std::array<Step, kMaxSteps>, kTracks> steps {};
    int bars() const noexcept { return std::max (1, (length + kStepsPerBar - 1) / kStepsPerBar); }
};

// The chain: up to 16 links, each a scene played a number of times.
struct ChainLink { uint8_t scene = 0, repeats = 1; };
struct SongSettings
{
    static constexpr int kMaxLinks = 16;
    bool chainOn = false;
    int numLinks = 0;
    std::array<ChainLink, kMaxLinks> chain {};
};

struct SeqEvent
{
    int offset = 0;      // sample offset inside the block
    int pad = 0;
    float velocity = 0.0f;
    int step = 0, sub = 0;   // the step it came from, and which hit of a roll
    double ppq = 0.0;        // where it lands in the song
    int scene = 0;
};

// Start of step s with swing (in quarter notes).
inline double swungStepStart (int s, float swing) noexcept
{
    const int pair = s >> 1;
    return 0.5 * pair + ((s & 1) ? 0.5 * (double) swing : 0.0);
}

class Sequencer
{
public:
    Sequencer() { for (auto& g : grid) g.reserve (4096); }

    void prepare (float sampleRate) noexcept
    {
        sr = sampleRate;
        // room for a full scene (16 lanes x 512 steps x 4 hits); filled patterns never allocate later
        for (auto& g : grid) g.reserve (Pattern::kTracks * Pattern::kMaxSteps * 4);
    }

    // Rebuild a scene's event grid (no allocation once prepared).
    void setScene (int scene, const Pattern& p) noexcept
    {
        if (scene < 0 || scene >= kNumScenes) return;
        auto& g = grid[(size_t) scene];
        length[(size_t) scene] = std::max (1, std::min (p.length, Pattern::kMaxSteps));
        g.clear();
        for (int tr = 0; tr < Pattern::kTracks; ++tr)
            for (int s = 0; s < length[(size_t) scene]; ++s)
            {
                const Step& st = p.steps[(size_t) tr][(size_t) s];
                if (st.vel == 0) continue;
                const float v = (float) st.vel / 127.0f;
                const int hits = st.roll == 5 ? 3 : (st.roll >= 2 && st.roll <= 4 ? st.roll : 1);
                for (int h = 0; h < hits && g.size() < g.capacity(); ++h)
                {
                    GridEvent e;
                    e.step = s;
                    e.span = st.roll == 5 ? 2 : 1;
                    e.frac = (float) h / (float) hits;
                    e.nudge = (float) st.nudge / 100.0f;
                    e.pad = tr;
                    e.sub = h;
                    e.velocity = hits == 1 ? v : v * (0.55f + 0.45f * (float) (h + 1) / (float) hits);
                    g.push_back (e);
                }
            }
    }
    // one pattern in every scene (the old single-pattern way; the tests and old projects)
    void setPattern (const Pattern& p) noexcept { for (int s = 0; s < kNumScenes; ++s) setScene (s, p); }

    void setSong (const SongSettings& s) noexcept { song = s; song.numLinks = std::max (0, std::min (s.numLinks, SongSettings::kMaxLinks)); }
    const SongSettings& songSettings() const noexcept { return song; }

    // one scene looping (chain off): which one. A change takes effect at `fromPpq` (the caller picks the bar line).
    void setLiveScene (int scene, double fromPpq) noexcept
    {
        scene = std::max (0, std::min (scene, kNumScenes - 1));
        if (scene == liveScene) { pendingScene = -1; return; }
        pendingScene = scene; pendingFrom = fromPpq;
    }
    void setLiveSceneNow (int scene) noexcept { liveScene = std::max (0, std::min (scene, kNumScenes - 1)); pendingScene = -1; }
    // once per block, with the block's own position: a queued switch that has been reached becomes the scene
    void settle (double ppq) noexcept { if (pendingScene >= 0 && ppq >= pendingFrom) { liveScene = pendingScene; pendingScene = -1; } }
    int currentLiveScene() const noexcept { return liveScene; }
    int queuedScene() const noexcept { return pendingScene; }

    int sceneLength (int scene) const noexcept { return length[(size_t) std::max (0, std::min (scene, kNumScenes - 1))]; }
    int patternLength() const noexcept { return length[(size_t) liveScene]; }
    double sceneBeats (int scene) const noexcept { return 0.25 * sceneLength (scene); }

    // The whole chain, in quarter notes (0 when the chain is off or empty).
    double chainBeats() const noexcept
    {
        if (! song.chainOn) return 0.0;
        double t = 0.0;
        for (int i = 0; i < song.numLinks; ++i) t += sceneBeats (song.chain[(size_t) i].scene) * std::max (1, (int) song.chain[(size_t) i].repeats);
        return t;
    }

    // Where a song position falls: the scene, the step inside it, and the link of the chain (-1: no chain).
    struct Where { int scene = 0, step = 0, link = -1; double origin = 0.0, start = -1.0e18, end = 1.0e18; };
    Where whereAt (double ppq) const noexcept
    {
        Where w;
        const double total = chainBeats();
        if (total > 0.0)
        {
            const double lap = std::floor (ppq / total);
            double t = lap * total;
            for (int i = 0; i < song.numLinks; ++i)
            {
                const int sc = std::min ((int) song.chain[(size_t) i].scene, kNumScenes - 1);
                const double len = sceneBeats (sc) * std::max (1, (int) song.chain[(size_t) i].repeats);
                if (ppq < t + len || i == song.numLinks - 1) { w.scene = sc; w.link = i; w.origin = t; w.start = t; w.end = t + len; break; }
                t += len;
            }
        }
        else
        {
            w.scene = liveScene;
            if (pendingScene >= 0)
            {
                if (ppq >= pendingFrom) { w.scene = pendingScene; w.start = pendingFrom; }
                else w.end = pendingFrom;
            }
        }
        const double loop = sceneBeats (w.scene);
        double p = std::fmod (ppq - w.origin, loop);
        if (p < 0) p += loop;
        w.step = std::min (sceneLength (w.scene) - 1, (int) (p / 0.25));
        return w;
    }

    // Pads whose sound needs time before its hit (a stretched rubber reaching its snap) are
    // started this many seconds early, so the snap lands on the step.
    void setPreroll (int pad, double seconds) noexcept { if (pad >= 0 && pad < Pattern::kTracks) preroll[(size_t) pad] = std::max (0.0, seconds); }

    // Collect the hits inside [ppqStart, ppqStart + block). laneMask: which lanes (bit per lane).
    // shiftBeats moves every hit later (negative: earlier), for layers that follow a lane off the beat.
    void collect (double ppqStart, double bpm, int numSamples, bool playing, float swing, float semis, std::vector<SeqEvent>& out,
                  uint32_t laneMask = 0xffffu, double shiftBeats = 0.0) noexcept
    {
        (void) semis;
        if (! playing || numSamples <= 0 || bpm <= 0.0) return;
        const double ppqPerSample = bpm / 60.0 / sr;
        const double ppqEnd = ppqStart + ppqPerSample * numSamples;
        const size_t first = out.size();
        // the stretches of the song this block can hear: a hit belongs to the stretch its step starts in,
        // even when a nudge or a pre-roll makes it sound a little before
        double maxPre = 0.0; for (double p : preroll) maxPre = std::max (maxPre, p);
        const double reach = maxPre * bpm / 60.0 + 0.3 + std::fabs (shiftBeats);
        double at = ppqStart - 0.3 - std::max (0.0, shiftBeats);
        int guard = 0;
        while (at < ppqEnd + reach && guard++ < 64)
        {
            const Where w = whereAt (at);
            collectStretch (w, ppqStart, ppqEnd, ppqPerSample, numSamples, bpm, swing, laneMask, shiftBeats, out);
            if (w.end >= 1.0e17) break;
            at = std::max (w.end, at + 1.0e-9);
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
    int stepAt (double ppq) const noexcept { return whereAt (ppq).step; }

    // ---- reading a scene without playing it (export, the layers that follow a lane)
    // every hit of a scene's loop, in quarter notes from the scene's start
    struct Hit { double beats; int pad, step, sub; float velocity; };
    template <typename Fn> void forEachHit (int scene, float swing, Fn&& fn) const
    {
        scene = std::max (0, std::min (scene, kNumScenes - 1));
        for (const auto& e : grid[(size_t) scene]) fn (Hit { position (e.step, e.span, e.frac, e.nudge, swing), e.pad, e.step, e.sub, e.velocity });
    }

private:
    struct GridEvent { int step = 0, span = 1; float frac = 0.0f, nudge = 0.0f; int pad = 0; float velocity = 0.0f; int sub = 0; };

    // A hit sits a fraction of the way across `span` steps (a triplet roll spans an even, unswung 8th).
    static double position (int s, int span, float frac, float nudge, float swing) noexcept
    {
        const double a = swungStepStart (s, swing), b = swungStepStart (s + span, swing);
        return a + (b - a) * frac + 0.25 * nudge;
    }

    void collectStretch (const Where& w, double ppqStart, double ppqEnd, double ppqPerSample, int numSamples, double bpm, float swing,
                         uint32_t laneMask, double shiftBeats, std::vector<SeqEvent>& out) noexcept
    {
        const double loop = sceneBeats (w.scene);
        for (const auto& e : grid[(size_t) w.scene])
        {
            if (((laneMask >> e.pad) & 1u) == 0) continue;
            const double nominal = swungStepStart (e.step, swing);
            const double p = position (e.step, e.span, e.frac, e.nudge, swing) - preroll[(size_t) e.pad] * bpm / 60.0 + shiftBeats;
            // the lap of this scene's loop that puts the hit inside the block
            const double k = std::ceil ((ppqStart - w.origin - p) / loop - 1.0e-9);
            for (double kk = k; ; kk += 1.0)
            {
                const double at = w.origin + p + kk * loop;
                if (at >= ppqEnd) break;
                if (at < ppqStart - 1.0e-9) continue;
                const double nom = w.origin + nominal + kk * loop;
                if (nom < w.start - 1.0e-9 || nom >= w.end - 1.0e-9) continue;   // that lap belongs to another stretch
                SeqEvent ev;
                ev.offset = std::max (0, std::min (numSamples - 1, (int) std::floor ((at - ppqStart) / ppqPerSample)));
                ev.pad = e.pad;
                ev.velocity = e.velocity;
                ev.step = e.step; ev.sub = e.sub; ev.ppq = at; ev.scene = w.scene;
                if (out.size() < out.capacity()) out.push_back (ev);
            }
        }
    }

    std::array<std::vector<GridEvent>, kNumScenes> grid;
    std::array<int, kNumScenes> length { 32, 32, 32, 32 };
    std::array<double, Pattern::kTracks> preroll {};
    SongSettings song;
    int liveScene = 0, pendingScene = -1;
    double pendingFrom = 0.0;
    float sr = 48000.0f;
};
} // namespace nb
