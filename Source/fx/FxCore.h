// NeoBeat effects on their own: Friction Stretch and Bounce Delay as stand-alone plugins.
//
// Both effects work on one whole hit: the hit goes in, a longer sound comes out. So each plugin has
// two ways of getting a hit:
//   LOAD     a sample (or the built-in clank). It is rendered in the background whenever a setting
//            changes, in two versions (a hit on the beat, a hit on the swung off-beat), and a MIDI note,
//            the PLAY button or the bar loop plays it.
//   CAPTURE  the plugin listens to its input, catches each hit, renders it, and joins the result in
//            where it would be by then (so the yanks and bounces still land on the song's grid).
//
// Framework-free: the audio thread never locks, allocates or waits. Renders reach it through
// RtPublisher (load) and a fixed set of capture slots handed back and forth with atomics.
#pragma once

#include "../dsp/BounceDelay.h"
#include "../dsp/FrictionStretch.h"
#include "../dsp/MaterialSpace.h"
#include "../engine/Library.h"
#include "../engine/Players.h"
#include "../engine/Rt.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace nbfx
{
using nb::Rendered;
using nb::RenderedPtr;
using HitPtr = std::shared_ptr<const nb::SampleData>;

enum class Kind { Friction, Bounce };
enum class Mode { Load = 0, Capture = 1 };

// everything that shapes a render
struct Settings
{
    nb::StretchParams fs;
    nb::BounceParams bd;
    int place = 0;            // Bounce Delay's room: 0 small room, 1 studio, 2 large hall
};

// read by the audio thread every block
struct Live
{
    Mode mode = Mode::Load;
    float dry = 1.0f;         // load: the loaded hit itself, beside the effect
    float thru = 1.0f;        // capture: the input, passed through
    float level = 1.0f;       // output gain
    float threshold = 0.1f;   // capture: a hit starts above this (linear peak)
    float captureMs = 250.0f; // capture: how much of the hit is taken
    float retrigMs = 60.0f;   // capture: no new hit sooner than this
    bool keytrack = false;    // load: the MIDI key transposes (C3 = as it is)
    bool choke = false;       // a new hit cuts the one before
    int loopBars = 0;         // load: play it every N bars while the host runs (0 = off)
    double stepBeats = 0.25;  // the effect's grid step in beats (0 = milliseconds: no parity)
};

struct Transport { bool playing = false; double ppq = 0.0; double bpm = 120.0; double beatsPerBar = 4.0; };
struct Midi { int offset = 0; bool on = true; int note = 60; float velocity = 1.0f; };

inline void hashStretch (nb::Hasher& h, const nb::StretchParams& p)
{
    h.i (p.rate.tempo).d (p.rate.beats).d (p.rate.ms).f (p.rate.swing).f (p.stretch).f (p.creep).f (p.holdFade).f (p.snap).f (p.slipMs)
     .f (p.grit).f (p.tape).f (p.attackMs).f (p.wet).u (p.seed).f (p.peel).f (p.freeze).f (p.screechHz).f (p.adhesive).f (p.grainMs)
     .f (p.rasp).f (p.roll).i (p.rollBody).f (p.rollSize).f (p.strip).f (p.width).d (p.gateBeats).d (p.gateMs).f (p.decayMs);
    for (const auto& l : p.lines) h.i (l.on).f (l.fLo).f (l.fHi).f (l.offset).f (l.tilt).f (l.spray).f (l.edge).f (l.level).f (l.pan);
}
inline void hashBounce (nb::Hasher& h, const nb::BounceParams& p)
{
    h.i (p.rate.tempo).d (p.rate.beats).d (p.rate.ms).f (p.rate.swing).d (p.gridBeats).d (p.gridMs).f (p.bounciness).i (p.maxBounces)
     .f (p.distStart).f (p.distEnd).f (p.speed).f (p.panRandom).f (p.panCenter).f (p.width).f (p.room).f (p.wet).f (p.tail).u (p.seed);
}

// The built-in hit, so a fresh plugin makes a sound at once: a dull metal clank (a few inharmonic
// partials and a burst of noise), 0.45 s.
inline HitPtr defaultHit (float sr = 48000.0f)
{
    auto s = std::make_shared<nb::SampleData>();
    s->sampleRate = sr; s->path = "Built-in clank";
    const int n = (int) (0.45f * sr);
    s->left.assign ((size_t) n, 0.0f); s->right.assign ((size_t) n, 0.0f);
    const double f[] = { 312.0, 507.0, 843.0, 1291.0, 1762.0, 2480.0, 3377.0 };
    const double d[] = { 9.0, 12.0, 16.0, 22.0, 30.0, 38.0, 52.0 };
    uint32_t rng = 12345u;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / (double) sr;
        double l = 0.0, r = 0.0;
        for (int k = 0; k < 7; ++k)
        {
            const double a = std::exp (-d[k] * t) / (1.0 + 0.35 * k);
            l += a * std::sin (6.283185307 * f[k] * t);
            r += a * std::sin (6.283185307 * f[k] * 1.003 * t + 0.4 * k);
        }
        rng = rng * 1664525u + 1013904223u;
        const double noise = ((rng >> 8) / 8388608.0 - 1.0) * std::exp (-90.0 * t);
        const double att = std::min (1.0, t * 2000.0);
        s->left[(size_t) i] = (float) (0.22 * att * (l + 0.9 * noise));
        s->right[(size_t) i] = (float) (0.22 * att * (r + 0.9 * noise));
    }
    return s;
}

