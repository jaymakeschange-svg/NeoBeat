// NeoBeat 3.0 — engine tests: every feature of the workstation, offline, with checks.
//   test_workstation [outdir]     (writes a few WAVs to outdir when given)
#include "../Source/engine/Factory.h"
#include "../Source/engine/Export.h"
#include "../Source/engine/Workstation.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

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

static RenderInputs baseInputs (Workstation& w, int preset = 0)
{
    RenderInputs in;
    const Preset p = patternPreset (preset);
    for (auto& sc : in.scenes) sc = p.pattern;
    in.bpm = p.tempo; in.swing = p.swing;
    const auto& lib = w.kit().lib();
    for (int pad = 0; pad < kNumPads; ++pad) in.pads[(size_t) pad] = kitPadSettings (lib, 0, pad);
    return in;
}

static void applyPads (Workstation& w, const RenderInputs& in) { for (int p = 0; p < kNumPads; ++p) w.kit().pads[(size_t) p] = in.pads[(size_t) p]; }

// everything the plugin does when something changes: the scenes, the live state (at once), the prepared sound
static void load (Workstation& w, const RenderInputs& in, bool prepare = true)
{
    for (int s = 0; s < kNumScenes; ++s) w.setScene (s, in.scenes[(size_t) s]);
    w.publishLive (buildLiveState (in.project));
    if (prepare) w.render().renderNow (in, SR);
}
static int gScene = 0;
static std::vector<MidiOut> gMidi;

// play `seconds` with the host transport running from ppq 0
static Out play (Workstation& w, const RenderInputs& in, double seconds, std::vector<MidiIn> midi = {}, int mode = 0, bool hostPlaying = true)
{
    Out o; const int total = (int) (seconds * SR);
    o.l.assign ((size_t) total, 0.0f); o.r.assign ((size_t) total, 0.0f);
    std::vector<MidiOut> mo; mo.reserve (256);
    BlockParams bp; bp.mode = mode; bp.swing = in.swing; bp.scene = gScene;
    double ppq = 0.0;
    gMidi.clear();
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        const int n = std::min (BLOCK, total - pos);
        std::vector<MidiIn> here;
        for (const auto& m : midi) if (m.offset >= pos && m.offset < pos + n) { MidiIn x = m; x.offset -= pos; here.push_back (x); }
        Transport tr; tr.bpm = in.bpm; tr.hostPlaying = hostPlaying; tr.hostPpq = ppq; tr.hostHasPpq = true;
        mo.clear();
        applyPads (w, in);
        w.process (o.l.data() + pos, o.r.data() + pos, n, tr, bp, here.data(), (int) here.size(), mo, nullptr);
        for (auto m : mo) { m.offset += pos; gMidi.push_back (m); }
        ppq += in.bpm / 60.0 * n / SR;
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
static bool finite (const Out& o) { for (size_t i = 0; i < o.l.size(); ++i) if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) return false; return true; }
static double rms (const Out& o, int a, int b)
{
    double s = 0.0; b = std::min (b, (int) o.l.size());
    for (int i = a; i < b; ++i) s += 0.5 * ((double) o.l[(size_t) i] * o.l[(size_t) i] + (double) o.r[(size_t) i] * o.r[(size_t) i]);
    return std::sqrt (s / std::max (1, b - a));
}
// one channel at a time. The rms above averages the two, which hides a channel that has
// gone quiet behind a 3 dB dip and a channel that is climbing behind the other one's level.
static double rmsCh (const std::vector<float>& v, int a, int b)
{
    double s = 0.0; b = std::min (b, (int) v.size());
    for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / std::max (1, b - a));
}
// + means the left is louder. A drum kit is not symmetrical, so this is for catching a
// channel that has been dropped or left to accumulate, not for judging a mix.
static double balanceDb (const Out& o)
{
    const int n = (int) o.l.size();
    return 20.0 * std::log10 (std::max (1e-9, rmsCh (o.l, 0, n)) / std::max (1e-9, rmsCh (o.r, 0, n)));
}
// how much louder the second half is than the first. Steady playing should sit near zero;
// a buffer that is never cleared climbs without ever tripping a peak check early on.
static double growthDb (const Out& o)
{
    const int n = (int) o.l.size(), h = n / 2;
    const double e = 0.5 * (rmsCh (o.l, 0, h) + rmsCh (o.r, 0, h));
    const double l = 0.5 * (rmsCh (o.l, h, n) + rmsCh (o.r, h, n));
    return 20.0 * std::log10 (std::max (1e-9, l) / std::max (1e-9, e));
}
static double diff (const Out& a, const Out& b)
{
    double s = 0.0; const size_t n = std::min (a.l.size(), b.l.size());
    for (size_t i = 0; i < n; ++i) s += std::fabs (a.l[i] - b.l[i]) + std::fabs (a.r[i] - b.r[i]);
    return s / std::max<size_t> (1, n);
}

static std::shared_ptr<const SampleData> sine (float hz, float seconds, float amp = 0.5f)
{
    auto s = std::make_shared<SampleData>(); s->sampleRate = SR;
    const int n = (int) (seconds * SR);
    for (int i = 0; i < n; ++i) { const float env = std::exp (-3.0f * i / (float) n); const float v = amp * env * std::sin (6.2831853f * hz * i / SR); s->left.push_back (v); s->right.push_back (v); }
    return s;
}

// strongest frequency (50 Hz .. 2 kHz) in a window: a plain DFT scan
static float pitchOf (const Out& o, int a, int b)
{
    float best = 0.0f, bestF = 0.0f;
    for (float f = 50.0f; f < 2000.0f; f += 1.0f)
    {
        double re = 0.0, im = 0.0; const double w = 6.283185307179586 * f / SR;
        for (int i = a; i < b; i += 2) { re += o.l[(size_t) i] * std::cos (w * i); im += o.l[(size_t) i] * std::sin (w * i); }
        const float m = (float) (re * re + im * im);
        if (m > best) { best = m; bestF = f; }
    }
    return bestF;
}

