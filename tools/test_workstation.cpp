// NeoBeat 2.0 — engine tests: every feature of the workstation, offline, with checks.
//   test_workstation [outdir]     (writes a few WAVs to outdir when given)
#include "../Source/engine/Factory.h"
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
    in.pattern = p.pattern; in.bpm = p.tempo; in.swing = p.swing;
    const auto& lib = w.kit().lib();
    for (int pad = 0; pad < kNumPads; ++pad) in.pads[(size_t) pad] = kitPadSettings (lib, 0, pad);
    return in;
}

static void applyPads (Workstation& w, const RenderInputs& in) { for (int p = 0; p < kNumPads; ++p) w.kit().pads[(size_t) p] = in.pads[(size_t) p]; }

// play `seconds` with the host transport running from ppq 0
static Out play (Workstation& w, const RenderInputs& in, double seconds, std::vector<MidiIn> midi = {}, int mode = 0, bool hostPlaying = true)
{
    Out o; const int total = (int) (seconds * SR);
    o.l.assign ((size_t) total, 0.0f); o.r.assign ((size_t) total, 0.0f);
    std::vector<MidiOut> mo; mo.reserve (256);
    BlockParams bp; bp.mode = mode; bp.swing = in.swing;
    double ppq = 0.0;
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        const int n = std::min (BLOCK, total - pos);
        std::vector<MidiIn> here;
        for (const auto& m : midi) if (m.offset >= pos && m.offset < pos + n) { MidiIn x = m; x.offset -= pos; here.push_back (x); }
        Transport tr; tr.bpm = in.bpm; tr.hostPlaying = hostPlaying; tr.hostPpq = ppq; tr.hostHasPpq = true;
        mo.clear();
        applyPads (w, in);
        w.process (o.l.data() + pos, o.r.data() + pos, n, tr, bp, here.data(), (int) here.size(), mo, nullptr);
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
    w->setPattern (in.pattern);
    w->render().renderNow (in, SR);
    Out base = play (*w, in, 4.0);
    CHECK (finite (base), "finite");
    CHECK (peak (base) > 0.05f && peak (base) < 1.0f, "audible and under full scale");
    if (! outdir.empty()) writeWav (outdir + "/01_drum_map.wav", base);

    if (! threadsOnly) {
    std::printf ("2. One Shot cells: a Freezing cell on the last snare of bar 1\n");
    {
        RenderInputs c = in;
        c.project.matrix.oneShot = makeOneShot (1, c.pattern);   // Clap Space
        CHECK (! c.project.matrix.oneShot.cells.empty(), "the recipe found snare hits");
        const double t = ms();
        w->render().renderNow (c, SR);
        std::printf ("     rendered %d cells in %.0f ms\n", (int) c.project.matrix.oneShot.cells.size(), ms() - t);
        const RtState* st = w->publisher.peek();
        bool all = true; for (const auto& rc : st->cells) all &= (bool) rc.audio && rc.audio->size() > 0;
        CHECK (all, "every cell has audio");
        Out o = play (*w, c, 4.0);
        CHECK (finite (o), "finite");
        CHECK (diff (o, base) > 1.0e-4, "the cells change the sound");
        CHECK (peak (o) < 1.0f, "under full scale");
        if (! outdir.empty()) writeWav (outdir + "/02_one_shot_clap_space.wav", o);
        // every factory recipe renders
        int ok = 0;
        for (int i = 1; i <= 32; ++i)
        {
            RenderInputs d = in; d.project.matrix.oneShot = makeOneShot (i, d.pattern);
            w->render().renderNow (d, SR);
            const RtState* s2 = w->publisher.peek();
            bool good = ! s2->cells.empty();
            for (const auto& rc : s2->cells)
            {
                good &= rc.audio != nullptr;
                if (rc.audio) for (size_t k = 0; k < rc.audio->l.size(); k += 7) good &= std::isfinite (rc.audio->l[k]) && std::fabs (rc.audio->l[k]) < 4.0f;
            }
            ok += good ? 1 : 0;
            if (! good) std::printf ("     recipe %d (%s) failed\n", i, oneShotRecipes()[(size_t) i - 1].name);
        }
        std::printf ("     all recipes: %d / 32 (%.0f ms so far)\n", ok, ms());
        CHECK (ok == 32, "all 32 One Shot recipes render finite audio");
    }

    std::printf ("3. pad inserts: Comb Filterbank and Chorus on the snare\n");
    {
        RenderInputs c = in;
        c.project.padFx[role::Snare].inserts[0] = CellFx::library (MatrixFx::CombFilterbank);
        c.project.padFx[role::Snare].inserts[1] = CellFx::library (MatrixFx::Chorus);
        w->render().renderNow (c, SR);
        const RtState* st = w->publisher.peek();
        CHECK (st->hasInserts[role::Snare] && st->insertAudio[role::Snare][2] && st->insertAudio[role::Snare][0], "three velocity layers rendered");
        Out o = play (*w, c, 3.0);
        CHECK (finite (o) && diff (o, base) > 1.0e-4, "the inserts are heard");
    }

    std::printf ("4. windows: a Friction Stretch slot on the snare (replace) and a Bounce Delay slot on the kick\n");
    {
        RenderInputs c = in;
        c.project.fs[0].on = true; c.project.fs[0].src = { SourceKind::Pad, role::Snare }; c.project.fs[0].p.stretch = 8.0f;
        c.project.bd[0].on = true; c.project.bd[0].src = { SourceKind::Pad, role::Kick };
        const double t = ms();
        w->render().renderNow (c, SR);
        std::printf ("     rendered in %.0f ms\n", ms() - t);
        const RtState* st = w->publisher.peek();
        int fsOk = 0, bdOk = 0;
        for (const auto& s : st->slots) { if (s.fs && s.audio[0] && s.audio[1]) ++fsOk; if (! s.fs && s.audio[0] && s.audio[1]) ++bdOk; }
        CHECK (fsOk == 1 && bdOk == 1, "both slots rendered for even and odd steps");
        CHECK (st->padSlots[role::Snare].size() == 1 && st->padSlots[role::Kick].size() == 1, "the pads know their slots");
        Out o = play (*w, c, 4.0);
        CHECK (finite (o) && peak (o) < 1.0f, "finite, under full scale");
        CHECK (diff (o, base) > 1.0e-4, "the windows are heard");
        if (! outdir.empty()) writeWav (outdir + "/04_windows.wav", o);
        // a 64x stretch on a slot (the extreme case)
        RenderInputs e = c; e.project.fs[0].p.stretch = 64.0f;
        const double t2 = ms();
        w->render().renderNow (e, SR);
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
        w->render().renderNow (c, SR);
        // A4 (root) plays 220 Hz, A5 plays 440 Hz
        Out a = play (*w, c, 0.6, { { 0, 0, 69, 0.9f } }, 2, false);
        Out b = play (*w, c, 0.6, { { 0, 0, 81, 0.9f } }, 2, false);
        const float pa = pitchOf (a, 2400, 12000), pb = pitchOf (b, 2400, 12000);
        std::printf ("     root %.0f Hz, octave up %.0f Hz\n", pa, pb);
        CHECK (std::fabs (pa - 220.0f) < 12.0f && std::fabs (pb - 440.0f) < 20.0f, "keytracking follows the key");
        // velocity layers: soft and hard zones on the snare's key, hard has an effect
        RenderInputs v = in; v.project.keymap = makeKeyMap (2);
        w->render().renderNow (v, SR);
        const RtState* st = w->publisher.peek();
        bool hardFx = false; for (const auto& rz : st->zones) if (rz.z.name == "Snare hard") hardFx = rz.fxAudio != nullptr;
        CHECK (hardFx, "the hard zone's effect rendered");
        Out soft = play (*w, v, 1.0, { { 0, 0, 38, 0.4f } }, 2, false);
        Out hard = play (*w, v, 1.0, { { 0, 0, 38, 1.0f } }, 2, false);
        CHECK (peak (soft) > 0.01f && peak (hard) > 0.01f, "both layers sound");
        // round robin: three zones on one key take turns
        RenderInputs rr = in; rr.project.keymap = makeKeyMap (4);
        w->render().renderNow (rr, SR);
        std::vector<MidiIn> notes; for (int i = 0; i < 6; ++i) notes.push_back ({ (int) (i * 0.25 * SR), 0, 40, 0.9f });
        Out o = play (*w, rr, 1.6, notes, 2, false);
        CHECK (finite (o) && peak (o) > 0.01f, "round robin plays");
        // a split preset with Friction Stretch low and Bounce Delay high
        RenderInputs sp = in; sp.project.keymap = makeKeyMap (1);
        w->render().renderNow (sp, SR);
        Out o2 = play (*w, sp, 3.0, { { 0, 0, 12, 0.9f }, { (int) (0.5 * SR), 0, 72, 0.9f }, { (int) SR, 0, 36, 0.9f } }, 2, false);
        CHECK (finite (o2) && peak (o2) > 0.01f && peak (o2) < 1.0f, "split zones play");
        if (! outdir.empty()) writeWav (outdir + "/05_split.wav", o2);
    }

    std::printf ("6. the grid: kit rows, swung columns, FS/BD tagged cells\n");
    {
        RenderInputs c = in;
        c.pattern = Pattern(); c.pattern.length = 16;   // the main pattern silent: only the grid plays
        auto& g = c.project.grid; g.play = true; g.useKit = true;
        auto& pat = g.patterns[0]; pat.length = 16; pat.rate = 1;
        for (int col = 0; col < 16; col += 2) pat.cells[(size_t) (role::Kick * 64 + col)] = GridPattern::pack (220, 128, 128, GridPattern::kOn);
        for (int col = 1; col < 16; col += 2) pat.cells[(size_t) (role::Hat * 64 + col)] = GridPattern::pack (160, 60, 150, GridPattern::kOn | GridPattern::kBd);
        c.project.bd[1].on = true; c.project.bd[1].src = { SourceKind::GridRow, role::Hat };
        w->setPattern (c.pattern);
        w->render().renderNow (c, SR);
        const RtState* st = w->publisher.peek();
        int rows = 0; for (const auto& s : st->gridSamples) rows += s ? 1 : 0;
        CHECK (rows == 16, "16 kit rows ready");
        CHECK (! st->rowSlots[role::Hat].empty(), "the hat row feeds the Bounce Delay slot");
        Out o = play (*w, c, 2.0);
        CHECK (finite (o) && peak (o) > 0.05f && peak (o) < 1.0f, "the grid plays");
        // onsets: the first kick at 0, the first hat (odd step) at 2 * 16th * swing
        const double s16 = 15.0 / c.bpm;
        const int hatAt = (int) (2.0 * s16 * c.swing * SR);
        CHECK (rms (o, 0, 2000) > 1.0e-3, "column 0 sounds at the start");
        CHECK (rms (o, hatAt, hatAt + 2000) > 1.0e-4, "column 1 lands on the swung 16th");
        if (! outdir.empty()) writeWav (outdir + "/06_grid.wav", o);
        w->setPattern (in.pattern);
    }

    std::printf ("7. the Sequencer view: every factory preset draws sound\n");
    {
        int ok = 0;
        for (int i = 1; i <= 31; ++i)
        {
            RenderInputs c = in; c.pattern = Pattern(); c.pattern.length = 16;
            c.project.matrix.seq = makeSeq (i);
            w->setPattern (c.pattern);
            w->render().renderNow (c, SR);
            Out o = play (*w, c, 3.0);
            const bool good = finite (o) && peak (o) > 0.002f && peak (o) < 1.0f;
            ok += good ? 1 : 0;
            if (! good) std::printf ("     %s: peak %.4f finite %d\n", seqNames()[(size_t) i].c_str(), peak (o), (int) finite (o));
            if (i == 1 && ! outdir.empty()) writeWav (outdir + "/07_seq_glass_sparkle_rain.wav", o);
            if (i == 2 && ! outdir.empty()) writeWav (outdir + "/07_seq_warble_drone.wav", o);
        }
        CHECK (ok == 31, "31 / 31 Sequencer presets play");
        w->setPattern (in.pattern);
    }

    std::printf ("8. the Sample view: boxes mangle pad 16's hit, height drives the main control\n");
    {
        int ok = 0;
        for (int i = 1; i <= 31; ++i)
        {
            RenderInputs c = in; c.project.matrix.sample = makeSample (i);
            w->render().renderNow (c, SR);
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

    std::printf ("9. modulation: LFO to level and pan, velocity to tone, algorithms\n");
    {
        RenderInputs c = in;
        c.project.mod.routes.push_back ({ ModSrc::Lfo1, "pad/*/level", -0.3f });
        c.project.mod.routes.push_back ({ ModSrc::Lfo2, "pad/4/pan", 0.8f });
        c.project.mod.routes.push_back ({ ModSrc::Velocity, "pad/2/tone", 0.5f });
        c.project.mod.routes.push_back ({ ModSrc::Random, "pad/0/tune", 0.1f });
        c.project.mod.routes.push_back ({ ModSrc::Lfo1, "master", 0.1f });
        for (const auto& a : algoPresets()) c.project.mod.algos[(size_t) a.pad] = a.algo;
        w->render().renderNow (c, SR);
        Out o = play (*w, c, 4.0);
        CHECK (finite (o) && peak (o) < 1.0f, "finite, under full scale");
        CHECK (diff (o, base) > 1.0e-4, "the modulation is heard");
        if (! outdir.empty()) writeWav (outdir + "/09_mod.wav", o);
        // a cell's main control from a route
        RenderInputs d = in; d.project.matrix.oneShot = makeOneShot (1, d.pattern);
        d.project.mod.routes.push_back ({ ModSrc::Step, "cell/0/main", 0.5f });
        w->render().renderNow (d, SR);
        CHECK (w->publisher.peek()->cells[0].audio != nullptr, "a modulated cell renders");
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
            w->setPattern (c.pattern);
            w->render().renderNow (c, SR);
            Out o = play (*w, c, 2.0);
            ok += (finite (o) && peak (o) > 0.01f && peak (o) < 1.0f) ? 1 : 0;
        }
        CHECK (ok == (numPatterns() + 8) / 9, "sampled patterns play cleanly");
        RenderInputs c = baseInputs (*w, 3);
        w->setPattern (c.pattern);
        w->render().renderNow (c, SR);
        Out k = play (*w, c, 2.0, { { 0, 0, 67, 0.9f } }, 1, false);   // key mode, G3 held: the pattern a fifth up
        CHECK (finite (k) && peak (k) > 0.01f, "key mode plays the pattern");
        w->setPattern (in.pattern);
    }

    }
    std::printf ("11. threads: the worker renders while the audio thread plays and edits keep coming\n");
    {
        w->render().renderNow (in, SR);
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
            c->project.matrix.oneShot = makeOneShot (1 + (i % 32), c->pattern);
            if (i % 3 == 0) c->project.padFx[(size_t) (i % 16)].inserts[0] = CellFx::library ((MatrixFx) (i % 24));
            if (i % 4 == 0) { c->project.bd[0].on = true; c->project.bd[0].src = { SourceKind::Pad, i % 16 }; }
            c->project.matrix.seq = makeSeq (1 + i % 31);
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
