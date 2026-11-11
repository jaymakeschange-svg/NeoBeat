// BatchCut engine tests: synthetic recordings where every event's true start is known.
// test_engine [outdir] also writes the test recordings and some one-shots as WAVs.
#include "core/Engine.h"
#include "core/Player.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace bc;
static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static const double SR = 48000.0;

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    std::FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const uint32_t n = (uint32_t) l.size(), bytes = n * 4, rate = (uint32_t) sr, br = rate * 4, riff = 36 + bytes, sixteen = 16;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&sixteen, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&rate, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) for (float v : { l[i], (r.empty() ? l : r)[i] }) { const int16_t s = (int16_t) std::lrint (32767.0f * std::max (-1.0f, std::min (1.0f, v))); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

struct Gen
{
    std::mt19937 rng { 1 };
    std::normal_distribution<float> nd;
    float white() { return nd (rng); }
};

// pink-ish noise (Paul Kellet's filter) plus a rumble, at a target RMS
static void addNoise (std::vector<float>& x, float rms, Gen& g, float rumble = 1.0f, double swellHz = 0.0)
{
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, lp = 0;
    std::vector<float> n (x.size());
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double w = g.white();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
        lp += 0.004 * (g.white() - lp);
        double v = pink + rumble * 30.0 * lp;
        if (swellHz > 0) v *= 0.35 + 0.65 * (0.5 + 0.5 * std::sin (6.283185307 * swellHz * (double) i / SR));   // traffic coming and going
        n[i] = (float) v; e += v * v;
    }
    const float k = rms / (float) std::sqrt (e / (double) x.size() + 1e-30);
    for (size_t i = 0; i < x.size(); ++i) x[i] += k * n[i];
}

// the RMS of what lies above fLo (a 4th-order high-pass), for checks on a high sound with a low one under it
static float highRms (const std::vector<float>& x, double fLo)
{
    const double w = std::tan (3.14159265358979 * fLo / SR), k = 1.0 / (1.0 + std::sqrt (2.0) * w + w * w);
    const double b0 = k, b1 = -2 * k, b2 = k, a1 = 2 * (w * w - 1) * k, a2 = (1 - std::sqrt (2.0) * w + w * w) * k;
    std::vector<double> y (x.begin(), x.end());
    for (int pass = 0; pass < 2; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : y) { const double in = v, out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = in; y2 = y1; y1 = out; v = out; }
    }
    double e = 0; for (double v : y) e += v * v;
    return (float) std::sqrt (e / std::max<size_t> (1, y.size()));
}
static float rmsOf (const std::vector<float>& x, size_t a = 0, size_t b = 0) { if (b == 0) b = x.size(); double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i]; return (float) std::sqrt (e / std::max<size_t> (1, b - a)); }

// a clank: inharmonic partials, fast attack
static void clank (std::vector<float>& x, size_t at, float amp, double f0, double decay, Gen& g)
{
    const double ratios[] = { 1.0, 1.61, 2.32, 3.71, 4.93, 6.4 };
    const size_t n = (size_t) (SR * 0.8);
    for (size_t i = 0; i < n && at + i < x.size(); ++i)
    {
        const double t = (double) i / SR; double v = 0;
        for (int k = 0; k < 6; ++k) v += std::exp (-decay * (1 + 0.6 * k) * t) * std::sin (6.283185307 * f0 * ratios[k] * t + k) / (1 + 0.5 * k);
        v += 0.6 * g.white() * std::exp (-300.0 * t);
        x[at + i] += (float) (amp * std::min (1.0, t * 4000.0) * v * 0.4);
    }
}
static void kick (std::vector<float>& x, size_t at, float amp)
{
    double ph = 0;
    for (size_t i = 0; i < (size_t) (SR * 0.35) && at + i < x.size(); ++i) { const double t = (double) i / SR; ph += 6.283185307 * (50 + 120 * std::exp (-t * 35)) / SR; x[at + i] += (float) (amp * std::exp (-t * 9) * std::sin (ph) * std::min (1.0, t * 3000)); }
}
static void hat (std::vector<float>& x, size_t at, float amp, Gen& g)
{
    double hp = 0, prev = 0;
    for (size_t i = 0; i < (size_t) (SR * 0.06) && at + i < x.size(); ++i) { const double t = (double) i / SR; const double w = g.white(); hp = 0.6 * (hp + w - prev); prev = w; x[at + i] += (float) (amp * 0.5 * std::exp (-t * 70) * hp); }
}
static void snare (std::vector<float>& x, size_t at, float amp, Gen& g)
{
    for (size_t i = 0; i < (size_t) (SR * 0.2) && at + i < x.size(); ++i) { const double t = (double) i / SR; x[at + i] += (float) (amp * std::min (1.0, t * 3000) * (0.5 * std::exp (-t * 30) * std::sin (6.283185307 * 190 * t) + 0.45 * std::exp (-t * 22) * g.white())); }
}

