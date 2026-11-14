// NeoBeat 3.0 — the live effect units: every effected hit, as it sounds.
//
// A unit is one hit going through a chain of live effects (LiveFx.h): a node's effect, the lane's
// inserts, a Bounce Delay window. Its source is the hit itself, either a prepared copy (the usual
// case: nothing to compute) or the lane's own voice routed in when no copy is ready yet. Controls that
// move reach a unit while it plays; a tail rings on with its hit.
//
// The spectral effects need the sound a frame (2048 samples) ahead. Hits the sequencer is about to
// play are known that far ahead, so their units start early and the effect lands exactly on the step
// (prestart, then claim at the hit). A hit that comes from nowhere (a key, a click) with a prepared
// copy is fed its first frame at once. Only a hit that comes from nowhere AND has no prepared copy is
// late by the frame; lateHits counts those, for the editor to say so.
//
// Limits: 24 units, 4 per lane, 14 spectral effects sounding at once (the oldest tail fades for a new
// hit), 40 effects in all.
#pragma once

#include "../dsp/LiveFx.h"
#include "Engine.h"

namespace nb
{
constexpr int kMaxChain = 5;

struct UnitRequest
{
    // the hit: a prepared copy, or (neither set) the lane's voice, routed in by the caller
    RenderedPtr rendered;
    std::shared_ptr<const SampleData> sample;
    float semis = 0.0f;
    // the chain, in order
    int numFx = 0;
    std::array<LiveSpec, kMaxChain> fx {};
    std::array<uint32_t, kMaxChain> fxId {};   // whose effect each is (0: nobody's): live control and the remembered level
    std::array<bool, kMaxChain> moved {};      // its settings were moved for this hit (modulation)
    int gateAfter = -1;                        // after this effect the sound is held to gateSamples from the hit, then fades (60 ms)
    int gateSamples = 0;
    // how it is heard
    float gain = 1.0f, pan = 0.0f;
    int pad = -1, choke = 0;
    const PadAlgo* algo = nullptr; float algoOffset = 0.0f, pitchHz = 200.0f;
    VoiceMod mod;
    HitContext hit;
    double bpm = 120.0, hitSeconds = 0.0;
    bool byHand = false;                       // a key or a click (not the sequencer): if it has to be late, the editor says so
    uint64_t signature = 0;                    // the same for the prestart and the hit that claims it
    int latency() const noexcept { int l = 0; for (int k = 0; k < numFx; ++k) l += LiveFx::latencyOf (fx[(size_t) k]); return l; }
};

class FxUnits
{
public:
    static constexpr int kUnits = Engine::kRoutes, kProcs = 40, kCaptures = 10, kPerPad = 4, kTolerance = 96, kBurst = 2048, kHeavy = 14;

    void prepare (float sampleRate, int maxBlock)
    {
        sr = sampleRate; maxN = std::max (16, maxBlock);
        pools.prepare (sr);
        for (auto& p : procs) { p.fx.prepare (sr, &pools); p.used = false; }
        capLen = (int) (3.0f * sr);
        for (auto& c : captures) { c.l.assign ((size_t) capLen, 0.0f); c.r.assign ((size_t) capLen, 0.0f); c.used = false; }
        for (auto& u : units)
        {
            u.inL.assign ((size_t) std::max (maxN, kBurst), 0.0f); u.inR.assign ((size_t) std::max (maxN, kBurst), 0.0f);
            u.active = false;
        }
        for (auto& g : gains) g = {};
        counter = 0;
    }

