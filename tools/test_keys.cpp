// Batchcut 3.0 - KEYS without any framework: an instrument built from pitched sounds (zones by pitch,
// velocity layers, round robins), in tune across the keyboard, loops and holds that sustain without a
// click, the release, the resonator for unpitched sounds, chops on keys, mono with glide.
#include "../Source/core/Keys.h"
#include "../Source/neobeat/gen/Scale.h"
#include <cstdlib>
#include "Portable.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bc;

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static const double kSr = 48000.0;
// a tuned, slowly dying tone (a bell-ish horn: a few harmonics), with a click of an attack
static std::shared_ptr<PoolItem> tone (const std::string& name, nbk::Learner& learner, double hz, double seconds, float amp, float bright, uint32_t seed)
{
    auto it = std::make_shared<PoolItem>();
    it->name = name; it->source = "horns"; it->sr = kSr;
    const int n = (int) (seconds * kSr);
    it->l.assign ((size_t) n, 0.0f);
    PortableRandom rng (seed);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr, env = std::min (1.0, t / 0.004) * std::exp (-t / 0.9);
        double x = 0.0; for (int h = 1; h <= 4; ++h) x += std::sin (6.283185307179586 * hz * h * t) * std::pow ((double) bright, h - 1) / h;
        it->l[(size_t) i] = amp * (float) (env * x * 0.6 + (t < 0.003 ? 0.05 * rng.uni (-1.0, 1.0) : 0.0));
    }
    it->r = it->l;
    analyseItem (*it, learner);
    return it;
}
static std::shared_ptr<PoolItem> noise (const std::string& name, nbk::Learner& learner, double seconds, uint32_t seed)
{
    auto it = std::make_shared<PoolItem>(); it->name = name; it->source = "street"; it->sr = kSr;
    const int n = (int) (seconds * kSr); it->l.assign ((size_t) n, 0.0f); PortableRandom rng (seed);
    for (int i = 0; i < n; ++i) { const double t = (double) i / kSr; it->l[(size_t) i] = (float) (0.5 * std::exp (-t / 0.05) * rng.uni (-1.0, 1.0)); }
    it->r = it->l; analyseItem (*it, learner); return it;
}

// what the player makes of some keys
struct Run { std::vector<float> l, r; };
static Run play (const Instrument& ins, std::vector<KeysNote> notes, int samples)
{
    KeysPlayer p; p.prepare (kSr); p.level.store (1.0f);
    p.setInstrument (std::make_shared<const Instrument> (ins));
    Run out; out.l.assign ((size_t) samples, 0.0f); out.r.assign ((size_t) samples, 0.0f);
    for (int b = 0; b < samples; b += 512)
    {
        std::vector<KeysNote> here;
        for (auto nt : notes) if (nt.offset >= b && nt.offset < b + 512) { nt.offset -= b; here.push_back (nt); }
        p.process (out.l.data() + b, out.r.data() + b, std::min (512, samples - b), here.data(), (int) here.size());
    }
    return out;
}
static float pitchHz (const std::vector<float>& x, int a, int b)
{
    float strength = 0.0f;
    const float rough = nbk::detail::pitchOf (x, a, b, (float) kSr, 30.0f, 3000.0f, strength);
    return (float) keysdetail::preciseHz (x.data() + a, b - a, kSr, rough);
}
// a kick's boom: a sine falling from 150 to 60 Hz, with a click
static std::shared_ptr<PoolItem> boom (const std::string& name, nbk::Learner& learner)
{
    auto it = std::make_shared<PoolItem>(); it->name = name; it->source = "kicks"; it->sr = kSr;
    const int n = (int) (0.4 * kSr); it->l.assign ((size_t) n, 0.0f); double ph = 0.0;
    for (int i = 0; i < n; ++i) { const double t = (double) i / kSr; ph += 6.283185307179586 * (60.0 + 90.0 * std::exp (-t * 30.0)) / kSr; it->l[(size_t) i] = (float) (0.8 * std::exp (-t * 12.0) * std::sin (ph) * std::min (1.0, t * 3000.0)); }
    it->r = it->l; analyseItem (*it, learner); return it;
}
static float rms (const std::vector<float>& x, int a, int b) { double s = 0.0; for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; return (float) std::sqrt (s / std::max (1, b - a)); }
static float cents (float hz, float want) { return 1200.0f * std::log2 (std::max (1.0f, hz) / want); }

