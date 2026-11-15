// The generator library shared by Batchcut and MidiJaQ (Source/neobeat/gen), without any framework.
#include "../Source/neobeat/gen/Dealer.h"
#include "../Source/neobeat/gen/Generators.h"
#include "../Source/neobeat/gen/Scale.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

int main()
{
    std::printf ("1. the random source\n");
    {
        gen::Rng a (42), b (42), c (43);
        bool same = true, differs = false;
        for (int i = 0; i < 1000; ++i) { const auto x = a.next(), y = b.next(), z = c.next(); same = same && x == y; differs = differs || x != z; }
        CHECK (same && differs, "a seed gives the same numbers every time; another seed others");
        // known values: the same on every compiler and computer (a seed saved on Windows rebuilds on Linux)
        gen::Rng k (12345);
        const uint32_t v0 = k.next(), v1 = k.next(), v2 = k.next();
        std::printf ("     seed 12345 -> %u %u %u\n", v0, v1, v2);
        gen::Rng k2 (12345);
        CHECK (k2.next() == v0 && k2.next() == v1 && k2.next() == v2, "and again");
        gen::Rng r (7); std::vector<int> count (6, 0);
        for (int i = 0; i < 60000; ++i) ++count[(size_t) r.below (6)];
        int lo = *std::min_element (count.begin(), count.end()), hi = *std::max_element (count.begin(), count.end());
        CHECK (lo > 9600 && hi < 10400, "below(6) is even: %d .. %d of 10000", lo, hi);
        std::vector<int> deck (52); std::iota (deck.begin(), deck.end(), 0);
        r.shuffle (deck); std::vector<int> sorted = deck; std::sort (sorted.begin(), sorted.end());
        int moved = 0; for (int i = 0; i < 52; ++i) moved += deck[(size_t) i] != i;
        CHECK (sorted[0] == 0 && sorted[51] == 51 && std::adjacent_find (sorted.begin(), sorted.end()) == sorted.end() && moved > 40, "a shuffle keeps every card and moves most (%d of 52)", moved);
        CHECK (gen::Rng::mix (5, 1) != gen::Rng::mix (5, 2) && gen::Rng::mix (5, 1) == gen::Rng::mix (5, 1), "a part of a whole gets its own seed, always the same one");
    }

    std::printf ("2. keys and scales\n");
    {
        const auto& major = gen::scale (1), & minorPent = gen::scale (10);
        CHECK (gen::numScales() == 32 && std::string (gen::scale (0).name) == "Chromatic" && major.size() == 7 && minorPent.size() == 5, "32 scales: chromatic, major (7 notes), minor pentatonic (5)...");
        CHECK (gen::quantize (61, 0, major) == 60 && gen::quantize (66, 0, major) == 65 && gen::quantize (64, 0, major) == 64, "in C major: C# goes to C, F# to F, E stays");
        CHECK (gen::quantize (61, 9, minorPent) == 60 && gen::quantize (70, 9, minorPent) == 69, "in A minor pentatonic: C# goes to C, A# to A");
        CHECK (gen::degreeNote (7, 60, 0, major) == 72 && gen::degreeNote (-1, 60, 0, major) == 59 && gen::degreeNote (2, 60, 0, major) == 64, "degrees walk the scale: 7 up is an octave, 1 down is B, 2 up is E");
        CHECK (gen::degreeNote (5, 57, 9, minorPent) == 69 && gen::noteName (60) == "C3" && gen::noteName (57) == "A2", "A minor pentatonic: five steps up from A2 is A3; note 60 is C3");
        bool all = true;
        for (int s = 0; s < gen::numScales(); ++s) for (int n = 0; n < 128; ++n) { const int q = gen::quantize (n, 3, gen::scale (s)); all = all && gen::scale (s).has (q - 3) && std::abs (q - n) <= 6; }
        CHECK (all, "every note quantizes into every scale, never more than a tritone away");
    }

    std::printf ("3. DRIFT: deja vu\n");
    {
        gen::DejaVu d; d.reset (9, 8);
        auto replays = [&] (float dv)
        {
            d.reset (9, 8); d.setDejaVu (dv);
            std::vector<float> v; for (int i = 0; i < 8 * 200; ++i) v.push_back (d.next());
            int same = 0; for (size_t i = 8; i < v.size(); ++i) same += v[i] == v[i - 8];
            return (float) same / (float) (v.size() - 8);
        };
        const float r0 = replays (0.0f), r5 = replays (0.5f), r1 = replays (1.0f);
        CHECK (r0 < 0.01f, "deja vu 0: every value new (%.0f%% repeat)", r0 * 100.0f);
        CHECK (r5 > 0.45f && r5 < 0.67f, "deja vu 0.5: about half come back (%.0f%%)", r5 * 100.0f);
        CHECK (r1 == 1.0f, "deja vu 1: a locked loop (%.0f%%)", r1 * 100.0f);
        gen::DejaVu e; e.reset (9, 8); e.setDejaVu (0.5f); d.reset (9, 8); d.setDejaVu (0.5f);
        bool same = true; for (int i = 0; i < 500; ++i) same = same && d.next() == e.next();
        CHECK (same, "the same seed, the same wandering");
    }

    std::printf ("4. HABIT: the slowly mutating loop\n");
    {
        auto period = [] (float change, int bits)
        {
            gen::Habit h; h.reset (3, bits); h.setChange (change);
            std::vector<int> v; for (int i = 0; i < 400; ++i) v.push_back (h.next().bit);
            for (int p = 1; p <= 64; ++p) { bool ok = true; for (size_t i = (size_t) p; i < v.size() && ok; ++i) ok = v[i] == v[i - (size_t) p]; if (ok) return p; }
            return -1;
        };
        const int p0 = period (0.0f, 8), p1 = period (1.0f, 8), ph = period (0.5f, 8);
        CHECK (p0 > 0 && 8 % p0 == 0, "change 0: an 8-step loop that never changes (repeats every %d)", p0);
        CHECK (p1 > 0 && 16 % p1 == 0 && p1 > 8 / 2, "all the way: turned over each time round (repeats every %d)", p1);
        CHECK (ph < 0, "half way: no loop at all");
        gen::Habit h; h.reset (11, 16); h.setChange (0.1f);
        std::vector<int> a, b; for (int i = 0; i < 16; ++i) a.push_back (h.next().bit); for (int i = 0; i < 16; ++i) b.push_back (h.next().bit);
        int diff = 0; for (int i = 0; i < 16; ++i) diff += a[(size_t) i] != b[(size_t) i];
        CHECK (diff >= 0 && diff <= 6, "a little change: the loop comes round nearly the same (%d of 16 steps changed)", diff);
        bool inRange = true; gen::Habit hv; hv.reset (1, 8); hv.setChange (0.5f); for (int i = 0; i < 1000; ++i) { const float x = hv.next().value; inRange = inRange && x >= 0.0f && x < 1.0f; }
        CHECK (inRange, "its value stays inside 0..1");
    }

    std::printf ("5. ORBIT: Euclidean rhythms\n");
    {
        auto str = [] (const std::vector<bool>& v) { std::string s; for (bool b : v) s += b ? 'x' : '.'; return s; };
        const auto t = gen::euclid (3, 8), c = gen::euclid (5, 8), r = gen::euclid (3, 8, 2);
        CHECK (str (t) == "x..x..x.", "3 in 8: %s (the tresillo)", str (t).c_str());
        CHECK (std::count (c.begin(), c.end(), true) == 5, "5 in 8: %s", str (c).c_str());
        CHECK (str (r) == "x.x..x..", "turned by 2: %s", str (r).c_str());
        bool even = true;
        for (int n = 1; n <= 32; ++n) for (int k = 1; k <= n; ++k)
        {
            const auto e = gen::euclid (k, n);
            std::vector<int> at; for (int i = 0; i < n; ++i) if (e[(size_t) i]) at.push_back (i);
            even = even && (int) at.size() == k;
            int gmin = 1 << 30, gmax = 0; for (size_t i = 0; i < at.size(); ++i) { const int g = (i + 1 < at.size() ? at[i + 1] : at[0] + n) - at[i]; gmin = std::min (gmin, g); gmax = std::max (gmax, g); }
            even = even && gmax - gmin <= 1;
        }
        CHECK (even, "every k in n up to 32: k hits, gaps never more than one step apart");
    }

    std::printf ("6. CUES: when a hit plays\n");
    {
        gen::Rng rng (5);
        gen::Cue every; every.kind = gen::Cue::EveryNth; every.n = 2; every.of = 4;
        std::string s; for (int round = 0; round < 8; ++round) s += every.passes (round, false, true, rng) ? 'x' : '.';
        CHECK (s == ".x...x..", "2 of 4: %s", s.c_str());
        gen::Cue first; first.kind = gen::Cue::First; gen::Cue fill; fill.kind = gen::Cue::Fill;
        CHECK (first.passes (0, false, true, rng) && ! first.passes (1, false, true, rng) && fill.passes (3, true, true, rng) && ! fill.passes (3, false, true, rng), "first time only; fills only");
        gen::Cue ch; ch.kind = gen::Cue::Chance; ch.chance = 0.25f; int on = 0; for (int i = 0; i < 4000; ++i) on += ch.passes (i, false, true, rng);
        CHECK (on > 880 && on < 1120, "25%% of the time (%d of 4000)", on);
    }

    std::printf ("7. the swung grid\n");
    {
        CHECK (std::fabs (gen::swung (1, 16, 0.5f) - 0.25) < 1e-6 && std::fabs (gen::swung (1, 16, 0.66f) - 0.33) < 1e-6 && std::fabs (gen::swung (4, 16, 0.66f) - 1.0) < 1e-6,
               "16ths: straight at 0.5, the off-beat at 0.33 of a beat at 66%%, the beat itself never moves");
        CHECK (std::fabs (gen::swung (1, 12, 0.66f) - 1.0 / 3.0) < 1e-6 && std::fabs (gen::swung (3, 8, 0.6f) - 1.6) < 1e-6, "triplets are played as written; 8ths swing in pairs of 8ths");
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
