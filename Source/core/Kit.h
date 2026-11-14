// Batchcut 2.0 - the Kit room's model: a pool of one-shots (cut from your recordings, or loaded from
// folders), the 16 lanes of a NeoBeat kit, and the player that auditions the kit inside a pattern.
// Framework-free.
//
//   the pool      every one-shot gathered so far, listened to once: what it could play in a kit, how
//                 usable it is (SoundAnalysis.h), how much you are likely to like it (Learner.h)
//   a lane        up to three layers of pool sounds, each with level, offset, pitch, trim, fade, reverse
//   flatten       a lane as one sound, the way NeoBeat will play it
//   KitPlayer     the kit played by one of NeoBeat's 128 patterns, at the kit's tempo and swing
#pragma once

#include "../neobeat/engine/PatternBank.h"
#include "../neobeat/shared/Learner.h"
#include "../neobeat/shared/Loudness.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace bc
{
constexpr int kLanes = 16, kLayers = 3;

struct PoolItem
{
    std::string name, source;          // source: where it came from (a recording, a folder)
    std::vector<float> l, r;
    double sr = 48000.0;
    nbk::Features f;
    std::array<float, nbk::kDim> x {};
    std::array<float, nbk::kRoles> roles {};
    float quality = 0.0f, lufs = -120.0f, peakDb = -120.0f;
    int bestRole = 0;
    float seconds() const noexcept { return sr > 0 ? (float) ((double) l.size() / sr) : 0.0f; }
};

// what a pool item needs once its sound is there
inline void analyseItem (PoolItem& it, nbk::Learner& learner)
{
    if (it.r.size() != it.l.size()) it.r = it.l;
    const int n = (int) std::min<size_t> (it.l.size(), (size_t) (it.sr * 10.0));
    it.f = nbk::analyse (it.l.data(), it.r.data(), n, (float) it.sr);
    it.x = nbk::vectorOf (it.f);
    it.roles = learner.roles (it.f);
    it.quality = nbk::musicalScore (it.f);
    it.bestRole = (int) (std::max_element (it.roles.begin(), it.roles.end()) - it.roles.begin());
    const auto lv = nbk::measureLevel (it.l, it.r, it.sr);
    it.lufs = lv.lufs; it.peakDb = lv.peakDb;
}

struct LaneLayer
{
    int item = -1;                     // index into the pool (-1: empty)
    float levelDb = 0.0f, offsetMs = 0.0f, semis = 0.0f;
    float start = 0.0f, end = 1.0f;    // the part of the sound that plays (0..1)
    float fadeMs = 4.0f;
    bool reverse = false;
};

struct KitLane
{
    std::array<LaneLayer, kLayers> layers {};
    float levelDb = 0.0f, pan = 0.0f, width = 1.0f;
    bool locked = false;               // choosing again leaves it alone
    bool empty() const noexcept { for (const auto& ly : layers) if (ly.item >= 0) return false; return true; }
};

inline bool operator== (const LaneLayer& a, const LaneLayer& b) noexcept
{
    return a.item == b.item && a.levelDb == b.levelDb && a.offsetMs == b.offsetMs && a.semis == b.semis && a.start == b.start && a.end == b.end && a.fadeMs == b.fadeMs && a.reverse == b.reverse;
}
inline bool operator== (const KitLane& a, const KitLane& b) noexcept
{
    return a.layers == b.layers && a.levelDb == b.levelDb && a.pan == b.pan && a.width == b.width && a.locked == b.locked;
}

struct KitSound { std::vector<float> l, r; double sr = 48000.0; float gain = 1.0f, pan = 0.0f; };

struct KitModel
{
    std::vector<std::shared_ptr<PoolItem>> pool;
    std::array<KitLane, kLanes> lanes {};
    std::string name = "My kit";
    double bpm = 100.0;
    float swing = 0.62f;
    int pattern = 0;                   // which of the 128 it is auditioned with

    const PoolItem* item (int i) const noexcept { return i >= 0 && i < (int) pool.size() ? pool[(size_t) i].get() : nullptr; }

    // the pool, best first, for a lane
    std::vector<int> candidates (int lane, nbk::Learner& learner) const
    {
        std::vector<std::pair<float, int>> scored;
        for (int i = 0; i < (int) pool.size(); ++i)
        {
            const auto& it = *pool[(size_t) i];
            if (! it.f.valid) continue;
            const float fit = std::min (1.0f, it.roles[(size_t) lane] * 3.0f);
            scored.push_back ({ (0.25f + 0.75f * fit) * (0.3f + 0.7f * it.quality) * (0.5f + learner.preference (it.f, lane)), i });
        }
        std::stable_sort (scored.begin(), scored.end(), [] (const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
        std::vector<int> out; for (const auto& s : scored) out.push_back (s.second);
        return out;
    }

    // a sound on every lane that is not locked: the best fits, each used once, no two alike
    void choose (nbk::Learner& learner)
    {
        std::vector<nbk::Candidate> cs; std::vector<int> index;
        std::vector<char> taken (pool.size(), 0);
        for (const auto& ln : lanes) if (ln.locked) for (const auto& ly : ln.layers) if (ly.item >= 0 && ly.item < (int) taken.size()) taken[(size_t) ly.item] = 1;
        for (int i = 0; i < (int) pool.size(); ++i)
        {
            if (taken[(size_t) i]) continue;
            nbk::Candidate c; c.f = pool[(size_t) i]->f; c.x = pool[(size_t) i]->x; c.roles = pool[(size_t) i]->roles; c.quality = pool[(size_t) i]->quality;
            cs.push_back (c); index.push_back (i);
        }
        std::array<bool, nbk::kRoles> skip {}; for (int l = 0; l < kLanes; ++l) skip[(size_t) l] = lanes[(size_t) l].locked;
        const auto pick = nbk::chooseKit (learner, cs, true, &skip);
        for (int l = 0; l < kLanes; ++l)
        {
            if (lanes[(size_t) l].locked) continue;
            lanes[(size_t) l].layers = {};
            if (pick[(size_t) l] >= 0) lanes[(size_t) l].layers[0].item = index[(size_t) pick[(size_t) l]];
        }
    }

    // another kit out of the same pool: each open lane takes one of its four best, at random
    void shuffle (nbk::Learner& learner, uint32_t seed)
    {
        std::mt19937 rng (seed);
        std::vector<char> taken (pool.size(), 0);
        for (const auto& ln : lanes) if (ln.locked) for (const auto& ly : ln.layers) if (ly.item >= 0 && ly.item < (int) taken.size()) taken[(size_t) ly.item] = 1;
        for (int l = 0; l < kLanes; ++l)
        {
            if (lanes[(size_t) l].locked) continue;
            std::vector<int> top;
            for (int i : candidates (l, learner)) { if (! taken[(size_t) i]) top.push_back (i); if (top.size() >= 4) break; }
            lanes[(size_t) l].layers = {};
            if (top.empty()) continue;
            const int i = top[(size_t) (rng() % (uint32_t) top.size())];
            lanes[(size_t) l].layers[0].item = i; taken[(size_t) i] = 1;
        }
    }

    // A lane as one sound: its layers trimmed, turned, tuned, placed and mixed. False when it is empty.
    bool flatten (int lane, KitSound& out) const
    {
        const KitLane& ln = lanes[(size_t) lane];
        out = KitSound();
        double sr = 0.0;
        for (const auto& ly : ln.layers) if (const auto* it = item (ly.item)) sr = std::max (sr, it->sr);
        if (sr <= 0.0) return false;
        out.sr = sr;
        float earliest = 0.0f; for (const auto& ly : ln.layers) if (item (ly.item) != nullptr) earliest = std::min (earliest, ly.offsetMs);
        for (const auto& ly : ln.layers)
        {
            const auto* it = item (ly.item);
            if (it == nullptr || it->l.empty()) continue;
            const int n = (int) it->l.size();
            int a = (int) (std::min (ly.start, ly.end) * n), b = (int) (std::max (ly.start, ly.end) * n);
            a = std::max (0, std::min (a, n - 2)); b = std::max (a + 2, std::min (b, n));
            // the layer at the kit's rate and pitch
            const double inc = it->sr / sr * std::exp2 ((double) ly.semis / 12.0);
            const int len = std::max (2, (int) ((double) (b - a - 1) / inc));
            const float match = nbk::matchGainDb ({ it->lufs, it->peakDb });      // every layer starts as loud as the kit's stock sounds
            const float g = std::pow (10.0f, (ly.levelDb + match) / 20.0f);
            const int at = (int) ((ly.offsetMs - earliest) / 1000.0 * sr);
            const int fade = std::max (1, (int) (ly.fadeMs / 1000.0 * sr));
            if ((int) out.l.size() < at + len) { out.l.resize ((size_t) (at + len), 0.0f); out.r.resize ((size_t) (at + len), 0.0f); }
            for (int i = 0; i < len; ++i)
            {
                const double pos = ly.reverse ? (double) (b - 1) - i * inc : (double) a + i * inc;
                const int i0 = std::max (0, std::min (n - 2, (int) pos)); const float fr = (float) (pos - i0);
                const float env = std::min (1.0f, std::min ((float) (i + 1) / (float) std::min (fade, 64), (float) (len - i) / (float) fade));
                out.l[(size_t) (at + i)] += g * env * (it->l[(size_t) i0] + fr * (it->l[(size_t) i0 + 1] - it->l[(size_t) i0]));
                out.r[(size_t) (at + i)] += g * env * (it->r[(size_t) i0] + fr * (it->r[(size_t) i0 + 1] - it->r[(size_t) i0]));
            }
        }
        if (out.l.empty()) return false;
        // width (mono to as it is; wider is NeoBeat's job) and a ceiling: layers that add up must not clip the file
        const float w = std::max (0.0f, std::min (1.0f, ln.width));
        float pk = 0.0f;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            const float mid = 0.5f * (out.l[i] + out.r[i]), side = 0.5f * (out.l[i] - out.r[i]) * w;
            out.l[i] = mid + side; out.r[i] = mid - side;
            pk = std::max (pk, std::max (std::fabs (out.l[i]), std::fabs (out.r[i])));
        }
        out.gain = std::pow (10.0f, ln.levelDb / 20.0f);
        if (pk > 0.985f) { const float k = 0.985f / pk; for (auto& v : out.l) v *= k; for (auto& v : out.r) v *= k; out.gain /= k; }
        out.pan = ln.pan;
        return true;
    }
};

// ------------------------------------------------------------------ the audition player (audio thread)
class KitPlayer
{
public:
    void prepare (double sampleRate) { sr = sampleRate; seq.prepare ((float) sr); events.reserve (4096); for (auto& v : voices) v = {}; ppq = 0.0; mixL.assign (kChunk, 0.0f); mixR.assign (kChunk, 0.0f); limiter = 1.0f; }

    // message thread: a lane's sound changed (null: the lane is empty). The old one is kept alive by the caller.
    void setSound (int lane, std::shared_ptr<const KitSound> s) { if (lane >= 0 && lane < kLanes) std::atomic_store (&sounds[(size_t) lane], std::move (s)); }
    void setPattern (const nb::Pattern& p)
    {
        // (the last few are kept here, so the audio thread never frees one when it takes the next)
        kept[(size_t) (keptAt++ % kept.size())] = std::make_shared<const nb::Pattern> (p);
        std::atomic_store (&nextPattern, kept[(size_t) ((keptAt - 1) % kept.size())]);
    }
    void hit (int lane, float vel = 0.9f) { if (lane >= 0 && lane < kLanes) audition[(size_t) lane].store (vel); }
    std::atomic<bool> playing { false };
    std::atomic<float> bpm { 100.0f }, swing { 0.62f }, level { 0.8f };
    std::atomic<int> step { -1 };
    std::array<std::atomic<bool>, kLanes> mute {};

    // adds into l/r
    void process (float* l, float* r, int n) noexcept
    {
        if (mixL.empty()) return;
        for (int o = 0; o < n; o += kChunk)
        {
            const int m = std::min (kChunk, n - o);
            std::fill (mixL.begin(), mixL.begin() + m, 0.0f); std::fill (mixR.begin(), mixR.begin() + m, 0.0f);
            render (mixL.data(), mixR.data(), m);
            // sixteen lanes at full level add up to more than the output holds: a limiter keeps the sum under it,
            // as NeoBeat's kit bus does (how hard a kit hits there is NeoBeat's to set)
            const float release = std::exp (-1.0f / (0.12f * (float) sr)), lv = level.load();
            for (int i = 0; i < m; ++i)
            {
                const float pk = std::max (std::fabs (mixL[(size_t) i]), std::fabs (mixR[(size_t) i]));
                const float want = pk > 0.89f ? 0.89f / pk : 1.0f;
                limiter = want < limiter ? want : 1.0f - (1.0f - limiter) * release;
                l[o + i] += lv * limiter * mixL[(size_t) i]; r[o + i] += lv * limiter * mixR[(size_t) i];
            }
        }
    }

private:
    void render (float* l, float* r, int n) noexcept
    {
        if (auto p = std::atomic_exchange (&nextPattern, std::shared_ptr<const nb::Pattern>())) { held = p; seq.setPattern (*held); }
        for (int ln = 0; ln < kLanes; ++ln) { const float a = audition[(size_t) ln].exchange (0.0f); if (a > 0.0f) start (ln, a, 0); }
        const bool run = playing.load();
        if (run && ! wasPlaying) ppq = 0.0;
        wasPlaying = run;
        if (run)
        {
            const double tempo = std::max (30.0, (double) bpm.load());
            events.clear();
            seq.collect (ppq, tempo, n, true, swing.load(), 0.0f, events);
            for (const auto& e : events) if (! mute[(size_t) e.pad].load()) start (e.pad, e.velocity, e.offset);
            step.store (seq.stepAt (ppq));
            ppq += tempo / 60.0 / sr * n;
        }
        else step.store (-1);
        const float lv = 1.0f;
        for (auto& v : voices)
        {
            if (! v.s) continue;
            const KitSound& s = *v.s;
            const double inc = s.sr / sr;
            const int len = (int) s.l.size();
            for (int i = std::max (0, v.delay); i < n; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 >= len - 1) { v.s.reset(); break; }
                const float f = (float) (v.pos - i0);
                const float cut = v.fade < 1.0f ? (v.fade -= 1.0f / (0.004f * (float) sr), std::max (0.0f, v.fade)) : 1.0f;
                l[i] += lv * v.gl * cut * (s.l[(size_t) i0] + f * (s.l[(size_t) i0 + 1] - s.l[(size_t) i0]));
                r[i] += lv * v.gr * cut * (s.r[(size_t) i0] + f * (s.r[(size_t) i0 + 1] - s.r[(size_t) i0]));
                v.pos += inc;
                if (v.fade <= 0.0f) { v.s.reset(); break; }
            }
            v.delay = std::max (0, v.delay - n);
        }
    }

    struct Voice { std::shared_ptr<const KitSound> s; double pos = 0.0; float gl = 1.0f, gr = 1.0f, fade = 1.0f; int delay = 0, lane = -1; uint64_t order = 0; };
    void start (int lane, float vel, int offset) noexcept
    {
        auto s = std::atomic_load (&sounds[(size_t) lane]);
        if (! s || s->l.size() < 2) return;
        // hits ring over each other, as in NeoBeat; when every voice is busy the oldest gives way
        Voice* use = nullptr;
        for (auto& v : voices) if (! v.s && use == nullptr) use = &v;
        if (use == nullptr) { use = &voices[0]; for (auto& v : voices) if (v.order < use->order) use = &v; }
        const float g = s->gain * vel / 0.8f;
        use->s = std::move (s); use->pos = 0.0; use->delay = offset; use->lane = lane; use->fade = 1.0f; use->order = ++counter;
        const float pan = std::max (-1.0f, std::min (1.0f, use->s->pan));
        use->gl = g * (pan > 0.0f ? 1.0f - pan : 1.0f); use->gr = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }

    double sr = 48000.0, ppq = 0.0;
    nb::Sequencer seq;
    std::vector<nb::SeqEvent> events;
    std::array<std::shared_ptr<const KitSound>, kLanes> sounds {};
    std::shared_ptr<const nb::Pattern> nextPattern, held;
    std::array<std::shared_ptr<const nb::Pattern>, 6> kept {};
    unsigned keptAt = 0;
    std::array<std::atomic<float>, kLanes> audition {};
    std::array<Voice, 32> voices {};
    uint64_t counter = 0;
    bool wasPlaying = false;
    static constexpr int kChunk = 1024;
    std::vector<float> mixL, mixR;
    float limiter = 1.0f;
};
} // namespace bc