int main (int argc, char** argv)
{
    const bool threadsOnly = argc > 1 && std::string (argv[1]) == "--threads";
    const std::string outdir = argc > 1 && ! threadsOnly ? argv[1] : "";
    auto w = std::make_unique<Workstation>();
    w->prepare (SR, BLOCK);
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count(); };

    std::printf ("1. the drum map plays preset 0 like build 1\n");
    RenderInputs in = baseInputs (*w, 0);
    load (*w, in);
    Out base = play (*w, in, 4.0);
    CHECK (finite (base), "finite");
    CHECK (peak (base) > 0.05f && peak (base) < 1.0f, "audible and under full scale");
    CHECK (std::fabs (balanceDb (base)) < 6.0, "both channels carry the kit");
    CHECK (growthDb (base) < 3.0, "four bars in, it is not climbing");
    std::printf ("     balance %+.1f dB, second half %+.1f dB against the first\n", balanceDb (base), growthDb (base));
    if (! outdir.empty()) writeWav (outdir + "/01_drum_map.wav", base);
    // where the first snare lands (for the timing checks)
    int snareStep = -1; for (int st = 0; st < in.scenes[0].length && snareStep < 0; ++st) if (in.scenes[0].steps[role::Snare][(size_t) st].vel > 0) snareStep = st;
    const int snareAt = (int) (stepSeconds16 (snareStep, in.bpm, in.swing) * SR);

    if (! threadsOnly) {
    std::printf ("2. nodes, live: a Freezing node on the snares\n");
    {
        RenderInputs c = in;
        c.project.matrix.oneShot = makeOneShot (1, c.scenes[0]);   // Clap Space
        CHECK (! c.project.matrix.oneShot.cells.empty(), "the recipe found snare hits");
        const double t = ms();
        load (*w, c);
        std::printf ("     prepared in %.0f ms\n", ms() - t);
        const RtState* st = w->publisher.peek();
        bool none = true; for (const auto& rc : st->cells) none &= ! rc.audio;
        CHECK (none, "nothing is rendered ahead for live nodes");
        CHECK (st->padDry[role::Snare][0] && st->padDry[role::Snare][1] && st->padDry[role::Snare][2], "the lane's own hit is ready at three strengths");
        Out o = play (*w, c, 4.0);
        CHECK (finite (o), "finite");
        CHECK (diff (o, base) > 1.0e-4, "the nodes change the sound");
        CHECK (peak (o) < 1.0f, "under full scale");
        if (! outdir.empty()) writeWav (outdir + "/02_one_shot_clap_space.wav", o);

        // one node, on the first snare: it must land on the step, and what it adds must be the effect
        RenderInputs one = in;
        OneShotCell cell; cell.pad = role::Snare; cell.step = snareStep; cell.span = 4; cell.fx = CellFx::library (MatrixFx::Freezing); cell.height = 0.8f;
        one.project.matrix.oneShot.cells = { cell };
        load (*w, one);
        Out a = play (*w, one, 2.0);
        const double hitBase = rms (base, snareAt, snareAt + 960), hitNode = rms (a, snareAt, snareAt + 960);
        std::printf ("     the first 20 ms of the snare: plain %.4f, with the node %.4f\n", hitBase, hitNode);
        CHECK (hitNode > 0.4 * hitBase, "a sequenced node sounds on its step (no delay)");
        CHECK (rms (a, 0, snareAt) > 0.0 && diff (a, base) > 1.0e-5, "the node is heard");
        CHECK ((w->lateHits() + w->lateSeqHits()) == 0, "no hit was late");
        // move the node's control: published at once, no render, heard at once
        const uint32_t late0 = (w->lateHits() + w->lateSeqHits());
        RenderInputs moved = one; moved.project.matrix.oneShot.cells[0].height = 0.1f;
        load (*w, moved, false);
        Out b = play (*w, moved, 2.0);
        CHECK (diff (a, b) > 1.0e-5, "a moved control is heard without any render");
        // another effect altogether, again with no render
        moved.project.matrix.oneShot.cells[0].fx = CellFx::library (MatrixFx::CombFilterbank);
        load (*w, moved, false);
        Out b2 = play (*w, moved, 2.0);
        CHECK (diff (b2, b) > 1.0e-5 && finite (b2), "a changed effect is heard without any render");
        // the lane's sound changes and nothing is prepared for it yet: the lane's own voice feeds the effect, still on the step
        RenderInputs fresh = one; fresh.pads[role::Snare].decay *= 0.7f;
        load (*w, fresh, false);
        Out v = play (*w, fresh, 2.0);
        const double hitVoice = rms (v, snareAt, snareAt + 960);
        std::printf ("     with nothing prepared (the voice feeds it): %.4f\n", hitVoice);
        CHECK (hitVoice > 0.3 * hitBase && finite (v), "with nothing prepared the effect still sounds, on the step");
        CHECK ((w->lateHits() + w->lateSeqHits()) == late0, "and still no hit was late");
        // played from a key with nothing prepared: the one case that has to be late (and says so)
        Out kq = play (*w, fresh, 0.5, {}, 2, false);
        (void) kq;
        load (*w, one);
        // every factory recipe plays
        int ok = 0;
        for (int i = 1; i <= 32; ++i)
        {
            RenderInputs d = in; d.project.matrix.oneShot = makeOneShot (i, d.scenes[0]);
            load (*w, d);
            Out r = play (*w, d, 2.0);
            const bool good = ! d.project.matrix.oneShot.cells.empty() && finite (r) && peak (r) < 1.0f && peak (r) > 0.02f;
            ok += good ? 1 : 0;
            if (! good) std::printf ("     recipe %d (%s) failed: peak %.3f\n", i, oneShotRecipes()[(size_t) i - 1].name, peak (r));
        }
        std::printf ("     all recipes: %d / 32 (%.0f ms so far)\n", ok, ms());
        CHECK (ok == 32, "all 32 One Shot recipes play, finite and under full scale");
        // every library effect as a node: plays, ends, and the engine falls silent afterwards
        int fine = 0;
        for (int f = 0; f < (int) matrixFxInfo().size(); ++f)
        {
            RenderInputs d = in;
            for (auto& sc : d.scenes) { sc = Pattern(); sc.length = 64; sc.steps[role::Snare][0].vel = 110; }
            OneShotCell n1; n1.pad = role::Snare; n1.step = 0; n1.span = 2; n1.fx = CellFx::library ((MatrixFx) f);
            d.project.matrix.oneShot.cells = { n1 };
            load (*w, d);
            Out r = play (*w, d, 1.5);
            const bool good = finite (r) && peak (r) > 0.01f && peak (r) < 1.0f && rms (r, (int) (1.2 * SR), (int) (1.5 * SR)) < 1.0e-3 && w->activeUnits() == 0;
            fine += good ? 1 : 0;
            if (! good) std::printf ("     %s: peak %.3f, tail %.5f, units %d\n", matrixFxInfo()[(size_t) f].name, peak (r), rms (r, (int) (1.2 * SR), (int) (1.5 * SR)), w->activeUnits());
        }
        CHECK (fine == (int) matrixFxInfo().size(), "all 24 effects work as a node, held to the node's reach");
    }

    std::printf ("3. lane inserts, live: Comb Filterbank and Chorus on the snare\n");
    {
        RenderInputs c = in;
        c.project.padFx[role::Snare].inserts[0] = CellFx::library (MatrixFx::CombFilterbank);
        c.project.padFx[role::Snare].inserts[1] = CellFx::library (MatrixFx::Chorus);
        load (*w, c);
        const RtState* st = w->publisher.peek();
        CHECK (! st->hasInserts[role::Snare] && ! st->insertAudio[role::Snare][2], "nothing rendered ahead");
        Out o = play (*w, c, 3.0);
        CHECK (finite (o) && diff (o, base) > 1.0e-4, "the inserts are heard");
        CHECK (peak (o) < 1.0f, "under full scale");
        // three spectral inserts in a row: three frames of look-ahead, still on the step
        RenderInputs d = in;
        d.project.padFx[role::Snare].inserts[0] = CellFx::library (MatrixFx::Tracing);
        d.project.padFx[role::Snare].inserts[1] = CellFx::library (MatrixFx::Blurring);
        d.project.padFx[role::Snare].inserts[2] = CellFx::library (MatrixFx::Shimmer);
        const uint32_t late0 = (w->lateHits() + w->lateSeqHits());
        load (*w, d);
        Out o3 = play (*w, d, 2.0);
        std::printf ("     three spectral inserts: the snare's first 20 ms %.4f (plain %.4f)\n", rms (o3, snareAt, snareAt + 960), rms (base, snareAt, snareAt + 960));
        CHECK (rms (o3, snareAt, snareAt + 960) > 0.15 * rms (base, snareAt, snareAt + 960), "three spectral inserts in a row still land on the step");
        CHECK ((w->lateHits() + w->lateSeqHits()) == late0, "no hit was late");
        // an insert with Friction Stretch in it is made ahead, as before
        RenderInputs f = in;
        f.project.padFx[role::Snare].inserts[0] = CellFx::friction (StretchParams());
        load (*w, f);
        st = w->publisher.peek();
        CHECK (st->hasInserts[role::Snare] && st->insertAudio[role::Snare][2] && st->insertAudio[role::Snare][0], "Friction Stretch inserts are rendered at three strengths");
        Out of = play (*w, f, 2.0);
        CHECK (finite (of) && diff (of, base) > 1.0e-4, "and heard");
    }

    std::printf ("4. windows: a Friction Stretch slot on the snare (replace) and a live Bounce Delay slot on the kick\n");
    {
        RenderInputs c = in;
        c.project.fs[0].on = true; c.project.fs[0].src = { SourceKind::Pad, role::Snare }; c.project.fs[0].p.stretch = 8.0f;
        c.project.bd[0].on = true; c.project.bd[0].src = { SourceKind::Pad, role::Kick };
        const double t = ms();
        load (*w, c);
        std::printf ("     rendered in %.0f ms\n", ms() - t);
        const RtState* st = w->publisher.peek();
        int fsOk = 0; for (const auto& s : st->slots) if (s.fs && s.audio[0] && s.audio[1]) ++fsOk;
        CHECK (fsOk == 1, "the Friction Stretch slot rendered for even and odd steps");
        CHECK (st->padSlots[role::Snare].size() == 1 && st->padSlots[role::Kick].empty(), "the snare plays its rendered slot; the kick's Bounce Delay is live");
        Out o = play (*w, c, 4.0);
        CHECK (finite (o) && peak (o) < 1.0f, "finite, under full scale");
        CHECK (diff (o, base) > 1.0e-4, "the windows are heard");
        if (! outdir.empty()) writeWav (outdir + "/04_windows.wav", o);
        // the Bounce Delay alone, then with its bounciness moved: no render in between
        RenderInputs b1 = in; b1.project.bd[0].on = true; b1.project.bd[0].src = { SourceKind::Pad, role::Kick };
        load (*w, b1);
        Out ob1 = play (*w, b1, 3.0);
        RenderInputs b2 = b1; b2.project.bd[0].p.bounciness = 0.5f;
        load (*w, b2, false);
        Out ob2 = play (*w, b2, 3.0);
        CHECK (diff (ob1, base) > 1.0e-4 && diff (ob1, ob2) > 1.0e-5, "Bounce Delay is live: a moved control is heard without any render");
        // a 64x stretch on a slot (the extreme case)
        RenderInputs e = c; e.project.fs[0].p.stretch = 64.0f;
        const double t2 = ms();
        load (*w, e);
        const RtState* s2 = w->publisher.peek();
        float len = 0.0f; for (const auto& s : s2->slots) if (s.fs && s.audio[0]) len = s.audio[0]->size() / SR;
        std::printf ("     64x stretch: %.1f s of audio in %.0f ms\n", len, ms() - t2);
        CHECK (len > 4.0f, "64x stretch renders a long accent");
    }

    std::printf ("5. key map: a keytracked sample zone, velocity layers and round robin\n");
    {
        RenderInputs c = in;
        c.files["sine"] = sine (220.0f, 1.0f);
        Zone z; z.name = "Sine"; z.loKey = 60; z.hiKey = 84; z.root = 69; z.src = ZoneSource::Sample; z.file = "sine"; z.keytrack = true;
        c.project.keymap.zones.push_back (z);
        load (*w, c);
        // A4 (root) plays 220 Hz, A5 plays 440 Hz
        Out a = play (*w, c, 0.6, { { 0, 0, 69, 0.9f } }, 2, false);
        Out b = play (*w, c, 0.6, { { 0, 0, 81, 0.9f } }, 2, false);
        const float pa = pitchOf (a, 2400, 12000), pb = pitchOf (b, 2400, 12000);
        std::printf ("     root %.0f Hz, octave up %.0f Hz\n", pa, pb);
        CHECK (std::fabs (pa - 220.0f) < 12.0f && std::fabs (pb - 440.0f) < 20.0f, "keytracking follows the key");
        // a live effect on that zone: played from a key, with its sample ready, it starts at once
        RenderInputs zf = c; zf.project.keymap.zones.back().fx = CellFx::library (MatrixFx::PitchShift);
        const uint32_t late0 = (w->lateHits() + w->lateSeqHits());
        load (*w, zf);
        Out az = play (*w, zf, 0.6, { { 0, 0, 69, 0.9f } }, 2, false);
        CHECK (finite (az) && diff (az, a) > 1.0e-5 && rms (az, 0, 960) > 0.2 * rms (a, 0, 960), "a zone's live effect, played from a key, starts at once");
        CHECK ((w->lateHits() + w->lateSeqHits()) == late0, "not late: its first frame is fed at once");
        // velocity layers: soft and hard zones on the snare's key, hard has an effect
        RenderInputs v = in; v.project.keymap = makeKeyMap (2);
        load (*w, v);
        Out soft = play (*w, v, 1.0, { { 0, 0, 38, 0.4f } }, 2, false);
        Out hard = play (*w, v, 1.0, { { 0, 0, 38, 1.0f } }, 2, false);
        CHECK (peak (soft) > 0.01f && peak (hard) > 0.01f, "both layers sound");
        // round robin: three zones on one key take turns
        RenderInputs rr = in; rr.project.keymap = makeKeyMap (4);
        load (*w, rr);
        std::vector<MidiIn> notes; for (int i = 0; i < 6; ++i) notes.push_back ({ (int) (i * 0.25 * SR), 0, 40, 0.9f });
        Out o = play (*w, rr, 1.6, notes, 2, false);
        CHECK (finite (o) && peak (o) > 0.01f, "round robin plays");
        // a split preset with Friction Stretch low and Bounce Delay high
        RenderInputs sp = in; sp.project.keymap = makeKeyMap (1);
        load (*w, sp);
        Out o2 = play (*w, sp, 3.0, { { 0, 0, 12, 0.9f }, { (int) (0.5 * SR), 0, 72, 0.9f }, { (int) SR, 0, 36, 0.9f } }, 2, false);
        CHECK (finite (o2) && peak (o2) > 0.01f && peak (o2) < 1.0f, "split zones play");
        if (! outdir.empty()) writeWav (outdir + "/05_split.wav", o2);
    }

    std::printf ("6. Layermax: kit rows, swung columns, a live Bounce Delay on a row, rows that follow a lane\n");
    {
        RenderInputs c = in;
        for (auto& sc : c.scenes) { sc = Pattern(); sc.length = 16; }   // the sequencer silent: only Layermax plays
        auto& g = c.project.grid; g.play = true; g.useKit = true;
        auto& pat = g.patterns[0]; pat.length = 16; pat.rate = 1;
        for (int col = 0; col < 16; col += 2) pat.cells[(size_t) (role::Kick * 64 + col)] = GridPattern::pack (220, 128, 128, GridPattern::kOn);
        for (int col = 1; col < 16; col += 2) pat.cells[(size_t) (role::Hat * 64 + col)] = GridPattern::pack (160, 60, 150, GridPattern::kOn | GridPattern::kBd);
        c.project.bd[1].on = true; c.project.bd[1].src = { SourceKind::GridRow, role::Hat };
        load (*w, c);
        const RtState* st = w->publisher.peek();
        int rows = 0; for (const auto& s : st->gridSamples) rows += s ? 1 : 0;
        CHECK (rows == 16, "16 kit rows ready");
        Out o = play (*w, c, 2.0);
        CHECK (finite (o) && peak (o) > 0.05f && peak (o) < 1.0f, "Layermax plays");
        // onsets: the first kick at 0, the first hat (odd step) at 2 * 16th * swing
        const double s16 = 15.0 / c.bpm;
        const int hatAt = (int) (2.0 * s16 * c.swing * SR);
        CHECK (rms (o, 0, 2000) > 1.0e-3, "column 0 sounds at the start");
        CHECK (rms (o, hatAt, hatAt + 2000) > 1.0e-4, "column 1 lands on the swung 16th");
        RenderInputs nb2 = c; nb2.project.bd[1].on = false;
        load (*w, nb2);
        Out o0 = play (*w, nb2, 2.0);
        CHECK (diff (o, o0) > 1.0e-5, "the row's Bounce Delay is heard (live)");
        if (! outdir.empty()) writeWav (outdir + "/06_grid.wav", o);

        // follow: Layermax stopped; row 5 (a hat) layers every kick of the sequencer
        RenderInputs f = in;
        for (auto& sc : f.scenes) { sc = Pattern(); sc.length = 16; for (int s2 = 0; s2 < 16; s2 += 4) sc.steps[role::Kick][(size_t) s2].vel = 110; }
        load (*w, f);
        Out kicks = play (*w, f, 2.0);
        RenderInputs fl = f; fl.project.grid.follow[(size_t) role::Hat].on = true; fl.project.grid.follow[(size_t) role::Hat].lane = role::Kick;
        load (*w, fl);
        Out layered = play (*w, fl, 2.0);
        CHECK (diff (layered, kicks) > 1.0e-5 && finite (layered), "a row that follows the kick lane layers every kick");
        // fill: the row plays on the 16ths the kick leaves empty
        RenderInputs ff = fl; ff.project.grid.follow[(size_t) role::Hat].mode = 1;
        load (*w, ff);
        Out filled = play (*w, ff, 2.0);
        const int gapAt = (int) (stepSeconds16 (2, f.bpm, f.swing) * SR);   // step 2: no kick there
        std::printf ("     at an empty 16th: kicks alone %.5f, layer %.5f, fill %.5f\n", rms (kicks, gapAt, gapAt + 1500), rms (layered, gapAt, gapAt + 1500), rms (filled, gapAt, gapAt + 1500));
        CHECK (rms (filled, gapAt, gapAt + 1500) > 4.0 * rms (layered, gapAt, gapAt + 1500) + 1.0e-4, "fill plays in the gaps the lane leaves");
        // every 2nd hit, 30 ms early
        RenderInputs fn = fl; fn.project.grid.follow[(size_t) role::Hat].mode = 2; fn.project.grid.follow[(size_t) role::Hat].nth = 2; fn.project.grid.follow[(size_t) role::Hat].offsetMs = 30.0f;
        load (*w, fn);
        Out nth = play (*w, fn, 2.0);
        CHECK (finite (nth) && diff (nth, layered) > 1.0e-5 && diff (nth, kicks) > 1.0e-5, "every Nth hit, off the beat");
        load (*w, in);
    }

    std::printf ("7. the Draw view: every factory preset draws sound\n");
    {
        int ok = 0;
        for (int i = 1; i <= 31; ++i)
        {
            RenderInputs c = in; for (auto& sc : c.scenes) { sc = Pattern(); sc.length = 16; }
            c.project.matrix.seq = makeSeq (i);
            load (*w, c);
            Out o = play (*w, c, 3.0);
            const bool good = finite (o) && peak (o) > 0.002f && peak (o) < 1.0f;
            ok += good ? 1 : 0;
            if (! good) std::printf ("     %s: peak %.4f finite %d\n", seqNames()[(size_t) i].c_str(), peak (o), (int) finite (o));
            if (i == 1 && ! outdir.empty()) writeWav (outdir + "/07_seq_glass_sparkle_rain.wav", o);
            if (i == 2 && ! outdir.empty()) writeWav (outdir + "/07_seq_warble_drone.wav", o);
        }
        CHECK (ok == 31, "31 / 31 Draw presets play");
        load (*w, in);
    }

    std::printf ("8. the Sample view: boxes mangle pad 16's hit, height drives the main control\n");
    {
        int ok = 0;
        for (int i = 1; i <= 31; ++i)
        {
            RenderInputs c = in; c.project.matrix.sample = makeSample (i);
            load (*w, c);
            const RtState* st = w->publisher.peek();
            const auto& s = st->padSample[role::Door];
            bool good = s && s->left.size() > 100;
            if (good) for (size_t k = 0; k < s->left.size(); k += 5) good &= std::isfinite (s->left[k]) && std::fabs (s->left[k]) <= 1.0f;
            ok += good ? 1 : 0;
            if (! good) std::printf ("     %s failed\n", sampleNames()[(size_t) i].c_str());
            if (i == 31 && ! outdir.empty())
            {
                Out o = play (*w, c, 2.5, { { 0, 0, 51, 0.9f } }, 2, false);
                writeWav (outdir + "/08_sample_total_mangle.wav", o);
            }
        }
        CHECK (ok == 31, "31 / 31 Sample presets mangle");
    }

    std::printf ("9. modulation: LFO to level and pan, velocity to tone, algorithms, a node's main control\n");
    {
        RenderInputs c = in;
        c.project.mod.routes.push_back ({ ModSrc::Lfo1, "pad/*/level", -0.3f });
        c.project.mod.routes.push_back ({ ModSrc::Lfo2, "pad/4/pan", 0.8f });
        c.project.mod.routes.push_back ({ ModSrc::Velocity, "pad/2/tone", 0.5f });
        c.project.mod.routes.push_back ({ ModSrc::Random, "pad/0/tune", 0.1f });
        c.project.mod.routes.push_back ({ ModSrc::Lfo1, "master", 0.1f });
        for (const auto& a : algoPresets()) c.project.mod.algos[(size_t) a.pad] = a.algo;
        load (*w, c);
        Out o = play (*w, c, 4.0);
        CHECK (finite (o) && peak (o) < 1.0f, "finite, under full scale");
        CHECK (diff (o, base) > 1.0e-4, "the modulation is heard");
        if (! outdir.empty()) writeWav (outdir + "/09_mod.wav", o);
        // a node's main control from a route
        RenderInputs d = in; d.project.matrix.oneShot = makeOneShot (1, d.scenes[0]);
        load (*w, d);
        Out plain = play (*w, d, 3.0);
        d.project.mod.routes.push_back ({ ModSrc::Lfo1, "cell/0/main", 0.6f });
        load (*w, d);
        Out moved = play (*w, d, 3.0);
        CHECK (finite (moved) && diff (moved, plain) > 1.0e-6, "a route moves a node's main control while it plays");
        Dest dd; int tg, pm;
        CHECK (parseDest ("fs/3/stretch", dd, tg, pm) && dd == Dest::FsParam && tg == 3 && pm == 0, "destinations parse");
        CHECK (! parseDest ("pad/x/nothing", dd, tg, pm), "bad destinations are refused");
    }

    std::printf ("10. key mode, pads mode and a 32-step pattern at 126 BPM through all 128 patterns\n");
    {
        int ok = 0;
        for (int i = 0; i < numPatterns(); i += 9)
        {
            RenderInputs c = baseInputs (*w, i);
            load (*w, c);
            Out o = play (*w, c, 2.0);
            ok += (finite (o) && peak (o) > 0.01f && peak (o) < 1.0f) ? 1 : 0;
        }
        CHECK (ok == (numPatterns() + 8) / 9, "sampled patterns play cleanly");
        RenderInputs c = baseInputs (*w, 3);
        load (*w, c);
        Out k = play (*w, c, 2.0, { { 0, 0, 67, 0.9f } }, 1, false);   // key mode, G3 held: the pattern a fifth up
        CHECK (finite (k) && peak (k) > 0.01f, "key mode plays the pattern");
        load (*w, in);
    }

    std::printf ("12. scenes: four of them, 32 bars each, a chain, and a live switch on the bar\n");
    {
        RenderInputs c = in;
        // A: a kick on every beat for one bar. B: silence for one bar. C: 32 bars with one kick at the very last step.
        c.scenes[0] = Pattern(); c.scenes[0].length = 16; for (int s2 = 0; s2 < 16; s2 += 4) c.scenes[0].steps[role::Kick][(size_t) s2].vel = 120;
        c.scenes[1] = Pattern(); c.scenes[1].length = 16;
        c.scenes[2] = Pattern(); c.scenes[2].length = Pattern::kMaxSteps; c.scenes[2].steps[role::Kick][Pattern::kMaxSteps - 1].vel = 120;
        c.bpm = 120.0; c.swing = 0.5f;
        const int bar = (int) (2.0 * SR);
        // chain: A, B, A, A
        c.project.song.chainOn = true; c.project.song.numLinks = 3;
        c.project.song.chain[0] = { 0, 1 }; c.project.song.chain[1] = { 1, 1 }; c.project.song.chain[2] = { 0, 2 };
        load (*w, c);
        Out o = play (*w, c, 10.0);
        std::printf ("     chain A B A A | A: bars %.3f %.5f %.3f %.3f %.3f\n", rms (o, 0, bar), rms (o, bar + 9600, 2 * bar), rms (o, 2 * bar, 3 * bar), rms (o, 3 * bar, 4 * bar), rms (o, 4 * bar, 5 * bar));
        CHECK (rms (o, 0, bar) > 0.01 && rms (o, bar + 24000, 2 * bar) < 1.0e-3 && rms (o, 2 * bar, 3 * bar) > 0.01 && rms (o, 3 * bar, 4 * bar) > 0.01, "the chain plays A, B (silent), A, A");
        CHECK (rms (o, 4 * bar, 5 * bar) > 0.01, "and round again");
        // no chain: scene A loops; asking for B mid-bar switches on the next bar
        c.project.song.chainOn = false;
        load (*w, c);
        gScene = 0; Out a = play (*w, c, 2.0);
        CHECK (rms (a, bar / 2, bar) > 0.005, "scene A loops");
        gScene = 1; Out b = play (*w, c, 1.0, {}, 0, true);   // (each play starts the song again: B is taken at once when stopped)
        CHECK (rms (b, 0, (int) SR) < 1.0e-3, "scene B (empty) is silent");
        // a switch asked for while running waits for the bar
        {
            Out sw; const int total = 3 * bar; sw.l.assign ((size_t) total, 0.0f); sw.r.assign ((size_t) total, 0.0f);
            std::vector<MidiOut> mo; mo.reserve (256); BlockParams bp; bp.swing = c.swing; double ppq = 0.0;
            // the first block establishes scene A while the transport is stopped
            { Transport tr; tr.bpm = c.bpm; bp.scene = 0; std::vector<float> zl (BLOCK), zr (BLOCK); applyPads (*w, c); w->process (zl.data(), zr.data(), BLOCK, tr, bp, nullptr, 0, mo, nullptr); }
            for (int pos = 0; pos < total; pos += BLOCK)
            {
                const int n = std::min (BLOCK, total - pos);
                bp.scene = pos >= bar / 4 ? 1 : 0;      // asked for a quarter of the way into bar 1
                Transport tr; tr.bpm = c.bpm; tr.hostPlaying = true; tr.hostPpq = ppq; mo.clear(); applyPads (*w, c);
                w->process (sw.l.data() + pos, sw.r.data() + pos, n, tr, bp, nullptr, 0, mo, nullptr);
                ppq += c.bpm / 60.0 * n / SR;
            }
            std::printf ("     switch asked at beat 1: rest of bar 1 %.4f, bar 2 %.5f\n", rms (sw, bar / 2, bar), rms (sw, bar + 24000, 2 * bar));
            CHECK (rms (sw, bar / 2, bar) > 0.005 && rms (sw, bar + 24000, 2 * bar) < 1.0e-3, "a live switch waits for the bar line");
        }
        // 32 bars: the last step of scene C sounds at bar 32, beat 4.75
        gScene = 2;
        Out longOne = play (*w, c, 65.0);
        const int lastAt = (int) ((Pattern::kMaxSteps - 1) * 0.125 * SR);
        std::printf ("     scene C: silent until step 512 at %.2f s: before %.6f, there %.4f\n", lastAt / SR, rms (longOne, 0, lastAt - 100), rms (longOne, lastAt, lastAt + 4800));
        CHECK (rms (longOne, 0, lastAt - 100) < 1.0e-4 && rms (longOne, lastAt, lastAt + 4800) > 0.01, "512 steps: the last one lands at bar 32");
        // a node in scene B only acts in scene B
        RenderInputs nd = c; nd.scenes[1] = nd.scenes[0];
        OneShotCell cell; cell.pad = role::Kick; cell.step = 0; cell.scene = 1; cell.span = 2; cell.fx = CellFx::library (MatrixFx::CombFilterbank);
        nd.project.matrix.oneShot.cells = { cell };
        load (*w, nd);
        gScene = 0; Out sa = play (*w, nd, 1.0);
        gScene = 1; Out sb = play (*w, nd, 1.0);
        CHECK (diff (sa, sb) > 1.0e-5, "a node belongs to its scene");
        gScene = 0;
        load (*w, in);
    }

    std::printf ("13. MIDI out: a note per lane, its channel and length, MIDI only, rolls\n");
    {
        RenderInputs c = in;
        for (auto& sc : c.scenes) { sc = Pattern(); sc.length = 16; sc.steps[role::Kick][0].vel = 100; sc.steps[role::Snare][4].vel = 80; sc.steps[role::Hat][8].vel = 90; sc.steps[role::Hat][8].roll = 3; }
        c.bpm = 120.0; c.swing = 0.5f;
        c.project.midi.note[role::Snare] = 50; c.project.midi.channel[role::Snare] = 3; c.project.midi.lengthSteps = 1.0f;
        load (*w, c);
        play (*w, c, 0.5, {}, 2, false);     // (let any note still held from before go)
        Out o = play (*w, c, 1.9);
        int ons = 0, offs = 0, kickAt = -1, snareOn = -1, snareOff = -1, snareCh = 0, hatOns = 0;
        for (const auto& m : gMidi)
        {
            if (m.on) ++ons; else ++offs;
            if (m.on && m.note == 36 + role::Kick && kickAt < 0) kickAt = m.offset;
            if (m.note == 50) { if (m.on) { snareOn = m.offset; snareCh = m.channel; } else snareOff = m.offset; }
            if (m.on && m.note == 36 + role::Hat) ++hatOns;
        }
        std::printf ("     %d note-ons, %d note-offs; snare on at %d (channel %d), off at %d; hat roll %d notes\n", ons, offs, snareOn, snareCh, snareOff, hatOns);
        CHECK (ons == 5 && offs == 5, "every note-on has its note-off");
        CHECK (kickAt == 0 && std::abs (snareOn - (int) (0.5 * SR)) <= 1 && snareCh == 3, "notes land on their steps, on their lane's note and channel");
        CHECK (std::abs ((snareOff - snareOn) - (int) (0.125 * SR)) <= BLOCK, "held for the length that was set");
        CHECK (hatOns == 3, "a roll sends each of its hits");
        CHECK (peak (o) > 0.01f, "sound and MIDI together");
        RenderInputs m1 = c; m1.project.midi.mode = 1; m1.project.midi.rolls = false;
        load (*w, m1);
        Out silent = play (*w, m1, 1.9);
        int ons1 = 0; for (const auto& m : gMidi) ons1 += m.on ? 1 : 0;
        CHECK (peak (silent) < 1.0e-4f && ons1 == 3, "MIDI only: the lanes are silent, the notes still go out (a roll as one note)");
        RenderInputs m2 = c; m2.project.midi.mode = 2;
        load (*w, m2);
        Out loud = play (*w, m2, 1.9);
        CHECK (peak (loud) > 0.01f && gMidi.empty(), "sound only: no notes");
        load (*w, in);
    }

    std::printf ("14. width and level: mono, wide, and a lane 24 dB up\n");
    {
        RenderInputs c = in;
        for (auto& sc : c.scenes) { sc = Pattern(); sc.length = 16; sc.steps[role::Snare][0].vel = 110; }
        auto sideOf = [] (const Out& o) { double s = 0, m = 0; for (size_t i = 0; i < o.l.size(); ++i) { s += (double) (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]); m += (double) (o.l[i] + o.r[i]) * (o.l[i] + o.r[i]); } return std::sqrt (s / std::max (1.0e-20, m)); };
        auto midOf = [] (const Out& o) { double m = 0; for (size_t i = 0; i < o.l.size(); ++i) m += (double) (o.l[i] + o.r[i]) * (o.l[i] + o.r[i]); return std::sqrt (m / std::max<size_t> (1, o.l.size())); };
        // a stereo sample on the lane (the same every hit, so the three can be compared)
        {
            auto st = std::make_shared<SampleData>(); st->sampleRate = SR;
            for (int i = 0; i < (int) (0.4f * SR); ++i)
            {
                const float env = std::exp (-8.0f * i / SR);
                st->left.push_back (0.4f * env * std::sin (6.2831853f * 300.0f * i / SR));
                st->right.push_back (0.4f * env * (0.5f * std::sin (6.2831853f * 300.0f * i / SR) + 0.5f * std::sin (6.2831853f * 517.0f * i / SR)));
            }
            auto& ps = c.pads[role::Snare];
            ps.useSample = true; ps.sample = st; ps.sampleRms = 0.1f; ps.hollow = 0.0f; ps.place = -1; ps.space = 0.0f; ps.body = 0.0f; ps.fat = 0.0f; ps.drive = 0.0f;
        }
        c.pads[role::Snare].pan = 0.0f; c.pads[role::Snare].levelDb = -12.0f;
        load (*w, c); Out normal = play (*w, c, 1.0);
        RenderInputs mono = c; mono.pads[role::Snare].width = 0.0f; load (*w, mono); Out om = play (*w, mono, 1.0);
        RenderInputs wide = c; wide.pads[role::Snare].width = 2.0f; load (*w, wide); Out ow = play (*w, wide, 1.0);
        std::printf ("     side / mid: mono %.4f, as it is %.4f, wide %.4f | mid level: %.4f %.4f %.4f\n", sideOf (om), sideOf (normal), sideOf (ow), midOf (om), midOf (normal), midOf (ow));
        CHECK (sideOf (om) < 0.02, "width 0 is mono");
        CHECK (sideOf (ow) > sideOf (normal) + 0.05, "width 2 is wider");
        CHECK (std::fabs (midOf (ow) / midOf (normal) - 1.0) < 0.12 && std::fabs (midOf (om) / midOf (normal) - 1.0) < 0.12, "the mono sum stays as it was");
        RenderInputs up = c; up.pads[role::Snare].levelDb = 12.0f; load (*w, up); Out hot = play (*w, up, 1.0);
        std::printf ("     24 dB more on the lane: peak %.3f -> %.3f\n", peak (normal), peak (hot));
        CHECK (peak (hot) > 2.0f * peak (normal) && peak (hot) <= 1.0f && finite (hot), "a lane can go 24 dB louder, and the output still stays under full scale");
        load (*w, in);
    }

    std::printf ("15. many effects at once: the limits hold and a block stays cheap\n");
    {
        RenderInputs c = in;
        for (auto& sc : c.scenes) { sc = Pattern(); sc.length = 16; for (int s2 = 0; s2 < 16; ++s2) for (int p = 0; p < 8; ++p) sc.steps[(size_t) p][(size_t) s2].vel = 100; }
        for (int p = 0; p < 8; ++p)
        {
            c.project.padFx[(size_t) p].inserts[0] = CellFx::library ((MatrixFx) (p * 2));          // spectral
            c.project.padFx[(size_t) p].inserts[1] = CellFx::library (MatrixFx::Freezing);            // long tails
        }
        load (*w, c);
        Out o; const int total = (int) (4.0 * SR); o.l.assign ((size_t) total, 0.0f); o.r.assign ((size_t) total, 0.0f);
        std::vector<MidiOut> mo; mo.reserve (256); BlockParams bp; bp.swing = c.swing; double ppq = 0.0, worst = 0.0, sum = 0.0; int blocks = 0, most = 0;
        for (int pos = 0; pos < total; pos += BLOCK)
        {
            const int n = std::min (BLOCK, total - pos);
            Transport tr; tr.bpm = c.bpm; tr.hostPlaying = true; tr.hostPpq = ppq; mo.clear(); applyPads (*w, c);
            const auto b0 = std::chrono::steady_clock::now();
            w->process (o.l.data() + pos, o.r.data() + pos, n, tr, bp, nullptr, 0, mo, nullptr);
            const double took = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - b0).count();
            worst = std::max (worst, took); sum += took; ++blocks; most = std::max (most, w->activeUnits());
            ppq += c.bpm / 60.0 * n / SR;
        }
        const double blockMs = 1000.0 * BLOCK / SR;
        std::printf ("     8 lanes of 16ths, two spectral inserts each: up to %d units, mean %.0f %% of real time, worst block %.1f ms (a block is %.1f ms)\n", most, 100.0 * sum / blocks / blockMs, worst, blockMs);
        CHECK (most <= FxUnits::kUnits, "never more than 24 units");
        CHECK (finite (o) && peak (o) <= 1.0f, "finite, under full scale");
        load (*w, in);
        CHECK (sum / blocks < 0.9 * blockMs, "on average well inside real time, even here");
        Out rest = play (*w, in, 15.0);
        (void) rest;
        CHECK (w->activeUnits() == 0, "the tails end and the units are given back");
    }

    std::printf ("16. export: the pattern as sound (what you hear, effects and all), stems, and a MIDI file\n");
    {
        RenderInputs c = in;
        c.scenes[0] = Pattern(); c.scenes[0].length = 32;
        for (int s2 = 0; s2 < 32; s2 += 4) c.scenes[0].steps[role::Kick][(size_t) s2].vel = 115;
        for (int s2 = 4; s2 < 32; s2 += 8) c.scenes[0].steps[role::Snare][(size_t) s2].vel = 100;
        c.scenes[0].steps[role::Hat][30].vel = 90; c.scenes[0].steps[role::Hat][30].roll = 4;
        c.bpm = 120.0; c.swing = 0.5f;
        c.project.bd[0].on = true; c.project.bd[0].src = { SourceKind::Pad, role::Snare };
        c.project.padFx[role::Kick].inserts[0] = CellFx::library (MatrixFx::Freezing);
        ExportRange r; r.mode = 0; r.scene = 0;
        const double t = ms();
        Rendered mix = renderSong (c, SR, r);
        const int body = (int) (4.0 * SR);
        std::printf ("     2 bars at 120: %.2f s of sound (the pattern is 4.00 s, the rest is the effects ringing out), in %.0f ms\n", mix.size() / SR, ms() - t);
        Out mo; mo.l = mix.l; mo.r = mix.r;
        CHECK (mix.size() > body && mix.size() < body + (int) (12.5 * SR) && finite (mo) && peak (mo) > 0.05f && peak (mo) <= 1.0f, "the mix: the pattern and its tail");
        Rendered kick = renderSong (c, SR, r, 0.0f, role::Kick), hat = renderSong (c, SR, r, 0.0f, role::Hat);
        Out ko; ko.l = kick.l; ko.r = kick.r; Out ho; ho.l = hat.l; ho.r = hat.r;
        const int hatAt = (int) (30 * 0.125 * SR);
        CHECK (rms (ko, 0, 4800) > 0.01 && rms (ho, 0, hatAt - 100) < 1.0e-4 && rms (ho, hatAt, hatAt + 4800) > 0.001, "stems: each lane alone");
        ExportRange bars; bars.mode = 2; bars.scene = 0; bars.barFrom = 1; bars.barTo = 2; bars.tailSeconds = 0.0;
        Rendered second = renderSong (c, SR, bars);
        CHECK (std::abs (second.size() - (int) (2.0 * SR)) < 16, "a range of bars is exactly that long when the tail is off");
        // MIDI
        const auto notes = songNotes (c, r, c.swing);
        int kicks = 0, hats = 0; for (const auto& n : notes) { kicks += n.lane == role::Kick ? 1 : 0; hats += n.lane == role::Hat ? 1 : 0; }
        CHECK (kicks == 8 && hats == 4 && (int) notes.size() == 8 + 4 + 4, "the notes: 8 kicks, 4 snares, a roll of 4");
        const auto file = writeMidiFile (notes, c.bpm, 8.0, { "Kick" });
        const bool header = file.size() > 22 && file[0] == 'M' && file[1] == 'T' && file[2] == 'h' && file[3] == 'd' && file[9] == 1 && file[11] == 4 && file[12] == 1 && file[13] == 0xe0;
        int tracks = 0; for (size_t i = 0; i + 4 <= file.size(); ++i) if (file[i] == 'M' && file[i + 1] == 'T' && file[i + 2] == 'r' && file[i + 3] == 'k') ++tracks;
        CHECK (header && tracks == 4, "a type 1 MIDI file: a tempo track and one track per lane");
        if (! outdir.empty()) { Out wv; wv.l = mix.l; wv.r = mix.r; writeWav (outdir + "/16_export.wav", wv); std::ofstream mf (outdir + "/16_export.mid", std::ios::binary); mf.write ((const char*) file.data(), (std::streamsize) file.size()); }
        // the chain as one file
        RenderInputs ch = c; ch.scenes[1] = Pattern(); ch.scenes[1].length = 16;
        ch.project.song.chainOn = true; ch.project.song.numLinks = 2; ch.project.song.chain[0] = { 0, 2 }; ch.project.song.chain[1] = { 1, 1 };
        ExportRange all; all.mode = 1; all.tailSeconds = 0.0;
        Rendered song = renderSong (ch, SR, all);
        CHECK (std::abs (song.size() - (int) (10.0 * SR)) < 16, "the chain (A A B) exports as 5 bars");
        CHECK ((int) songNotes (ch, all, -1.0f).size() == 32, "and as MIDI: the notes of A twice");
    }

    }
    std::printf ("11. threads: the worker renders while the audio thread plays and edits keep coming\n");
    {
        load (*w, in);
        w->startRenderer();
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };
        std::atomic<bool> bad { false };
        std::atomic<double> longest { 0.0 };   // the slowest process() call, in ms: a stall means the audio waited on the worker
        std::thread audio ([&]
        {
            std::vector<float> L (BLOCK), R (BLOCK); std::vector<MidiOut> mo; mo.reserve (256);
            double ppq = 0.0; BlockParams bp; bp.swing = in.swing;
            while (! stop.load())
            {
                std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
                Transport tr; tr.bpm = in.bpm; tr.hostPlaying = true; tr.hostPpq = ppq;
                mo.clear();
                const auto t0 = std::chrono::steady_clock::now();
                w->process (L.data(), R.data(), BLOCK, tr, bp, nullptr, 0, mo, nullptr);
                const double took = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                if (took > longest.load()) longest = took;
                for (int i = 0; i < BLOCK; ++i) if (! std::isfinite (L[(size_t) i]) || std::fabs (L[(size_t) i]) > 1.0f) bad = true;
                ppq += in.bpm / 60.0 * BLOCK / SR;
                ++blocks;
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
        for (int i = 0; i < 24; ++i)
        {
            auto c = std::make_unique<RenderInputs> (in);
            c->project.matrix.oneShot = makeOneShot (1 + (i % 32), c->scenes[0]);
            if (i % 3 == 0) c->project.padFx[(size_t) (i % 16)].inserts[0] = CellFx::library ((MatrixFx) (i % 24));
            if (i % 4 == 0) { c->project.bd[0].on = true; c->project.bd[0].src = { SourceKind::Pad, i % 16 }; }
            c->project.matrix.seq = makeSeq (1 + i % 31);
            if (i % 5 == 0) c->project.padFx[(size_t) ((i + 3) % 16)].inserts[1] = CellFx::friction (StretchParams());
            w->publishLive (buildLiveState (c->project));
            w->render().submit (std::move (c));
            std::this_thread::sleep_for (std::chrono::milliseconds (60));
        }
        for (int i = 0; i < 300 && w->render().busy(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (100));
        stop = true; audio.join();
        w->render().stop();
        std::printf ("     %d blocks played during the edits, slowest block %.1f ms, %zu replaced states waiting\n",
                     blocks.load(), longest.load(), w->publisher.pending());
        CHECK (! bad.load(), "the audio stayed finite and under full scale");
        // the audio thread never waits on the worker (a lock or a render on the audio thread would take
        // hundreds of ms); the limit is loose so it also holds under the sanitizers, which run 5-15x slower
        CHECK (blocks.load() > 20 && longest.load() < 150.0, "the audio thread kept running, never waiting on the worker");

        // a new sample rate, the way the plugin does it (prepare while the worker runs, then restart it):
        // the worker renders the last inputs again for the new rate, with no new edit
        w->prepare (96000.0f, BLOCK);
        w->startRenderer();
        bool rerendered = false;
        for (int i = 0; i < 1200 && ! rerendered; ++i)   // up to 60 s (sanitizer builds are slow)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            const RtState* st = w->publisher.peek();
            rerendered = st != nullptr && st->sr == 96000.0f && ! w->render().busy();
        }
        CHECK (rerendered, "after a sample-rate change everything renders again at the new rate");
        w->render().stop();
        w->prepare (SR, BLOCK);
    }

    std::printf ("\n%s: %d failure(s), %.1f s\n", failures == 0 ? "PASS" : "FAIL", failures, ms() / 1000.0);
    return failures == 0 ? 0 : 1;
}