int main()
{
    nbk::Learner learner;
    std::vector<std::shared_ptr<const PoolItem>> horns;
    const double notes[] = { 48.0, 55.0, 60.0, 67.0, 72.0 };
    for (int i = 0; i < 5; ++i) horns.push_back (tone ("horn" + std::to_string (i), learner, 440.0 * std::exp2 ((notes[i] - 69.0) / 12.0), 1.2, 0.5f, 0.5f, 10u + (uint32_t) i));
    // C3 again: a softer one (a velocity layer) and a second loud one with another colour (a round robin)
    horns.push_back (tone ("horn soft", learner, 261.6256, 1.2, 0.12f, 0.3f, 20u));
    horns.push_back (tone ("horn again", learner, 261.6256, 1.2, 0.5f, 0.8f, 21u));
    bool allPitched = true; for (const auto& h : horns) allPitched = allPitched && keysdetail::pitched (*h);
    CHECK (allPitched, "seven horns, every one heard with a pitch");

    std::printf ("1. building the instrument\n");
    Instrument ins; ins.releaseMs = 200.0f;
    buildInstrument (ins, horns);
    int c3 = 0; bool roots = true;
    for (const auto& z : ins.zones) { const float want = (float) std::lround (z.root); roots = roots && std::fabs (z.root - want) < 0.1f; c3 += std::lround (z.root) == 60 ? 1 : 0; }
    CHECK (ins.zones.size() == 7 && roots && c3 == 3, "7 zones, each at its own pitch (within 10 cents); three at C3");
    bool covered = true;
    for (int k = 0; k < 128; ++k) for (int v : { 10, 120 }) { int n = 0; for (const auto& z : ins.zones) n += (k >= z.lowKey && k <= z.highKey && v >= z.lowVel && v <= z.highVel && z.rr == 0) ? 1 : 0; covered = covered && n == 1; }
    CHECK (covered, "every key at every velocity has exactly one zone to play (round robins aside)");
    int layers = 0, rrs = 0; for (const auto& z : ins.zones) if (std::lround (z.root) == 60) { layers += z.lowVel == 1 ? 1 : 0; rrs = std::max (rrs, z.rrCount); }
    CHECK (rrs == 2, "C3: a soft layer, and the two loud ones as round robins (%d)", rrs);
    const auto* g2 = &ins.zones[1];
    CHECK (g2->lowKey <= 55 && g2->highKey >= 55 && g2->lowKey > 48 && g2->highKey < 60, "G2's zone covers the keys nearest it (%d to %d)", g2->lowKey, g2->highKey);
    bool loops = true; for (const auto& z : ins.zones) { const double period = kSr / keysdetail::hzOf (z.root); const double k = (z.loopEnd - z.loopStart) / period; loops = loops && z.loopStart > 0 && std::fabs (k - std::round (k)) < 0.02; }
    CHECK (loops, "every zone has a loop a whole number of periods long");

    std::printf ("2. in tune across the keys\n");
    float worst = 0.0f; int tested = 0;
    for (int key = 36; key <= 96; key += 5)
    {
        const auto out = play (ins, { { 0, key, 0.9f, true } }, 24000);
        const float hz = pitchHz (out.l, 4800, 14400), want = keysdetail::hzOf ((float) key);
        worst = std::max (worst, std::fabs (cents (hz, want))); ++tested;
    }
    CHECK (worst < 8.0f, "%d keys from C1 to C6: every one within %.1f cents of its note", tested, worst);

    std::printf ("3. velocity layers and round robins\n");
    {
        const auto soft = play (ins, { { 0, 60, 0.15f, true } }, 9600), loud = play (ins, { { 0, 60, 0.95f, true } }, 9600);
        // the soft layer is its own sound (darker), not the loud one turned down
        const float hs = rms (soft.l, 2400, 9600), hl = rms (loud.l, 2400, 9600);
        CHECK (hl > hs * 2.0f, "a soft key is quieter (%.3f against %.3f)", hs, hl);
        const auto rr = play (ins, { { 0, 60, 0.95f, true }, { 4800, 60, 0.0f, false }, { 9600, 60, 0.95f, true }, { 14400, 60, 0.0f, false }, { 19200, 60, 0.95f, true } }, 24000);
        double d12 = 0.0, d13 = 0.0;
        for (int i = 0; i < 4000; ++i) { d12 += std::fabs (rr.l[(size_t) (200 + i)] - rr.l[(size_t) (9800 + i)]); d13 += std::fabs (rr.l[(size_t) (200 + i)] - rr.l[(size_t) (19400 + i)]); }
        CHECK (d12 > 10.0 * d13 + 1e-3, "the same key three times: alike sounds take turns (1st and 2nd differ, 1st and 3rd the same)");
    }

    std::printf ("4. sustain and release\n");
    {
        Instrument rec = ins; rec.sustain = KeysSustain::AsRecorded;
        Instrument lp = ins; lp.sustain = KeysSustain::Loop;
        Instrument hd = ins; hd.sustain = KeysSustain::Hold;
        const int len = (int) (3.0 * kSr);
        const auto a = play (rec, { { 0, 64, 0.9f, true } }, len), b = play (lp, { { 0, 64, 0.9f, true } }, len), c = play (hd, { { 0, 64, 0.9f, true } }, len);
        const int lateA = len - 24000, lateB = len;
        const float ra = rms (a.l, lateA, lateB), rb = rms (b.l, lateA, lateB), rc = rms (c.l, lateA, lateB), early = rms (b.l, 2400, 26400);
        CHECK (ra < 0.01f * early && rb > 0.15f * early && rc > 0.15f * early, "held 3 s: as recorded it has died (%.4f); Loop (%.3f) and Hold (%.3f) still sound (at first %.3f)", ra, rb, rc, early);
        auto maxJump = [] (const std::vector<float>& x, int from, int to) { float m = 0.0f; for (int i = from + 1; i < to; ++i) m = std::max (m, std::fabs (x[(size_t) i] - x[(size_t) i - 1])); return m; };
        auto peak = [] (const std::vector<float>& x, int from, int to) { float m = 0.0f; for (int i = from; i < to; ++i) m = std::max (m, std::fabs (x[(size_t) i])); return m; };
        const float hzMax = keysdetail::hzOf (64.0f) * 4.0f;     // the highest harmonic
        const float limitB = peak (b.l, 4800, len) * 6.2831853f * hzMax / (float) kSr * 1.3f, limitC = peak (c.l, 4800, len) * 6.2831853f * hzMax / (float) kSr * 1.3f;
        CHECK (maxJump (b.l, 4800, len) < limitB && maxJump (c.l, 4800, len) < limitC, "no clicks: the loop's seam and the hold's grains are smooth");
        const float lpHz = pitchHz (b.l, len - 9600, len), hdHz = pitchHz (c.l, len - 9600, len), want = keysdetail::hzOf (64.0f);
        CHECK (std::fabs (cents (lpHz, want)) < 10.0f && std::fabs (cents (hdHz, want)) < 10.0f, "and still in tune after 3 s (%.1f and %.1f cents)", cents (lpHz, want), cents (hdHz, want));
        const auto rel = play (lp, { { 0, 64, 0.9f, true }, { 48000, 64, 0.0f, false } }, (int) (1.6 * kSr));
        const float during = rms (rel.l, 24000, 48000), after = rms (rel.l, 48000 + (int) (0.3 * kSr), 48000 + (int) (0.6 * kSr));
        CHECK (after < during * 0.002f, "let go: the release (200 ms) fades it out (%.6f after, %.3f before)", after, during);
    }

    std::printf ("5. the resonator, chops, mono\n");
    {
        Instrument res; res.mode = KeysMode::Resonator; res.resonance = 0.8f; res.brightness = 0.6f;
        buildInstrument (res, { noise ("drill", learner, 0.3, 5u), noise ("screech", learner, 0.2, 6u) });
        const auto out = play (res, { { 0, 57, 0.9f, true } }, 24000);
        const float hz = pitchHz (out.l, 4800, 19200);
        CHECK (res.zones.size() == 2 && std::fabs (cents (hz, 220.0f)) < 15.0f, "a noise burst struck into the resonator rings at the key's pitch: A2 gives %.1f Hz", hz);
        // in tune however dark it is set, up high too (the damping filter's lag is allowed for)
        // (struck by a click, so what is heard is the ringing alone, not a noise pushing it about)
        Instrument clicked; clicked.mode = KeysMode::Resonator; clicked.resonance = 0.8f;
        {
            auto c = std::make_shared<PoolItem>(); c->name = "click"; c->source = "street"; c->sr = kSr; c->l.assign (480, 0.0f);
            PortableRandom rng (77u); for (int i = 0; i < 48; ++i) c->l[(size_t) i] = (float) (0.8 * std::exp (-i / 8.0) * rng.uni (-1.0, 1.0));
            c->r = c->l; analyseItem (*c, learner); buildInstrument (clicked, { c });
        }
        float worst = 0.0f; int worstKey = 0; float worstBr = 0.0f;
        for (const float br : { 0.0f, 0.5f, 1.0f })
            for (const int key : { 45, 57, 69, 81, 93 })
            {
                Instrument r2 = clicked; r2.brightness = br;
                const auto o = play (r2, { { 0, key, 0.9f, true } }, 24000);
                const float c = std::fabs (cents (pitchHz (o.l, 4800, 19200), keysdetail::hzOf ((float) key)));
                if (getenv ("KEYS_VERBOSE")) std::printf ("    key %d bright %.1f: %.2f cents, rms %.4f\n", key, br, c, rms (o.l, 4800, 19200));
                if (c > worst) { worst = c; worstKey = key; worstBr = br; }
            }
        CHECK (worst < 5.0f, "the resonator is in tune from A1 to A5, dark or bright (worst %.1f cents, %s at brightness %.1f)", worst, gen::noteName (worstKey).c_str(), worstBr);
        // a kick struck into it: its boom is kept out, the note is heard
        Instrument rk; rk.mode = KeysMode::Resonator; rk.resonance = 0.7f; rk.brightness = 0.5f;
        buildInstrument (rk, { boom ("kick", learner) });
        const auto ok = play (rk, { { 0, 57, 0.9f, true } }, 24000);
        const float kickHz = pitchHz (ok.l, 4800, 19200);
        CHECK (rk.zones.size() == 1 && std::fabs (cents (kickHz, 220.0f)) < 15.0f, "a kick struck into the resonator rings at the key, not at its boom: A2 gives %.1f Hz", kickHz);
        // mono: a second key glides the ringing note over
        Instrument rm = res; rm.mono = true; rm.glideMs = 30.0f;
        const auto og = play (rm, { { 0, 57, 0.9f, true }, { 9600, 64, 0.9f, true } }, 36000);
        const float before = pitchHz (og.l, 4800, 9600), after = pitchHz (og.l, 24000, 36000);
        CHECK (std::fabs (cents (before, 220.0f)) < 15.0f && std::fabs (cents (after, keysdetail::hzOf (64.0f))) < 15.0f, "mono: the resonator glides from A2 to E3 (%.1f Hz, then %.1f Hz)", before, after);
        Instrument ch; ch.mode = KeysMode::Chops;
        buildInstrument (ch, horns);
        const auto c0 = play (ch, { { 0, 49, 0.9f, true } }, 24000);
        const float own = (float) horns[1]->f.pitchHz;
        CHECK (ch.zones.size() == 7 && ch.zones[1].lowKey == 49 && std::fabs (cents (pitchHz (c0.l, 4800, 19200), own)) < 10.0f, "chops: C#2 plays the second sound as it is (%.0f Hz)", own);
        Instrument mono = ins; mono.mono = true; mono.glideMs = 60.0f; mono.sustain = KeysSustain::Loop;
        KeysPlayer p; p.prepare (kSr); p.level.store (1.0f); p.setInstrument (std::make_shared<const Instrument> (mono));
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        KeysNote a { 0, 60, 0.9f, true }, b { 0, 72, 0.9f, true };
        p.process (l.data(), r.data(), 512, &a, 1);
        for (int k = 1; k < 20; ++k) p.process (l.data() + k * 512, r.data() + k * 512, 512, nullptr, 0);
        p.process (l.data() + 20 * 512, r.data() + 20 * 512, 512, &b, 1);
        for (int k = 21; k < 93; ++k) p.process (l.data() + k * 512, r.data() + k * 512, 512, nullptr, 0);
        const float mhz = pitchHz (l, 40000, 47000), mwant = keysdetail::hzOf (72.0f);
        CHECK (p.active.load() == 1 && std::fabs (cents (mhz, mwant)) < 12.0f, "mono: one voice, gliding from C3 up to C4 (%.1f cents off at the end)", cents (mhz, mwant));
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