    // ---- starting
    // prestart: the hit is `latency` samples away; it sounds only once claimed. Returns the unit, or -1.
    int start (const UnitRequest& rq, bool prestart) noexcept
    {
        if (rq.numFx <= 0 || rq.numFx > kMaxChain) return -1;
        int ui = -1;
        for (int i = 0; i < kUnits; ++i) if (! units[(size_t) i].active) { ui = i; break; }
        if (ui < 0) return -1;
        int freeProcs = 0; for (const auto& p : procs) freeProcs += p.used ? 0 : 1;
        if (freeProcs < rq.numFx) return -1;
        Unit& u = units[(size_t) ui];
        u = Unit::fresh (std::move (u));
        u.active = true; u.claimed = ! prestart; u.order = ++counter;
        u.signature = rq.signature;
        u.rendered = rq.rendered; u.sample = rq.sample;
        u.live = ! rq.rendered && ! rq.sample;
        if (u.rendered) { u.L = u.rendered->l.data(); u.R = u.rendered->r.data(); u.len = u.rendered->size(); u.srcRate = u.rendered->sampleRate; }
        else if (u.sample) { u.L = u.sample->left.data(); u.R = u.sample->right.empty() ? u.sample->left.data() : u.sample->right.data(); u.len = (int) u.sample->left.size(); u.srcRate = u.sample->sampleRate; }
        u.inc = u.live ? 1.0 : (double) u.srcRate / sr * std::exp2 (rq.semis / 12.0);
        u.pos = 0.0; u.srcDone = ! u.live && u.len < 2;
        const bool direct = ! u.live && std::fabs (u.inc - 1.0) < 1.0e-9;
        const int spectralNow = spectralCount();
        u.n = rq.numFx; u.lat = 0;
        for (int k = 0; k < u.n; ++k)
        {
            Proc* p = nullptr; for (auto& q : procs) if (! q.used) { p = &q; break; }
            p->used = true; u.proc[(size_t) k] = p;
            u.fxId[(size_t) k] = rq.fxId[(size_t) k];
            u.skip[(size_t) k] = u.lat;                       // it starts when its own input does
            const LiveSpec& sp = rq.fx[(size_t) k];
            const Gain g = knownGain (rq.fxId[(size_t) k]);
            p->fx.start (sp, rq.bpm, rq.hitSeconds, g.gain, g.version == sp.version && ! rq.moved[(size_t) k], spectralNow > 8);
            u.lat += p->fx.latency();
            u.cap[(size_t) k] = nullptr; u.capFill[(size_t) k] = 0; u.capFinal[(size_t) k] = false;
            if (LiveFx::needsHit (sp) && ! (k == 0 && direct))
            {
                for (auto& c : captures) if (! c.used) { c.used = true; u.cap[(size_t) k] = &c; break; }
                if (u.cap[(size_t) k] != nullptr && k == 0 && ! u.live)
                {   // a prepared hit at another pitch or rate: the whole of it, as it will be fed
                    Capture& c = *u.cap[0];
                    const int m = std::min (capLen, (int) ((double) (u.len - 1) / u.inc));
                    for (int i = 0; i < m; ++i)
                    {
                        const double x = i * u.inc; const int i0 = (int) x; const float f = (float) (x - i0);
                        c.l[(size_t) i] = u.L[i0] + f * (u.L[i0 + 1] - u.L[i0]);
                        c.r[(size_t) i] = u.R[i0] + f * (u.R[i0 + 1] - u.R[i0]);
                    }
                    u.capFill[0] = std::max (0, m); u.capFinal[0] = true;
                }
            }
        }
        u.gateAfter = rq.gateAfter; u.gateSamples = rq.gateSamples; u.gateFade = std::max (1, (int) (0.06f * sr));
        u.untilHit = u.lat; u.overdue = 0; u.fed = 0;
        u.hit = rq.hit;
        setOutput (u, rq);
        if (! prestart)
        {
            if (u.live) { if (u.lat > 0) late (rq); u.untilHit = 0; }
            else if (u.lat > 0)
            {   // the first frame at once; anything beyond it (several spectral effects in a row) makes the hit that much late
                const int burst = std::min (u.lat, kBurst);
                feed (u, burst, nullptr, nullptr, nullptr, -1, false);
                if (u.lat > burst) late (rq);
                u.untilHit = 0;
            }
            arrive (u);
        }
        return ui;
    }

    // The hit a prestarted unit was waiting for. Returns the unit, or -1 (then start a fresh one).
    int claim (const UnitRequest& rq) noexcept
    {
        for (int i = 0; i < kUnits; ++i)
        {
            Unit& u = units[(size_t) i];
            if (! u.active || u.claimed || u.signature != rq.signature) continue;
            if (u.untilHit > kTolerance) continue;
            u.claimed = true; u.untilHit = 0;
            u.hit = rq.hit;
            for (int k = 0; k < u.n && k < rq.numFx; ++k) u.proc[(size_t) k]->fx.update (rq.fx[(size_t) k]);
            setOutput (u, rq);
            arrive (u);
            return i;
        }
        return -1;
    }
    bool waiting (uint64_t signature) const noexcept
    {
        for (const auto& u : units) if (u.active && ! u.claimed && u.signature == signature) return true;
        return false;
    }

    // ---- while they play
    // new settings for whoever's effect this is (a control moved, modulation)
    template <typename Fn> void refresh (Fn&& specFor) noexcept
    {
        for (auto& u : units)
        {
            if (! u.active) continue;
            for (int k = 0; k < u.n; ++k)
            {
                if (u.fxId[(size_t) k] == 0) continue;
                LiveSpec s;
                if (! specFor (u.fxId[(size_t) k], u.hit, s)) continue;
                const LiveSpec& c = u.proc[(size_t) k]->fx.current();
                if (s.kind != c.kind || (s.kind == 1 && s.fx != c.fx)) continue;
                if (s.kind == 1 ? s.p != c.p : ! sameBounce (s.bounce, c.bounce)) u.proc[(size_t) k]->fx.update (s);
            }
        }
    }

