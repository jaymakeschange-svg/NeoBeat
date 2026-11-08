// NeoBeat effects previews, offline: Friction Stretch and Bounce Delay on pads' own hits.
//
//   fx_demo hit <kit> <pad> <out.wav> [bpm=120 vel=1 key=value ...]
//       the dry hit on beat 1, then the processed hit on the next bar
//   fx_demo pattern <preset> <kit> <out.wav> [bars=4 fs.pads=2,3 bd.pads=9 key=value ...]
//       a preset with pads mapped to the effects (fs = Friction Stretch replaces the dry hit,
//       bd = Bounce Delay adds bounces; a pad in both is stretched, then bounced)
//
// Friction Stretch keys: fs.rate=1/16 (1/8d dotted, 1/16t triplet, or ms:125) fs.swing=0.62 fs.stretch=4
//   fs.creep=0 fs.fade=0 fs.snap=0 fs.slip=2 (ms) fs.grit=0 fs.tape=0 fs.attack=15 (ms) fs.wet=1
//   fs.line=lo:hi[:offset_s[:tilt_s[:spray[:level[:pan]]]]]   (repeat for up to six lines)
// Bounce Delay keys: bd.rate=1/4 (or ms:400) bd.grid=1/16 (or ms:60) bd.swing=0.62 bd.bounce=0.78
//   bd.start=1.5 bd.end=10 (m) bd.speed=0.3 bd.pan=0.5 bd.center=0 bd.width=1 bd.room=0.25 bd.wet=1 bd.seed=1
#include "../Source/engine/Engine.h"
#include "../Source/engine/Presets.h"
#include "../Source/engine/Kits.h"
#include "../Source/dsp/FrictionStretch.h"
#include "../Source/dsp/BounceDelay.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

static constexpr float kFs = 48000.0f;

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) { std::fprintf (stderr, "cannot write %s\n", path.c_str()); return; }
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 8;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f); std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3); u16 (2);
    u32 ((uint32_t) kFs); u32 ((uint32_t) kFs * 8); u16 (8); u16 (32); std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

// "1/16" "1/8d" "1/16t" "ms:125"
static bool parseRate (const std::string& s, nb::RateSpec& r, double& beats, double& ms)
{
    if (s.rfind ("ms:", 0) == 0) { r.tempo = false; ms = std::atof (s.c_str() + 3); return true; }
    const size_t sl = s.find ('/'); if (sl == std::string::npos) return false;
    const double num = std::atof (s.substr (0, sl).c_str()), den = std::atof (s.substr (sl + 1).c_str());
    double b = 4.0 * num / std::max (den, 1.0);
    if (s.back() == 'd') b *= 1.5; else if (s.back() == 't') b *= 2.0 / 3.0;
    r.tempo = true; beats = b; return true;
}

struct Setup
{
    nb::StretchParams fs;
    nb::BounceParams bd;
    std::set<int> fsPads, bdPads, fsHits;   // fsHits: which of the mapped pad's hits (1-based) are stretched; empty = all
    double bpm = 120; float vel = 1.0f; int bars = 4;
    bool fsSwingSet = false, bdSwingSet = false;
    std::string planPath;   // bd.plan=<file>: the bounces of the (first) hit, for drawing
};

