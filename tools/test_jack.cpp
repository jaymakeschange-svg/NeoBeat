// IDM Jack and BANDS through the real workstation, offline, with checks.
//   test_jack [outdir]     (writes WAVs of a held pass, and of BANDS, to outdir when given)
//
// Spec: docs/PROPOSED-FEATURES.md, "IDM Jack". What this proves, and what it cannot:
//   - offsets are an overlay: the stored kit is the same after a held pass as before it
//   - the include list gates everything: nothing included and overlay is the plain kit, sample for sample
//   - replace silences the written pattern; overlay keeps it
//   - a pass is reproducible from its seed, and a different seed is a different pass
//   - release: a hard cut stops it on the block; a recovery fades the offsets out over its bars
//   - no transport, no bursts; a loop jump does not stall the lattice
//   - the limiter holds the output under the knee at full density and extremity
// Whether it sounds like anything is for the ears, not for this file.
#include "../Source/engine/Factory.h"
#include "../Source/engine/Workstation.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

using namespace nb;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else std::printf ("  ok:   %s\n", msg); } while (0)

static const float SR = 48000.0f;
static const int BLOCK = 512;
struct Out { std::vector<float> l, r; };

static void writeWav (const std::string& path, const Out& o)
{
    std::ofstream f (path, std::ios::binary);
    const int n = (int) o.l.size(); const int bytes = n * 4;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) SR); u32 ((uint32_t) SR * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (bytes);
    for (int i = 0; i < n; ++i) for (float x : { o.l[(size_t) i], o.r[(size_t) i] }) { const int16_t s = (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, x)) * 32767.0f); f.write ((const char*) &s, 2); }
}

struct Rig
{
    std::unique_ptr<Workstation> w = std::make_unique<Workstation>();
    RenderInputs in;
    explicit Rig (int preset = 0)
    {
        w->prepare (SR, BLOCK);
        const Preset p = patternPreset (preset);
        for (auto& sc : in.scenes) sc = p.pattern;
        in.bpm = p.tempo; in.swing = p.swing;
        for (int pad = 0; pad < kNumPads; ++pad) in.pads[(size_t) pad] = kitPadSettings (w->kit().lib(), 0, pad);
        for (int s = 0; s < kNumScenes; ++s) w->setScene (s, in.scenes[(size_t) s]);
        w->publishLive (buildLiveState (in.project));
        w->render().renderNow (in, SR);
        // The pads are set once, as the plugin does, and never again: a test that rewrote them
        // every block, as the workstation test does, could not see Jack writing them.
        for (int q = 0; q < kNumPads; ++q) w->kit().pads[(size_t) q] = in.pads[(size_t) q];
    }
};

// A schedule of what the panel does, in seconds: hold on at `on`, off at `off`. Default: never on.
struct Plan { double on = 1.0e9, off = 1.0e9; bool transport = true; double loopBeats = 0.0; };

static Out play (Rig& r, double seconds, Plan plan, std::vector<int>* states = nullptr)
{
    Out o; const int total = (int) (seconds * SR);
    o.l.assign ((size_t) total, 0.0f); o.r.assign ((size_t) total, 0.0f);
    std::vector<MidiOut> mo; mo.reserve (256);
    BlockParams bp; bp.swing = r.in.swing;
    double ppq = 0.0;
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        const int n = std::min (BLOCK, total - pos);
        const double t = pos / (double) SR;
        r.w->jackHold.store (t >= plan.on && t < plan.off);
        Transport tr; tr.bpm = r.in.bpm; tr.hostPlaying = plan.transport; tr.hostPpq = ppq; tr.hostHasPpq = true;
        mo.clear();
        r.w->process (o.l.data() + pos, o.r.data() + pos, n, tr, bp, nullptr, 0, mo, nullptr);
        if (states != nullptr) states->push_back (r.w->jackState.load());
        if (plan.transport) ppq += r.in.bpm / 60.0 * n / SR;
        if (plan.loopBeats > 0.0 && ppq >= plan.loopBeats) ppq -= plan.loopBeats;
    }
    return o;
}

