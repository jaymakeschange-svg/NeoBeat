// Batchcut 2.0 - the kit model and its audition player, without any framework.
#include "../Source/core/Kit.h"
#include "Portable.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bc;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static const double kSr = 48000.0;
static std::shared_ptr<PoolItem> make (const std::string& name, nbk::Learner& learner, double seconds, double f0, double f1, double decay, float noise, float amp, uint32_t seed)
{
    auto it = std::make_shared<PoolItem>();
    it->name = name; it->source = "test"; it->sr = kSr;
    const int n = (int) (seconds * kSr);
    it->l.assign ((size_t) n, 0.0f);
    PortableRandom rng (seed);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr, env = std::exp (-t / decay), f = f1 + (f0 - f1) * std::exp (-t / 0.03);
        ph += 6.283185307179586 * f / kSr;
        it->l[(size_t) i] = amp * (float) (env * ((1.0 - noise) * std::sin (ph) + noise * rng.uni (-1.0, 1.0)));
    }
    it->r = it->l;
    analyseItem (*it, learner);
    return it;
}

int main()
{
    nbk::Learner learner;
    KitModel kit;
    std::printf ("1. the pool\n");
    for (int i = 0; i < 6; ++i) kit.pool.push_back (make ("kick" + std::to_string (i), learner, 0.35, 160.0 + 10 * i, 48.0 + 3 * i, 0.09 + 0.01 * i, 0.02f, 0.2f + 0.1f * (float) i, 10u + (uint32_t) i));
    for (int i = 0; i < 6; ++i) kit.pool.push_back (make ("snare" + std::to_string (i), learner, 0.25, 260.0 + 15 * i, 190.0 + 8 * i, 0.06 + 0.008 * i, 0.6f, 0.5f, 30u + (uint32_t) i));
    for (int i = 0; i < 8; ++i) kit.pool.push_back (make ("hat" + std::to_string (i), learner, 0.06 + 0.05 * i, 9000.0, 9000.0, 0.012 + 0.03 * i, 1.0f, 0.3f, 50u + (uint32_t) i));
    for (int i = 0; i < 6; ++i) kit.pool.push_back (make ("bell" + std::to_string (i), learner, 0.9, 700.0 + 260 * i, 700.0 + 260 * i, 0.3, 0.0f, 0.4f, 70u + (uint32_t) i));
    bool valid = true; for (const auto& it : kit.pool) valid = valid && it->f.valid && it->lufs > -70.0f;
    CHECK (valid && kit.pool.size() == 26, "26 sounds listened to");
    CHECK (kit.pool[0]->bestRole <= 1 && kit.pool[6]->bestRole >= 2 && kit.pool[6]->bestRole <= 3, "a kick is heard as a kick (%s), a snare as a snare (%s)", nb::roleName (kit.pool[0]->bestRole), nb::roleName (kit.pool[6]->bestRole));

    std::printf ("2. choosing\n");
    kit.choose (learner);
    int filled = 0; std::vector<int> used;
    for (const auto& ln : kit.lanes) if (! ln.empty()) { ++filled; used.push_back (ln.layers[0].item); }
    std::sort (used.begin(), used.end());
    CHECK (filled == 16 && std::adjacent_find (used.begin(), used.end()) == used.end(), "16 lanes, each sound once");
    CHECK (kit.lanes[0].layers[0].item < 6 && kit.lanes[2].layers[0].item >= 6 && kit.lanes[2].layers[0].item < 12 && kit.lanes[4].layers[0].item >= 12 && kit.lanes[4].layers[0].item < 20,
           "kick, snare and closed hat lanes got a kick (%d), a snare (%d), a hat (%d)", kit.lanes[0].layers[0].item, kit.lanes[2].layers[0].item, kit.lanes[4].layers[0].item);
    const int keep = kit.lanes[0].layers[0].item;
    kit.lanes[0].locked = true;
    bool kept = true, others = false;
    for (uint32_t s = 1; s <= 5; ++s) { const auto before = kit.lanes; kit.shuffle (learner, s); kept = kept && kit.lanes[0].layers[0].item == keep; for (int l = 1; l < 16; ++l) others = others || ! (kit.lanes[(size_t) l] == before[(size_t) l]); }
    CHECK (kept && others, "shuffle leaves a locked lane alone");
    const auto cand = kit.candidates (4, learner);
    CHECK (cand.size() == 26 && cand[0] >= 12 && cand[0] < 20, "the closed hat lane's best candidate is a hat (%d)", cand[0]);

    std::printf ("3. a lane as one sound\n");
    kit.lanes[2].layers = {}; kit.lanes[2].layers[0].item = 6;
    KitSound one; const bool flat = kit.flatten (2, one);
    CHECK (flat && one.l.size() == kit.pool[6]->l.size() - 1, "one layer: the sound as it is (%zu of %zu samples)", one.l.size(), kit.pool[6]->l.size());
    const auto lv = nbk::measureLevel (one.l, one.r, one.sr);
    CHECK (std::fabs (lv.lufs - nbk::kKitLoudness) < 1.0f || lv.peakDb > -1.6f, "matched to the kit's loudness (%.1f LUFS, peak %.1f dB)", lv.lufs, lv.peakDb);
    kit.lanes[2].layers[1].item = 0; kit.lanes[2].layers[1].offsetMs = 20.0f;
    KitSound two; kit.flatten (2, two);
    CHECK (two.l.size() == (size_t) (0.020 * kSr) + kit.pool[0]->l.size() - 1, "a layer 20 ms late makes the lane that much longer (%zu)", two.l.size());
    kit.lanes[2].layers[1].offsetMs = -20.0f;
    KitSound early; kit.flatten (2, early);
    float lead = 0.0f; for (int i = 0; i < 900; ++i) lead = std::max (lead, std::fabs (early.l[(size_t) i]));
    CHECK (lead > 0.01f && early.l.size() >= one.l.size() + 900, "a layer 20 ms early starts the lane (the other waits)");
    kit.lanes[2].layers[1] = LaneLayer(); kit.lanes[2].layers[0].semis = 12.0f;
    KitSound up; kit.flatten (2, up);
    CHECK (std::abs ((int) up.l.size() * 2 - (int) one.l.size()) < 8, "an octave up is half as long (%zu)", up.l.size());
    kit.lanes[2].layers[0].semis = 0.0f; kit.lanes[2].layers[0].start = 0.25f; kit.lanes[2].layers[0].end = 0.75f;
    KitSound cut; kit.flatten (2, cut);
    CHECK (std::abs ((int) cut.l.size() * 2 - (int) one.l.size()) < 8, "half of it plays (%zu)", cut.l.size());
    kit.lanes[2].layers[0].start = 0.0f; kit.lanes[2].layers[0].end = 1.0f; kit.lanes[2].layers[0].reverse = true; kit.lanes[2].layers[0].fadeMs = 0.0f;
    KitSound rev; kit.flatten (2, rev);
    float eFirst = 0.0f, eLast = 0.0f; const size_t q = rev.l.size() / 4;
    for (size_t i = 0; i < q; ++i) { eFirst += rev.l[i] * rev.l[i]; eLast += rev.l[rev.l.size() - 1 - i] * rev.l[rev.l.size() - 1 - i]; }
    CHECK (eLast > 4.0f * eFirst, "reversed: the loud part comes last");
    kit.lanes[2].layers[0].reverse = false; kit.lanes[2].layers[0].fadeMs = 4.0f;
    for (int y = 0; y < 3; ++y) { kit.lanes[3].layers[(size_t) y].item = y; kit.lanes[3].layers[(size_t) y].levelDb = 18.0f; }
    KitSound hot; kit.flatten (3, hot);
    float pk = 0.0f; for (float v : hot.l) pk = std::max (pk, std::fabs (v));
    CHECK (pk <= 0.986f && hot.gain > 1.5f, "three loud layers do not clip the lane's file: the level goes to its gain (peak %.3f, gain %.1f)", pk, hot.gain);
    kit.lanes[5].width = 0.0f;
    KitSound mono; kit.flatten (5, mono); bool same = true; for (size_t i = 0; i < mono.l.size(); ++i) same = same && mono.l[i] == mono.r[i];
    CHECK (same, "width 0 is mono");
    kit.lanes[15].layers = {};
    KitSound none; CHECK (! kit.flatten (15, none), "an empty lane has no sound");

    std::printf ("4. the audition\n");
    KitPlayer player; player.prepare (kSr);
    std::vector<std::shared_ptr<const KitSound>> sounds;
    for (int l = 0; l < kLanes; ++l) { auto s = std::make_shared<KitSound>(); if (kit.flatten (l, *s)) { sounds.push_back (s); player.setSound (l, s); } }
    const auto preset = nb::patternPreset (0);
    player.setPattern (preset.pattern); player.bpm.store (120.0f); player.swing.store (0.5f); player.level.store (1.0f);
    std::vector<float> L (512), R (512);
    float silent = 0.0f; for (int b = 0; b < 20; ++b) { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); player.process (L.data(), R.data(), 512); for (float v : L) silent = std::max (silent, std::fabs (v)); }
    CHECK (silent == 0.0f && player.step.load() == -1, "silent until PLAY");
    player.playing.store (true);
    float top = 0.0f; double energy = 0.0; int steps = 0, last = -1; bool finite = true;
    const int blocks = (int) (4.0 * kSr / 512);      // two bars at 120
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        player.process (L.data(), R.data(), 512);
        for (int i = 0; i < 512; ++i) { top = std::max (top, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i]))); energy += L[(size_t) i] * L[(size_t) i]; finite = finite && std::isfinite (L[(size_t) i]); }
        const int st = player.step.load(); if (st != last) { ++steps; last = st; }
    }
    CHECK (finite && top > 0.2f && top <= 0.9f, "the kit plays, under the ceiling (peak %.2f)", top);
    CHECK (steps >= 30 && steps <= 34, "two bars are 32 steps (%d seen)", steps);
    player.mute[0].store (true); player.mute[1].store (true);
    double muted = 0.0;
    for (int b = 0; b < blocks; ++b) { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); player.process (L.data(), R.data(), 512); for (float v : L) muted += v * v; }
    CHECK (muted < energy * 0.9, "muting the kicks takes them out (energy %.1f -> %.1f)", energy, muted);
    player.playing.store (false);
    for (int b = 0; b < 400; ++b) { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); player.process (L.data(), R.data(), 512); }
    float tail = 0.0f; for (float v : L) tail = std::max (tail, std::fabs (v));
    CHECK (tail < 1.0e-5f, "after STOP the tails ring out and it is silent");
    player.hit (4); std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); player.process (L.data(), R.data(), 512);
    float hit = 0.0f; for (float v : L) hit = std::max (hit, std::fabs (v));
    CHECK (hit > 0.01f, "a lane can be heard on its own (%.2f)", hit);
    // a block of any size, and a sound taken away while it rings
    std::vector<float> big (5000), big2 (5000);
    player.playing.store (true); player.process (big.data(), big2.data(), 5000); player.setSound (4, nullptr); player.process (big.data(), big2.data(), 1); player.process (big.data(), big2.data(), 4999);
    bool ok = true; for (float v : big) ok = ok && std::isfinite (v);
    CHECK (ok, "odd block sizes, and a sound replaced while it rings");

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