// ====================================================================== the render worker
class Renderer
{
public:
    static constexpr int kSlots = 8;
    static constexpr float kMaxHitSeconds = 4.0f;

    struct LoadState { RenderedPtr wet[2]; RenderedPtr dry; float sr = 48000.0f; };

    // one caught hit, handed audio -> worker -> audio
    struct Slot
    {
        enum : int { Free, Filling, Queued, Rendering, Ready };
        std::atomic<int> state { Free };
        std::vector<float> l, r;
        int n = 0;
        double hitTime = 0.0, bpm = 120.0;   // where on the song it started (seconds from an on-beat grid point)
        int64_t hitSample = 0;               // ... and on the audio clock
        RenderedPtr result;
    };

    explicit Renderer (Kind k) : kind (k)
    {
        library = std::make_unique<nb::Library>();
        library->build();
    }
    ~Renderer() { stop(); }

    void start (float sampleRate)
    {
        stop();
        {
            std::lock_guard<std::mutex> lock (mutex);
            sr = sampleRate; quit = false; loadDirty = true;
            for (auto& s : slots)
            {
                s.l.assign ((size_t) (kMaxHitSeconds * sr) + 512, 0.0f); s.r.assign (s.l.size(), 0.0f);
                s.n = 0; s.result.reset(); s.state.store (Slot::Free);
            }
        }
        worker = std::thread ([this] { run(); });
    }
    void stop()
    {
        { std::lock_guard<std::mutex> lock (mutex); quit = true; }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    // message thread: what to render from now on
    void set (const Settings& s, HitPtr hit, double bpm)
    {
        std::lock_guard<std::mutex> lock (mutex);
        settings = s; loadHit = std::move (hit); loadBpm = bpm; loadDirty = true;
        cv.notify_all();
    }

    bool busy() const noexcept { return isBusy.load(); }
    RenderedPtr shown() { std::lock_guard<std::mutex> lock (shownMutex); return shownResult; }      // for the display
    HitPtr lastCapture() { std::lock_guard<std::mutex> lock (shownMutex); return lastCaptured; }
    int captures() const noexcept { return captureCount.load(); }

    nb::RtPublisher<LoadState> publisher;
    std::array<Slot, kSlots> slots;

    // ---- the renders themselves (also used directly by the tests)
    RenderedPtr render (const Settings& s, const float* l, const float* r, int n, float fs, double bpm, double hitTime) const
    {
        Rendered o; o.sampleRate = fs;
        n = std::min (n, (int) (kMaxHitSeconds * fs));
        if (n < 8) return std::make_shared<const Rendered> (std::move (o));
        if (kind == Kind::Friction)
        {
            const auto& rub = library->tables.rubbers;
            const auto& bodies = library->tables.bodies;
            if (rub.empty()) { o.l.assign (l, l + n); o.r.assign (r, r + n); return std::make_shared<const Rendered> (std::move (o)); }
            const nb::Engine2Material* roll = bodies.empty() ? nullptr : &bodies[(size_t) std::max (0, std::min (s.fs.rollBody, (int) bodies.size() - 1))];
            nb::StretchParams p = s.fs; p.wet = std::max (0.0f, std::min (1.0f, p.wet));
            nb::FrictionStretch f;
            f.render (p, rub[0], bpm, hitTime, 0.0, l, r, n, fs, o.l, o.r, roll);
            // an accent: at most 24 s
            const int cap = (int) (24.0f * fs), fade = (int) (0.5f * fs);
            if ((int) o.l.size() > cap)
            {
                o.l.resize ((size_t) cap); o.r.resize ((size_t) cap);
                for (int i = 0; i < fade; ++i) { const float g = (float) i / (float) fade; o.l[(size_t) (cap - 1 - i)] *= g; o.r[(size_t) (cap - 1 - i)] *= g; }
            }
        }
        else
        {
            // the bounces only (the hit itself is the dry sound, mixed by the player)
            nb::BounceDelay b;
            const auto plan = b.plan (s.bd, bpm, hitTime, 0.0);
            std::vector<nb::Bounce> rel = plan; for (auto& x : rel) x.t -= hitTime;
            const int len = n + (int) (((rel.empty() ? 0.0 : rel.back().t) + s.bd.tail + 0.2) * fs);
            o.l.assign ((size_t) len, 0.0f); o.r.assign ((size_t) len, 0.0f);
            std::vector<float> send (s.bd.room > 0.001f ? (size_t) len : 0, 0.0f);
            b.render (s.bd, rel, 0.0, l, r, n, fs, o.l.data(), o.r.data(), send.empty() ? nullptr : send.data(), len);
            if (! send.empty())
            {
                // the room each bounce excites
                static const int presets[] = { 4, 1, 5 };
                nb::PlaceSpec sp = nb::placePreset (presets[(size_t) std::max (0, std::min (2, s.place))]);
                nb::RoomBus bus; bus.configure (fs, sp);
                const int tail = (int) (std::min (sp.rt60 * 1.2f + 0.1f, 4.0f) * fs);
                o.l.resize ((size_t) (len + tail), 0.0f); o.r.resize ((size_t) (len + tail), 0.0f);
                const int lat = nb::RoomBus::kB;   // the convolution's one block of latency, taken back out
                const float g = 0.9f * s.bd.wet;
                for (int i = 0; i < len + tail + lat; ++i)
                {
                    float wl, wr; bus.process (i < len ? send[(size_t) i] : 0.0f, wl, wr);
                    const int oi = i - lat;
                    if (oi >= 0 && oi < len + tail) { o.l[(size_t) oi] += g * wl; o.r[(size_t) oi] += g * wr; }
                }
            }
        }
        for (auto& v : o.l) if (! std::isfinite (v)) v = 0.0f;
        for (auto& v : o.r) if (! std::isfinite (v)) v = 0.0f;
        return std::make_shared<const Rendered> (std::move (o));
    }

    // the hit at the engine's rate, as a Rendered (cubic resampling; hits are short)
    static RenderedPtr atRate (const nb::SampleData& s, float fs)
    {
        Rendered o; o.sampleRate = fs;
        const int n = (int) s.left.size();
        if (n < 2) return std::make_shared<const Rendered> (std::move (o));
        const auto& R = s.right.size() == s.left.size() ? s.right : s.left;
        const double ratio = (double) s.sampleRate / fs;
        const int m = std::min ((int) (n / ratio), (int) (kMaxHitSeconds * fs));
        o.l.resize ((size_t) m); o.r.resize ((size_t) m);
        auto at = [n] (const std::vector<float>& x, int k) { return x[(size_t) std::min (n - 1, std::max (0, k))]; };
        for (int i = 0; i < m; ++i)
        {
            const double pos = i * ratio; const int k = (int) pos; const float f = (float) (pos - k);
            auto cub = [&] (const std::vector<float>& x)
            {
                const float y0 = at (x, k - 1), y1 = at (x, k), y2 = at (x, k + 1), y3 = at (x, k + 2);
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * f + c2) * f + c1) * f + y1;
            };
            o.l[(size_t) i] = cub (s.left); o.r[(size_t) i] = cub (R);
        }
        return std::make_shared<const Rendered> (std::move (o));
    }

    // the off-beat hit's time, in seconds from an on-beat grid point (the second step of a swung pair)
    double oddTime (const Settings& s, double bpm) const
    {
        nb::RateSpec r = kind == Kind::Friction ? s.fs.rate : s.bd.rate;
        if (kind == Kind::Bounce) { r.beats = s.bd.gridBeats; r.ms = s.bd.gridMs; }
        if (! r.tempo) return 0.0;
        return nb::SwingGrid::make (r, bpm, 0.0, 0.0).timeOf (1);
    }

