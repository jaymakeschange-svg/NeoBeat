// Per-chop placement, listened to.
//
// The Stereoroom draws every chop of a loop as its own node. This asks whether that is true of the
// sound as well as the picture: does placing one chop left and another right actually put them
// there, does a locked chop refuse to move, and does a travelling chop arrive somewhere different
// at different points in the bar. A room that only looks right is a drawing.

#include "neobeat/shared/rooms/Loops.h"
#include "neobeat/shared/rooms/Rooms.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 256;

struct Heard
{
    double rmsL = 0.0, rmsR = 0.0;
    // Where the sound sits: -1 hard left, 0 the middle, +1 hard right.
    //
    // This used to be a channel ratio in dB, and that was a bad measure. When a chop is hard over,
    // the quiet side is zero, so the ratio is pinned by whichever epsilon stops the division - it
    // printed numbers like +103 dB, which is not a loudness and not a position, only "silent on
    // the other side, as far as the floor allows". It could not tell a full sweep from a 95% one,
    // and its asymmetry was filter tails rather than anything about the pan.
    //
    // An equal-power pan sets L = cos(a) and R = sin(a) with a = (pi/4)(x+1), so the angle between
    // the two channels gives x back. Bounded, mirror-symmetric, and it says what it measures.
    double where() const
    {
        const double a = std::atan2 (std::max (0.0, rmsR), std::max (0.0, rmsL));
        return std::max (-1.0, std::min (1.0, a / 0.78539816339744831 - 1.0));
    }
    double level() const { return std::sqrt (0.5 * (rmsL * rmsL + rmsR * rmsR)); }
    bool silent() const { return level() < 1e-6; }
};

// a short tone, so a chop is audible and tells left from right
static std::shared_ptr<bc::PoolItem> tone (double hz)
{
    auto it = std::make_shared<bc::PoolItem>();
    it->name = "chop";
    it->sr = kSr;
    const int n = (int) (0.12 * kSr);
    it->l.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        it->l[(size_t) i] = (float) (0.6 * std::exp (-t * 6.0) * std::sin (6.283185307179586 * hz * t));
    }
    it->r = it->l;
    it->f.valid = true;
    return it;
}

// renders the loop over `beats`, measuring only the window where the chop in question sounds
static Heard listen (bc::LoopPlayer& player, double fromBeat, double toBeat, double bpm = 120.0)
{
    const double secondsPerBeat = 60.0 / bpm;
    const int skip = (int) (fromBeat * secondsPerBeat * kSr);
    const int want = (int) ((toBeat - fromBeat) * secondsPerBeat * kSr);

    std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
    Heard h;
    double sumL = 0.0, sumR = 0.0;
    long long counted = 0, at = 0;

    while (at < skip + want)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        bc::LoopHost host;
        host.valid = true; host.playing = true; host.bpm = bpm;
        host.ppq = (double) at / kSr / secondsPerBeat;
        player.process (l.data(), r.data(), kBlock, host);
        for (int i = 0; i < kBlock; ++i, ++at)
        {
            if (at < skip || at >= skip + want) continue;
            sumL += (double) l[(size_t) i] * l[(size_t) i];
            sumR += (double) r[(size_t) i] * r[(size_t) i];
            ++counted;
        }
    }
    if (counted > 0) { h.rmsL = std::sqrt (sumL / (double) counted); h.rmsR = std::sqrt (sumR / (double) counted); }
    return h;
}

// a loop of four chops, one to a beat
static bc::LoopSet makeLoop (const std::vector<std::shared_ptr<bc::PoolItem>>& sounds)
{
    bc::LoopSet set;
    set.numLayers = 1;
    set.bpm = 120.0;
    set.swing = 0.5f;
    auto& L = set.layers[0];
    L.bars = 1; L.perBar = 4;
    L.hits.clear();
    for (int i = 0; i < 4; ++i)
    {
        bc::LoopHit h;
        h.step = i;
        h.len = 1;
        h.sound = sounds[(size_t) i];
        L.hits.push_back (h);
    }
    return set;
}

static std::shared_ptr<bc::LoopPlacing> placing (const std::vector<nsr::Place>& places, int chops)
{
    auto p = std::make_shared<bc::LoopPlacing>();
    p->places = places;
    p->stride = chops;
    p->index.assign ((size_t) bc::kLoopLayers * (size_t) chops, -1);
    for (int i = 0; i < chops; ++i) p->index[(size_t) i] = i;
    return p;
}

