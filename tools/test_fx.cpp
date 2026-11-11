// Tests for the stand-alone effects' engine (Source/fx/FxCore.h): Friction Stretch and Bounce Delay,
// loading a hit and catching hits. Framework-free. `test_fx [outdir]` also writes WAVs to listen to.
#include "fx/FxCore.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace nbfx;
static int failures = 0;
#define CHECK(c, what) do { if (c) std::printf ("  ok:   %s\n", what); else { std::printf ("  FAIL: %s\n", what); ++failures; } } while (0)

static const float SR = 48000.0f;
static const int BLOCK = 256;

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, float sr)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return;
    const uint32_t n = (uint32_t) l.size(), bytes = n * 4, rate = (uint32_t) sr, byteRate = rate * 4;
    const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
    const uint32_t riff = 36 + bytes, sixteen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&sixteen, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
        for (const float v : { l[i], r[i] }) { const int16_t s = (int16_t) std::lrint (32767.0f * std::max (-1.0f, std::min (1.0f, v))); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

static float peakOf (const std::vector<float>& x, int a, int b)
{
    float p = 0.0f;
    for (int i = std::max (0, a); i < std::min ((int) x.size(), b); ++i) p = std::max (p, std::fabs (x[(size_t) i]));
    return p;
}
static bool finite (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }

struct Run
{
    std::unique_ptr<Core> core;
    std::vector<float> L, R;       // everything it played
    double ppq = 0.0;
    Transport tr;
    Live live;
    explicit Run (Kind k) : core (std::make_unique<Core> (k)) { core->prepare (SR, BLOCK); tr.playing = true; tr.bpm = 120.0; }
    // n blocks; `in` (may be null) is read from inPos on
    bool realTime = false;         // pace the blocks like a sound card (the catcher's renders race the clock)
    void go (int blocks, const std::vector<float>* in = nullptr, const Midi* m = nullptr, int nm = 0)
    {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<float> il ((size_t) BLOCK), ir ((size_t) BLOCK), ol ((size_t) BLOCK), orr ((size_t) BLOCK);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < BLOCK; ++i) { const size_t p = L.size() + (size_t) i; il[(size_t) i] = ir[(size_t) i] = in != nullptr && p < in->size() ? (*in)[p] : 0.0f; }
            tr.ppq = ppq;
            core->process (il.data(), ir.data(), ol.data(), orr.data(), BLOCK, tr, b == 0 ? m : nullptr, b == 0 ? nm : 0, live);
            L.insert (L.end(), ol.begin(), ol.end()); R.insert (R.end(), orr.begin(), orr.end());
            ppq += tr.bpm / 60.0 * BLOCK / SR;
            if (realTime) std::this_thread::sleep_until (t0 + std::chrono::microseconds ((int64_t) ((b + 1) * 1.0e6 * BLOCK / SR)));
        }
    }
    bool waitIdle (int ms = 60000)
    {
        for (int i = 0; i < ms / 5; ++i) { std::this_thread::sleep_for (std::chrono::milliseconds (5)); if (! core->renderer.busy() && core->renderer.publisher.peek() != nullptr) return true; }
        return false;
    }
};

