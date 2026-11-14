// NeoBeat 3.0 — the live state: everything the audio thread needs to run the effects as they are set
// right now. It holds no rendered sound, so building it costs nothing: the editor builds and publishes
// one the moment anything changes (a control, a node, the tempo), and the next block hears it.
//
//   nodes        each node's effect on its hit (scene, lane, step), with its reach and its modulation
//   inserts      each lane's three inserts
//   bounce       the Bounce Delay windows, and who they listen to
//   zone effects the key map's
//   MIDI out     the notes NeoBeat sends
//   follow       the Layermax rows that follow a lane
//
// An effect that cannot run live (Friction Stretch: it needs the whole hit before it can start) is
// marked prepared: the render worker makes it ahead, as before.
#pragma once

#include "../dsp/LiveFx.h"
#include "ModSystem.h"
#include "Rt.h"

namespace nb
{
// who an effect belongs to: remembered levels and live control are keyed by this
inline uint32_t nodeKey (int scene, int pad, int step) noexcept { return ((uint32_t) scene << 13) | ((uint32_t) pad << 9) | (uint32_t) step; }
inline uint32_t nodeFxId (uint32_t key) noexcept { return 0x10000u + key; }
inline uint32_t insertFxId (int pad, int k) noexcept { return 0x100u + (uint32_t) (pad * 4 + k); }
inline uint32_t bounceFxId (int slot) noexcept { return 0x200u + (uint32_t) slot; }
inline uint32_t zoneFxId (int zone) noexcept { return 0x1000u + (uint32_t) (zone & 0xfff); }

// A cell's effect as a live one (kind 0 when it cannot run live or is empty).
inline LiveSpec liveSpecOf (const CellFx& c)
{
    LiveSpec s;
    if (c.kind == CellFx::Library)
    {
        s.kind = 1; s.fx = c.fx; s.seed = c.settings.seed;
        const auto def = FxSettings::defaults (c.fx);
        for (size_t i = 0; i < s.p.size(); ++i) s.p[i] = i < c.settings.p.size() ? c.settings.p[i] : (i < def.p.size() ? def.p[i] : 0.0f);
    }
    else if (c.kind == CellFx::BounceDelayFx) { s.kind = 2; s.bounce = c.bounce; }
    Hasher h; h.i (s.kind).i ((int) s.fx).u (s.seed);
    for (float v : s.p) h.f (v);
    const auto& b = s.bounce;
    h.i (b.rate.tempo).d (b.rate.beats).d (b.rate.ms).f (b.rate.swing).d (b.gridBeats).d (b.gridMs).f (b.bounciness).i (b.maxBounces)
     .f (b.distStart).f (b.distEnd).f (b.speed).f (b.panRandom).f (b.panCenter).f (b.width).f (b.wet).f (b.tail).u (b.seed);
    s.version = h.h | 1u;
    return s;
}
inline bool isFriction (const CellFx& c) noexcept { return c.kind == CellFx::FrictionStretchFx; }

// The main control of a live effect (what a node's height, and modulation of it, moves).
struct LiveMain
{
    int index = -1; float lo = 0, hi = 1, qlo = 0, qhi = 1; bool log = false, invert = false, keepSign = false, round = false, on = false;
    void apply (LiveSpec& s, float height) const noexcept
    {
        if (! on) return;
        float h = std::min (1.0f, std::max (0.0f, height));
        if (invert) h = 1.0f - h;
        float v = (log && lo > 0 && hi > 0) ? lo * std::pow (hi / lo, h) : lo + (hi - lo) * h;
        if (s.kind == 2) { s.bounce.bounciness = v; return; }
        if (index < 0 || index >= (int) s.p.size()) return;
        if (round) v = std::round (v);
        if (keepSign && s.p[(size_t) index] < 0) v = -v;
        s.p[(size_t) index] = std::min (qhi, std::max (qlo, v));
    }
};
inline LiveMain liveMainOf (const CellFx& c)
{
    LiveMain m;
    if (c.kind != CellFx::Library && c.kind != CellFx::BounceDelayFx) return m;
    const MainControl mc = mainControl (c);
    m.on = true; m.index = mc.index; m.lo = mc.lo; m.hi = mc.hi; m.log = mc.log; m.invert = mc.invert; m.keepSign = mc.keepSign;
    if (c.kind == CellFx::Library && mc.index >= 0 && mc.index < (int) matrixFxInfo()[(size_t) c.fx].params.size())
    {
        const auto& q = matrixFxInfo()[(size_t) c.fx].params[(size_t) mc.index];
        m.qlo = q.lo; m.qhi = q.hi;
        m.round = q.unit[0] == '\0' && mc.hi > 2 && std::string (mc.name) != "amount";
    }
    return m;
}

struct LiveNode
{
    uint32_t key = 0;
    int scene = 0, pad = 0, step = 0, span = 2, cellIndex = 0;
    float height = 0.5f;
    bool prepared = false;         // made ahead by the render worker (Friction Stretch in the chain)
    LiveSpec spec;                 // height already applied
    LiveMain main;
    std::vector<ResolvedRoute> routes;   // modulation of the main control
};

struct LiveBounce
{
    bool on = false;
    SourceKind kind = SourceKind::None; int index = 0;
    uint32_t cellKey = 0;          // Cell source: the node it listens to
    LiveSpec spec;                 // the bounces without the hit
    float level = 0.7f; bool replace = false;
    std::vector<ResolvedRoute> routes;
};

struct LiveFollow { int row = 0; GridFollow f; };

struct LiveState
{
    bool oneShotOn = true;
    std::vector<LiveNode> nodes;                                   // sorted by key
    std::array<std::array<LiveSpec, kNumInserts>, kNumPads> inserts {};
    std::array<uint8_t, kNumPads> insertMode {};                   // 0 none, 1 live, 2 prepared
    std::array<LiveBounce, kNumSlots> bounce {};
    std::array<std::vector<int>, kNumPads> padBounce;              // the Bounce Delay windows listening to each lane
    std::array<std::vector<int>, GridPattern::kSize> rowBounce;    // ... and to each Layermax row
    std::vector<LiveSpec> zoneFx;                                  // by zone (kind 0: none, or prepared)
    std::vector<uint8_t> zonePrepared;
    bool anyLatency = false;       // a spectral effect somewhere: hits are prepared a frame ahead
    MidiSettings midi;
    SongSettings song;
    std::array<std::vector<LiveFollow>, kNumPads> follow;          // by lane
    bool anyFollowShift = false, anyFollowFill = false;