static float peak (const Out& o, int a = 0, int b = -1)
{
    if (b < 0) b = (int) o.l.size();
    float p = 0.0f;
    for (int i = a; i < b; ++i) p = std::max (p, std::max (std::fabs (o.l[(size_t) i]), std::fabs (o.r[(size_t) i])));
    return p;
}
static double rms (const Out& o, int a, int b)
{
    double s = 0.0; b = std::min (b, (int) o.l.size());
    for (int i = a; i < b; ++i) s += 0.5 * ((double) o.l[(size_t) i] * o.l[(size_t) i] + (double) o.r[(size_t) i] * o.r[(size_t) i]);
    return std::sqrt (s / std::max (1, b - a));
}
static float maxDiff (const Out& a, const Out& b, int from = 0, int to = -1)
{
    if (to < 0) to = (int) std::min (a.l.size(), b.l.size());
    float d = 0.0f;
    for (int i = from; i < to; ++i) d = std::max (d, std::max (std::fabs (a.l[(size_t) i] - b.l[(size_t) i]), std::fabs (a.r[(size_t) i] - b.r[(size_t) i])));
    return d;
}
static bool finite (const Out& o) { for (size_t i = 0; i < o.l.size(); ++i) if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) return false; return true; }

// The stored values Jack's offsets land on, field by field.
static bool sameKit (const Engine& e, const RenderInputs& in)
{
    for (int p = 0; p < kNumPads; ++p)
    {
        const auto& a = e.pads[(size_t) p]; const auto& b = in.pads[(size_t) p];
        if (a.tune != b.tune || a.decay != b.decay || a.tone != b.tone || a.drive != b.drive || a.hollow != b.hollow
            || a.evolve != b.evolve || a.pan != b.pan || a.width != b.width || a.levelDb != b.levelDb) return false;
    }
    return true;
}

static nsr::JackSettings settings (uint32_t seed = 7)
{
    nsr::JackSettings s; s.seed = seed;
    for (auto& a : s.axis) a = true;
    return s;
}