struct Score { int hits = 0, missed = 0, extra = 0; double meanErrMs = 0, maxErrMs = 0; std::vector<int> match; };
static Score score (const std::vector<Event>& ev, const std::vector<int64_t>& truth, double tolMs)
{
    Score s; s.match.assign (truth.size(), -1);
    std::vector<bool> used (ev.size(), false);
    for (size_t t = 0; t < truth.size(); ++t)
    {
        int best = -1; double bd = 1e9;
        for (size_t i = 0; i < ev.size(); ++i) if (! used[i]) { const double d = std::fabs ((double) (ev[i].onset - truth[t])) / SR * 1000.0; if (d < bd) { bd = d; best = (int) i; } }
        if (best >= 0 && bd <= tolMs) { used[(size_t) best] = true; s.match[t] = best; ++s.hits; s.meanErrMs += bd; s.maxErrMs = std::max (s.maxErrMs, bd); }
        else ++s.missed;
    }
    for (bool u : used) if (! u) ++s.extra;
    if (s.hits) s.meanErrMs /= s.hits;
    return s;
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";
    Gen g;

    std::printf ("1. a ball bouncing in a noisy room (the hits sit under the noise's level)\n");
    {
        Audio a; a.sr = SR; a.l.assign ((size_t) (SR * 14), 0.0f);
        std::vector<int64_t> truth;
        double t = 1.0, gap = 0.9; float amp = 0.5f;
        while (gap > 0.05 && t < 13.0) { truth.push_back ((int64_t) (t * SR)); clank (a.l, (size_t) (t * SR), amp, 820.0, 18.0, g); t += gap; gap *= 0.78; amp *= 0.85f; }
        const float hitRms = rmsOf (a.l, (size_t) SR, (size_t) (SR * 5));
        addNoise (a.l, 0.06f, g, 1.2f);
        const float noiseRms = rmsOf (a.l, (size_t) (SR * 13.2), (size_t) (SR * 14));
        std::printf ("     %zu bounces, the quietest peaks at %.3f, the noise's own RMS is %.3f (peak about %.2f)\n", truth.size(), amp / 0.85f * 0.4f, noiseRms, 4.0f * noiseRms);
        (void) hitRms;
        const auto t0 = std::chrono::steady_clock::now();
        auto an = analyze (a);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("     analysis of %.0f s took %.2f s\n", an->seconds(), took);
        DetectParams p;
        auto ev = detect (a, *an, p);
        const auto s = score (ev, truth, 6.0);
        std::printf ("     found %zu: %d of %zu bounces, %d extra, start error mean %.2f ms, worst %.2f ms\n", ev.size(), s.hits, truth.size(), s.missed + s.hits == (int) truth.size() ? s.extra : s.extra, s.meanErrMs, s.maxErrMs);
        CHECK (s.missed <= 3, "the bounces over the noise found (%d missed; the last 3 are quieter than the noise)", s.missed);
        CHECK (s.extra == 0, "nothing else found in the noise (%d extra)", s.extra);
        CHECK (s.maxErrMs <= 4.0, "each start within 4 ms of the truth (worst %.2f ms)", s.maxErrMs);
        // FIND MORE from the first bounce: searched for in its own bands, the quiet ones come out of the noise
        {
            const Template tp = makeTemplate (*an, ev[0]);
            auto more = detect (a, *an, p, {}, {}, &tp, 0.5f);
            const auto sm = score (more, truth, 6.0);
            int found = 0; for (const auto& e : more) found += e.found ? 1 : 0;
            std::printf ("     FIND MORE like bounce 1: %d of %zu bounces, %d extra (%d added by the search)\n", sm.hits, truth.size(), sm.extra, found);
            CHECK (sm.missed <= 1 && sm.extra <= 1, "FIND MORE finds the quiet bounces too (%d missed, %d extra)", sm.missed, sm.extra);
            CHECK (sm.maxErrMs <= 6.0, "their starts within 6 ms (worst %.2f ms)", sm.maxErrMs);
        }
        // a plain level gate cannot do this: how many bounces does the waveform show above the noise's peaks?
        float noisePk = 0; for (size_t i = (size_t) (SR * 13.2); i < a.l.size(); ++i) noisePk = std::max (noisePk, std::fabs (a.l[i]));
        int visible = 0; for (auto tr : truth) { float pk = 0; for (int64_t i = tr; i < tr + (int64_t) (0.02 * SR); ++i) pk = std::max (pk, std::fabs (a.l[(size_t) i])); visible += pk > noisePk ? 1 : 0; }
        std::printf ("     (a level gate set just over the noise would see %d of them)\n", visible);
        auto st = LayerState();
        if (! out.empty())
        {
            writeWav (out + "/bounces_in_noise.wav", a.l, a.l, SR);
            ExportParams ep; std::vector<float> l, r;
            for (int i = 0; i < std::min<int> (4, (int) ev.size()); ++i) { renderEvent (a, *an, st, ev[(size_t) i], ep, -1, l, r); writeWav (out + "/bounce_" + std::to_string (i + 1) + ".wav", l, r, SR); }
            LayerState clean; clean.layers[Background].mute = true;
            for (int i = 0; i < std::min<int> (4, (int) ev.size()); ++i) { renderEvent (a, *an, clean, ev[(size_t) i], ep, -1, l, r); writeWav (out + "/bounce_" + std::to_string (i + 1) + "_clean.wav", l, r, SR); }
        }
        // sensitivity: low finds the obvious only, high finds more and no junk
        DetectParams lo = p; lo.sensitivity = 0.1f; DetectParams hi = p; hi.sensitivity = 0.9f;
        const auto sl = score (detect (a, *an, lo), truth, 6.0), sh = score (detect (a, *an, hi), truth, 6.0);
        std::printf ("     sensitivity 0.1: %d found, %d extra | 0.9: %d found, %d extra\n", sl.hits, sl.extra, sh.hits, sh.extra);
        CHECK (sl.hits <= s.hits && sl.extra == 0, "low sensitivity finds no more and nothing false");
        CHECK (sh.hits >= s.hits && sh.extra <= 2, "high sensitivity finds at least as many, little junk (%d extra)", sh.extra);
        // background removed: the noise between bounces is gone
        LayerState clean; clean.layers[Background].mute = true;
        std::vector<float> l, r; render (a, *an, clean, (int64_t) (SR * 13.2), (int64_t) (SR * 13.9), l, r);
        const float left = rmsOf (l);
        std::printf ("     background muted: the noise falls from %.4f to %.4f RMS (%.1f dB)\n", noiseRms, left, 20 * std::log10 (left / noiseRms + 1e-9));
        CHECK (left < noiseRms * 0.2f, "muting the background removes the noise (%.1f dB)", 20 * std::log10 (left / noiseRms + 1e-9));
        if (ev.size() > 1)
        {
            std::vector<float> l1, r1, l2, r2;
            render (a, *an, LayerState(), ev[1].start, ev[1].end, l1, r1, -1, true);
            render (a, *an, clean, ev[1].start, ev[1].end, l2, r2);
            CHECK (rmsOf (l2) > 0.5f * rmsOf (l1), "the bounce itself survives (%.0f %% of its level)", 100 * rmsOf (l2) / rmsOf (l1));
        }
    }

    std::printf ("2. hits that are quieter than the noise, but in other bands (a high tick in traffic rumble)\n");
    {
        Audio a; a.sr = SR; a.l.assign ((size_t) (SR * 10), 0.0f);
        std::vector<int64_t> truth;
        for (int i = 0; i < 8; ++i) { const int64_t at = (int64_t) ((1.0 + i * 1.07) * SR); truth.push_back (at); for (int k = 0; k < (int) (0.03 * SR); ++k) a.l[(size_t) (at + k)] += 0.05f * std::exp (-k / (0.004f * (float) SR)) * std::sin (6.283185f * 6200.0f * k / (float) SR); }
        Audio rumble = a; rumble.l.assign (a.l.size(), 0.0f);
        Gen g2; g2.rng.seed (9);
        for (size_t i = 0; i < rumble.l.size(); ++i) rumble.l[i] = 0.0f;
        { double lp = 0, lp2 = 0; for (size_t i = 0; i < rumble.l.size(); ++i) { lp += 0.01 * (g2.white() - lp); lp2 += 0.01 * (lp - lp2); rumble.l[i] = (float) lp2; } }
        const float k = 0.15f / rmsOf (rumble.l); for (size_t i = 0; i < a.l.size(); ++i) a.l[i] += k * rumble.l[i];
        addNoise (a.l, 0.004f, g2, 0.0f);
        float quietest = 1; for (auto tr : truth) { float pk = 0; for (int64_t i = tr; i < tr + 400; ++i) pk = std::max (pk, std::fabs (a.l[(size_t) i] - k * rumble.l[(size_t) i])); quietest = std::min (quietest, pk); }
        std::printf ("     ticks peak at %.3f; the rumble's RMS is 0.150\n", quietest);
        auto an = analyze (a);
        auto ev = detect (a, *an, DetectParams());
        const auto s = score (ev, truth, 6.0);
        CHECK (s.missed == 0 && s.extra == 0, "all 8 ticks found, nothing else (%d missed, %d extra)", s.missed, s.extra);
        CHECK (s.maxErrMs <= 3.0, "starts within 3 ms (worst %.2f ms)", s.maxErrMs);
        if (! out.empty()) writeWav (out + "/ticks_in_rumble.wav", a.l, a.l, SR);
    }

    std::printf ("3. things that are not events: traffic swelling, a pitch gliding, steady hum\n");
    {
        Audio a; a.sr = SR; a.l.assign ((size_t) (SR * 20), 0.0f);
        addNoise (a.l, 0.08f, g, 1.5f, 0.12);
        double ph = 0;
        for (size_t i = 0; i < a.l.size(); ++i) { const double t = (double) i / SR; ph += 6.283185307 * (400 + 300 * std::sin (6.283185307 * 0.2 * t)) / SR; a.l[i] += (float) (0.05 * std::sin (ph) + 0.03 * std::sin (6.283185307 * 50 * t)); }
        auto an = analyze (a);
        auto ev = detect (a, *an, DetectParams());
        CHECK (ev.size() == 0, "nothing found in 20 s of it (%zu found)", ev.size());
        DetectParams hi; hi.sensitivity = 0.85f;
        CHECK (detect (a, *an, hi).size() <= 3, "even at high sensitivity, at most 3 (%zu)", detect (a, *an, hi).size());
        if (! out.empty()) writeWav (out + "/not_events.wav", a.l, a.l, SR);
    }

    std::printf ("4. a dense drum loop (sixteenth hats, kick, snare; 120 BPM, stereo)\n");
    Audio loop; loop.sr = SR; loop.l.assign ((size_t) (SR * 8.5), 0.0f);
    std::vector<int64_t> truth; std::vector<int> kind;
    {
        const double step = 60.0 / 120.0 / 4.0;
        for (int s = 0; s < 64; ++s)
        {
            const size_t at = (size_t) ((0.25 + s * step) * SR);
            if (s % 8 == 0) { kick (loop.l, at, 0.8f); truth.push_back ((int64_t) at); kind.push_back (0); }
            else if (s % 8 == 4) { snare (loop.l, at, 0.5f, g); truth.push_back ((int64_t) at); kind.push_back (1); }
            else { hat (loop.l, at, s % 2 ? 0.25f : 0.4f, g); truth.push_back ((int64_t) at); kind.push_back (2); }
        }
        loop.r = loop.l;
        for (size_t i = 0; i < loop.r.size(); ++i) loop.r[i] = 0.8f * loop.l[i] + 0.0002f * g.white();
        addNoise (loop.l, 0.001f, g, 0.0f);
        auto an = analyze (loop);
        DetectParams p; p.minGapMs = 40.0f;
        auto ev = detect (loop, *an, p);
        const auto s = score (ev, truth, 6.0);
        std::printf ("     found %zu of %zu, %d extra, start error mean %.2f ms, worst %.2f ms\n", ev.size(), truth.size(), s.extra, s.meanErrMs, s.maxErrMs);
        CHECK (s.missed == 0, "every hit found (%d missed)", s.missed);
        CHECK (s.extra == 0, "no double triggers (%d extra)", s.extra);
        CHECK (s.maxErrMs <= 3.0, "starts within 3 ms (worst %.2f ms)", s.maxErrMs);
        bool cut = true; for (size_t i = 0; i + 1 < ev.size(); ++i) cut = cut && ev[i].end <= ev[i + 1].start;
        CHECK (cut, "each one-shot ends where the next one starts");
        // groups: kicks, snares and hats apart
        std::vector<Group> groups; group (ev, groups, 0.5f);
        int pure = 0, matched = 0;
        for (int gi = 0; gi < (int) groups.size(); ++gi)
        {
            int c[3] = { 0, 0, 0 };
            for (size_t t = 0; t < truth.size(); ++t) if (s.match[t] >= 0 && ev[(size_t) s.match[t]].group == gi) ++c[kind[t]];
            pure += std::max (c[0], std::max (c[1], c[2])); matched += c[0] + c[1] + c[2];
            std::printf ("     group %-12s %2d events: %d kick, %d snare, %d hat\n", groups[(size_t) gi].name.c_str(), groups[(size_t) gi].count, c[0], c[1], c[2]);
        }
        CHECK (groups.size() >= 3 && groups.size() <= 6, "3 to 6 groups (%zu)", groups.size());
        CHECK (pure >= matched * 95 / 100, "kicks, snares and hats are not mixed (%d of %d in their group's majority)", pure, matched);
        // pick: best of each, more like this
        PickParams pk; pk.mode = Pick::Best; pk.bestN = 1;
        CHECK (pick (ev, groups, pk).size() == groups.size(), "BEST: one per group");
        int kickIdx = s.match[0];
        pk.mode = Pick::Like; pk.ref = ev[(size_t) kickIdx].feat; pk.hasRef = true; pk.refRise = ev[(size_t) kickIdx].rise; pk.hasRise = true; pk.similarity = 0.5f;
        const auto like = pick (ev, groups, pk);
        int kicks = 0; for (int i : like) for (size_t t = 0; t < truth.size(); ++t) if (s.match[t] == i && kind[t] == 0) ++kicks;
        CHECK (kicks == 8 && (int) like.size() == 8, "MORE LIKE THIS on a kick: the 8 kicks and nothing else (%zu picked, %d kicks)", like.size(), kicks);
        ev[0].enabled = false; groups[(size_t) ev[3].group].enabled = false; pk.mode = Pick::All;
        const auto all = pick (ev, groups, pk);
        bool none = true; for (int i : all) none = none && i != 0 && ev[(size_t) i].group != ev[3].group;
        CHECK (none, "switched-off events and groups are left out");
        if (! out.empty()) writeWav (out + "/drum_loop.wav", loop.l, loop.r, SR);

        std::printf ("5. layers: nothing changes until you change it\n");
        LayerState st;
        std::vector<float> l, r, l2, r2;
        render (loop, *an, st, 0, loop.size(), l, r);
        double err = 0; for (size_t i = 0; i < l.size(); ++i) err = std::max (err, (double) std::fabs (l[i] - loop.l[i]) + std::fabs (r[i] - loop.r[i]));
        CHECK (err == 0.0, "untouched, the original samples come out exactly");
        render (loop, *an, st, (int64_t) SR, (int64_t) (SR * 3), l2, r2, -1, true);
        double e2 = 0, p2 = 0; for (size_t i = 0; i < l2.size(); ++i) { const float d = l2[i] - loop.l[(size_t) SR + i]; e2 += (double) d * d; p2 += (double) loop.l[(size_t) SR + i] * loop.l[(size_t) SR + i]; }
        const double recDb = 10 * std::log10 (e2 / p2 + 1e-30);
        CHECK (recDb < -60.0, "all four layers together rebuild the sound (error %.1f dB)", recDb);
        // each layer alone, summed, is the whole
        std::vector<float> sum (l2.size(), 0.0f);
        for (int L = 0; L < kAuto; ++L) { std::vector<float> a1, b1; render (loop, *an, st, (int64_t) SR, (int64_t) (SR * 3), a1, b1, L); for (size_t i = 0; i < sum.size(); ++i) sum[i] += a1[i]; }
        double e3 = 0; for (size_t i = 0; i < sum.size(); ++i) { const float d = sum[i] - loop.l[(size_t) SR + i]; e3 += (double) d * d; }
        CHECK (10 * std::log10 (e3 / p2 + 1e-30) < -60.0, "the layers rendered one by one add up to the sound (%.1f dB)", 10 * std::log10 (e3 / p2 + 1e-30));
        // the kick is mostly tone + transient, the hat mostly noise + transient
        std::vector<float> lt, rt, ln, rn;
        const int64_t k0 = truth[0], h0 = truth[1];
        render (loop, *an, st, k0, k0 + (int64_t) (0.25 * SR), lt, rt, Tone);
        render (loop, *an, st, h0, h0 + (int64_t) (0.05 * SR), ln, rn, Noise);
        std::vector<float> lk, rk, lh, rh;
        render (loop, *an, st, k0, k0 + (int64_t) (0.25 * SR), lk, rk, -1, true);
        render (loop, *an, st, h0, h0 + (int64_t) (0.05 * SR), lh, rh, -1, true);
        std::printf ("     the kick's tone layer holds %.0f %% of its energy; the hat's noise layer %.0f %% of its\n", 100 * std::pow (rmsOf (lt) / rmsOf (lk), 2), 100 * std::pow (rmsOf (ln) / rmsOf (lh), 2));
        CHECK (rmsOf (lt) > 0.5f * rmsOf (lk), "the kick sits mostly in TONE");

        std::printf ("6. edits\n");
        // erase a box over the kick's low end: that energy goes, the hat above is untouched
        EditOp er; er.kind = EditOp::Gain; er.gain = 0.0f; er.shape = Shape::rect ((double) k0 / SR - 0.02, (double) k0 / SR + 0.3, 0.0f, an->tab.yOf (400.0f));
        st.apply (er);
        render (loop, *an, st, k0, k0 + (int64_t) (0.25 * SR), lt, rt);
        CHECK (rmsOf (lt) < 0.1f * rmsOf (lk), "ERASE over the kick removes it (%.1f dB)", 20 * std::log10 (rmsOf (lt) / rmsOf (lk)));
        render (loop, *an, st, (int64_t) (SR * 2), (int64_t) (SR * 3), l, r);
        double same = 0; for (size_t i = 0; i < l.size(); ++i) same = std::max (same, (double) std::fabs (l[i] - loop.l[(size_t) (SR * 2) + i]));
        CHECK (same == 0.0, "sound outside the edit is untouched");
        CHECK (st.undo(), "undo");
        render (loop, *an, st, k0, k0 + (int64_t) (0.25 * SR), lt, rt);
        CHECK (rmsOf (lt) > 0.99f * rmsOf (lk), "after undo the kick is back");
        CHECK (st.redo() && st.undo(), "redo and undo again");
        // move a region to a new layer: muting that layer takes it out, the rest stays
        const int nl = st.addLayer();
        EditOp mv; mv.kind = EditOp::Move; mv.target = nl; mv.shape = Shape::rect ((double) h0 / SR - 0.01, (double) h0 / SR + 0.06, an->tab.yOf (3000.0f), 1.0f);
        st.apply (mv);
        std::vector<float> lm, rm; render (loop, *an, st, h0, h0 + (int64_t) (0.05 * SR), lm, rm, -1, true);
        CHECK (rmsOf (lm) > 0.98f * rmsOf (lh) && rmsOf (lm) < 1.02f * rmsOf (lh), "moving it to a new layer changes nothing you hear");
        st.layers[(size_t) nl].mute = true;
        render (loop, *an, st, h0, h0 + (int64_t) (0.05 * SR), lm, rm);
        // measured above 4 kHz: the kick before the hat is still ringing in this window, far below
        CHECK (highRms (lm, 4000) < 0.25f * highRms (lh, 4000), "muting that layer takes the hat out (%.1f dB above 4 kHz)", 20 * std::log10 (highRms (lm, 4000) / highRms (lh, 4000)));
        CHECK (rmsOf (lm) > 0.9f * rmsOf (lh) * 0.5f, "the kick's tail under it stays");
        std::vector<float> lo, ro; render (loop, *an, st, h0, h0 + (int64_t) (0.05 * SR), lo, ro, nl);
        CHECK (highRms (lo, 4000) > 0.7f * highRms (lh, 4000), "and the layer alone holds it (%.1f dB)", 20 * std::log10 (highRms (lo, 4000) / highRms (lh, 4000)));
        // a brush shape (bitmap) works like the box
        Shape br; br.t0 = (double) truth[2] / SR - 0.01; br.t1 = br.t0 + 0.08; br.y0 = 0.6f; br.y1 = 1.0f; br.w = 8; br.h = 8; br.a.assign (64, 0); for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j) br.a[(size_t) (j * 8 + i)] = 255;
        EditOp eb; eb.kind = EditOp::Gain; eb.gain = 0.25f; eb.shape = br; st.apply (eb);
        render (loop, *an, st, truth[2], truth[2] + (int64_t) (0.05 * SR), lm, rm);
        std::vector<float> lref, rref; render (loop, *an, LayerState(), truth[2], truth[2] + (int64_t) (0.05 * SR), lref, rref);
        const float hiDb = 20 * std::log10 (highRms (lm, 5000) / highRms (lref, 5000));
        CHECK (hiDb < -8.0f && hiDb > -16.0f, "a painted -12 dB turns the hat down (%.1f dB above 5 kHz)", hiDb);
    }

    std::printf ("7. events by hand, and edits that survive detecting again\n");
    {
        auto an = analyze (loop);
        DetectParams p; p.minGapMs = 40.0f;
        auto ev = detect (loop, *an, p);
        const auto m = makeManual (loop, *an, p, 0.25 + 0.125);   // exactly on the first hat
        CHECK (m.manual && std::llabs (m.onset - (int64_t) ((0.375) * SR)) <= 1, "a placed event starts where you clicked");
        std::vector<Event> keep { m }; keep[0].onset += (int64_t) (0.004 * SR); keep[0].start = keep[0].onset;   // nudged off the detected start
        std::vector<int64_t> deleted { truth[4] };
        auto again = detect (loop, *an, p, keep, deleted);
        int near1 = 0; for (const auto& e : again) if (std::llabs (e.onset - (int64_t) (0.375 * SR)) < (int64_t) (0.03 * SR)) ++near1;
        bool gone = true; for (const auto& e : again) if (std::llabs (e.onset - truth[4]) < (int64_t) (0.01 * SR)) gone = false;
        CHECK (near1 == 1, "your event replaces the detected one there (not both)");
        CHECK (gone, "an event you deleted is not found again");
        CHECK (again.size() == ev.size() - 1, "everything else is as before (%zu vs %zu)", again.size(), ev.size());
    }

    std::printf ("7b. where events end\n");
    {
        auto an = analyze (loop);
        DetectParams p; p.minGapMs = 40.0f;
        p.endMode = 1; auto full = detect (loop, *an, p);
        bool joined = true; for (size_t i = 0; i + 1 < full.size(); ++i) joined = joined && full[i].end == full[i + 1].start;
        CHECK (joined, "NEXT: every one-shot runs to the next one");
        p.endMode = 2; auto quiet = detect (loop, *an, p);
        bool inGap = true;
        for (size_t i = 0; i + 1 < quiet.size(); ++i)
        {
            const int64_t span = quiet[i + 1].start - quiet[i].onset;
            inGap = inGap && quiet[i].end <= quiet[i + 1].start && quiet[i].end >= quiet[i + 1].start - span / 10 - 1;
        }
        CHECK (inGap, "QUIET: each cut lies in the last tenth before the next hit");
        p.endMode = 3; auto db = detect (loop, *an, p);
        // the kick (the first event) has fallen 40 dB where it ends; it is cut well before the next hit
        std::vector<float> env; detail::envelope (loop, db[0].onset, db[0].end, 96, env);
        float pk = 0; for (float v : env) pk = std::max (pk, v);
        CHECK (env.back() < 0.012f * pk || db[0].end == full[0].end, "-40 dB: the kick ends where it has fallen 40 dB (%.1f dB)", 20 * std::log10 (env.back() / pk));
        // a recording that begins on a hit
        Audio hit; hit.sr = SR; hit.l.assign ((size_t) SR, 0.0f);
        for (size_t i = 0; i < hit.l.size(); ++i) hit.l[i] = 0.6f * std::exp (-(float) i / 4000.0f) * std::sin (6.2831853f * 180.0f * (float) i / (float) SR);
        Gen g4; addNoise (hit.l, 0.002f, g4, 0.0f);
        auto ah = analyze (hit); auto eh = detect (hit, *ah, DetectParams());
        CHECK (! eh.empty() && eh[0].onset == 0, "a recording that starts on a hit has an event at 0");
    }

    std::printf ("8. export: fades, normalise, separate layers\n");
    {
        auto an = analyze (loop);
        auto ev = detect (loop, *an, DetectParams());
        ExportParams ep; ep.normMode = 1; ep.normDb = -1.0f;
        std::vector<float> l, r;
        renderEvent (loop, *an, LayerState(), ev[0], ep, -1, l, r);
        float pk = 0; for (size_t i = 0; i < l.size(); ++i) pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i])));
        CHECK (std::fabs (20 * std::log10 (pk) + 1.0f) < 0.05f, "normalised to -1 dB (%.2f)", 20 * std::log10 (pk));
        CHECK (std::fabs (l.back()) < 1e-4f && std::fabs (l.front()) < 0.05f, "faded at both ends");
        int onsetIdx = (int) (ev[0].onset - ev[0].start);
        float pre = 0; for (int i = 0; i < onsetIdx; ++i) pre = std::max (pre, std::fabs (l[(size_t) i]));
        CHECK (pre < 0.1f, "the pre-roll is quiet: the cut is before the hit (%.3f)", pre);
    }

    std::printf ("9. edge cases\n");
    {
        Audio empty; empty.sr = SR;
        auto an = analyze (empty);
        CHECK (an != nullptr && detect (empty, *an, DetectParams()).empty(), "an empty file");
        Audio silent; silent.sr = 44100; silent.l.assign (44100 * 3, 0.0f);
        an = analyze (silent);
        CHECK (detect (silent, *an, DetectParams()).empty(), "digital silence");
        Audio one; one.sr = 96000; one.l.assign (96000, 0.0f); one.l[48000] = 0.9f;
        an = analyze (one);
        auto ev = detect (one, *an, DetectParams());
        CHECK (ev.size() == 1 && std::llabs (ev[0].onset - 48000) < 96, "a single click in silence at 96 kHz, to the millisecond (%zu found)", ev.size());
        Audio full; full.sr = 22050; full.l.assign (22050 * 2, 0.0f); for (size_t i = 0; i < full.l.size(); ++i) full.l[i] = (i / 2205) % 2 ? 1.0f : -1.0f;
        an = analyze (full);
        ev = detect (full, *an, DetectParams());
        bool fin = true; for (auto& e : ev) fin = fin && e.end > e.start;
        CHECK (fin, "a full-scale square wave at 22 kHz does not break it (%zu events)", ev.size());
        std::atomic<int> calls { 0 };
        auto cancelled = analyze (loop, [&] (float) { return ++calls < 3; });
        CHECK (cancelled == nullptr, "analysis can be cancelled");
    }

    std::printf ("10. a long recording\n");
    {
        Audio a; a.sr = 44100; a.l.assign ((size_t) (44100 * 600), 0.0f); a.r = a.l;
        Gen g3; g3.rng.seed (3);
        int planted = 0;
        for (double t = 2.0; t < 598.0; t += 1.7 + 0.9 * std::sin (t)) { clank (a.l, (size_t) (t * 44100), 0.3f, 600 + 400 * std::sin (t * 0.3), 14, g3); ++planted; }
        addNoise (a.l, 0.02f, g3); a.r = a.l;
        const auto t0 = std::chrono::steady_clock::now();
        auto an = analyze (a);
        const double ta = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const auto t1 = std::chrono::steady_clock::now();
        auto ev = detect (a, *an, DetectParams());
        std::vector<Group> groups; group (ev, groups, 0.5f);
        const double td = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        std::printf ("     10 min stereo: analysis %.1f s, detection and grouping %.2f s, %zu of %d found, %zu groups; display %d x %d\n", ta, td, ev.size(), planted, groups.size(), an->cols, kRows);
        CHECK (ta < 60.0 && td < 5.0, "fast enough to be usable");
        CHECK (std::abs ((int) ev.size() - planted) <= planted / 50 + 1, "found about every hit (%zu of %d)", ev.size(), planted);
    }

    std::printf ("11. the preview player (what you hear is what is exported)\n");
    {
        auto audio = std::make_shared<Audio> (loop);
        std::shared_ptr<const Analysis> an = analyze (*audio);
        LayerState st; st.layers[Transient].gainDb = -12.0f;
        std::vector<float> ref, refR; render (*audio, *an, st, (int64_t) SR, (int64_t) (SR * 4), ref, refR);
        Preview pv; pv.startWorker();
        pv.play (audio, an, st, (int64_t) SR, (int64_t) (SR * 4));
        std::vector<float> L, R; const int B = 512;
        std::atomic<bool> stop { false }; std::atomic<int> starvedBlocks { 0 }, firstBlock { -1 };
        // the audio thread, at real time speed
        std::thread audioThread ([&]
        {
            std::vector<float> l ((size_t) B), r ((size_t) B);
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; ! stop.load() && b < (int) (3.2 * SR / B); ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                pv.process (l.data(), r.data(), B, SR);
                bool any = false; for (float v : l) any = any || v != 0.0f;
                if (any && firstBlock.load() < 0) firstBlock = b;
                if (pv.waiting() && firstBlock.load() >= 0) ++starvedBlocks;
                L.insert (L.end(), l.begin(), l.end()); R.insert (R.end(), r.begin(), r.end());
                std::this_thread::sleep_until (t0 + std::chrono::microseconds ((int64_t) ((b + 1) * 1.0e6 * B / SR)));
            }
        });
        audioThread.join();
        // the start may wait a block or two for the first chunk: line it up by the first sound
        size_t lead = 0; while (lead < L.size() && L[lead] == 0.0f) ++lead;
        size_t refLead = 0; while (refLead < ref.size() && ref[refLead] == 0.0f) ++refLead;
        double err = 0, pw = 0;
        for (size_t i = 0; i + lead < L.size() && i + refLead < ref.size() && i < (size_t) (2.9 * SR); ++i) { const double d = L[i + lead] - ref[i + refLead]; err += d * d; pw += (double) ref[i + refLead] * ref[i + refLead]; }
        std::printf ("     played 3 s with the transient layer at -12 dB: first sound after %.1f ms\n", firstBlock.load() * 1000.0 * B / SR);
        CHECK (10 * std::log10 (err / pw + 1e-30) < -80.0, "the player plays exactly the rendered sound (%.1f dB)", 10 * std::log10 (err / pw + 1e-30));
        CHECK (starvedBlocks.load() == 0, "once it has started it never runs dry (%d blocks)", starvedBlocks.load());
        CHECK (firstBlock.load() * B < (int) (0.06 * SR), "it starts within 60 ms");
        CHECK (! pv.playing(), "it stops at the end");
        // a clip, and playing at another rate (a 44.1 kHz file on a 48 kHz device)
        std::vector<float> clip ((size_t) 4410, 0.5f);
        pv.playClip (clip, clip, 44100.0);
        std::vector<float> l ((size_t) 4800, 0.0f), r ((size_t) 4800, 0.0f);
        pv.process (l.data(), r.data(), 4800, 48000.0);
        int nonzero = 0; for (float v : l) nonzero += v != 0.0f ? 1 : 0;
        CHECK (std::abs (nonzero - 4800) <= 2, "a 0.1 s clip at 44.1 kHz lasts 0.1 s on a 48 kHz device (%d samples)", nonzero);
        pv.stopPlaying(); pv.collect();
        CHECK (pv.position() < 0, "stopped");
        pv.stopWorker();
    }

    std::printf ("12. one level for every one-shot (ITU-R BS.1770 loudness)\n");
    {
        Biquad sh, hp; kWeighting (48000.0, sh, hp);
        const double t1[] = { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        const double got1[] = { sh.b0, sh.b1, sh.b2, sh.a1, sh.a2 };
        double worst = 0; for (int i = 0; i < 5; ++i) worst = std::max (worst, std::fabs (got1[i] - t1[i]));
        worst = std::max (worst, std::max (std::fabs (hp.a1 - -1.99004745483398), std::fabs (hp.a2 - 0.99007225036621)));
        CHECK (worst < 1.0e-6, "the K-weighting at 48 kHz is the Recommendation's table (largest difference %.1e)", worst);
        // the Recommendation's own check: a 0 dBFS 1 kHz sine in one channel reads -3.01 (in both, 0.0), at any rate
        for (double sr : { 48000.0, 44100.0, 96000.0 })
        {
            std::vector<float> l ((size_t) (3 * sr)), z (l.size(), 0.0f);
            for (size_t i = 0; i < l.size(); ++i) l[i] = (float) std::sin (6.283185307179586 * 997.0 * (double) i / sr);
            const float one = measureLevel (l, z, sr).lufs, both = measureLevel (l, l, sr).lufs;
            CHECK (std::fabs (one + 3.01f) < 0.1f && std::fabs (both) < 0.1f, "a 0 dBFS 997 Hz sine at %.1f kHz: %.2f LUFS in one channel, %.2f in both", sr / 1000, one, both);
        }
        // very different one-shots, at very different levels
        Gen g4; g4.rng.seed (21);
        std::vector<std::vector<float>> shots;
        const char* names[] = { "5 ms click", "clank", "kick", "hat", "snare", "1 s tone", "noise burst" };
        { std::vector<float> x ((size_t) (0.05 * SR), 0.0f); for (size_t i = 0; i < (size_t) (0.005 * SR); ++i) x[i] = 0.9f * g4.white() * 0.3f; shots.push_back (x); }
        { std::vector<float> x ((size_t) (0.9 * SR), 0.0f); clank (x, 10, 0.5f, 900.0, 12.0, g4); shots.push_back (x); }
        { std::vector<float> x ((size_t) (0.4 * SR), 0.0f); kick (x, 10, 0.9f); shots.push_back (x); }
        { std::vector<float> x ((size_t) (0.08 * SR), 0.0f); hat (x, 10, 0.3f, g4); shots.push_back (x); }
        { std::vector<float> x ((size_t) (0.25 * SR), 0.0f); snare (x, 10, 0.2f, g4); shots.push_back (x); }
        { std::vector<float> x ((size_t) SR); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.05f * (float) std::sin (6.283185307 * 330.0 * (double) i / SR); shots.push_back (x); }
        { std::vector<float> x ((size_t) (0.2 * SR)); for (auto& v : x) v = 0.1f * g4.white(); shots.push_back (x); }
        ExportParams ep; ep.normMode = 2; ep.loudDb = -16.0f; ep.normDb = -1.0f;
        std::vector<float> got; int held = 0; float lo = 1e9f, hi = -1e9f, pkMax = -1e9f;
        std::printf ("     as recorded:");
        for (size_t i = 0; i < shots.size(); ++i) { const Level lv = measureLevel (shots[i], shots[i], SR); std::printf (" %s %.1f |", names[i], lv.lufs); }
        std::printf ("\n");
        std::vector<float> after;
        for (auto& x : shots)
        {
            auto y = x, yr = x;
            const Level lv = measureLevel (y, yr, SR); bool h = false;
            applyGainDb (y, yr, normGainDb (lv, lv.peakDb, ep, &h)); held += h ? 1 : 0;
            const Level nv = measureLevel (y, yr, SR); after.push_back (nv.lufs); pkMax = std::max (pkMax, nv.peakDb);
            lo = std::min (lo, nv.lufs); hi = std::max (hi, nv.lufs);
            x = y;
        }
        std::printf ("     at -16 LUFS:"); for (size_t i = 0; i < shots.size(); ++i) std::printf (" %s %.1f |", names[i], after[i]); std::printf ("  (%d held by the -1 dBFS ceiling)\n", held);
        CHECK (pkMax <= -0.99f, "no peak passes the ceiling (highest %.2f dBFS)", pkMax);
        // matched: everyone comes down to the quietest (as the export does when some were held back)
        for (size_t i = 0; i < shots.size(); ++i) { auto r2 = shots[i]; applyGainDb (shots[i], r2, lo - after[i]); }
        float lo2 = 1e9f, hi2 = -1e9f; for (auto& x : shots) { const float v = measureLevel (x, x, SR).lufs; lo2 = std::min (lo2, v); hi2 = std::max (hi2, v); }
        CHECK (hi2 - lo2 < 0.1f, "matched: all seven within %.2f dB of each other (at %.1f LUFS)", hi2 - lo2, lo2);
        // peak mode
        ExportParams pp; pp.normMode = 1; pp.normDb = -1.0f; float pmin = 1e9f, pmax = -1e9f;
        for (auto x : shots) { auto r2 = x; const Level lv = measureLevel (x, r2, SR); applyGainDb (x, r2, normGainDb (lv, lv.peakDb, pp)); const float v = peakDbOf (x, r2); pmin = std::min (pmin, v); pmax = std::max (pmax, v); }
        CHECK (pmax - pmin < 0.01f && std::fabs (pmax + 1.0f) < 0.01f, "peak mode: every peak at -1 dBFS (%.2f .. %.2f)", pmin, pmax);
        // through the renderer: a detected event comes out at the target
        Audio a; a.sr = SR; a.l.assign ((size_t) (SR * 3), 0.0f); clank (a.l, (size_t) SR, 0.05f, 800.0, 15.0, g4);
        auto an = analyze (a); auto ev = detect (a, *an, DetectParams());
        if (! ev.empty())
        {
            std::vector<float> l, r; ExportParams e2; e2.normMode = 2; e2.loudDb = -20.0f;
            renderEvent (a, *an, LayerState(), ev[0], e2, -1, l, r);
            const Level lv = measureLevel (l, r, SR);
            CHECK (std::fabs (lv.lufs + 20.0f) < 0.1f && lv.peakDb <= -0.99f, "an exported event at -20 LUFS: %.2f LUFS, peak %.1f dBFS", lv.lufs, lv.peakDb);
        }
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