    void chokeGroup (int g) noexcept { if (g > 0) for (auto& u : units) if (u.active && u.claimed && u.choke == g) fade (u, 0.006f); }
    void allOff() noexcept { for (auto& u : units) if (u.active) { if (u.claimed) fade (u, 0.01f); else u.cancel = true; } }

    // ---- the block. Call order: begin (before the engine renders its voices), render (after).
    void begin (Engine& engine, int n) noexcept
    {
        for (int i = 0; i < kUnits; ++i)
        {
            Unit& u = units[(size_t) i];
            if (u.active && u.live)
            {
                std::fill (u.inL.begin(), u.inL.begin() + n, 0.0f); std::fill (u.inR.begin(), u.inR.begin() + n, 0.0f);
                engine.setRoute (i, u.inL.data(), u.inR.data());
            }
            else engine.setRoute (i, nullptr, nullptr);
        }
    }

    // adds into outL/outR (before the master)
    void render (Engine& engine, float* outL, float* outR, int n, float* tap, int tapPad) noexcept
    {
        for (int i = 0; i < kUnits; ++i)
        {
            Unit& u = units[(size_t) i];
            if (! u.active) continue;
            if (u.cancel) { release (engine, i); continue; }
            int m = n;
            if (! u.claimed)
            {
                m = std::max (0, std::min (n, u.untilHit));      // not past its hit
                if (m < n) { u.overdue += n - m; if (u.overdue > kTolerance) { release (engine, i); continue; } }
            }
            if (u.live) u.srcDone = u.fed > 0 && ! engine.routeBusy (i);
            if (m > 0) feed (u, m, outL, outR, tap, tapPad, true);
            if (u.finished) release (engine, i);
        }
    }

    int activeCount() const noexcept { int c = 0; for (const auto& u : units) c += u.active ? 1 : 0; return c; }
    // hits that had to sound late (no prepared copy and no warning). By hand: the editor tells the user once.
    // The sequencer's own can only be the very first hits after play is pressed or the song jumps.
    std::atomic<uint32_t> lateHits { 0 }, lateSeqHits { 0 };
    void late (const UnitRequest& rq) noexcept { (rq.byHand ? lateHits : lateSeqHits).fetch_add (1, std::memory_order_relaxed); }

private:
    struct Proc { LiveFx fx; bool used = false; };
    struct Capture { std::vector<float> l, r; bool used = false; };
    struct Gain { uint32_t id = 0; uint64_t version = 0; float gain = 0.0f; };

    struct Unit
    {
        bool active = false, claimed = false, live = false, srcDone = false, finished = false, cancel = false;
        uint64_t order = 0, signature = 0;
        RenderedPtr rendered; std::shared_ptr<const SampleData> sample;
        const float* L = nullptr; const float* R = nullptr;
        int len = 0; float srcRate = 48000.0f;
        double pos = 0.0, inc = 1.0;
        int n = 0, lat = 0;
        std::array<Proc*, kMaxChain> proc {};
        std::array<uint32_t, kMaxChain> fxId {};
        std::array<int, kMaxChain> skip {};
        std::array<Capture*, kMaxChain> cap {};
        std::array<int, kMaxChain> capFill {};
        std::array<bool, kMaxChain> capFinal {};
        int gateAfter = -1, gateSamples = 0, gateFade = 2880; int64_t gatePos = 0;
        int untilHit = 0, overdue = 0; int64_t fed = 0, outPos = 0;
        // output
        float gain = 1.0f, basePan = 0.0f, gL = 1.0f, gR = 1.0f;
        int pad = -1, choke = 0, tick = 0;
        float fadeGain = 1.0f, fadeStep = 0.0f;
        VoiceMod mod; AlgoState algo; bool algoOn = false;
        HitContext hit;
        std::vector<float> inL, inR;

        // everything back to its defaults, keeping the buffers
        static Unit fresh (Unit&& old) { Unit u; u.inL = std::move (old.inL); u.inR = std::move (old.inR); return u; }
    };