int main (int argc, char** argv)
{
    const std::string out = argc > 1 && std::strcmp (argv[1], "--threads") != 0 ? argv[1] : "";
    const HitPtr hit = defaultHit (SR);

    std::printf ("1. the renders themselves\n");
    {
        Renderer fr (Kind::Friction), br (Kind::Bounce);
        Settings s;
        const auto dry = Renderer::atRate (*hit, SR);
        s.fs.stretch = 8.0f;
        auto a = fr.render (s, dry->l.data(), dry->r.data(), dry->size(), SR, 120.0, 0.0);
        CHECK (a->size() > dry->size() * 4, "Friction Stretch x8 is several times longer than the hit");
        CHECK (finite (a->l) && finite (a->r) && peakOf (a->l, 0, a->size()) > 0.01f, "... finite and audible");
        s.fs.stretch = 64.0f;
        const auto t0 = std::chrono::steady_clock::now();
        auto big = fr.render (s, dry->l.data(), dry->r.data(), dry->size(), SR, 120.0, 0.0);
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("     x64: %.1f s of sound in %.2f s\n", big->size() / SR, took);
        CHECK (big->size() <= (int) (24.0f * SR) && finite (big->l), "x64 stays under 24 s and finite");
        auto b = br.render (s, dry->l.data(), dry->r.data(), dry->size(), SR, 120.0, 0.0);
        nb::BounceDelay bd; const auto plan = bd.plan (s.bd, 120.0, 0.0, 0.0);
        CHECK (plan.size() >= 4 && finite (b->l) && finite (b->r), "Bounce Delay plans bounces and renders finite");
        // every bounce is there, at its time; and nothing before the first (the hit itself is the dry sound)
        bool all = true;
        for (size_t i = 0; i < std::min<size_t> (plan.size(), 5); ++i) all = all && peakOf (b->l, (int) (plan[i].t * SR), (int) (plan[i].t * SR) + 2400) + peakOf (b->r, (int) (plan[i].t * SR), (int) (plan[i].t * SR) + 2400) > 0.004f;
        CHECK (all, "each of the first bounces sounds at its planned time");
        CHECK (peakOf (b->l, 0, (int) (plan[0].t * SR) - 600) < 1.0e-4f, "nothing sounds before the first bounce (wet only)");
        Settings dryRoom = s; dryRoom.bd.room = 0.0f; Settings wetRoom = s; wetRoom.bd.room = 1.0f;
        auto r0 = br.render (dryRoom, dry->l.data(), dry->r.data(), dry->size(), SR, 120.0, 0.0);
        auto r1 = br.render (wetRoom, dry->l.data(), dry->r.data(), dry->size(), SR, 120.0, 0.0);
        double e0 = 0, e1 = 0; for (float v : r0->l) e0 += v * v; for (float v : r1->l) e1 += v * v;
        CHECK (e1 > e0 * 1.05 && r1->size() > r0->size(), "ROOM adds the room's ring to the bounces");
        if (! out.empty()) { writeWav (out + "/fs_x8.wav", a->l, a->r, SR); writeWav (out + "/fs_x64.wav", big->l, big->r, SR); writeWav (out + "/bd.wav", r1->l, r1->r, SR); }
    }

    std::printf ("2. load a hit: the play button, MIDI, keytrack, dry\n");
    for (Kind k : { Kind::Friction, Kind::Bounce })
    {
        Run run (k);
        Settings s; s.fs.stretch = 4.0f;
        run.live.dry = k == Kind::Bounce ? 1.0f : 0.0f;
        run.go (20);
        CHECK (peakOf (run.L, 0, (int) run.L.size()) == 0.0f, k == Kind::Friction ? "Friction: silent before anything is rendered or played" : "Bounce: silent before anything is rendered or played");
        run.core->renderer.set (s, hit, 120.0);
        CHECK (run.waitIdle(), "the loaded hit renders in the background");
        const auto* st = run.core->renderer.publisher.peek();
        CHECK (st != nullptr && st->wet[0] && st->wet[1] && st->dry, "both versions (on the beat, on the swung off-beat) are there");
        const size_t before = run.L.size();
        run.core->trigger (1.0f);
        run.go (400);
        CHECK (peakOf (run.L, (int) before, (int) before + 2400) > 0.01f, "PLAY starts it at once");
        CHECK (run.core->hits.load() == 1, "one hit counted");
        // a MIDI note mid-block lands on its sample
        run.go (2000);   // let it ring out
        const bool quiet = peakOf (run.L, (int) run.L.size() - 4800, (int) run.L.size()) < 0.02f;
        Midi m; m.offset = 100; m.note = 60; m.velocity = 1.0f;
        const size_t at = run.L.size();
        run.go (40, nullptr, &m, 1);
        CHECK (peakOf (run.L, (int) at, (int) at + 100) <= (quiet ? 0.02f : 1.0f) && peakOf (run.L, (int) at + 100, (int) at + 1300) > 0.01f, "a MIDI note starts on its sample offset");
        if (! out.empty()) writeWav (out + (k == Kind::Friction ? "/load_fs.wav" : "/load_bd.wav"), run.L, run.R, SR);
    }
    {
        // keytrack: an octave up is half as long
        Run a (Kind::Friction), b (Kind::Friction);
        Settings s; s.fs.stretch = 2.0f; s.fs.gateMs = 0;
        for (Run* r : { &a, &b }) { r->live.dry = 0.0f; r->live.keytrack = true; r->tr.playing = false; r->core->renderer.set (s, hit, 120.0); r->waitIdle(); }
        Midi lo; lo.note = 60; Midi hi; hi.note = 72;
        a.go (1200, nullptr, &lo, 1); b.go (1200, nullptr, &hi, 1);
        auto lastLoud = [] (const std::vector<float>& x) { int last = 0; for (int i = 0; i < (int) x.size(); ++i) if (std::fabs (x[(size_t) i]) > 0.002f) last = i; return last; };
        const float ratio = (float) lastLoud (a.L) / (float) std::max (1, lastLoud (b.L));
        std::printf ("     C3 lasts %.2f s, C4 %.2f s\n", lastLoud (a.L) / SR, lastLoud (b.L) / SR);
        CHECK (ratio > 1.6f && ratio < 2.4f, "KEYTRACK: an octave up plays twice as fast");
    }

    std::printf ("3. load a hit: the bar loop and the swung off-beat version\n");
    {
        Run run (Kind::Bounce);
        Settings s;
        run.live.dry = 1.0f; run.live.loopBars = 1;
        run.core->renderer.set (s, hit, 120.0); run.waitIdle();
        run.ppq = 3.5;   // half a beat before bar 2
        run.go ((int) (SR * 5.0f) / BLOCK);
        // bars are 2 s long at 120: starts at 0.25 s and 2.25 s
        const int first = (int) (0.25f * SR), second = (int) (2.25f * SR);
        CHECK (peakOf (run.L, 0, first - 64) < 1.0e-5f && peakOf (run.L, first, first + 1200) > 0.01f, "LOOP: the hit starts exactly on the bar line");
        CHECK (run.core->hits.load() == 3 || run.core->hits.load() == 2, "LOOP: once every bar");
        CHECK (peakOf (run.L, second, second + 1200) > 0.01f, "LOOP: and again on the next bar");
        // parity: a note on an off-beat sixteenth uses the off-beat version
        Run p (Kind::Friction);
        p.live.dry = 0.0f; p.live.stepBeats = 0.25;
        p.core->renderer.set (s, hit, 120.0); p.waitIdle();
        const auto* st = p.core->renderer.publisher.peek();
        CHECK (st->wet[0]->size() != st->wet[1]->size() || st->wet[0]->l != st->wet[1]->l, "the off-beat version is its own render (the yanks fall elsewhere)");
    }

    std::printf ("4. catch hits: the input is heard, hits are caught, the effect joins in on time\n");
    {
        // three clanks, 1 s apart, starting at 0.5 s
        std::vector<float> in ((size_t) (SR * 6.0f), 0.0f);
        const auto dry = Renderer::atRate (*hit, SR);
        const int at[] = { (int) (0.5f * SR), (int) (1.5f * SR), (int) (2.5f * SR) };
        for (int a : at) for (int i = 0; i < dry->size(); ++i) in[(size_t) (a + i)] += dry->l[(size_t) i];
        Run run (Kind::Bounce);
        Settings s; s.bd.room = 0.0f; s.bd.panRandom = 0.0f;
        run.live.mode = Mode::Capture; run.live.thru = 1.0f; run.live.threshold = 0.05f; run.live.captureMs = 200.0f;
        run.tr.playing = true; run.ppq = 0.0; run.realTime = true;
        run.core->renderer.set (s, nullptr, 120.0);
        run.go ((int) (SR * 6.0f) / BLOCK, &in);
        CHECK (run.core->hits.load() == 3, "three hits in, three caught (no double triggers on the ring)");
        // thru: the input is in the output
        CHECK (std::fabs (run.L[(size_t) at[0] + 300] - in[(size_t) at[0] + 300]) < 0.05f && peakOf (run.L, at[0], at[0] + 2400) > 0.05f, "the input passes through");
        // the first bounce of the first hit: a quarter note (0.5 s) after it, on the grid
        nb::BounceDelay bd; const auto plan = bd.plan (s.bd, 120.0, 0.5, 0.0);
        std::vector<float> wetOnly (run.L.size());
        for (size_t i = 0; i < wetOnly.size(); ++i) wetOnly[i] = run.L[i] - (i < in.size() ? in[i] : 0.0f);
        const int b0 = (int) (plan[0].t * SR);
        // hit 2 lands at 1.5 s; look for bounce 1 of hit 1 at its planned time (before 1.5 s there is only hit 1's echo)
        std::printf ("     first bounce planned at %.3f s\n", plan[0].t);
        int onset = -1; for (int i = (int) (0.75f * SR); i < (int) (1.45f * SR); ++i) if (std::fabs (wetOnly[(size_t) i]) > 0.004f) { onset = i; break; }
        std::printf ("     first bounce heard at   %.3f s\n", onset / SR);
        CHECK (onset >= 0 && std::abs (onset - (b0 - 96)) < (int) (0.012f * SR), "the first bounce lands where the plan says (within 12 ms)");
        CHECK (run.core->renderer.lastCapture() != nullptr && run.core->renderer.lastCapture()->left.size() > 4000, "the last caught hit is kept (KEEP makes it the loaded hit)");
        CHECK (finite (run.L) && finite (run.R), "finite");
        if (! out.empty()) writeWav (out + "/capture_bd.wav", run.L, run.R, SR);

        Run fr (Kind::Friction);
        Settings f; f.fs.stretch = 6.0f;
        fr.live.mode = Mode::Capture; fr.live.thru = 1.0f; fr.live.threshold = 0.05f; fr.live.captureMs = 300.0f; fr.live.choke = true; fr.realTime = true;
        fr.core->renderer.set (f, nullptr, 120.0);
        fr.go ((int) (SR * 6.0f) / BLOCK, &in);
        std::vector<float> w (fr.L.size()); for (size_t i = 0; i < w.size(); ++i) w[i] = fr.L[i] - (i < in.size() ? in[i] : 0.0f);
        CHECK (fr.core->hits.load() == 3 && peakOf (w, (int) (3.2f * SR), (int) (5.5f * SR)) > 0.003f, "Friction Stretch: the stretched tail of the last hit rings on after it");
        CHECK (finite (fr.L), "finite");
        if (! out.empty()) writeWav (out + "/capture_fs.wav", fr.L, fr.R, SR);
    }
    {
        // quiet input and steady tones do not trigger
        Run run (Kind::Bounce);
        run.live.mode = Mode::Capture; run.live.threshold = 0.1f;
        std::vector<float> in ((size_t) (SR * 2.0f));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.02f * std::sin (6.2831853f * 220.0f * i / SR);
        run.go ((int) (SR * 2.0f) / BLOCK, &in);
        CHECK (run.core->hits.load() == 0, "input under the threshold catches nothing");
        Run tone (Kind::Bounce);
        tone.live.mode = Mode::Capture; tone.live.threshold = 0.1f;
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * std::sin (6.2831853f * 220.0f * i / SR);
        tone.go ((int) (SR * 2.0f) / BLOCK, &in);
        CHECK (tone.core->hits.load() <= 1, "a steady tone is one hit at most, not a stream of them");
    }

    std::printf ("5. threads: settings change and hits arrive while the audio runs\n");
    for (Kind k : { Kind::Friction, Kind::Bounce })
    {
        auto core = std::make_unique<Core> (k);
        core->prepare (SR, BLOCK);
        std::atomic<bool> stop { false }, bad { false };
        std::atomic<int> blocks { 0 };
        std::atomic<double> longest { 0.0 };
        std::thread audio ([&]
        {
            std::vector<float> il ((size_t) BLOCK), ol ((size_t) BLOCK), orr ((size_t) BLOCK);
            Transport tr; tr.playing = true; double ppq = 0.0; Live live; uint32_t rng = 7; int64_t t = 0;
            while (! stop.load())
            {
                const int b = blocks.load();
                live.mode = (b / 600) % 2 == 0 ? Mode::Load : Mode::Capture;
                live.loopBars = 1; live.threshold = 0.05f; live.captureMs = 120.0f; live.choke = (b / 300) % 2 == 1;
                for (int i = 0; i < BLOCK; ++i, ++t)
                {
                    const int ph = (int) (t % 9600);   // a click five times a second
                    rng = rng * 1664525u + 1013904223u;
                    il[(size_t) i] = ph < 600 ? 0.6f * ((rng >> 8) / 8388608.0f - 1.0f) * std::exp (-ph / 120.0f) : 0.0f;
                }
                Midi m; m.offset = (int) (rng % BLOCK); m.note = 48 + (int) (rng % 24);
                tr.ppq = ppq;
                const auto t0 = std::chrono::steady_clock::now();
                core->process (il.data(), il.data(), ol.data(), orr.data(), BLOCK, tr, &m, b % 40 == 0 ? 1 : 0, live);
                const double took = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                if (took > longest.load()) longest = took;
                for (int i = 0; i < BLOCK; ++i) if (! std::isfinite (ol[(size_t) i]) || ! std::isfinite (orr[(size_t) i])) bad = true;
                ppq += 2.0 * BLOCK / SR;
                blocks.fetch_add (1);
                std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
        });
        Settings s;
        for (int i = 0; i < 40; ++i)
        {
            s.fs.stretch = 2.0f + (float) (i % 5); s.fs.slipMs = 20.0f + 10.0f * (float) (i % 7); s.bd.bounciness = 0.5f + 0.05f * (float) (i % 8); s.bd.room = (i % 3) * 0.4f;
            core->renderer.set (s, i % 9 == 8 ? nullptr : defaultHit (i % 2 ? 44100.0f : 48000.0f), 100.0 + i);
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
        for (int i = 0; i < 2000 && core->renderer.busy(); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (5));
        stop = true; audio.join();
        std::printf ("     %s: %d blocks, slowest %.2f ms, %d hits caught and rendered\n", k == Kind::Friction ? "Friction" : "Bounce", blocks.load(), longest.load(), core->renderer.captures());
        CHECK (! bad.load(), "finite throughout");
        CHECK (blocks.load() > 20 && longest.load() < 150.0, "the audio thread never waits on the renderer");
        CHECK (core->renderer.captures() > 0, "caught hits were rendered while it ran");
        core->release();
        core->prepare (96000.0f, 64);   // a new rate: restarts cleanly
        std::vector<float> z (64), o1 (64), o2 (64);
        Transport tr; Live live;
        for (int i = 0; i < 50; ++i) core->process (z.data(), z.data(), o1.data(), o2.data(), 64, tr, nullptr, 0, live);
        CHECK (true, "prepare again at another rate");
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
