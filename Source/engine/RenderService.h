// NeoBeat 2.0 — the render worker. Everything heavy is rendered ahead on its own thread and played
// back by the audio thread:
//   One Shot cells      the pad's hit at that step through the cell's effect (height = main control)
//   Pad inserts         the pad's hit through its 3 inserts, at three velocities
//   Window slots        Friction Stretch / Bounce Delay of a pad, grid row or cell (even and odd 16ths)
//   Zone effects        a key-map zone's effect on its sound
//   Sample view         the sample mangled box by box, each box acting on its time span and band
//   Grain source        the hit the Sequencer view's SMP engine takes grains from
// The editor submits the inputs whenever something changes; the worker renders what is missing (a
// content hash finds what is already done) and publishes a new RtState after each result, so edits
// are heard as soon as their render lands. Replaced audio is kept two minutes before it is freed.
#pragma once

#include "../shared/Bands.h"
#include "Engine.h"
#include "Kits.h"
#include "LiveState.h"
#include "Project.h"
#include "Sequencer.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <thread>
#include <unordered_map>

namespace nb
{
// ------------------------------------------------------------------ what the audio thread reads
struct RtCell { int pad = 0, step = 0, span = 2; float height = 0.5f; RenderedPtr audio; int scene = 0; uint32_t key = 0; };
struct RtSlot { bool fs = true; int slot = 0; SourceKind kind = SourceKind::None; int index = 0; float level = 1.0f; bool replace = false; std::array<RenderedPtr, 2> audio {}; };
struct RtZone { Zone z; std::shared_ptr<const SampleData> sample; RenderedPtr fxAudio; };

struct RtState
{
    float sr = 48000.0f;
    std::array<std::array<RenderedPtr, 3>, kNumPads> insertAudio {};
    std::array<bool, kNumPads> hasInserts {};
    std::array<std::shared_ptr<const SampleData>, kNumPads> padSample {};   // Sample view result
    std::array<float, kNumPads> padSampleRms {};
    bool oneShotOn = true;
    std::vector<RtCell> cells;
    std::vector<std::pair<uint32_t, int>> cellIndex;                         // (node key, index in cells), sorted: the prepared nodes
    std::array<std::array<RenderedPtr, 3>, kNumPads> padDry {};             // each lane's own hit at three strengths, for the live effects
    int cellAt (int scene, int pad, int step) const noexcept
    {
        const uint32_t k = nodeKey (scene, pad, step);
        size_t lo = 0, hi = cellIndex.size();
        while (lo < hi) { const size_t mid = (lo + hi) / 2; if (cellIndex[mid].first < k) lo = mid + 1; else hi = mid; }
        return lo < cellIndex.size() && cellIndex[lo].first == k ? cellIndex[lo].second : -1;
    }
    std::vector<RtSlot> slots;
    std::array<std::vector<int>, kNumPads> padSlots;
    std::array<std::vector<int>, GridPattern::kSize> rowSlots;
    std::vector<std::vector<int>> cellSlots;
    std::vector<ZoneLayer> layers;
    std::vector<RtZone> zones;
    std::vector<std::array<PadSettings, kNumPads>> kitPads;
    std::shared_ptr<const std::vector<GridPattern>> grid;
    std::vector<std::shared_ptr<const SampleData>> gridSamples;
    int gridCurrent = 0; bool gridPlay = false; float gridLevel = 0.8f;
    bool seqOn = true;
    SeqPreset seq;
    std::shared_ptr<const SampleData> grains;
    ModState mod;
    std::array<std::vector<ResolvedRoute>, kNumPads> padRoutes;
    std::vector<ResolvedRoute> seqRoutes, gridRoutes, masterRoutes;
    float glue = 0.4f;
};

// What the editor hands over.
struct RenderInputs
{
    Project project;
    std::array<PadSettings, kNumPads> pads {};
    // (on the heap: four scenes are 98 KB, and a RenderInputs on a Windows stack, whose threads get 1 MB, must stay small)
    std::vector<Pattern> scenes = std::vector<Pattern> ((size_t) kNumScenes);
    double bpm = 120.0;
    float swing = 0.62f;
    std::map<std::string, std::shared_ptr<const SampleData>> files;   // loaded by the editor
    // BANDS, so an export hears it: the bank as the window set it, and the ALL send
    bc::BandsState bands;
    float bandsAll = 0.0f;
};

// What the editor draws (the windows' sonograms and rooms, the Sample view's before/after).
struct DisplayData
{
    std::array<RenderedPtr, kNumSlots> fs {}, bd {};
    std::array<std::vector<Bounce>, kNumSlots> bdPlans {};
    std::vector<RenderedPtr> cells;
    RenderedPtr sampleIn, sampleOut;
};

inline PadSettings kitPadSettings (const Library& lib, int kit, int pad)
{
    PadSettings ps;
    const auto& ks = kits();
    if (ks.empty()) return ps;
    const auto& kp = ks[(size_t) std::max (0, std::min (kit, (int) ks.size() - 1))].pads[(size_t) pad];
    const int si = std::max (0, lib.find (kp.sound));
    ps.sound = si; ps.tune = kp.tune; ps.decay = kp.decay; ps.tone = kp.tone; ps.drive = kp.drive;
    ps.hollow = kp.hollow >= 0.0f ? kp.hollow : lib.sounds[(size_t) si].defaultHollow;
    ps.evolve = kp.evolve; ps.pan = kp.pan; ps.levelDb = kp.levelDb; ps.choke = kp.choke;
    return ps;
}

// Seconds from the bar's start to a 16th step, with swing (odd steps late).
inline double stepSeconds16 (int step, double bpm, float swing)
{
    const double s16 = 15.0 / std::max (1.0, bpm);
    return 2.0 * s16 * (step >> 1) + ((step & 1) ? 2.0 * s16 * swing : 0.0);
}

class RenderService
{
public:
    explicit RenderService (RtPublisher<RtState>& p) : pub (p) {}
    ~RenderService() { stop(); }

    void start (float sampleRate)
    {
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (worker.joinable() && ! quit && sampleRate == sr) return;   // already running at this rate
        }
        stop();
        {
            std::lock_guard<std::mutex> lock (mutex);
            // a new rate: everything renders again for it, from the last inputs (the editor only resubmits on edits)
            if (sampleRate != sr) { cache.clear(); sr = sampleRate; engineReady = false; if (current) dirty = true; }
            quit = false;
        }
        worker = std::thread ([this] { run(); });
    }