    static bool sameBounce (const BounceParams& a, const BounceParams& b) noexcept
    {
        return a.bounciness == b.bounciness && a.maxBounces == b.maxBounces && a.distStart == b.distStart && a.distEnd == b.distEnd
            && a.speed == b.speed && a.panRandom == b.panRandom && a.panCenter == b.panCenter && a.width == b.width && a.wet == b.wet
            && a.tail == b.tail && a.rate.tempo == b.rate.tempo && a.rate.beats == b.rate.beats && a.rate.ms == b.rate.ms
            && a.rate.swing == b.rate.swing && a.gridBeats == b.gridBeats && a.gridMs == b.gridMs && a.seed == b.seed;
    }

    int spectralCount() const noexcept { int c = 0; for (const auto& p : procs) if (p.used && p.fx.latency() > 0) ++c; return c; }

    void setOutput (Unit& u, const UnitRequest& rq) noexcept
    {
        u.gain = rq.gain; u.basePan = rq.pan; u.pad = rq.pad; u.choke = rq.choke;
        u.mod = rq.mod; u.mod.t = 0.0; u.tick = 0;
        u.algoOn = false;
        if (rq.algo != nullptr && rq.algo->type > 0) { u.algo.start (*rq.algo, sr, rq.pitchHz, rq.algoOffset); u.algoOn = u.algo.on(); }
        u.mod.update (0, sr);
        applyMod (u);
    }
    static void applyMod (Unit& u) noexcept
    {
        const float pan = std::max (-1.0f, std::min (1.0f, u.basePan + (u.mod.active() ? u.mod.pan : 0.0f)));
        const float g = u.gain * (u.mod.active() ? u.mod.levelMul : 1.0f);
        u.gL = g * (pan > 0.0f ? 1.0f - pan : 1.0f);
        u.gR = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }
    void fade (Unit& u, float seconds) noexcept
    {
        const float step = 1.0f / std::max (1.0f, seconds * sr);
        if (u.fadeStep < step) u.fadeStep = step;
    }

    // a hit has arrived on its lane: the oldest tails make room
    void arrive (Unit& me) noexcept
    {
        if (me.choke > 0) for (auto& u : units) if (&u != &me && u.active && u.claimed && u.choke == me.choke) fade (u, 0.006f);
        auto thin = [&] (bool samePad, int limit)
        {
            while (true)
            {
                int count = 0; Unit* oldest = nullptr;
                for (auto& u : units)
                {
                    if (! u.active || ! u.claimed || u.fadeStep > 0.0f || (samePad && u.pad != me.pad)) continue;
                    ++count;
                    if (&u != &me && (oldest == nullptr || u.order < oldest->order)) oldest = &u;
                }
                if (count <= limit || oldest == nullptr) break;
                fade (*oldest, 0.03f);
            }
        };
        if (me.pad >= 0) thin (true, kPerPad);
        thin (false, kUnits - 4);
        // the spectral effects are the heavy ones: only so many at once
        while (true)
        {
            int heavy = 0; Unit* oldest = nullptr;
            for (auto& u : units)
            {
                if (! u.active || ! u.claimed || u.fadeStep > 0.0f) continue;
                int mine = 0; for (int k = 0; k < u.n; ++k) mine += u.proc[(size_t) k]->fx.latency() > 0 ? 1 : 0;
                heavy += mine;
                if (mine > 0 && &u != &me && (oldest == nullptr || u.order < oldest->order)) oldest = &u;
            }
            if (heavy <= kHeavy || oldest == nullptr) break;
            fade (*oldest, 0.03f);
        }
    }

    Gain knownGain (uint32_t id) const noexcept
    {
        if (id == 0) return {};
        const Gain& g = gains[(size_t) (id * 2654435761u >> 24)];
        return g.id == id ? g : Gain {};
    }
    void remember (uint32_t id, uint64_t version, float gain) noexcept
    {
        if (id == 0 || gain <= 0.0f) return;
        gains[(size_t) (id * 2654435761u >> 24)] = { id, version, gain };
    }

    void release (Engine& engine, int i) noexcept
    {
        Unit& u = units[(size_t) i];
        for (int k = 0; k < u.n; ++k)
        {
            Proc* p = u.proc[(size_t) k];
            if (p == nullptr) continue;
            if (u.claimed) remember (u.fxId[(size_t) k], p->fx.current().version, p->fx.measuredGain());
            p->fx.stop(); p->used = false;
            if (u.cap[(size_t) k] != nullptr) u.cap[(size_t) k]->used = false;
            u.proc[(size_t) k] = nullptr; u.cap[(size_t) k] = nullptr;
        }
        if (u.live) { engine.killRoute (i); engine.setRoute (i, nullptr, nullptr); }
        u.rendered.reset(); u.sample.reset();
        u.n = 0; u.active = false;
    }