int main()
{
    std::vector<std::shared_ptr<bc::PoolItem>> sounds;
    for (int i = 0; i < 4; ++i) sounds.push_back (tone (220.0 * std::pow (1.5, i)));

    const auto set = makeLoop (sounds);
    auto snap = bc::makeSnapshot (set);

    auto fresh = [&] (const std::vector<nsr::Place>& places)
    {
        auto player = std::make_unique<bc::LoopPlayer>();
        player->prepare (kSr);
        player->setSnapshot (snap);
        player->playing.store (true);          // the player is stopped until told otherwise
        if (! places.empty()) player->setPlacing (placing (places, 4));
        return player;
    };

    std::printf ("one chop left, its neighbour right\n");
    {
        std::vector<nsr::Place> places (4);
        places[0].on = true; places[0].x = -1.0f;
        places[1].on = true; places[1].x =  1.0f;
        places[2].on = true;
        places[3].on = true;

        auto player = fresh (places);
        const auto first  = listen (*player, 0.0, 0.9);
        const auto second = listen (*player, 1.0, 1.9);

        std::printf ("     chop 1 at %+.3f, chop 2 at %+.3f  (-1 left, +1 right)\n", first.where(), second.where());
        CHECK (! first.silent() && ! second.silent(), "both chops sound");
        CHECK (first.where() < -0.97, "the first chop really is hard left (%+.3f)", first.where());
        CHECK (second.where() > 0.97, "the second really is hard right (%+.3f)", second.where());
        CHECK (std::fabs (first.where() + second.where()) < 0.02,
               "and the two are mirror images (%+.3f against %+.3f)", first.where(), second.where());
    }

    std::printf ("\nplacing a chop does not change how loud it is\n");
    {
        std::vector<nsr::Place> centre (4), sides (4);
        for (auto& p : centre) p.on = true;
        for (auto& p : sides) p.on = true;
        sides[0].x = -1.0f;

        auto a = fresh (centre), b = fresh (sides);
        const auto mid = listen (*a, 0.0, 0.9);
        const auto left = listen (*b, 0.0, 0.9);
        const double drop = 20.0 * std::log10 (std::max (1e-9, left.level()) / std::max (1e-9, mid.level()));
        std::printf ("     hard left against the middle: %+.2f dB\n", drop);
        CHECK (std::fabs (drop) < 1.5, "a chop moved to the side is as loud as it was (%+.2f dB)", drop);
    }

    std::printf ("\na locked chop will not move\n");
    {
        std::vector<nsr::Place> places (4);
        for (auto& p : places) p.on = true;
        places[0].locked = true;
        places[0].x = -1.0f;              // asked for hard left, and should refuse
        places[0].travel = nsr::Travel::Sway;
        places[0].depth = 1.0f;
        places[0].bars = 0.25f;

        auto player = fresh (places);
        const auto h = listen (*player, 0.0, 0.9);
        std::printf ("     locked chop at %+.3f\n", h.where());
        CHECK (! h.silent(), "it still sounds (%.4f)", h.level());
        CHECK (std::fabs (h.where()) < 0.02, "it stays in the middle however it is set (%+.3f)", h.where());
    }

    std::printf ("\na travelling chop is somewhere different later in the bar\n");
    {
        std::vector<nsr::Place> places (4);
        for (auto& p : places) p.on = true;
        // every chop sways over one bar, a quarter turn apart, so chop 1 and chop 3 are opposite
        for (int i = 0; i < 4; ++i)
        {
            places[(size_t) i].travel = nsr::Travel::Sway;
            places[(size_t) i].depth = 1.0f;
            places[(size_t) i].bars = 1.0f;
            places[(size_t) i].phase = 0.0f;
        }

        // A chop is placed where it stands when it starts, and keeps that place for its own short
        // length. So "ear to ear" is successive hits landing in different places, which is what the
        // ear hears anyway for a chop this short - and far cheaper than moving it mid-note.
        // Beats 0 and 2 are the two points where a one-bar sway crosses the middle, so measuring
        // there proves nothing: beats 1 and 3 are the extremes.
        auto player = fresh (places);
        // Short windows, held near each extreme: a long one averages most of the path and tells
        // you about the window rather than the pan.
        const auto over  = listen (*player, 1.0, 1.25);   // a quarter of the way round: hard over
        const auto back  = listen (*player, 3.0, 3.25);   // three quarters: the other side
        const auto mid   = listen (*player, 0.0, 0.25);   // the top of the lap, in the middle

        std::printf ("     a quarter round %+.2f, three quarters %+.2f, the middle %+.2f\n",
                     over.where(), back.where(), mid.where());
        CHECK (! over.silent() && ! back.silent(), "both sound");
        CHECK (over.where() > 0.55, "a quarter of the way round it is over to one side (%+.2f)", over.where());
        CHECK (back.where() < -0.55, "three quarters round it is over to the other (%+.2f)", back.where());
        CHECK (std::fabs (mid.where()) < 0.25, "and passes through the middle on the way (%+.2f)", mid.where());
        // The path is a sway about the middle, so the two extremes have to mirror each other. A
        // channel ratio in dB could not ask this: its magnitudes were set by the clamp.
        CHECK (std::fabs (over.where() + back.where()) < 0.2,
               "the sway is even on both sides (%+.2f against %+.2f)", over.where(), back.where());
    }

    std::printf ("\nwith nothing placed, nothing changes\n");
    {
        auto plain = fresh ({});
        std::vector<nsr::Place> off (4);          // every place left switched off
        auto alsoPlain = fresh (off);
        const auto a = listen (*plain, 0.0, 0.9);
        const auto b = listen (*alsoPlain, 0.0, 0.9);
        const double diff = 20.0 * std::log10 (std::max (1e-9, b.level()) / std::max (1e-9, a.level()));
        CHECK (std::fabs (diff) < 0.5, "a loop with no places is the loop it was (%+.2f dB)", diff);
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