    const LiveNode* nodeAt (int scene, int pad, int step) const noexcept
    {
        const uint32_t k = nodeKey (scene, pad, step);
        size_t lo = 0, hi = nodes.size();
        while (lo < hi) { const size_t mid = (lo + hi) / 2; if (nodes[mid].key < k) lo = mid + 1; else hi = mid; }
        return lo < nodes.size() && nodes[lo].key == k ? &nodes[lo] : nullptr;
    }
};

inline void applyBounceMod (BounceParams& p, int param, float m) noexcept
{
    float* t[] = { &p.bounciness, &p.distStart, &p.distEnd, &p.speed, &p.panRandom, &p.width, &p.room, &p.wet };
    const float range[] = { 0.5f, 20.0f, 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    if (param < 0 || param >= 8) return;
    *t[param] = std::max (0.0f, *t[param] + m * range[param]);
    if (param == 0) p.bounciness = std::min (0.97f, p.bounciness);
}

inline std::unique_ptr<LiveState> buildLiveState (const Project& P)
{
    auto out = std::make_unique<LiveState>();
    LiveState& S = *out;
    S.midi = P.midi; S.song = P.song;
    S.oneShotOn = P.matrix.oneShotOn;
    auto latent = [&] (const LiveSpec& s) { if (LiveFx::latencyOf (s) > 0) S.anyLatency = true; };

    // inserts
    for (int p = 0; p < kNumPads; ++p)
    {
        bool any = false, friction = false;
        for (int k = 0; k < kNumInserts; ++k)
        {
            const auto& c = P.padFx[(size_t) p].inserts[(size_t) k];
            if (c.kind == CellFx::None) continue;
            any = true;
            if (isFriction (c)) friction = true;
            S.inserts[(size_t) p][(size_t) k] = liveSpecOf (c);
        }
        S.insertMode[(size_t) p] = ! any ? 0 : (friction ? 2 : 1);
        if (any && ! friction) for (const auto& s : S.inserts[(size_t) p]) latent (s);
    }

    // routes
    std::vector<ResolvedRoute> cellRoutes, bdRoutes;
    for (const auto& r : P.mod.routes)
    {
        ResolvedRoute rr; rr.src = r.src; rr.depth = r.depth;
        if (! parseDest (r.dest, rr.dest, rr.target, rr.param)) continue;
        if (rr.dest == Dest::CellMain) cellRoutes.push_back (rr);
        else if (rr.dest == Dest::BdParam) bdRoutes.push_back (rr);
    }

    // nodes
    const auto& cells = P.matrix.oneShot.cells;
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        const auto& c = cells[ci];
        if (c.fx.kind == CellFx::None) continue;
        LiveNode n;
        n.scene = std::max (0, std::min (c.scene, kNumScenes - 1));
        n.pad = std::max (0, std::min (c.pad, kNumPads - 1)); n.step = std::max (0, std::min (c.step, Pattern::kMaxSteps - 1));
        n.span = std::max (1, c.span); n.height = c.height; n.cellIndex = (int) ci;
        n.key = nodeKey (n.scene, n.pad, n.step);
        n.prepared = isFriction (c.fx) || S.insertMode[(size_t) n.pad] == 2;
        n.main = liveMainOf (c.fx);
        n.spec = liveSpecOf (c.fx);
        n.main.apply (n.spec, n.height);
        for (const auto& r : cellRoutes) if (r.target == (int) ci) n.routes.push_back (r);
        if (! n.prepared) latent (n.spec);
        S.nodes.push_back (std::move (n));
    }
    std::stable_sort (S.nodes.begin(), S.nodes.end(), [] (const LiveNode& a, const LiveNode& b) { return a.key < b.key; });
    // the first node on a hit wins (as before)
    S.nodes.erase (std::unique (S.nodes.begin(), S.nodes.end(), [] (const LiveNode& a, const LiveNode& b) { return a.key == b.key; }), S.nodes.end());

    // Bounce Delay windows
    for (int si = 0; si < kNumSlots; ++si)
    {
        const auto& b = P.bd[(size_t) si];
        LiveBounce& lb = S.bounce[(size_t) si];
        if (! b.on || b.src.kind == SourceKind::None) continue;
        lb.on = true; lb.kind = b.src.kind; lb.index = b.src.index; lb.level = b.level; lb.replace = b.replace;
        lb.spec = liveSpecOf (CellFx::bouncing (b.p)); lb.spec.wetOnly = true;
        for (const auto& r : bdRoutes) if (r.target == si) lb.routes.push_back (r);
        if (lb.kind == SourceKind::Pad && lb.index >= 0 && lb.index < kNumPads) S.padBounce[(size_t) lb.index].push_back (si);
        else if (lb.kind == SourceKind::GridRow && lb.index >= 0 && lb.index < GridPattern::kSize) S.rowBounce[(size_t) lb.index].push_back (si);
        else if (lb.kind == SourceKind::Cell && lb.index >= 0 && lb.index < (int) cells.size())
        {
            const auto& c = cells[(size_t) lb.index];
            lb.cellKey = nodeKey (std::max (0, std::min (c.scene, kNumScenes - 1)), std::max (0, std::min (c.pad, kNumPads - 1)), std::max (0, std::min (c.step, Pattern::kMaxSteps - 1)));
        }
        else lb.on = false;
    }

    // zone effects
    S.zoneFx.resize (P.keymap.zones.size()); S.zonePrepared.assign (P.keymap.zones.size(), 0);
    for (size_t zi = 0; zi < P.keymap.zones.size(); ++zi)
    {
        const auto& z = P.keymap.zones[zi];
        if (z.fx.kind == CellFx::None) continue;
        if (isFriction (z.fx)) { S.zonePrepared[zi] = 1; continue; }
        S.zoneFx[zi] = liveSpecOf (z.fx);
        latent (S.zoneFx[zi]);
    }

    // Layermax rows that follow a lane
    for (int row = 0; row < (int) P.grid.follow.size() && row < GridPattern::kSize; ++row)
    {
        const auto& f = P.grid.follow[(size_t) row];
        if (! f.on || f.lane < 0 || f.lane >= kNumPads) continue;
        S.follow[(size_t) f.lane].push_back ({ row, f });
        if (f.mode == 1) S.anyFollowFill = true;
        else if (std::fabs (f.offsetMs) > 0.01f) S.anyFollowShift = true;
    }
    return out;
}
} // namespace nb