    // m samples of the hit through the chain. outL == nullptr: fed ahead, nothing is heard.
    void feed (Unit& u, int m, float* outL, float* outR, float* tap, int tapPad, bool fromBlock) noexcept
    {
        float* x = u.inL.data(); float* y = u.inR.data();
        if (! u.live)
        {
            for (int i = 0; i < m; ++i)
            {
                const int i0 = (int) u.pos;
                if (u.srcDone || i0 >= u.len - 1) { u.srcDone = true; x[i] = 0.0f; y[i] = 0.0f; continue; }
                const float f = (float) (u.pos - i0);
                x[i] = u.L[i0] + f * (u.L[i0 + 1] - u.L[i0]);
                y[i] = u.R[i0] + f * (u.R[i0 + 1] - u.R[i0]);
                u.pos += u.inc;
            }
        }
        else if (! fromBlock) { std::fill (x, x + m, 0.0f); std::fill (y, y + m, 0.0f); }
        bool upstreamDone = u.srcDone;
        for (int k = 0; k < u.n; ++k)
        {
            LiveFx& fx = u.proc[(size_t) k]->fx;
            const int s = std::min (m, u.skip[(size_t) k]);          // before its input begins: nothing
            u.skip[(size_t) k] -= s;
            const int run = m - s;
            if (run > 0)
            {
                HitView view;
                if (Capture* c = u.cap[(size_t) k])
                {
                    if (! u.capFinal[(size_t) k])
                    {
                        const int room = std::min (run, capLen - u.capFill[(size_t) k]);
                        if (room > 0)
                        {
                            std::copy (x + s, x + s + room, c->l.begin() + u.capFill[(size_t) k]);
                            std::copy (y + s, y + s + room, c->r.begin() + u.capFill[(size_t) k]);
                            u.capFill[(size_t) k] += room;
                        }
                        if (upstreamDone || u.capFill[(size_t) k] >= capLen) u.capFinal[(size_t) k] = true;
                    }
                    view = { c->l.data(), c->r.data(), u.capFill[(size_t) k], u.capFinal[(size_t) k] };
                }
                else if (k == 0 && ! u.live) view = { u.L, u.R, u.len, true };
                fx.process (x + s, y + s, x + s, y + s, run, view, upstreamDone);
            }
            if (s > 0) { std::fill (x, x + s, 0.0f); std::fill (y, y + s, 0.0f); }
            upstreamDone = upstreamDone && fx.finished();
            if (k == u.gateAfter && u.gateSamples > 0)
            {   // the node's reach: held to its steps, then a short fade
                const int64_t at0 = u.fed - (u.lat - latencyAfter (u, k));     // this stage's place from the hit
                for (int i = 0; i < m; ++i)
                {
                    const int64_t at = at0 + i;
                    if (at <= u.gateSamples) continue;
                    const float g = std::max (0.0f, 1.0f - (float) (at - u.gateSamples) / (float) u.gateFade);
                    x[i] *= g; y[i] *= g;
                }
                if (at0 > u.gateSamples + u.gateFade)
                {
                    if (k == u.n - 1) u.finished = true;
                    upstreamDone = true;
                }
            }
        }
        u.fed += m; u.untilHit = std::max (0, u.untilHit - m);
        if (upstreamDone) u.finished = true;
        if (outL == nullptr) return;
        const bool tapped = tap != nullptr && u.pad == tapPad;
        for (int i = 0; i < m; ++i)
        {
            if (u.tick-- <= 0) { u.tick = 31; if (u.mod.active()) { u.mod.update (32, sr); applyMod (u); } }
            float l = x[i], r = y[i];
            if (u.algoOn) u.algo.process (l, r);
            if (u.fadeStep > 0.0f)
            {
                u.fadeGain -= u.fadeStep;
                if (u.fadeGain <= 0.0f) { u.finished = true; break; }
                l *= u.fadeGain; r *= u.fadeGain;
            }
            const float ol = l * u.gL, orr = r * u.gR;
            outL[i] += ol; outR[i] += orr;
            if (tapped) tap[i] += 0.5f * (ol + orr);
        }
    }
    // the latency of the effects after stage k
    static int latencyAfter (const Unit& u, int k) noexcept
    {
        int l = 0; for (int j = k + 1; j < u.n; ++j) l += u.proc[(size_t) j]->fx.latency(); return l;
    }

    float sr = 48000.0f;
    int maxN = 512, capLen = 144000;
    uint64_t counter = 0;
    LiveFxPools pools;
    std::array<Proc, kProcs> procs;
    std::array<Capture, kCaptures> captures;
    std::array<Unit, kUnits> units;
    std::array<Gain, 256> gains {};
};
} // namespace nb
