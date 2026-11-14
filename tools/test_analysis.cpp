#include "../Source/shared/Learner.h"
#include <cstdio>
using namespace nbk;
static std::vector<float> synth (int kind, float sr, uint32_t seed)
{
    std::vector<float> v; uint32_t x = seed * 747796405u + 1u;
    auto noise = [&] { x = x * 1664525u + 1013904223u; return (float) (x >> 8) / 8388608.0f - 1.0f; };
    const int n = (int) (sr * (kind == 3 ? 0.6f : kind == 5 ? 1.5f : 0.35f));
    float ph = 0, lp = 0, hp = 0;
    for (int i = 0; i < n; ++i)
    {
        const float t = i / sr; float s = 0;
        switch (kind)
        {
            case 0: { const float f = 50.0f + 140.0f * std::exp (-t * 35.0f); ph += 6.2831853f * f / sr; s = std::sin (ph) * std::exp (-t * 11.0f); break; }             // kick
            case 1: { ph += 6.2831853f * 190.0f / sr; s = 0.5f * std::sin (ph) * std::exp (-t * 22.0f) + 0.6f * noise() * std::exp (-t * 16.0f); break; }                // snare
            case 2: { const float w = noise(); hp = w - lp; lp += 0.35f * (w - lp); s = hp * std::exp (-t * 90.0f); break; }                                               // closed hat
            case 3: { const float w = noise(); hp = w - lp; lp += 0.35f * (w - lp); s = hp * std::exp (-t * 7.0f); break; }                                                // open hat
            case 4: { s = (std::sin (6.2831853f * 1830.0f * t) + 0.6f * std::sin (6.2831853f * 2911.0f * t)) * 0.5f * std::exp (-t * 60.0f); if (i > (int) (0.05f * sr)) s = 0; break; }   // rim
            default: { s = (std::sin (6.2831853f * 2100.0f * t) + 0.5f * std::sin (6.2831853f * 5477.0f * t) + 0.3f * std::sin (6.2831853f * 8231.0f * t)) * 0.5f * std::exp (-t * 3.0f); break; } // bell
        }
        v.push_back (0.8f * s);
    }
    return v;
}
int main()
{
    const float sr = 48000.0f; const int want[] = { Kick, Snare, ClosedHat, OpenHat, Rim, Bell };
    Learner L; int ok = 0;
    std::vector<Candidate> pool;
    for (int k = 0; k < 6; ++k)
    {
        const auto v = synth (k, sr, 3);
        const Features f = analyse (v.data(), v.data(), (int) v.size(), sr);
        const auto r = L.roles (f);
        int best = 0; for (int i = 1; i < kRoles; ++i) if (r[i] > r[best]) best = i;
        std::printf ("%-10s -> %-10s (%.2f) | %.2fs att %.1fms dec %.0fms cen %.0fHz flat %.2f low %.2f high %.2f pitch %.0f(%.2f) fall %.2f snr %.0f onsets %d quality %.2f\n", roleName (want[k]), roleName (best), r[best],
                     f.seconds, f.attackMs, f.decayMs, f.centroidHz, f.flatness, f.low, f.high, f.pitchHz, f.pitchStrength, f.pitchFall, f.snrDb, f.onsets, musicalScore (f));
        ok += best == want[k];
        Candidate c; c.f = f; c.x = vectorOf (f); c.roles = r; c.quality = musicalScore (f); pool.push_back (c);
    }
    // a clipped, noisy copy of the kick scores lower
    { auto v = synth (0, sr, 3); uint32_t x = 5; for (auto& s : v) { x = x * 1664525u + 1013904223u; s = std::max (-0.999f, std::min (0.999f, s * 6.0f + 0.05f * ((float) (x >> 8) / 8388608.0f - 1.0f))); }
      const Features f = analyse (v.data(), v.data(), (int) v.size(), sr); std::printf ("clipped noisy kick: quality %.2f (clean %.2f)\n", musicalScore (f), pool[0].quality); ok += musicalScore (f) < 0.6f * pool[0].quality; }
    const auto pick = chooseKit (L, pool, false);
    std::printf ("kit: kick %d snare %d hat %d open %d rim %d bell %d\n", pick[Kick], pick[Snare], pick[ClosedHat], pick[OpenHat], pick[Rim], pick[Bell]);
    ok += pick[Kick] == 0 && pick[Snare] == 1 && pick[ClosedHat] == 2 && pick[OpenHat] == 3 && pick[Bell] == 5;
    // teach it: the "rim" is a Clank for me, and I like bells, not snares
    L.teachRole (pool[4].x, Clank, "my rim");
    const auto r2 = L.roles (pool[4].f); int b2 = 0; for (int i = 1; i < kRoles; ++i) if (r2[i] > r2[b2]) b2 = i;
    std::printf ("after teaching: rim -> %s (%.2f)\n", roleName (b2), r2[b2]); ok += b2 == Clank;
    for (int s = 0; s < 6; ++s) { const auto v = synth (5, sr, 10 + s); const Features f = analyse (v.data(), v.data(), (int) v.size(), sr); L.rate (vectorOf (f), Bell, 5.0f, "bell"); const auto w = synth (1, sr, 10 + s); const Features g = analyse (w.data(), w.data(), (int) w.size(), sr); L.rate (vectorOf (g), Snare, 1.0f, "snare"); }
    const float likeBell = L.preference (pool[5].f, Bell), likeSnare = L.preference (pool[1].f, Snare), likeKick = L.preference (pool[0].f, Kick);
    std::printf ("after 12 ratings: bell %.2f snare %.2f kick (never rated) %.2f\n", likeBell, likeSnare, likeKick); ok += likeBell > 0.7f && likeSnare < 0.3f && likeKick > 0.3f && likeKick < 0.8f;
    L.save ("learner_test.txt"); Learner M; ok += M.load ("learner_test.txt") && M.numRatings() == L.numRatings() && std::fabs (M.preference (pool[5].f, Bell) - likeBell) < 1e-4f;
    std::printf (ok == 11 ? "ANALYSIS PASS\n" : "ANALYSIS FAIL (%d / 11)\n", ok);
    return ok == 11 ? 0 : 1;
}