static bool parseKey (Setup& S, const std::string& k, const std::string& v)
{
    const float x = (float) std::atof (v.c_str());
    auto pads = [&] (std::set<int>& out) { size_t p = 0; while (p < v.size()) { size_t q = v.find (',', p); if (q == std::string::npos) q = v.size(); out.insert (std::atoi (v.substr (p, q - p).c_str())); p = q + 1; } };
    if (k == "bpm") S.bpm = x; else if (k == "vel") S.vel = x; else if (k == "bars") S.bars = (int) x;
    else if (k == "fs.pads") pads (S.fsPads); else if (k == "bd.pads") pads (S.bdPads);
    else if (k == "fs.rate") return parseRate (v, S.fs.rate, S.fs.rate.beats, S.fs.rate.ms);
    else if (k == "fs.swing") { S.fs.rate.swing = x; S.fsSwingSet = true; } else if (k == "fs.stretch") S.fs.stretch = x; else if (k == "fs.creep") S.fs.creep = x;
    else if (k == "fs.slip") S.fs.slipMs = x;
    else if (k == "fs.peel") S.fs.peel = x; else if (k == "fs.freeze") S.fs.freeze = x; else if (k == "fs.screech") S.fs.screechHz = x;
    else if (k == "fs.adhesive") S.fs.adhesive = x; else if (k == "fs.grainms") S.fs.grainMs = x; else if (k == "fs.rasp") S.fs.rasp = x; else if (k == "fs.roll") S.fs.roll = x;
    else if (k == "fs.rollbody") S.fs.rollBody = (int) x; else if (k == "fs.rollsize") S.fs.rollSize = x; else if (k == "fs.strip") S.fs.strip = x;
    else if (k == "fs.width") S.fs.width = x; else if (k == "fs.decay") S.fs.decayMs = x;
    else if (k == "fs.gate") { if (v.rfind ("ms:", 0) == 0) S.fs.gateMs = std::atof (v.c_str() + 3); else { nb::RateSpec r; double ms = 0; parseRate (v, r, S.fs.gateBeats, ms); } }
    else if (k == "fs.hits") { size_t p = 0; while (p < v.size()) { size_t q = v.find (',', p); if (q == std::string::npos) q = v.size(); S.fsHits.insert (std::atoi (v.substr (p, q - p).c_str())); p = q + 1; } } else if (k == "fs.fade") S.fs.holdFade = x; else if (k == "fs.snap") S.fs.snap = x; else if (k == "fs.grit") S.fs.grit = x; else if (k == "fs.tape") S.fs.tape = x;
    else if (k == "fs.attack") S.fs.attackMs = x; else if (k == "fs.wet") S.fs.wet = x; else if (k == "fs.seed") S.fs.seed = (uint32_t) x;
    else if (k == "fs.line")
    {
        nb::StretchLine l; float a[7] = { 20, 20000, 0, 0, 0, 1, 0 }; int n = 0; size_t p = 0;
        while (p <= v.size() && n < 7) { size_t q = v.find (':', p); if (q == std::string::npos) q = v.size(); a[n++] = (float) std::atof (v.substr (p, q - p).c_str()); p = q + 1; }
        l.fLo = a[0]; l.fHi = a[1]; l.offset = a[2]; l.tilt = a[3]; l.spray = a[4]; l.level = a[5]; l.pan = a[6];
        S.fs.lines.push_back (l);
    }
    else if (k == "bd.rate") { double ms = S.bd.rate.ms; const bool ok = parseRate (v, S.bd.rate, S.bd.rate.beats, ms); S.bd.rate.ms = ms; return ok; }
    else if (k == "bd.grid") { nb::RateSpec r = S.bd.rate; return parseRate (v, r, S.bd.gridBeats, S.bd.gridMs); }
    else if (k == "bd.swing") { S.bd.rate.swing = x; S.bdSwingSet = true; } else if (k == "bd.bounce") S.bd.bounciness = x;
    else if (k == "bd.start") S.bd.distStart = x; else if (k == "bd.end") S.bd.distEnd = x; else if (k == "bd.speed") S.bd.speed = x;
    else if (k == "bd.pan") S.bd.panRandom = x; else if (k == "bd.center") S.bd.panCenter = x; else if (k == "bd.width") S.bd.width = x;
    else if (k == "bd.room") S.bd.room = x; else if (k == "bd.wet") S.bd.wet = x; else if (k == "bd.seed") S.bd.seed = (uint32_t) x;
    else if (k == "bd.max") S.bd.maxBounces = (int) x;
    else if (k == "bd.plan") S.planPath = v;
    else return false;
    return true;
}

// One pad's hit, alone, as it sounds in the kit (captured once per pad and velocity).
struct HitCache
{
    std::unique_ptr<nb::Engine> e;
    std::map<std::pair<int, int>, std::pair<std::vector<float>, std::vector<float>>> hits;
    explicit HitCache (int kit) : e (std::make_unique<nb::Engine>()) { e->prepare (kFs); nb::applyKit (*e, nb::kits()[(size_t) kit]); }
    const std::pair<std::vector<float>, std::vector<float>>& get (int pad, float vel)
    {
        const auto key = std::make_pair (pad, (int) std::lround (vel * 127.0f));
        auto it = hits.find (key);
        if (it != hits.end()) return it->second;
        // let anything still ringing die first
        std::vector<float> l (512), r (512);
        for (int g = 0; g < 2000 && e->activeVoices() > 0; ++g) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); e->render (l.data(), r.data(), 512); }
        e->trigger (pad, vel);
        const int n = (int) (4.0f * kFs);
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
        e->render (L.data(), R.data(), n);
        // trim the silent end (keeps 50 ms after it falls below -80 dB of its peak)
        float pk = 1e-9f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i])));
        int end = n;
        while (end > 1 && std::max (std::fabs (L[(size_t) end - 1]), std::fabs (R[(size_t) end - 1])) < pk * 1e-4f) --end;
        end = std::min (n, end + (int) (0.05f * kFs));
        L.resize ((size_t) end); R.resize ((size_t) end);
        return hits[key] = { L, R };
    }
};

