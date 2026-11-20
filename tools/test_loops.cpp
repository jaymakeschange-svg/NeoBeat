// Batchcut 3.0 - LOOPS without any framework: rolling layers by the rules, deja vu, seeds, locks, the four
// fills, borrowed rhythms, the swung events, the rendered loop (tails wrapped round), and the player
// (transport, Trigger and Slices from MIDI) against the render.
#include "../Source/neobeat/shared/rooms/Loops.h"
#include "Portable.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace bc;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static const double kSr = 48000.0;
static std::shared_ptr<PoolItem> make (const std::string& name, const std::string& source, nbk::Learner& learner, double seconds, double f0, double f1, double decay, float noise, float amp, uint32_t seed)
{
    auto it = std::make_shared<PoolItem>();
    it->name = name; it->source = source; it->sr = kSr;
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

static std::string sig (const LoopLayer& L, int fromStep, int toStep)
{
    std::string s;
    for (const auto& h : L.hits) if (h.step >= fromStep && h.step < toStep) s += std::to_string (h.step - fromStep) + ":" + h.sound->name + " ";
    return s;
}

int main()
{
    nbk::Learner learner;
    std::vector<std::shared_ptr<PoolItem>> pool;
    for (int i = 0; i < 5; ++i) pool.push_back (make ("kick" + std::to_string (i), "yard", learner, 0.35, 160.0 + 10 * i, 48.0 + 3 * i, 0.09 + 0.01 * i, 0.02f, 0.5f, 10u + (uint32_t) i));
    for (int i = 0; i < 5; ++i) pool.push_back (make ("snare" + std::to_string (i), "yard", learner, 0.25, 260.0 + 15 * i, 190.0 + 8 * i, 0.06, 0.6f, 0.5f, 30u + (uint32_t) i));
    for (int i = 0; i < 8; ++i) pool.push_back (make ("hat" + std::to_string (i), "kitchen", learner, 0.08, 9000.0, 9000.0, 0.015, 1.0f, 0.3f, 50u + (uint32_t) i));
    static const double bellHz[] = { 220.0, 261.63, 329.63, 392.0, 440.0, 523.25 };
    for (int i = 0; i < 6; ++i) pool.push_back (make ("bell" + std::to_string (i), "bells", learner, 0.6, bellHz[i], bellHz[i], 0.25, 0.0f, 0.4f, 70u + (uint32_t) i));
    bool valid = true; for (const auto& it : pool) valid = valid && it->f.valid;
    CHECK (valid && pool.size() == 24, "a pool of 24: kicks and snares from the yard, hats from the kitchen, six tuned bells");

    std::printf ("1. rolling a layer (Pool)\n");
    LoopLayer L; L.mode = LoopMode::Pool; L.bars = 4; L.perBar = 16; L.density = 0.55f; L.dejavu = 1.0f; L.seed = 1234;
    const bool rolled = rollLayer (L, pool, &learner, 0, 3, 100.0);
    CHECK (rolled && ! L.hits.empty(), "a 4-bar layer: %zu hits", L.hits.size());
    CHECK (sig (L, 0, 16) == sig (L, 16, 32) && sig (L, 0, 16) == sig (L, 48, 64), "deja vu 1: every bar the same, chops and all");
    bool onOne = false; for (const auto& h : L.hits) onOne = onOne || h.step == 0;
    CHECK (onOne, "it starts on the one");
    {
        LoopLayer M = L; M.dejavu = 0.0f; rollLayer (M, pool, &learner, 0, 3, 100.0);
        int same = 0; for (int b = 1; b < 4; ++b) same += sig (M, 0, 16) == sig (M, b * 16, b * 16 + 16);
        CHECK (same == 0, "deja vu 0: every bar new");
        LoopLayer N = M; rollLayer (N, pool, &learner, 0, 3, 100.0);
        CHECK (N.hits == M.hits, "the same seed rolls the same layer again");
        LoopLayer O = M; O.seed = 99; rollLayer (O, pool, &learner, 0, 3, 100.0);
        CHECK (! (O.hits == M.hits), "another seed, another layer");
        LoopLayer Z = M; Z.density = 0.0f; rollLayer (Z, pool, &learner, 0, 3, 100.0);
        LoopLayer F = M; F.density = 1.0f; rollLayer (F, pool, &learner, 0, 3, 100.0);
        CHECK (Z.hits.empty() && F.hits.size() >= 56, "density 0: silence; density 1: %zu of 64 steps", F.hits.size());
        // strong beats take the heavy half
        double hs = 0.0, ws = 0.0; int nh = 0, nw = 0;
        for (const auto& h : F.hits) { const float w = loopsdetail::weight (h.step, 16), hv = loopsdetail::heaviness (*h.sound); if (w >= 0.7f) { hs += hv; ++nh; } else if (w <= 0.3f) { ws += hv; ++nw; } }
        CHECK (nh > 0 && nw > 0 && hs / nh > ws / nw + 0.2, "the strong beats get the heavy chops (%.2f against %.2f)", hs / std::max (1, nh), ws / std::max (1, nw));
        bool cut = true; for (size_t i = 0; i + 1 < F.hits.size(); ++i) cut = cut && F.hits[i].step + F.hits[i].len <= F.hits[i + 1].step;
        CHECK (cut, "each chop is cut before the next starts");
    }
    {
        LoopLayer K = L; K.dejavu = 0.0f; rollLayer (K, pool, &learner, 0, 3, 100.0);
        K.hits[2].locked = true; const auto kept = K.hits[2];
        K.seed = 777; rollLayer (K, pool, &learner, 0, 3, 100.0);
        bool there = false; for (const auto& h : K.hits) there = there || (h.locked && h.step == kept.step && h.sound == kept.sound);
        int at = 0; for (const auto& h : K.hits) at += h.step == kept.step;
        CHECK (there && at == 1, "a locked chop stays where it is through a new roll");
        const auto before = K.hits[5].sound;
        CHECK (rerollHit (K, 5, pool, &learner, 42) && K.hits[5].sound != before, "one chop rolled again: a different sound, same place");
    }

    std::printf ("2. the other fills\n");
    {
        LoopLayer S; S.mode = LoopMode::Slice; S.source.kind = LoopSource::From; S.source.from = "kitchen"; S.bars = 2; S.density = 0.6f; S.dejavu = 0.3f; S.seed = 5;
        rollLayer (S, pool, &learner, 0, 3, 100.0);
        bool kitchen = ! S.hits.empty(); for (const auto& h : S.hits) kitchen = kitchen && h.sound->source == "kitchen";
        CHECK (kitchen, "Slice: only the kitchen's chops (%zu hits)", S.hits.size());
        LoopLayer T; T.mode = LoopMode::Stutter; T.bars = 2; T.density = 0.7f; T.seed = 8;
        rollLayer (T, pool, &learner, 0, 3, 100.0);
        std::set<const PoolItem*> chops; int ratchets = 0; for (const auto& h : T.hits) { chops.insert (h.sound.get()); ratchets += h.ratchet > 1; }
        CHECK (! T.hits.empty() && chops.size() <= 3 && ratchets > 0, "Stutter: %zu chops at most 3 different, %d ratchets", T.hits.size(), ratchets);
        LoopLayer M; M.mode = LoopMode::Melody; M.bars = 2; M.density = 0.6f; M.seed = 3; M.source.kind = LoopSource::From; M.source.from = "bells";
        const int root = 9, sc = 10;   // A minor pentatonic
        rollLayer (M, pool, &learner, root, sc, 100.0);
        bool bells = ! M.hits.empty(), inKey = true;
        for (const auto& h : M.hits)
        {
            bells = bells && h.sound->name.rfind ("bell", 0) == 0;
            const float note = loopsdetail::midiPitch (*h.sound) + h.semis;
            inKey = inKey && std::fabs (note - std::round (note)) < 0.2f && gen::scale (sc).has ((int) std::lround (note) - root);
        }
        CHECK (bells && inKey, "Melody: the bells, every note in A minor pentatonic (%zu notes)", M.hits.size());
        LoopLayer M2 = M; M2.source = LoopSource(); rollLayer (M2, pool, &learner, root, sc, 100.0);
        bool pitched = ! M2.hits.empty(); for (const auto& h : M2.hits) pitched = pitched && loopsdetail::midiPitch (*h.sound) > 0.0f;
        CHECK (pitched, "from the whole pool, Melody keeps to the sounds with a pitch (%zu notes)", M2.hits.size());
        LoopLayer R; R.mode = LoopMode::Pool; R.random = true; R.bars = 8; R.density = 0.9f; R.dejavu = 0.0f; R.seed = 6;
        rollLayer (R, pool, &learner, 0, 3, 100.0);
        std::set<const PoolItem*> used; for (const auto& h : R.hits) used.insert (h.sound.get());
        CHECK (used.size() >= 22, "RANDOM: unranked, %zu of the 24 sounds heard in 8 bars", used.size());
        LoopLayer B; B.rhythm = 0; B.rhythmLane = 0; B.bars = 2; B.density = 1.0f; B.seed = 2;
        rollLayer (B, pool, &learner, 0, 3, 100.0);
        const auto preset = nb::patternPreset (0); int want = 0, got = 0;
        for (int s = 0; s < 32; ++s) { const bool on = preset.pattern.steps[0][(size_t) (s % preset.pattern.length)].vel > 0; want += on; bool here = false; for (const auto& h : B.hits) here = here || h.step == s; got += on && here; }
        CHECK (want > 0 && got == want && (int) B.hits.size() == want, "a borrowed rhythm: the kick lane of pattern 1, %d hits where it has them", got);
    }

    std::printf ("3. the loop as events and as sound\n");
    LoopSet set; set.bpm = 120.0; set.swing = 0.66f; set.numLayers = 2;
    {
        auto& A = set.layers[0]; A = LoopLayer(); A.bars = 2; A.hits.clear();
        LoopHit h; h.sound = pool[0]; h.step = 0; h.len = 4; A.hits.push_back (h);
        h.step = 1; h.sound = pool[10]; h.len = 1; A.hits.push_back (h);
        h.step = 31; h.sound = pool[18]; h.len = 8; A.hits.push_back (h);      // a bell on the last 16th: its tail wraps round
        h.step = 8; h.sound = pool[5]; h.len = 1; h.ratchet = 3; A.hits.push_back (h);
        auto& Bl = set.layers[1]; Bl = LoopLayer(); Bl.bars = 1; Bl.levelDb = -6.0f; Bl.pan = 0.5f;
        LoopHit k; k.sound = pool[12]; k.step = 4; k.len = 2; Bl.hits.push_back (k);
    }
    const auto snap = makeSnapshot (set);
    CHECK ((int) snap->events.size() == 4 + 2 + 1, "7 events: 4 hits, a ratchet of 3, one more layer");
    CHECK (std::fabs (snap->events[1].beat - 0.33) < 1e-6 && std::fabs (snap->events[0].beat) < 1e-9, "swing: the second 16th lands at 0.33 of a beat");
    std::vector<float> mix, mixR; renderLoop (set, kSr, mix, mixR);
    CHECK ((int) mix.size() == (int) (4.0 * 2 * 0.5 * kSr), "two bars at 120: %zu samples (4 s)", mix.size());
    float head = 0.0f; for (int i = 0; i < 2000; ++i) head = std::max (head, std::fabs (mix[(size_t) i] - 0.0f));
    {
        // the bell on the last 16th rings on past the end: its tail is at the start (in the gap before the kick's swing)
        LoopSet only = set; only.layers[0].hits = { set.layers[0].hits[2] }; only.numLayers = 1;
        std::vector<float> a, b; renderLoop (only, kSr, a, b);
        float early = 0.0f; for (int i = 0; i < 4000; ++i) early = std::max (early, std::fabs (a[(size_t) i]));
        CHECK (early > 0.01f, "a tail past the end wraps round to the start (%.3f there): the file loops without a click", early);
    }
    std::vector<float> s0, s0r, s1, s1r; renderLoop (set, kSr, s0, s0r, 0); renderLoop (set, kSr, s1, s1r, 1);
    double worst = 0.0; for (size_t i = 0; i < mix.size(); ++i) worst = std::max (worst, (double) std::fabs (mix[i] - s0[i] - s1[i]));
    CHECK (worst < 1e-5, "the stems add up to the mix (%.2g)", worst);
    float lr = 0.0f, rr = 0.0f; for (size_t i = 0; i < s1.size(); ++i) { lr = std::max (lr, std::fabs (s1[i])); rr = std::max (rr, std::fabs (s1r[i])); }
    CHECK (rr > lr * 1.5f && lr > 0.0f, "pan right: the right side louder (%.2f against %.2f)", rr, lr);
    {
        LoopSet m = set; m.layers[1].mute = true; std::vector<float> a, b; renderLoop (m, kSr, a, b);
        double d = 0.0; for (size_t i = 0; i < a.size(); ++i) d = std::max (d, (double) std::fabs (a[i] - s0[i]));
        LoopSet so = set; so.layers[1].solo = true; std::vector<float> c, e; renderLoop (so, kSr, c, e);
        double d2 = 0.0; for (size_t i = 0; i < c.size(); ++i) d2 = std::max (d2, (double) std::fabs (c[i] - s1[i]));
        CHECK (d < 1e-6 && d2 < 1e-6, "mute takes a layer out; solo leaves it alone");
    }

    std::printf ("4. the player against the render\n");
    // (tonal sounds: where a noise burst starts by a sample is all its difference)
    LoopSet tonal = set;
    tonal.layers[0].hits[1].sound = pool[18]; tonal.layers[0].hits[3].sound = pool[2]; tonal.layers[1].hits[0].sound = pool[20];
    {
        LoopPlayer p; p.prepare (kSr); p.level.store (1.0f);
        LoopSet q = tonal; for (int i = 0; i < 2; ++i) q.layers[(size_t) i].levelDb -= 12.0f;     // quiet: under the player's limiter
        std::vector<float> want, wantR; renderLoop (q, kSr, want, wantR);
        p.setSnapshot (makeSnapshot (q));
        p.playing.store (true);
        const int loop = (int) want.size(), blocks = (2 * loop) / 512 + 1;
        std::vector<float> outL ((size_t) blocks * 512, 0.0f), outR (outL.size(), 0.0f);
        LoopHost host;   // no host: its own clock
        for (int b = 0; b < blocks; ++b) p.process (outL.data() + b * 512, outR.data() + b * 512, 512, host);
        // the second time round, the first time's tails are there as in the render
        double err = 0.0, ref = 0.0;
        for (int i = 0; i < loop - 2000; ++i) { err = std::max (err, (double) std::fabs (outL[(size_t) (loop + i)] - want[(size_t) i])); ref = std::max (ref, (double) std::fabs (want[(size_t) i])); }
        CHECK (ref > 0.01 && err < 0.02 * ref, "played round twice, it matches the rendered loop to %.2f%% of its peak", 100.0 * err / std::max (1e-9, ref));
        CHECK (p.beatNow.load() >= 0.0 && p.beatNow.load() < q.beats(), "the playhead is inside the loop (beat %.2f)", p.beatNow.load());
        // following the host: its beat 2 is our beat 2
        LoopPlayer h2; h2.prepare (kSr); h2.level.store (1.0f); h2.setSnapshot (makeSnapshot (q)); h2.playing.store (true);
        LoopHost host2; host2.valid = true; host2.playing = true; host2.bpm = 120.0; host2.ppq = 2.0;
        std::vector<float> hl (512, 0.0f), hr (512, 0.0f); h2.process (hl.data(), hr.data(), 512, host2);
        const int at = (int) (2.0 * 0.5 * kSr);
        double e2 = 0.0; for (int i = 0; i < 512; ++i) e2 = std::max (e2, (double) std::fabs (hl[(size_t) i] - want[(size_t) (at + i)]));
        float any = 0.0f; for (float v : hl) any = std::max (any, std::fabs (v));
        CHECK (e2 < 0.05 * ref + 1e-4, "on the host's beat: from beat 2 it plays what the loop has at beat 2 (%.4f)", e2);
        (void) any;
    }
    {
        // Trigger: a key plays the loop from its start, transposed; letting go stops new hits
        LoopSet q = tonal; q.numLayers = 1; q.layers[0].levelDb = -12.0f;
        q.layers[0].hits.erase (q.layers[0].hits.begin() + 2);        // (no tail wrapping round: a key plays the loop from silence)
        LoopPlayer p; p.prepare (kSr); p.level.store (1.0f); p.setSnapshot (makeSnapshot (q));
        std::vector<float> want, wr; renderLoop (q, kSr, want, wr, -1, 7.0f);
        LoopHost host; LoopNote on { 100, 67, 1.0f, true };
        std::vector<float> L2 (8192, 0.0f), R2 (8192, 0.0f);
        p.process (L2.data(), R2.data(), 512, host, &on, 1);
        for (int b = 1; b < 16; ++b) p.process (L2.data() + b * 512, R2.data() + b * 512, 512, host);
        double err = 0.0, ref = 0.0;
        for (int i = 0; i < 8000 - 100; ++i) { if (i < 4000) continue; err = std::max (err, (double) std::fabs (L2[(size_t) (100 + i)] - want[(size_t) i])); ref = std::max (ref, (double) std::fabs (want[(size_t) i])); }
        CHECK (ref > 0.001 && err < 0.03 * ref + 1e-5, "Trigger: G3 plays the loop from its start, 7 semitones up, on the sample it came (%.2f%%)", 100.0 * err / std::max (1e-9, ref));
        LoopNote off { 0, 67, 0.0f, false };
        std::vector<float> L3 (48000 * 6, 0.0f), R3 (L3.size(), 0.0f);
        p.process (L3.data(), R3.data(), 512, host, &off, 1);
        for (int b = 1; b < (int) L3.size() / 512; ++b) p.process (L3.data() + b * 512, R3.data() + b * 512, 512, host);
        float late = 0.0f; for (size_t i = 48000 * 4; i < L3.size(); ++i) late = std::max (late, std::fabs (L3[i]));
        CHECK (late < 1e-6f, "after the key is let go, nothing more starts");
        // Slices: C3 is the first chop, C#3 the next
        LoopSet sl = set; sl.layers[0].midiMode = 1; sl.layers[1].midiMode = 1;
        auto ss = makeSnapshot (sl);
        CHECK (ss->slices.size() == 5 && ss->slices[0].sound == pool[0].get(), "Slices: the %zu distinct chops on C3 upwards, in the order they play", ss->slices.size());
        LoopPlayer sp; sp.prepare (kSr); sp.level.store (1.0f); sp.setSnapshot (ss);
        LoopNote c3 { 0, 60, 1.0f, true }; std::vector<float> a (4096, 0.0f), b (4096, 0.0f);
        sp.process (a.data(), b.data(), 4096, host, &c3, 1);
        float pk = 0.0f; for (float v : a) pk = std::max (pk, std::fabs (v));
        LoopNote far { 0, 90, 1.0f, true }; std::vector<float> c (4096, 0.0f), d (4096, 0.0f);
        LoopPlayer sp2; sp2.prepare (kSr); sp2.setSnapshot (ss); sp2.process (c.data(), d.data(), 4096, host, &far, 1);
        float none = 0.0f; for (float v : c) none = std::max (none, std::fabs (v));
        CHECK (pk > 0.05f && none == 0.0f, "C3 plays the first chop (%.2f); a key past the last chop plays nothing", pk);
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