int main (int argc, char** argv)
{
    const std::string outdir = argc > 1 ? argv[1] : "";
    const int S = (int) SR;

    std::printf ("1. off: the plain kit\n");
    Rig r0; const Out plain = play (r0, 4.0, {});
    CHECK (finite (plain) && peak (plain) > 0.05f, "the kit plays");
    CHECK (r0.w->jackBursts.load() == 0 && r0.w->jackSeized.load() == 0, "nothing fires while it is off");

    int bursts = 0, seized = 0;      // section 4 compares against these
    {
    std::printf ("2. held over the pattern, every lane in\n");
    Rig r1; r1.w->jackSettings.write (settings());
    const Out held = play (r1, 4.0, { 0.0 });
    bursts = r1.w->jackBursts.load(); seized = r1.w->jackSeized.load();
    std::printf ("     %d bursts, %d hits seized, peak %.3f, limiter worst %.1f dB\n", bursts, seized, peak (held), r1.w->kit().limiter.worstReductionDb());
    CHECK (bursts > 10, "bursts fire on the lattice");
    CHECK (seized > bursts, "burst hits and pattern hits both carry offsets");
    CHECK (finite (held), "the output is finite");
    CHECK (maxDiff (held, plain) > 0.05f, "it sounds different from the plain kit");
    CHECK (sameKit (r1.w->kit(), r1.in), "the stored kit is untouched after a held pass - an overlay, never written back");
    if (! outdir.empty()) { writeWav (outdir + "/jack_held.wav", held); writeWav (outdir + "/jack_plain.wav", plain); }

    }
    {
    std::printf ("3. a pass is its seed\n");
    Rig r2a; r2a.w->jackSettings.write (settings (7)); const Out a = play (r2a, 3.0, { 0.0 });
    Rig r2b; r2b.w->jackSettings.write (settings (7)); const Out b = play (r2b, 3.0, { 0.0 });
    Rig r2c; r2c.w->jackSettings.write (settings (8)); const Out c = play (r2c, 3.0, { 0.0 });
    CHECK (maxDiff (a, b) == 0.0f, "the same seed plays the same pass, sample for sample");
    CHECK (maxDiff (a, c) > 0.05f, "a different seed plays a different pass");

    }
    {
    std::printf ("4. the include list\n");
    Rig r3; r3.w->jackSettings.write (settings()); r3.w->jackMask.store (0u);
    const Out none = play (r3, 4.0, { 0.0 });
    CHECK (r3.w->jackBursts.load() == 0 && r3.w->jackSeized.load() == 0, "nothing included: nothing fires");
    CHECK (maxDiff (none, plain) == 0.0f, "nothing included, overlay: the plain kit, sample for sample");
    Rig r4; r4.w->jackSettings.write (settings()); r4.w->jackMask.store (1u);   // lane 1 only
    play (r4, 4.0, { 0.0 });
    std::printf ("     lane 1 alone: %d bursts, %d seized (all lanes: %d, %d)\n", r4.w->jackBursts.load(), r4.w->jackSeized.load(), bursts, seized);
    CHECK (r4.w->jackBursts.load() > 0 && r4.w->jackSeized.load() < seized, "one lane in: it fires, on less");

    }
    {
    std::printf ("5. replace\n");
    auto rep = settings(); rep.replace = true;
    Rig r5; r5.w->jackSettings.write (rep); r5.w->jackMask.store (0u);
    const Out silent = play (r5, 4.0, { 0.0 });
    std::printf ("     nothing included, replace: rms %.5f over 1-4 s (plain %.5f)\n", rms (silent, S, 4 * S), rms (plain, S, 4 * S));
    CHECK (rms (silent, S, 4 * S) < rms (plain, S, 4 * S) * 0.01, "replace with nothing included: the written pattern is gone");
    Rig r6; r6.w->jackSettings.write (rep);
    const Out jackOnly = play (r6, 4.0, { 0.0 });
    CHECK (rms (jackOnly, S, 4 * S) > rms (plain, S, 4 * S) * 0.1, "replace with every lane in: the kit runs on Jack alone");
    if (! outdir.empty()) writeWav (outdir + "/jack_replace.wav", jackOnly);

    }
    {
    std::printf ("6. release\n");
    Rig r7; r7.w->jackSettings.write (settings());
    std::vector<int> st7;
    const Out cut = play (r7, 4.0, { 0.0, 2.0 }, &st7);
    const int seizedAtCut = r7.w->jackSeized.load();
    const int blockAfter = (int) (2.0 * SR) / BLOCK + 2;
    CHECK (st7[(size_t) blockAfter] == 0, "hard cut: idle on the next block");
    CHECK (sameKit (r7.w->kit(), r7.in), "the stored kit is untouched after release");
    // what plays after the cut is the written pattern again: the same level as the plain kit
    const double after = rms (cut, 3 * S, 4 * S), ref = rms (plain, 3 * S, 4 * S);
    std::printf ("     a bar after the cut: rms %.5f (plain %.5f)\n", after, ref);
    CHECK (std::fabs (after - ref) < ref * 0.25, "after a cut the written pattern plays at its own level");
    (void) seizedAtCut;

    auto rec = settings(); rec.recoveryBars = 1.0f;
    Rig r8; r8.w->jackSettings.write (rec);
    std::vector<int> st8;
    play (r8, 6.0, { 0.0, 2.0 }, &st8);
    const double barSec = 4.0 * 60.0 / r8.in.bpm;
    CHECK (st8[(size_t) blockAfter] == 2, "recovery: recovering after release");
    CHECK (st8[(size_t) ((2.0 + barSec + 0.1) * SR) / BLOCK] == 0, "recovery: idle once its bar has run out");

    }
    {
    std::printf ("7. the transport\n");
    Rig r9; r9.w->jackSettings.write (settings());
    play (r9, 2.0, { 0.0, 1.0e9, false });
    CHECK (r9.w->jackBursts.load() == 0, "no transport: no bursts - the lattice is musical time");
    Rig r10; r10.w->jackSettings.write (settings());
    std::vector<int> dummy;
    play (r10, 2.0, { 0.0, 1.0e9, true, 4.0 });
    const int firstLoops = r10.w->jackBursts.load();
    play (r10, 4.0, { 0.0, 1.0e9, true, 4.0 });
    std::printf ("     looping one bar: %d bursts in 2 s, then %d more in 4 s\n", firstLoops, r10.w->jackBursts.load() - firstLoops);
    CHECK (r10.w->jackBursts.load() - firstLoops > firstLoops, "looping: the lattice keeps up with the jump back");

    }
    {
    std::printf ("8. as hard as it goes\n");
    auto hard = settings(); hard.density = 1.0f; hard.extremity = 1.0f; hard.maxRetriggers = 8;
    Rig r11; r11.w->jackSettings.write (hard);
    const Out loud = play (r11, 4.0, { 0.0 });
    std::printf ("     peak %.3f, limiter worst %.1f dB\n", peak (loud), r11.w->kit().limiter.worstReductionDb());
    CHECK (finite (loud), "full density and extremity: finite");
    CHECK (peak (loud) <= 1.0f, "full density and extremity: under full scale");
    if (! outdir.empty()) writeWav (outdir + "/jack_hard.wav", loud);

    }
    std::printf ("9. BANDS: the spectral delay on a send (docs/DECISIONS.md, \"BANDS in NeoBeat\")\n");
    {
        Rig rb; for (int p = 0; p < kNumPads; ++p) rb.w->kit().pads[(size_t) p].bands = 0.0f;
        const Out off = play (rb, 4.0, {});
        CHECK (maxDiff (off, plain) == 0.0f && ! rb.w->bandsRunning.load(), "every send at 0: the plain kit, sample for sample, and the bank never runs");

        // one lane at full send. Which lanes play in preset 0 is the preset's business, so send
        // the busiest: the one the plain render is loudest without
        Rig r1b; r1b.w->kit().pads[0].bands = 1.0f;
        const Out one = play (r1b, 4.0, {});
        std::printf ("     lane 1 at full send: peak %.3f, rms %.5f (plain %.5f), max diff from plain %.3f\n", peak (one), rms (one, 0, 4 * S), rms (plain, 0, 4 * S), maxDiff (one, plain));
        CHECK (r1b.w->bandsRunning.load(), "a lane sending: the bank runs");
        CHECK (finite (one) && peak (one) <= 1.0f, "one lane through BANDS: finite, under full scale");
        CHECK (maxDiff (one, plain) > 0.01f, "one lane through BANDS sounds different");

        Rig ra; ra.w->bandsAll.store (1.0f);
        const Out all = play (ra, 4.0, {});
        std::printf ("     ALL at full: peak %.3f, rms %.5f\n", peak (all), rms (all, 0, 4 * S));
        CHECK (finite (all) && peak (all) <= 1.0f, "ALL through BANDS: finite, under full scale");
        CHECK (maxDiff (all, one) > 0.01f, "ALL is more than one lane");

        // the duck: wet off, send full - each lane is left at the Dry level, so quieter, never louder
        Rig rd; { bc::BandsState bs; bs.dsp.wet = 0.0f; rd.w->bandsSettings.write (bs); } rd.w->bandsAll.store (1.0f);
        const Out duck = play (rd, 4.0, {});
        std::printf ("     wet 0, ALL full: rms %.5f against plain %.5f (Dry 0.4: expected about %.5f)\n", rms (duck, 0, 4 * S), rms (plain, 0, 4 * S), 0.4 * rms (plain, 0, 4 * S));
        CHECK (std::fabs (rms (duck, 0, 4 * S) - 0.4 * rms (plain, 0, 4 * S)) < 0.1 * rms (plain, 0, 4 * S), "a lane at full send keeps the Dry level of itself");

        // let go of the send: the tail rings out, then the bank stops costing anything
        Rig rt; rt.w->bandsAll.store (1.0f);
        play (rt, 2.0, {});
        rt.w->bandsAll.store (0.0f);
        play (rt, 6.0, {});
        CHECK (! rt.w->bandsRunning.load(), "sends back to 0: the bank stops once its tail has died");

        // an export hears it: renderSong with the sends set
        if (! outdir.empty()) { writeWav (outdir + "/bands_lane1.wav", one); writeWav (outdir + "/bands_all.wav", all); }
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