private:
    void keep (const RenderedPtr& r)
    {
        // replaced renders stay alive two minutes, so the audio thread never frees one
        const auto now = std::chrono::steady_clock::now();
        graveyard.push_back ({ now, r });
        while (! graveyard.empty() && now - graveyard.front().first > std::chrono::seconds (120)) graveyard.pop_front();
    }

    void run()
    {
        while (true)
        {
            Settings s; HitPtr hit; double bpm = 120.0; bool doLoad = false; float fs = 48000.0f;
            {
                std::unique_lock<std::mutex> lock (mutex);
                cv.wait_for (lock, std::chrono::milliseconds (3));   // captured hits arrive without a notify (the audio thread never signals)
                if (quit) return;
                s = settings; fs = sr;
                if (loadDirty) { doLoad = true; hit = loadHit; bpm = loadBpm; loadDirty = false; }
            }
            publisher.collect();
            // ---- caught hits first: they are playing against the clock
            bool any = false;
            for (int pass = 0; pass < kSlots; ++pass)
            {
                Slot* next = nullptr;
                for (auto& sl : slots) if (sl.state.load() == Slot::Queued && (next == nullptr || sl.hitSample < next->hitSample)) next = &sl;
                if (next == nullptr) break;
                any = true; isBusy.store (true);
                next->state.store (Slot::Rendering);
                auto r = render (s, next->l.data(), next->r.data(), next->n, fs, next->bpm, next->hitTime);
                keep (r);
                {
                    auto cap = std::make_shared<nb::SampleData>();
                    cap->sampleRate = fs; cap->path = "Caught hit";
                    cap->left.assign (next->l.begin(), next->l.begin() + next->n); cap->right.assign (next->r.begin(), next->r.begin() + next->n);
                    std::lock_guard<std::mutex> lock (shownMutex);
                    shownResult = r; lastCaptured = cap;
                }
                next->result = r;
                captureCount.fetch_add (1);
                next->state.store (Slot::Ready);
            }
            // ---- the loaded hit, in both versions
            if (doLoad)
            {
                isBusy.store (true);
                auto st = std::make_unique<LoadState>();
                st->sr = fs;
                if (hit && hit->left.size() > 8)
                {
                    st->dry = atRate (*hit, fs); keep (st->dry);
                    for (int parity = 0; parity < 2; ++parity)
                    {
                        if (newer()) break;
                        const double t = parity == 0 ? 0.0 : oddTime (s, bpm);
                        if (parity == 1 && t <= 0.0) { st->wet[1] = st->wet[0]; break; }
                        st->wet[(size_t) parity] = render (s, st->dry->l.data(), st->dry->r.data(), st->dry->size(), fs, bpm, t);
                        keep (st->wet[(size_t) parity]);
                        if (parity == 0)
                        {
                            { std::lock_guard<std::mutex> lock (shownMutex); shownResult = st->wet[0]; }
                            auto first = std::make_unique<LoadState> (*st);   // audible as soon as the first version is done
                            publisher.publish (std::move (first));
                        }
                    }
                }
                else { std::lock_guard<std::mutex> lock (shownMutex); shownResult.reset(); }
                if (! newer()) publisher.publish (std::move (st));
            }
            if (! any && ! doLoad) isBusy.store (false);
        }
    }
    bool newer() { std::lock_guard<std::mutex> lock (mutex); return quit || loadDirty; }

    Kind kind;
    std::unique_ptr<nb::Library> library;
    std::thread worker;
    std::mutex mutex, shownMutex;
    std::condition_variable cv;
    bool quit = false, loadDirty = false;
    float sr = 48000.0f;
    Settings settings;
    HitPtr loadHit;
    double loadBpm = 120.0;
    RenderedPtr shownResult;
    HitPtr lastCaptured;
    std::deque<std::pair<std::chrono::steady_clock::time_point, RenderedPtr>> graveyard;
    std::atomic<bool> isBusy { false };
    std::atomic<int> captureCount { 0 };
};