    void stop()
    {
        { std::lock_guard<std::mutex> lock (mutex); quit = true; }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void submit (std::unique_ptr<RenderInputs> in)
    {
        { std::lock_guard<std::mutex> lock (mutex); next = std::move (in); }
        cv.notify_all();
    }

    // Renders everything for these inputs on the calling thread (tests, the offline renderer).
    void renderNow (const RenderInputs& in, float sampleRate)
    {
        if (sampleRate != sr) { cache.clear(); sr = sampleRate; engineReady = false; }
        process (in, false);
    }

    int jobsTotal() const noexcept { return total.load(); }
    int jobsDone() const noexcept { return done.load(); }
    bool busy() const noexcept { return isBusy.load(); }
    std::string currentJob() const { std::lock_guard<std::mutex> lock (labelMutex); return label; }
    std::shared_ptr<const DisplayData> display() const { std::lock_guard<std::mutex> lock (labelMutex); return shown; }

    // the offline engine (built on first use); the tests use it to check hits
    Engine* offline() noexcept { ensureEngine(); return engine.get(); }

private:
    // ------------------------------------------------------------------ thread
    void run()
    {
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock (mutex);
                cv.wait_for (lock, std::chrono::milliseconds (120), [this] { return quit || next != nullptr; });
                if (quit) break;
                if (next) { current = std::move (next); dirty = true; }
            }
            pub.collect();
            purgeGraveyard();
            if (current && dirty)
            {
                dirty = false;
                if (! process (*current, true)) dirty = true;   // interrupted: newer inputs are waiting
            }
        }
    }

    bool newerWaiting()
    {
        std::lock_guard<std::mutex> lock (mutex);
        return next != nullptr || quit;
    }

    void ensureEngine()
    {
        if (engineReady) return;
        if (! engine) engine = std::make_unique<Engine>();
        engine->prepare (sr);
        engineReady = true;
    }

    // ------------------------------------------------------------------ the cache
    RenderedPtr find (uint64_t key) const
    {
        const auto it = cache.find (key);
        return it == cache.end() ? nullptr : it->second;
    }
    RenderedPtr keep (uint64_t key, Rendered&& r)
    {
        auto p = std::make_shared<const Rendered> (std::move (r));
        cache[key] = p;
        return p;
    }
    void purgeGraveyard()
    {
        const auto now = std::chrono::steady_clock::now();
        while (! graveyard.empty() && now - graveyard.front().at > std::chrono::seconds (120)) graveyard.pop_front();
    }
    void evict (const std::vector<uint64_t>& wanted)
    {
        std::unordered_map<uint64_t, bool> w; for (auto k : wanted) w[k] = true;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = cache.begin(); it != cache.end();)
        {
            if (w.count (it->first) == 0 && cache.size() > 48)
            {
                graveyard.push_back ({ std::static_pointer_cast<const void> (it->second), now });
                it = cache.erase (it);
            }
            else ++it;
        }
        while (graveyard.size() > 512) graveyard.pop_front();
    }
    void bury (std::shared_ptr<const void> p) { if (p) graveyard.push_back ({ std::move (p), std::chrono::steady_clock::now() }); }

    // ------------------------------------------------------------------ hashing
    static void hashPad (Hasher& h, const PadSettings& p)
    {
        h.i (p.sound).f (p.tune).f (p.decay).f (p.tone).f (p.drive).f (p.hollow).f (p.evolve).i (p.choke).f (p.body).f (p.fat).f (p.space)
         .i (p.character).i (p.useSample).p (p.sample.get()).f (p.sampleRms).i (p.place).i (p.variation).i (p.gesture).f (p.gestureSeconds)
         .i (p.bounces).f (p.bounceFlight).f (p.restitution).f (p.width);
    }
    static void hashRate (Hasher& h, const RateSpec& r) { h.i (r.tempo).d (r.beats).d (r.ms).f (r.swing); }
    static void hashFx (Hasher& h, const CellFx& c)
    {
        h.i ((int) c.kind);
        if (c.kind == CellFx::Library) { h.i ((int) c.fx).u (c.settings.seed); for (float v : c.settings.p) h.f (v); }
        else if (c.kind == CellFx::FrictionStretchFx)
        {
            const auto& p = c.stretch;
            hashRate (h, p.rate);
            h.f (p.stretch).f (p.creep).f (p.holdFade).f (p.snap).f (p.slipMs).f (p.grit).f (p.tape).f (p.attackMs).f (p.wet).u (p.seed)
             .f (p.peel).f (p.freeze).f (p.screechHz).f (p.adhesive).f (p.grainMs).f (p.rasp).f (p.roll).i (p.rollBody).f (p.rollSize)
             .f (p.strip).f (p.width).d (p.gateBeats).d (p.gateMs).f (p.decayMs);
            h.i ((int64_t) p.lines.size());
            for (const auto& l : p.lines) h.i (l.on).f (l.fLo).f (l.fHi).f (l.edge).f (l.offset).f (l.tilt).f (l.spray).f (l.level).f (l.pan);
        }
        else if (c.kind == CellFx::BounceDelayFx)
        {
            const auto& p = c.bounce;
            hashRate (h, p.rate);
            h.d (p.gridBeats).d (p.gridMs).f (p.bounciness).i (p.maxBounces).f (p.distStart).f (p.distEnd).f (p.speed).f (p.panRandom)
             .f (p.panCenter).f (p.width).f (p.room).f (p.wet).f (p.tail).u (p.seed);
        }
    }

    // ------------------------------------------------------------------ renders
    static PadSettings neutral (PadSettings p) { p.levelDb = 0.0f; p.pan = 0.0f; return p; }

    uint64_t dryKey (const PadSettings& ps, float vel) const
    {
        Hasher h; h.s ("dry").f (sr); hashPad (h, neutral (ps)); h.f (vel); return h.h;
    }
    RenderedPtr dry (const PadSettings& ps, float vel)
    {
        const uint64_t k = dryKey (ps, vel);
        if (auto r = find (k)) return r;
        ensureEngine();
        Rendered out;
        engine->renderHit (neutral (ps), vel, 8.0f, out);
        return keep (k, std::move (out));
    }

    const FrictionMaterial* rubber() { ensureEngine(); const auto& r = engine->lib().tables.rubbers; return r.empty() ? nullptr : &r[0]; }
    const Engine2Material* rollBody (int i)
    {
        ensureEngine(); const auto& b = engine->lib().tables.bodies;
        return b.empty() ? nullptr : &b[(size_t) std::max (0, std::min (i, (int) b.size() - 1))];
    }

    // one effect on a sound (the cell renderer), stereo in, stereo out
    void applyFx (const CellFx& fx, const Rendered& in, double bpm, double hitTime, double barStart, Rendered& out)
    {
        out.sampleRate = sr;
        if (fx.kind == CellFx::None || in.size() == 0) { out.l = in.l; out.r = in.r; return; }
        if (fx.kind == CellFx::FrictionStretchFx)
        {
            const auto* rb = rubber();
            if (rb == nullptr) { out.l = in.l; out.r = in.r; return; }
            FrictionStretch f;
            f.render (fx.stretch, *rb, bpm, hitTime, barStart, in.l.data(), in.r.data(), std::min (in.size(), (int) (4.0f * sr)), sr, out.l, out.r, rollBody (fx.stretch.rollBody));
            fadeTo (out, (int) (24.0f * sr), (int) (0.5f * sr));
            return;
        }
        renderCell (fx, in.l.data(), in.r.data(), in.size(), sr, bpm, hitTime, rubber(), rollBody (5), out.l, out.r);
    }

    RenderedPtr chain (const RenderedPtr& in, uint64_t inKey, const PadFx& fx, double bpm, uint64_t& keyOut)
    {
        Hasher h; h.s ("chain").u (inKey).d (bpm);
        for (const auto& c : fx.inserts) hashFx (h, c);
        keyOut = h.h;
        if (auto r = find (keyOut)) return r;
        if (! in) return nullptr;
        Rendered cur = *in;
        for (const auto& c : fx.inserts)
        {
            if (c.kind == CellFx::None) continue;
            Rendered o; applyFx (c, cur, bpm, 0.0, 0.0, o);
            cur = std::move (o);
        }
        return keep (keyOut, std::move (cur));
    }

    static bool hasInserts (const PadFx& f) { for (const auto& c : f.inserts) if (c.kind != CellFx::None) return true; return false; }

    static void fadeTo (Rendered& r, int len, int fadeLen)
    {
        if (r.size() <= len) return;
        const int end = std::min (r.size(), len + fadeLen);
        for (int i = len; i < end; ++i) { const float g = 1.0f - (float) (i - len) / (float) fadeLen; r.l[(size_t) i] *= g; r.r[(size_t) i] *= g; }
        r.l.resize ((size_t) end); r.r.resize ((size_t) end);
    }

    static Rendered fromSample (const SampleData& s, float sr)
    {
        Rendered r; r.sampleRate = sr;
        const int n = (int) s.left.size();
        if (n == 0) return r;
        const double ratio = s.sampleRate / sr;
        const int m = std::max (1, (int) (n / ratio));
        r.l.resize ((size_t) m); r.r.resize ((size_t) m);
        const auto& R = s.right.empty() ? s.left : s.right;
        for (int i = 0; i < m; ++i)
        {
            const double x = i * ratio; const int i0 = (int) x; const float f = (float) (x - i0);
            const int i1 = std::min (i0 + 1, n - 1);
            r.l[(size_t) i] = s.left[(size_t) i0] + f * (s.left[(size_t) i1] - s.left[(size_t) i0]);
            r.r[(size_t) i] = R[(size_t) i0] + f * (R[(size_t) i1] - R[(size_t) i0]);
        }
        return r;
    }

    // ------------------------------------------------------------------ the Sample view
    static void fftLocal (std::vector<std::complex<float>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979 / (double) len * (inverse ? 1 : -1);
            const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (float) n;
    }

    // split x into the band [lo, hi] (soft edges of a sixth of an octave) and the rest
    void bandSplit (const std::vector<float>& x, float lo, float hi, std::vector<float>& band) const
    {
        size_t n = 1; while (n < x.size() * 2) n <<= 1;
        std::vector<std::complex<float>> a (n);
        for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
        fftLocal (a, false);
        const float edge = std::pow (2.0f, 1.0f / 6.0f);
        for (size_t k = 0; k <= n / 2; ++k)
        {
            const float f = (float) k * sr / (float) n;
            float w = 1.0f;
            if (f < lo) w = f <= lo / edge ? 0.0f : (f - lo / edge) / (lo - lo / edge);
            else if (f > hi) w = f >= hi * edge ? 0.0f : 1.0f - (f - hi) / (hi * edge - hi);
            w = w * w * (3.0f - 2.0f * w);
            a[k] *= w;
            if (k > 0 && k < n / 2) a[n - k] *= w;
        }
        fftLocal (a, true);
        band.resize (x.size());
        for (size_t i = 0; i < x.size(); ++i) band[i] = a[i].real();
    }

    Rendered mangle (const SamplePreset& sp, const Rendered& src, const ModState&, double bpm)
    {
        Rendered out = src;
        const int len = src.size();
        if (len < 32) return out;
        for (const auto& op : sp.ops)
        {
            if (op.fx.kind == CellFx::None) continue;
            const int a = std::max (0, std::min (len - 1, (int) ((double) std::min (op.c0, op.c1) / 32.0 * len)));
            const int b = std::max (a + 16, std::min (len, (int) ((double) std::max (op.c0, op.c1) / 32.0 * len)));
            const float lo = std::max (20.0f, std::min (op.fLo, op.fHi)), hi = std::min (0.45f * sr, std::max (op.fLo, op.fHi));
            Rendered seg, band, rest;
            seg.sampleRate = band.sampleRate = sr;
            seg.l.assign (out.l.begin() + a, out.l.begin() + b); seg.r.assign (out.r.begin() + a, out.r.begin() + b);
            bandSplit (seg.l, lo, hi, band.l); bandSplit (seg.r, lo, hi, band.r);
            CellFx fx = op.fx;
            const float height = std::log2 (hi / lo) / std::log2 (16000.0f / 30.0f);   // the box's height: its main control
            applyHeight (fx, std::max (0.0f, std::min (1.0f, height)));
            Rendered proc; applyFx (fx, band, bpm, 0.0, 0.0, proc);
            const int pl = proc.size();
            if (a + pl > (int) out.l.size()) { out.l.resize ((size_t) (a + pl), 0.0f); out.r.resize ((size_t) (a + pl), 0.0f); }
            for (int i = 0; i < b - a; ++i)
            {
                // remove the band from the original span, keep the dry part of the mix
                out.l[(size_t) (a + i)] -= op.mix * band.l[(size_t) i];
                out.r[(size_t) (a + i)] -= op.mix * band.r[(size_t) i];
            }
            for (int i = 0; i < pl; ++i)
            {
                out.l[(size_t) (a + i)] += op.mix * proc.l[(size_t) i];
                out.r[(size_t) (a + i)] += op.mix * proc.r[(size_t) i];
            }
        }
        // keep it in range
        float pk = 0.0f; for (size_t i = 0; i < out.l.size(); ++i) pk = std::max (pk, std::max (std::fabs (out.l[i]), std::fabs (out.r[i])));
        if (pk > 0.98f) { const float g = 0.98f / pk; for (auto& v : out.l) v *= g; for (auto& v : out.r) v *= g; }
        return out;
    }

    // ------------------------------------------------------------------ one pass
    struct Need { uint64_t key; int priority; std::function<RenderedPtr()> make; std::string what; };
    struct Keys
    {
        std::vector<uint64_t> cells, zones;
        std::array<std::array<uint64_t, 3>, kNumPads> inserts {};
        std::vector<std::array<uint64_t, 2>> slots;
        uint64_t sample = 0, sampleSrc = 0, grain = 0;
        std::array<uint64_t, kNumPads> kitRows {};
        std::array<std::array<uint64_t, 3>, kNumPads> dry {};
    };

    bool process (const RenderInputs& in, bool interruptible)
    {
        ensureEngine();
        const auto& P = in.project;
        const double bpm = std::max (20.0, in.bpm);
        std::vector<Need> needs;
        std::vector<uint64_t> wanted;

        auto state = std::make_unique<RtState>();
        RtState& S = *state;
        S.sr = sr;
        S.glue = P.glue;
        S.mod = P.mod;
        // what runs live (no render) and what is made ahead
        const auto lv = buildLiveState (P);
        std::array<bool, kNumPads> needDry {};
        for (const auto& n : lv->nodes) if (! n.prepared) needDry[(size_t) n.pad] = true;
        for (int p = 0; p < kNumPads; ++p) if (lv->insertMode[(size_t) p] == 1 || ! lv->padBounce[(size_t) p].empty()) needDry[(size_t) p] = true;
        for (size_t zi = 0; zi < P.keymap.zones.size() && zi < lv->zoneFx.size(); ++zi)
        {
            const auto& z = P.keymap.zones[zi];
            if (lv->zoneFx[zi].on() && z.src == ZoneSource::Pad && z.index >= 0 && z.index < kNumPads) needDry[(size_t) z.index] = true;
        }

        // ---- routes
        for (const auto& r : P.mod.routes)
        {
            ResolvedRoute rr; rr.src = r.src; rr.depth = r.depth;
            if (! parseDest (r.dest, rr.dest, rr.target, rr.param)) continue;
            if ((int) rr.dest <= (int) Dest::PadLevel)
            {
                for (int p = 0; p < kNumPads; ++p) if (rr.target < 0 || rr.target == p) S.padRoutes[(size_t) p].push_back (rr);
            }
            else if (rr.dest == Dest::SeqLevel || rr.dest == Dest::SeqPan || rr.dest == Dest::SeqPitch) S.seqRoutes.push_back (rr);
            else if (rr.dest == Dest::GridLevel || rr.dest == Dest::GridPan || rr.dest == Dest::GridPitch) S.gridRoutes.push_back (rr);
            else if (rr.dest == Dest::Master) S.masterRoutes.push_back (rr);
        }
        auto renderMods = [&] (Dest d, int target, const HitContext& hc, int param = -1)
        {
            float sum = 0.0f;
            for (const auto& r : P.mod.routes)
            {
                ResolvedRoute rr;
                if (! parseDest (r.dest, rr.dest, rr.target, rr.param)) continue;
                if (rr.dest != d || rr.target != target || (param >= 0 && rr.param != param)) continue;
                sum += r.depth * sourceAtHit (r.src, P.mod, hc);
            }
            return sum;
        };

        // ---- kits (for Kit zones)
        for (int k = 0; k < (int) kits().size(); ++k)
        {
            std::array<PadSettings, kNumPads> a {};
            for (int p = 0; p < kNumPads; ++p) a[(size_t) p] = kitPadSettings (engine->lib(), k, p);
            S.kitPads.push_back (a);
        }

        Keys K;
        // ---- pad inserts
        static constexpr float kLayers[3] = { 0.4f, 0.7f, 1.0f };
        for (int p = 0; p < kNumPads; ++p)
        {
            if (needDry[(size_t) p])
                for (int L = 0; L < 3; ++L)
                {
                    const PadSettings ps = in.pads[(size_t) p];
                    const float v = kLayers[L];
                    const uint64_t dk = dryKey (ps, v);
                    K.dry[(size_t) p][(size_t) L] = dk; wanted.push_back (dk);
                    S.padDry[(size_t) p][(size_t) L] = find (dk);
                    needs.push_back ({ dk, 0, [this, ps, v] { return dry (ps, v); }, "Lane " + std::to_string (p + 1) + " hit" });
                }
            S.hasInserts[(size_t) p] = lv->insertMode[(size_t) p] == 2;      // only inserts that hold a Friction Stretch are made ahead
            if (! S.hasInserts[(size_t) p]) continue;
            for (int L = 0; L < 3; ++L)
            {
                const PadSettings ps = in.pads[(size_t) p];
                const float v = kLayers[L];
                const uint64_t dk = dryKey (ps, v);
                Hasher h; h.s ("chain").u (dk).d (bpm); for (const auto& c : P.padFx[(size_t) p].inserts) hashFx (h, c);
                const uint64_t key = h.h;
                K.inserts[(size_t) p][(size_t) L] = key;
                wanted.push_back (key); wanted.push_back (dk);
                S.insertAudio[(size_t) p][(size_t) L] = find (key);
                const PadFx fx = P.padFx[(size_t) p];
                needs.push_back ({ key, 2, [this, ps, v, fx, bpm] { uint64_t k2; auto d = dry (ps, v); return chain (d, dryKey (ps, v), fx, bpm, k2); }, "Pad " + std::to_string (p + 1) + " inserts" });
            }
        }

        // ---- One Shot cells
        S.oneShotOn = P.matrix.oneShotOn;
        const auto& cells = P.matrix.oneShot.cells;
        S.cellSlots.assign (cells.size(), {});
        K.cells.assign (cells.size(), 0);
        std::vector<uint64_t>& cellKeys = K.cells;
        std::vector<std::function<RenderedPtr()>> cellMakes (cells.size());
        std::vector<char> cellPrepared (cells.size(), 0);
        for (size_t ci = 0; ci < cells.size(); ++ci)
        {
            const auto& c = cells[ci];
            RtCell rc; rc.pad = std::max (0, std::min (c.pad, kNumPads - 1)); rc.step = std::max (0, std::min (c.step, Pattern::kMaxSteps - 1));
            rc.span = std::max (1, c.span); rc.height = c.height; rc.scene = std::max (0, std::min (c.scene, kNumScenes - 1));
            rc.key = nodeKey (rc.scene, rc.pad, rc.step);
            const Pattern& pattern = in.scenes[(size_t) rc.scene];
            const bool prepared = isFriction (c.fx) || lv->insertMode[(size_t) rc.pad] == 2;
            cellPrepared[ci] = prepared ? 1 : 0;
            const Step st = pattern.steps[(size_t) rc.pad][(size_t) rc.step];
            const float vel = st.vel > 0 ? st.vel / 127.0f : 0.8f;
            const double hitTime = stepSeconds16 (rc.step, bpm, in.swing);
            const double barStart = (rc.step / 16) * 4.0 * 60.0 / bpm;
            HitContext hc; hc.beats = hitTime * bpm / 60.0; hc.bpm = bpm; hc.velocity = vel; hc.height = c.height;
            hc.step = (float) rc.step / (float) std::max (1, pattern.length); hc.seed = (uint32_t) ci * 7919u + 1u;
            const float height = std::max (0.0f, std::min (1.0f, c.height + renderMods (Dest::CellMain, (int) ci, hc)));
            CellFx fx = c.fx; applyHeight (fx, height);
            const double stepSec = 15.0 / bpm;
            if (fx.kind == CellFx::FrictionStretchFx && fx.stretch.gateBeats <= 0.0 && fx.stretch.gateMs <= 0.0)
            { if (fx.stretch.rate.tempo) fx.stretch.gateBeats = rc.span * 0.25; else fx.stretch.gateMs = rc.span * stepSec * 1000.0; }
            const PadSettings ps = in.pads[(size_t) rc.pad];
            const PadFx pfx = P.padFx[(size_t) rc.pad];
            Hasher h; h.s ("cell").u (dryKey (ps, vel)).d (bpm).f (in.swing).i (rc.step).i (rc.span); hashFx (h, fx);
            for (const auto& c2 : pfx.inserts) hashFx (h, c2);
            const uint64_t key = h.h;
            cellKeys[ci] = key;
            if (prepared) { wanted.push_back (key); rc.audio = find (key); }
            const int span = rc.span, padIdx = rc.pad;
            cellMakes[ci] = [this, ps, vel, fx, pfx, bpm, hitTime, barStart, stepSec, span]
            {
                auto d = dry (ps, vel);
                Rendered o; applyFx (fx, *d, bpm, hitTime - barStart, 0.0, o);
                for (const auto& ins : pfx.inserts) if (ins.kind != CellFx::None) { Rendered o2; applyFx (ins, o, bpm, 0.0, 0.0, o2); o = std::move (o2); }
                fadeTo (o, std::max ((int) (span * stepSec * sr), std::min (d->size(), (int) (0.25 * sr))), (int) (0.06 * sr));
                return std::make_shared<const Rendered> (std::move (o));
            };
            if (prepared) needs.push_back ({ key, 1, cellMakes[ci], "Node " + std::to_string (ci + 1) + " | lane " + std::to_string (padIdx + 1) });
            S.cells.push_back (rc);
            if (prepared) S.cellIndex.push_back ({ rc.key, (int) ci });
        }
        std::stable_sort (S.cellIndex.begin(), S.cellIndex.end(), [] (const std::pair<uint32_t, int>& a, const std::pair<uint32_t, int>& b) { return a.first < b.first; });
        S.cellIndex.erase (std::unique (S.cellIndex.begin(), S.cellIndex.end(), [] (const std::pair<uint32_t, int>& a, const std::pair<uint32_t, int>& b) { return a.first == b.first; }), S.cellIndex.end());

        // ---- grid samples
        S.gridCurrent = std::max (0, std::min (P.grid.current, 15));
        S.gridPlay = P.grid.play; S.gridLevel = P.grid.level;
        {
            Hasher gh; for (const auto& pat : P.grid.patterns) { gh.i (pat.length).i (pat.rate).f (pat.swing); gh.bytes (pat.cells.data(), pat.cells.size() * 4); }
            if (! lastGrid || gh.h != lastGridHash) { lastGrid = std::make_shared<const std::vector<GridPattern>> (P.grid.patterns); lastGridHash = gh.h; }
            S.grid = lastGrid;
        }
        if (P.grid.useKit || P.grid.files.empty())
        {
            for (int p = 0; p < kNumPads; ++p)
            {
                const PadSettings ps = in.pads[(size_t) p];
                if (ps.useSample && ps.sample) { S.gridSamples.push_back (ps.sample); continue; }
                const uint64_t k = dryKey (ps, 0.85f); wanted.push_back (k);
                K.kitRows[(size_t) p] = k;
                S.gridSamples.push_back (find (k) ? asSample (k, *find (k)) : nullptr);
                needs.push_back ({ k, 8, [this, ps] { return dry (ps, 0.85f); }, "Grid row " + std::to_string (p + 1) });
            }
        }
        else
            for (const auto& f : P.grid.files)
            {
                const auto it = in.files.find (f);
                S.gridSamples.push_back (it != in.files.end() ? it->second : nullptr);
            }

        // ---- window slots
        auto slotSource = [&] (const SlotSource& src, uint64_t& key) -> std::function<RenderedPtr()>
        {
            if (src.kind == SourceKind::Pad && src.index >= 0 && src.index < kNumPads)
            {
                const PadSettings ps = in.pads[(size_t) src.index];
                const PadFx pfx = P.padFx[(size_t) src.index];
                const bool ins = hasInserts (pfx);
                Hasher h; h.s ("src-pad").u (dryKey (ps, 0.85f)); if (ins) for (const auto& c : pfx.inserts) hashFx (h, c);
                key = h.h;
                return [this, ps, pfx, ins, bpm] { auto d = dry (ps, 0.85f); if (! ins) return d; uint64_t k2; return chain (d, dryKey (ps, 0.85f), pfx, bpm, k2); };
            }
            if (src.kind == SourceKind::GridRow && ! S.gridSamples.empty())
            {
                const auto smp = S.gridSamples[(size_t) (std::max (0, src.index) % (int) S.gridSamples.size())];
                if (! smp) return {};
                Hasher h; h.s ("src-row").p (smp.get()).f (sr); key = h.h;
                const float rate = sr;
                return [smp, rate] { return std::make_shared<const Rendered> (fromSample (*smp, rate)); };
            }
            if (src.kind == SourceKind::Cell && src.index >= 0 && src.index < (int) cells.size())
            {
                key = cellKeys[(size_t) src.index];
                const uint64_t ck = key;
                // the node's own sound, made here for the window to work on
                auto mk = cellMakes[(size_t) src.index];
                if (mk) return [this, ck, mk] { if (auto r = find (ck)) return r; auto r = mk(); cache[ck] = r; return r; };
            }
            return {};
        };

        auto addSlot = [&] (bool isFs, int si, bool on, const SlotSource& src, float level, bool replace, const CellFx& fxIn)
        {
            if (! on || src.kind == SourceKind::None) return;
            uint64_t srcKey = 0;
            auto make = slotSource (src, srcKey);
            if (! make) return;
            RtSlot rs; rs.fs = isFs; rs.slot = si; rs.kind = src.kind; rs.index = src.index; rs.level = level; rs.replace = replace;
            std::array<uint64_t, 2> sk {};
            for (int parity = 0; parity < 2; ++parity)
            {
                const double hitTime = parity == 0 ? 0.0 : stepSeconds16 (1, bpm, in.swing);
                HitContext hc; hc.beats = hitTime * bpm / 60.0; hc.bpm = bpm; hc.seed = (uint32_t) (si * 31 + parity);
                CellFx fx = fxIn;
                // render-time modulation of the slot's parameters
                const auto& names = isFs ? fsModParams() : bdModParams();
                for (int pi = 0; pi < (int) names.size(); ++pi)
                {
                    const float m = renderMods (isFs ? Dest::FsParam : Dest::BdParam, si, hc, pi);
                    if (m == 0.0f) continue;
                    if (isFs)
                    {
                        auto& p = fx.stretch;
                        float* t[] = { &p.stretch, &p.creep, &p.peel, &p.freeze, &p.screechHz, &p.adhesive, &p.rasp, &p.roll, &p.strip, &p.width, &p.decayMs, &p.wet };
                        const float range[] = { 63.0f, 1.0f, 1.0f, 1.0f, 3000.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 3000.0f, 1.0f };
                        *t[pi] = std::max (pi == 0 ? 1.0f : 0.0f, *t[pi] + m * range[pi]);
                        if (pi == 0) p.stretch = std::min (64.0f, p.stretch);
                    }
                    else
                    {
                        auto& p = fx.bounce;
                        float* t[] = { &p.bounciness, &p.distStart, &p.distEnd, &p.speed, &p.panRandom, &p.width, &p.room, &p.wet };
                        const float range[] = { 0.5f, 20.0f, 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
                        *t[pi] = std::max (0.0f, *t[pi] + m * range[pi]);
                    }
                }
                Hasher h; h.s (isFs ? "fs" : "bd").u (srcKey).d (bpm).f (in.swing).i (parity); hashFx (h, fx);
                const uint64_t key = h.h;
                sk[(size_t) parity] = key;
                wanted.push_back (key); wanted.push_back (srcKey);
                rs.audio[(size_t) parity] = find (key);
                if (! isFs && parity == 1) continue;                    // Bounce Delay runs live: one render, for the window's picture
                needs.push_back ({ key, isFs ? 3 + parity : 9, [this, make, fx, isFs, bpm, hitTime]
                {
                    auto src = make();
                    if (! src || src->size() == 0) return RenderedPtr();
                    Rendered o; o.sampleRate = sr;
                    if (isFs)
                    {
                        const auto* rb = rubber();
                        if (rb == nullptr) return RenderedPtr();
                        StretchParams p = fx.stretch; p.wet = std::max (0.0f, std::min (1.0f, p.wet));
                        FrictionStretch f;
                        f.render (p, *rb, bpm, hitTime, 0.0, src->l.data(), src->r.data(), std::min (src->size(), (int) (4.0f * sr)), sr, o.l, o.r, rollBody (p.rollBody));
                        fadeTo (o, (int) (24.0f * sr), (int) (0.5f * sr));   // an accent: at most 24 s
                    }
                    else
                    {
                        BounceDelay b;
                        const auto plan = b.plan (fx.bounce, bpm, hitTime, 0.0);
                        const int len = src->size() + (int) (((plan.empty() ? 0.0 : plan.back().t - hitTime) + fx.bounce.tail + 0.2) * sr);
                        o.l.assign ((size_t) len, 0.0f); o.r.assign ((size_t) len, 0.0f);
                        std::vector<Bounce> rel = plan; for (auto& x : rel) x.t -= hitTime;
                        b.render (fx.bounce, rel, 0.0, src->l.data(), src->r.data(), src->size(), sr, o.l.data(), o.r.data(), nullptr, len);
                    }
                    return std::make_shared<const Rendered> (std::move (o));
                }, std::string (isFs ? "Friction Stretch" : "Bounce Delay") + " slot " + std::to_string (si + 1) });
            }
            const int idx = (int) S.slots.size();
            S.slots.push_back (rs);
            K.slots.push_back (sk);
            if (! isFs) return;                                          // not played from here
            if (src.kind == SourceKind::Pad && src.index >= 0 && src.index < kNumPads) S.padSlots[(size_t) src.index].push_back (idx);
            if (src.kind == SourceKind::GridRow && src.index >= 0 && src.index < GridPattern::kSize) S.rowSlots[(size_t) src.index].push_back (idx);
            if (src.kind == SourceKind::Cell && src.index >= 0 && src.index < (int) S.cellSlots.size()) S.cellSlots[(size_t) src.index].push_back (idx);
        };
        for (int si = 0; si < kNumSlots; ++si)
        {
            const auto& f = P.fs[(size_t) si];
            addSlot (true, si, f.on, f.src, f.level, f.replace, CellFx::friction (f.p));
            const auto& b = P.bd[(size_t) si];
            addSlot (false, si, b.on, b.src, b.level, b.replace, CellFx::bouncing (b.p));
        }

        // ---- key map
        S.layers = P.keymap.layers;
        for (size_t zi = 0; zi < P.keymap.zones.size(); ++zi)
        {
            const auto& z = P.keymap.zones[zi];
            RtZone rz; rz.z = z;
            uint64_t zk = 0;
            if (z.src == ZoneSource::Sample && ! z.file.empty())
            {
                const auto it = in.files.find (z.file);
                if (it != in.files.end()) rz.sample = it->second;
            }
            if (isFriction (z.fx))
            {
                const float vel = (z.loVel + z.hiVel) * 0.5f / 127.0f;
                if (z.src == ZoneSource::Pad && z.index >= 0 && z.index < kNumPads)
                {
                    const PadSettings ps = in.pads[(size_t) z.index];
                    Hasher h; h.s ("zone").u (dryKey (ps, vel)).d (bpm); hashFx (h, z.fx);
                    const uint64_t key = h.h; wanted.push_back (key); zk = key;
                    rz.fxAudio = find (key);
                    const CellFx fx = z.fx;
                    needs.push_back ({ key, 5, [this, ps, vel, fx, bpm] { auto d = dry (ps, vel); Rendered o; applyFx (fx, *d, bpm, 0.0, 0.0, o); return std::make_shared<const Rendered> (std::move (o)); }, "Zone " + z.name });
                }
                else if (z.src == ZoneSource::Sample && rz.sample)
                {
                    const auto smp = rz.sample;
                    Hasher h; h.s ("zone-smp").p (smp.get()).d (bpm); hashFx (h, z.fx);
                    const uint64_t key = h.h; wanted.push_back (key); zk = key;
                    rz.fxAudio = find (key);
                    const CellFx fx = z.fx;
                    needs.push_back ({ key, 5, [this, smp, fx, bpm] { Rendered src = fromSample (*smp, sr); Rendered o; applyFx (fx, src, bpm, 0.0, 0.0, o); return std::make_shared<const Rendered> (std::move (o)); }, "Zone " + z.name });
                }
            }
            S.zones.push_back (rz);
            K.zones.push_back (zk);
        }

        // ---- the Sample view
        const auto& sp = P.matrix.sample;
        uint64_t& sampleKey = K.sample; uint64_t& sampleSrcKey = K.sampleSrc;
        std::function<RenderedPtr()> sampleSrc;
        {
            const int pad = std::max (0, std::min (sp.pad, kNumPads - 1));
            if (! sp.file.empty())
            {
                const auto it = in.files.find (sp.file);
                if (it != in.files.end() && it->second)
                {
                    const auto smp = it->second;
                    Hasher h; h.s ("smp-file").p (smp.get()).f (sr); sampleSrcKey = h.h;
                    const float rate = sr;
                    sampleSrc = [smp, rate] { return std::make_shared<const Rendered> (fromSample (*smp, rate)); };
                }
            }
            else
            {
                const PadSettings ps = in.pads[(size_t) pad];
                sampleSrcKey = dryKey (ps, 0.9f);
                sampleSrc = [this, ps] { return dry (ps, 0.9f); };
            }
            if (sampleSrc)
            {
                wanted.push_back (sampleSrcKey);
                needs.push_back ({ sampleSrcKey, 6, sampleSrc, "Sample view source" });
                if (P.matrix.sampleOn && ! sp.ops.empty())
                {
                    Hasher h; h.s ("mangle").u (sampleSrcKey).d (bpm);
                    for (const auto& op : sp.ops) { h.i (op.c0).i (op.c1).f (op.fLo).f (op.fHi).f (op.mix); hashFx (h, op.fx); }
                    sampleKey = h.h; wanted.push_back (sampleKey);
                    const SamplePreset spc = sp; const ModState ms = P.mod;
                    auto srcFn = sampleSrc; const uint64_t sk = sampleSrcKey;
                    needs.push_back ({ sampleKey, 6, [this, spc, ms, srcFn, sk, bpm]
                    {
                        RenderedPtr s = find (sk); if (! s) { s = srcFn(); if (s) cache[sk] = s; }
                        if (! s) return RenderedPtr();
                        return std::make_shared<const Rendered> (mangle (spc, *s, ms, bpm));
                    }, "Sample view" });
                    if (auto r = find (sampleKey)) setPadSample (S, pad, r);
                }
            }
        }

        // ---- the Sequencer view
        S.seqOn = P.matrix.seqOn;
        S.seq = P.matrix.seq;
        if (S.seq.engine == MarkEngine::Smp)
        {
            const int gp = S.seq.samplePad >= 0 ? std::min (S.seq.samplePad, kNumPads - 1) : 0;
            const PadSettings ps = in.pads[(size_t) gp];
            if (ps.useSample && ps.sample) S.grains = ps.sample;
            else
            {
                const uint64_t k = dryKey (ps, 0.9f); wanted.push_back (k); K.grain = k;
                if (auto r = find (k)) S.grains = grainCache (k, *r);
                needs.push_back ({ k, 7, [this, ps] { return dry (ps, 0.9f); }, "Grain source" });
            }
        }

        // publish what is ready, then render what is missing
        sort (needs);
        int missing = 0; for (const auto& n : needs) if (! find (n.key)) ++missing;
        total.store (missing); done.store (0);
        isBusy.store (missing > 0);
        publishState (S, in, K);
        auto lastPublish = std::chrono::steady_clock::now();
        for (const auto& n : needs)
        {
            if (find (n.key)) continue;
            if (interruptible && newerWaiting()) { isBusy.store (false); return false; }
            { std::lock_guard<std::mutex> lock (labelMutex); label = n.what; }
            RenderedPtr r;
            try { r = n.make(); } catch (...) { r = nullptr; }
            if (! r) { Rendered empty; empty.sampleRate = sr; r = std::make_shared<const Rendered> (std::move (empty)); }
            cache[n.key] = r;
            done.fetch_add (1);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastPublish > std::chrono::milliseconds (150)) { publishState (S, in, K); lastPublish = now; }
        }
        publishState (S, in, K);
        { std::lock_guard<std::mutex> lock (labelMutex); label.clear(); }
        isBusy.store (false);
        evict (wanted);
        return true;
    }

    static void sort (std::vector<Need>& needs)
    {
        std::stable_sort (needs.begin(), needs.end(), [] (const Need& a, const Need& b) { return a.priority < b.priority; });
    }

    std::shared_ptr<const SampleData> asSample (uint64_t key, const Rendered& r)
    {
        const auto it = converted.find (key);
        if (it != converted.end()) return it->second;
        if (converted.size() > 96) { for (auto& c : converted) bury (c.second); converted.clear(); }
        auto s = std::make_shared<SampleData>();
        s->left = r.l; s->right = r.r; s->sampleRate = r.sampleRate;
        converted[key] = s;
        return s;
    }

    std::shared_ptr<const SampleData> grainCache (uint64_t key, const Rendered& r)
    {
        if (grainKey == key && grainData) return grainData;
        auto s = std::make_shared<SampleData>();
        s->left = r.l; s->right = r.r; s->sampleRate = r.sampleRate;
        bury (grainData);
        grainData = s; grainKey = key;
        return grainData;
    }

    void setPadSample (RtState& S, int pad, const RenderedPtr& r)
    {
        if (! r || r->size() == 0) return;
        if (! (sampleOutKey == (uint64_t) (uintptr_t) r.get() && sampleOut))
        {
            auto s = std::make_shared<SampleData>();
            s->left = r->l; s->right = r->r; s->sampleRate = r->sampleRate; s->path = "Sample view";
            double sum = 0.0; int last = 1;
            for (int i = 0; i < r->size(); ++i) { sum += 0.5 * ((double) r->l[(size_t) i] * r->l[(size_t) i] + (double) r->r[(size_t) i] * r->r[(size_t) i]); if (std::fabs (r->l[(size_t) i]) > 1.0e-4f) last = i + 1; }
            sampleOutRms = (float) std::sqrt (sum / std::max (last, 1));
            bury (sampleOut);
            sampleOut = s; sampleOutKey = (uint64_t) (uintptr_t) r.get();
        }
        S.padSample[(size_t) pad] = sampleOut;
        S.padSampleRms[(size_t) pad] = sampleOutRms;
    }

    // fill in whatever is ready now and publish a copy
    void publishState (RtState& S, const RenderInputs& in, const Keys& K)
    {
        for (const auto& ci : S.cellIndex) if ((size_t) ci.second < K.cells.size()) S.cells[(size_t) ci.second].audio = find (K.cells[(size_t) ci.second]);
        for (int p = 0; p < kNumPads; ++p)
            for (int L = 0; L < 3; ++L) if (K.dry[(size_t) p][(size_t) L] != 0) S.padDry[(size_t) p][(size_t) L] = find (K.dry[(size_t) p][(size_t) L]);
        for (int p = 0; p < kNumPads; ++p)
            if (S.hasInserts[(size_t) p]) for (int L = 0; L < 3; ++L) S.insertAudio[(size_t) p][(size_t) L] = find (K.inserts[(size_t) p][(size_t) L]);
        for (size_t i = 0; i < S.slots.size() && i < K.slots.size(); ++i)
            for (int q = 0; q < 2; ++q) S.slots[i].audio[(size_t) q] = find (K.slots[i][(size_t) q]);
        for (size_t i = 0; i < S.zones.size() && i < K.zones.size(); ++i) if (K.zones[i] != 0) S.zones[i].fxAudio = find (K.zones[i]);
        const int pad = std::max (0, std::min (in.project.matrix.sample.pad, kNumPads - 1));
        if (K.sample != 0) if (auto r = find (K.sample)) setPadSample (S, pad, r);
        if (S.seq.engine == MarkEngine::Smp && ! S.grains && K.grain != 0) if (auto r = find (K.grain)) S.grains = grainCache (K.grain, *r);
        for (int p = 0; p < kNumPads && p < (int) S.gridSamples.size(); ++p)
            if (K.kitRows[(size_t) p] != 0 && ! S.gridSamples[(size_t) p]) if (auto r = find (K.kitRows[(size_t) p])) S.gridSamples[(size_t) p] = asSample (K.kitRows[(size_t) p], *r);
        pub.publish (std::make_unique<RtState> (S));
        // what the editor draws
        auto d = std::make_shared<DisplayData>();
        for (const auto& s2 : S.slots) (s2.fs ? d->fs : d->bd)[(size_t) s2.slot] = s2.audio[0];
        for (int si = 0; si < kNumSlots; ++si)
        {
            BounceDelay bd;
            d->bdPlans[(size_t) si] = bd.plan (in.project.bd[(size_t) si].p, std::max (20.0, in.bpm), 0.0, 0.0);
        }
        for (const auto& c : S.cells) d->cells.push_back (c.audio);
        if (K.sampleSrc != 0) d->sampleIn = find (K.sampleSrc);
        if (K.sample != 0) d->sampleOut = find (K.sample);
        std::lock_guard<std::mutex> lock (labelMutex);
        shown = d;
    }

    RtPublisher<RtState>& pub;
    std::unique_ptr<Engine> engine;
    bool engineReady = false;
    float sr = 0.0f;
    std::unordered_map<uint64_t, RenderedPtr> cache;
    struct Grave { std::shared_ptr<const void> p; std::chrono::steady_clock::time_point at; };
    std::deque<Grave> graveyard;
    std::shared_ptr<const std::vector<GridPattern>> lastGrid; uint64_t lastGridHash = 0;
    std::unordered_map<uint64_t, std::shared_ptr<const SampleData>> converted;
    std::shared_ptr<const SampleData> sampleOut, grainData; uint64_t sampleOutKey = 0, grainKey = 0; float sampleOutRms = 0.05f;

    std::thread worker;
    mutable std::mutex mutex, labelMutex;
    std::condition_variable cv;
    std::unique_ptr<RenderInputs> next;
    std::unique_ptr<RenderInputs> current;   // the inputs being (or last) rendered: the worker's, or start()'s while it is stopped
    bool quit = false, dirty = false;
    std::atomic<int> total { 0 }, done { 0 };
    std::atomic<bool> isBusy { false };
    std::string label;
    std::shared_ptr<const DisplayData> shown = std::make_shared<DisplayData>();
};
} // namespace nb