struct Mixer
{
    std::vector<float> L, R, room;
    nb::RoomBus rb;
    explicit Mixer (size_t n) : L (n, 0.0f), R (n, 0.0f), room (n, 0.0f) { rb.configure (kFs, nb::placePreset (1)); }
    void add (const std::vector<float>& a, const std::vector<float>& b, double t)
    {
        const int64_t s = (int64_t) std::llround (t * kFs);
        for (size_t i = 0; i < a.size() && s + (int64_t) i < (int64_t) L.size(); ++i) { L[(size_t) (s + (int64_t) i)] += a[i]; R[(size_t) (s + (int64_t) i)] += b[i]; }
    }
    void finish()
    {
        for (size_t i = 0; i < L.size(); ++i) { float wl, wr; rb.process (room[i], wl, wr); L[i] = nb::Engine::safety (L[i] + wl); R[i] = nb::Engine::safety (R[i] + wr); }
    }
};

// stretch and/or bounce one hit at hitTime (seconds) on a song whose bar starts at 0
static const nb::Engine2Material* gRoll = nullptr;
static void processHit (Setup& S, const nb::FrictionMaterial& rubber, const std::vector<float>& hl, const std::vector<float>& hr,
                        double hitTime, bool doStretch, bool doBounce, bool keepDry, Mixer& mx, uint32_t seed)
{
    std::vector<float> l = hl, r = hr;
    if (doStretch)
    {
        nb::FrictionStretch fx; std::vector<float> ol, orr;
        nb::StretchParams p = S.fs; p.seed = S.fs.seed + seed;
        fx.render (p, rubber, S.bpm, hitTime, 0.0, hl.data(), hr.data(), (int) hl.size(), kFs, ol, orr, gRoll);
        l.swap (ol); r.swap (orr);
    }
    if (keepDry || doStretch) mx.add (l, r, hitTime);
    if (doBounce)
    {
        nb::BounceDelay bd; nb::BounceParams p = S.bd; p.seed = S.bd.seed + seed * 7919u;
        const auto plan = bd.plan (p, S.bpm, hitTime, 0.0);
        if (! S.planPath.empty())
        {
            if (FILE* f = std::fopen (S.planPath.c_str(), "w"))
            {
                std::fprintf (f, "# t_after_hit dist pan gain width\n");
                for (const auto& b : plan) std::fprintf (f, "%.5f %.3f %.3f %.5f %.3f\n", b.t - hitTime, b.dist, b.pan, b.gain, b.width);
                std::fclose (f);
            }
            S.planPath.clear();
        }
        bd.render (p, plan, hitTime, l.data(), r.data(), (int) l.size(), kFs, mx.L.data(), mx.R.data(), mx.room.data(), (int) mx.L.size());
    }
}

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "fx_demo hit <kit> <pad> <out.wav> ... | fx_demo pattern <preset> <kit> <out.wav> ...\n"); return 1; }
    const std::string mode = argv[1];
    Setup S;
    for (int a = 5; a < argc; ++a)
    {
        const char* eq = std::strchr (argv[a], '='); if (! eq) continue;
        if (! parseKey (S, std::string (argv[a], (size_t) (eq - argv[a])), eq + 1)) { std::fprintf (stderr, "bad key %s\n", argv[a]); return 1; }
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (mode == "hit")
    {
        const int kit = std::atoi (argv[2]), pad = std::atoi (argv[3]);
        HitCache cache (kit);
        const auto& rubber = cache.e->lib().tables.rubbers[0];
        { const auto& bodies = cache.e->lib().tables.bodies; gRoll = bodies.empty() ? nullptr : &bodies[(size_t) std::min ((int) bodies.size() - 1, std::max (0, S.fs.rollBody))]; }
        const auto& h = cache.get (pad, S.vel);
        const double bar = 4.0 * 60.0 / S.bpm;
        const bool st = ! S.fs.lines.empty() || S.fsPads.count (pad) || S.fsPads.empty() == false;
        (void) st;
        const bool doStretch = S.fsPads.count (pad) > 0, doBounce = S.bdPads.count (pad) > 0;
        // how long: the stretched hit and the bounces
        double len = bar + (double) h.first.size() / kFs * (doStretch ? std::max (1.0f, S.fs.stretch) : 1.0) + 0.5;
        if (doStretch && (S.fs.gateBeats > 0 || S.fs.gateMs > 0)) len = std::min (len, bar + (S.fs.gateBeats > 0 ? S.fs.gateBeats * 60.0 / S.bpm : S.fs.gateMs / 1000.0) + S.fs.decayMs / 1000.0 + 1.0);
        if (doBounce) { nb::BounceDelay bd; const auto pl = bd.plan (S.bd, S.bpm, bar, 0.0); if (! pl.empty()) len = std::max (len, pl.back().t + S.bd.tail + 2.0); }
        Mixer mx ((size_t) ((len + 1.0) * kFs));
        mx.add (h.first, h.second, 0.0);                       // the dry hit
        processHit (S, rubber, h.first, h.second, bar, doStretch, doBounce, true, mx, 1);
        mx.finish();
        // trim the silent end
        size_t end = mx.L.size(); while (end > 1 && std::fabs (mx.L[end - 1]) + std::fabs (mx.R[end - 1]) < 1e-6f) --end;
        mx.L.resize (std::min (mx.L.size(), end + (size_t) (0.2f * kFs))); mx.R.resize (mx.L.size());
        writeWav (argv[4], mx.L, mx.R);
    }
    else if (mode == "pattern")
    {
        const auto& pr = nb::presets()[(size_t) std::atoi (argv[2])];
        const int kit = std::atoi (argv[3]);
        S.bpm = pr.tempo;
        if (! S.fsSwingSet) S.fs.rate.swing = pr.swing;   // the effects swing with the pattern
        if (! S.bdSwingSet) S.bd.rate.swing = pr.swing;
        auto e = std::make_unique<nb::Engine>(); e->prepare (kFs); nb::applyKit (*e, nb::kits()[(size_t) kit]);
        HitCache cache (kit);
        const auto& rubber = cache.e->lib().tables.rubbers[0];
        { const auto& bodies = cache.e->lib().tables.bodies; gRoll = bodies.empty() ? nullptr : &bodies[(size_t) std::min ((int) bodies.size() - 1, std::max (0, S.fs.rollBody))]; }
        nb::Sequencer seq; seq.prepare (kFs); seq.setPattern (pr.pattern);
        for (int pad = 0; pad < nb::Engine::kPads; ++pad) seq.setPreroll (pad, e->prerollSeconds (pad));
        const double ppqLen = 4.0 * S.bars, secs = ppqLen * 60.0 / S.bpm;
        const size_t n = (size_t) ((secs + 8.0) * kFs);
        Mixer mx (n);
        std::vector<nb::SeqEvent> ev; ev.reserve (512);
        struct Hit { double t; int pad; float vel; };
        std::vector<Hit> mapped;
        for (size_t pos = 0; pos < n; pos += 256)
        {
            const int len = (int) std::min<size_t> (256, n - pos);
            const double ppq0 = (double) pos / kFs * S.bpm / 60.0;
            ev.clear();
            seq.collect (ppq0, S.bpm, len, ppq0 < ppqLen, pr.swing, 0.0f, ev);
            int done = 0;
            for (const auto& x : ev)
            {
                if (x.offset > done) { e->render (mx.L.data() + pos + done, mx.R.data() + pos + done, x.offset - done); done = x.offset; }
                const bool fsP = S.fsPads.count (x.pad) > 0, bdP = S.bdPads.count (x.pad) > 0;
                if (fsP || bdP) mapped.push_back ({ (double) (pos + (size_t) x.offset) / kFs, x.pad, x.velocity });
                if (! fsP) e->trigger (x.pad, x.velocity);
            }
            if (len > done) e->render (mx.L.data() + pos + done, mx.R.data() + pos + done, len - done);
        }
        uint32_t k = 0;
        std::map<int, int> count;
        for (const auto& h : mapped)
        {
            const auto& hit = cache.get (h.pad, h.vel);
            const int nth = ++count[h.pad];
            const bool st = S.fsPads.count (h.pad) > 0 && (S.fsHits.empty() || S.fsHits.count (nth) > 0);
            const bool dryHere = S.fsPads.count (h.pad) > 0 && ! st;     // an unstretched hit of a stretch pad plays dry
            if (dryHere) mx.add (hit.first, hit.second, h.t);
            processHit (S, rubber, hit.first, hit.second, h.t, st, S.bdPads.count (h.pad) > 0, false, mx, ++k);
        }
        mx.finish();
        size_t end = mx.L.size(); while (end > 1 && std::fabs (mx.L[end - 1]) + std::fabs (mx.R[end - 1]) < 1e-6f) --end;
        mx.L.resize (std::min (mx.L.size(), end + (size_t) (0.2f * kFs))); mx.R.resize (mx.L.size());
        writeWav (argv[4], mx.L, mx.R);
        std::printf ("%s: %zu hits through the effects\n", pr.name.c_str(), mapped.size());
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("rendered in %.1f s\n", secs);
    return 0;
}