// ====================================================================== the audio side
class Core
{
public:
    explicit Core (Kind k) : kind (k), renderer (k) { pool = std::make_unique<nb::PlayerPool>(); }

    void prepare (float sampleRate, int maxBlock)
    {
        sr = sampleRate;
        pool->prepare (sr);
        scratchL.assign ((size_t) std::max (maxBlock, 8192), 0.0f); scratchR.assign (scratchL.size(), 0.0f);
        pre.assign ((size_t) kPre, {});
        prePos = 0; envFast = envSlow = 0.0f; sinceOnset = 1 << 30; filling = nullptr;
        clock = 0;
        renderer.start (sr);
    }
    void release() { renderer.stop(); }

    void trigger (float velocity = 1.0f) noexcept { uiVelocity.store (velocity); uiTrigger.store (true); }

    // in and out may be the same buffers
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Transport& tr, const Midi* midi, int numMidi, const Live& live) noexcept
    {
        if (n <= 0) return;
        if (n > (int) scratchL.size()) { for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; } return; }   // a host breaking its promise
        const auto* ls = renderer.publisher.beginBlock();
        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i) { scratchL[(size_t) i] = inL != nullptr ? inL[i] : 0.0f; scratchR[(size_t) i] = inR != nullptr ? inR[i] : scratchL[(size_t) i]; inPeak = std::max (inPeak, std::max (std::fabs (scratchL[(size_t) i]), std::fabs (scratchR[(size_t) i]))); }
        const float thru = live.mode == Mode::Capture ? live.thru : 0.0f;
        for (int i = 0; i < n; ++i) { outL[i] = thru * scratchL[(size_t) i]; outR[i] = thru * scratchR[(size_t) i]; }
        meter.store (std::max (inPeak, meter.load() * 0.9f));

        const double beatsPerSample = tr.bpm / 60.0 / sr;
        auto parityAt = [&] (int offset) -> int
        {
            if (! tr.playing || live.stepBeats <= 0.0) return 0;
            const double ppq = tr.ppq + offset * beatsPerSample;
            return (int) (std::llround (ppq / live.stepBeats) & 1);
        };

        // ---- what starts in this block, in order
        struct Start { int offset; float velocity; float semis; };
        Start starts[kMaxStarts]; int numStarts = 0;
        auto addStart = [&] (int offset, float vel, float semis) { if (numStarts < kMaxStarts) starts[numStarts++] = { std::max (0, std::min (n - 1, offset)), vel, semis }; };
        if (live.mode == Mode::Load)
        {
            if (uiTrigger.exchange (false)) addStart (0, uiVelocity.load(), 0.0f);
            for (int i = 0; i < numMidi; ++i)
                if (midi[i].on) addStart (midi[i].offset, midi[i].velocity, live.keytrack ? (float) (midi[i].note - 60) : 0.0f);
            if (live.loopBars > 0 && tr.playing && beatsPerSample > 0.0)
            {
                const double period = live.loopBars * std::max (1.0, tr.beatsPerBar);
                const double end = tr.ppq + n * beatsPerSample;
                double t = std::ceil ((tr.ppq - 1.0e-9) / period) * period;
                if (t < end) addStart ((int) ((t - tr.ppq) / beatsPerSample), 0.9f, 0.0f);
            }
            for (int a = 1; a < numStarts; ++a) for (int b = a; b > 0 && starts[b].offset < starts[b - 1].offset; --b) std::swap (starts[b], starts[b - 1]);
        }
        else
        {
            uiTrigger.store (false);
            catchHits (n, tr, live, beatsPerSample);
            // renders that came back: join each where it would be by now
            for (auto& sl : renderer.slots)
                if (sl.state.load() == Renderer::Slot::Ready)
                {
                    const int64_t late = clock - sl.hitSample;
                    if (sl.result && late >= 0 && late < (int64_t) sl.result->size() - 64)
                    {
                        nb::PlayRequest rq; rq.rendered = std::move (sl.result); rq.gain = 1.0f; rq.fadeInMs = late > 0 ? 6.0f : 0.0f; rq.bus = 2; rq.startPos = (double) late;
                        pool->play (rq);
                    }
                    sl.result.reset();
                    sl.state.store (Renderer::Slot::Free);
                }
        }

        // ---- play, a segment at a time, so starts land on their sample
        int done = 0;
        for (int si = 0; si <= numStarts; ++si)
        {
            const int until = si < numStarts ? starts[si].offset : n;
            if (until > done) { pool->render (outL + done, outR + done, until - done, nullptr, -1); done = until; }
            if (si < numStarts && ls != nullptr)
            {
                const auto& st = starts[si];
                if (live.choke) pool->allOff();
                const int parity = parityAt (st.offset);
                const RenderedPtr& wet = ls->wet[(size_t) parity] ? ls->wet[(size_t) parity] : ls->wet[(size_t) (1 - parity)];
                nb::PlayRequest rq; rq.gain = st.velocity; rq.semis = st.semis; rq.bus = 2;
                if (wet) { rq.rendered = wet; pool->play (rq); }
                if (ls->dry && live.dry > 0.0005f) { rq.rendered = ls->dry; rq.gain = st.velocity * live.dry; pool->play (rq); }
                hits.fetch_add (1);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            float l = outL[i] * live.level, r = outR[i] * live.level;
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            outL[i] = l; outR[i] = r;
        }
        renderer.publisher.endBlock();
        clock += n;
    }

    Kind kind;
    Renderer renderer;
    std::atomic<uint32_t> hits { 0 };      // hits started (load) or caught (capture): the window lights with them
    std::atomic<float> meter { 0.0f };     // input peak
    int voices() const noexcept { return pool->activeCount(); }

private:
    static constexpr int kMaxStarts = 32, kPre = 96;

    // the hit catcher: a hit starts when the fast envelope crosses the threshold and stands clear of the slow one
    void catchHits (int n, const Transport& tr, const Live& live, double beatsPerSample) noexcept
    {
        const float aF = 1.0f - std::exp (-1.0f / (0.0005f * sr)), rF = 1.0f - std::exp (-1.0f / (0.012f * sr));
        const float aS = 1.0f - std::exp (-1.0f / (0.03f * sr)), rS = 1.0f - std::exp (-1.0f / (0.25f * sr));
        const int retrig = std::max (1, (int) (live.retrigMs * 0.001f * sr));
        const int capLen = std::max (64, std::min ((int) (live.captureMs * 0.001f * sr), (int) (Renderer::kMaxHitSeconds * sr)));
        for (int i = 0; i < n; ++i)
        {
            const float l = scratchL[(size_t) i], r = scratchR[(size_t) i];
            const float x = std::max (std::fabs (l), std::fabs (r));
            envFast += (x > envFast ? aF : rF) * (x - envFast);
            envSlow += (x > envSlow ? aS : rS) * (x - envSlow);
            if (sinceOnset < (1 << 30)) ++sinceOnset;
            if (envFast > live.threshold && envFast > 1.8f * envSlow && sinceOnset >= retrig)
            {
                sinceOnset = 0;
                if (filling != nullptr) { filling->state.store (Renderer::Slot::Queued); filling = nullptr; }   // the hit before ends here
                for (auto& sl : renderer.slots)
                {
                    int expected = Renderer::Slot::Free;
                    if (sl.state.compare_exchange_strong (expected, Renderer::Slot::Filling)) { filling = &sl; break; }
                }
                if (filling != nullptr)
                {
                    if (live.choke) pool->allOff();
                    // the last 2 ms before the crossing belong to the hit (its first rise)
                    filling->n = 0;
                    for (int k = 0; k < kPre; ++k) { const auto& p = pre[(size_t) ((prePos + k) % kPre)]; filling->l[(size_t) filling->n] = p.first; filling->r[(size_t) filling->n] = p.second; ++filling->n; }
                    filling->hitSample = clock + i - kPre;
                    filling->bpm = tr.bpm;
                    filling->hitTime = tr.playing ? std::max (0.0, (tr.ppq + (i - kPre) * beatsPerSample) * 60.0 / std::max (1.0, tr.bpm)) : 0.0;
                    hits.fetch_add (1);
                }
            }
            if (filling != nullptr)
            {
                if (filling->n < (int) filling->l.size()) { filling->l[(size_t) filling->n] = l; filling->r[(size_t) filling->n] = r; ++filling->n; }
                if (filling->n >= capLen + kPre)
                {
                    // a short fade so a ringing hit does not end on a click
                    const int fade = std::min (filling->n / 4, (int) (0.01f * sr));
                    for (int k = 0; k < fade; ++k) { const float g = (float) k / (float) fade; filling->l[(size_t) (filling->n - 1 - k)] *= g; filling->r[(size_t) (filling->n - 1 - k)] *= g; }
                    filling->state.store (Renderer::Slot::Queued); filling = nullptr;
                }
            }
            pre[(size_t) prePos] = { l, r }; prePos = (prePos + 1) % kPre;
        }
    }

    float sr = 48000.0f;
    std::unique_ptr<nb::PlayerPool> pool;
    std::vector<float> scratchL, scratchR;
    std::vector<std::pair<float, float>> pre;
    int prePos = 0, sinceOnset = 1 << 30;
    float envFast = 0.0f, envSlow = 0.0f;
    Renderer::Slot* filling = nullptr;
    int64_t clock = 0;
    std::atomic<bool> uiTrigger { false };
    std::atomic<float> uiVelocity { 1.0f };
};
} // namespace nbfx
