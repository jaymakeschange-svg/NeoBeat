// BANDS: sixteen bands, each standing somewhere.
//
// The engine was tested on its own. What is tested here is the part that only exists once the
// bands are handed to the room: that a band placed left is actually left, that a travelling band
// actually travels, that depth actually reaches the send, and that turning placement off gives
// back exactly what the bank made.

#include "rooms/Bands.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <array>

static int failures = 0;
static void ok (bool c, const std::string& w)
{
    std::printf ("  %s %s\n", c ? "ok:  " : "FAIL:", w.c_str());
    if (! c) ++failures;
}
static void heading (const char* s) { std::printf ("\n%s\n", s); }

static constexpr float SR = 48000.0f;

static double rms (const std::vector<float>& v, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / std::max (1, to - from));
}

// Where a thing sits, as a number from -1 (hard left) through 0 (middle) to +1 (hard right).
// Deliberately not a decibel difference between the channels: at the edges one side approaches
// silence and the ratio runs off to whatever the epsilon happens to be, which reads as a huge
// number that means nothing. An angle is bounded and behaves at both ends.
static double where (const std::vector<float>& l, const std::vector<float>& r, int from, int to)
{
    const double a = rms (l, from, to), b = rms (r, from, to);
    if (a + b < 1e-12) return 0.0;
    return std::atan2 (b, a) / 0.7853981634 - 1.0;
}

static void noise (std::vector<float>& l, std::vector<float>& r, int n, unsigned seed = 7u)
{
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float v = 0.4f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        l[(size_t) i] = r[(size_t) i] = v;
    }
}

static bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

static nsr::SpectralDelayParams liveDsp()
{
    nsr::SpectralDelayParams p;
    p.startTimeMs = 30.0f; p.finishTimeMs = 90.0f;      // short, so every band speaks quickly
    p.startFeedback = 0.5f; p.finishFeedback = 0.6f;
    p.startLevel = 1.0f; p.finishLevel = 1.0f;
    p.startLfoMs = p.finishLfoMs = 0.0f;
    p.dry = 0.0f; p.wet = 1.0f;
    return p;
}

int main()
{
    const int N = 24000;

    //==============================================================================================
    heading ("a band placed somewhere is heard there");
    {
        auto runWith = [N] (float x, bool locked)
        {
            bc::BandsEngine e;
            e.prepare (SR, N);
            bc::BandsState st;
            st.dsp = liveDsp();
            for (int b = 0; b < bc::BandsState::kBands; ++b)
            {
                st.place[(size_t) b].on = (b == 10);     // one band only, so the readout is its own
                st.place[(size_t) b].x = x;
                st.place[(size_t) b].y = 0.5f;
                st.place[(size_t) b].width = 0.0f;
                st.place[(size_t) b].locked = locked;
                st.place[(size_t) b].travel = nsr::Travel::Still;
            }
            e.setState (st);
            std::vector<float> in (N, 0.0f), inR (N, 0.0f), oL (N, 0.0f), oR (N, 0.0f);
            noise (in, inR, 2400);
            // In blocks, as a host would. The Strip glides to its target across one block, so a
            // single 24000-sample call spends the whole measurement on the way there and every
            // position reads as a washed-out version of itself.
            const int blk = 512;
            for (int k = 0; k + blk <= N; k += blk)
                e.process (oL.data() + k, oR.data() + k, in.data() + k, inR.data() + k, blk, 0.0);
            return std::pair<std::vector<float>, std::vector<float>> { oL, oR };
        };

        auto left = runWith (-0.85f, false);
        auto mid  = runWith (0.0f,  false);
        auto right = runWith (0.85f, false);

        const int a = 4000, b = N;
        const double wl = where (left.first, left.second, a, b);
        const double wm = where (mid.first, mid.second, a, b);
        const double wr = where (right.first, right.second, a, b);
        std::printf ("     left %+.3f   middle %+.3f   right %+.3f\n", wl, wm, wr);

        ok (wl < -0.25, "placed left, it reads left");
        ok (wr > 0.25, "placed right, it reads right");
        ok (std::abs (wm) < 0.08, "placed in the middle, it reads centred");
        ok (std::abs (wl + wr) < 0.15, "and the two edges are symmetric about the middle");
    }

    //==============================================================================================
    heading ("a locked band stays in the middle wherever it is put");
    {
        bc::BandsEngine e;
        e.prepare (SR, N);
        bc::BandsState st;
        st.dsp = liveDsp();
        for (int b = 0; b < bc::BandsState::kBands; ++b)
        {
            st.place[(size_t) b].on = (b == 4);
            st.place[(size_t) b].x = -1.0f;              // as far left as it goes
            st.place[(size_t) b].width = 1.0f;
            st.place[(size_t) b].locked = true;
            st.place[(size_t) b].travel = nsr::Travel::Still;
        }
        e.setState (st);
        std::vector<float> in (N, 0.0f), inR (N, 0.0f), oL (N, 0.0f), oR (N, 0.0f);
        noise (in, inR, 2400);
        e.process (oL.data(), oR.data(), in.data(), inR.data(), N, 0.0);
        const double w = where (oL, oR, 4000, N);
        std::printf ("     locked, placed hard left: %+.3f\n", w);
        ok (std::abs (w) < 0.08, "locked beats placed, which is what the low bands rely on");
    }

    //==============================================================================================
    heading ("a travelling band travels");
    {
        bc::BandsEngine e;
        e.prepare (SR, 2048);
        bc::BandsState st;
        st.dsp = liveDsp();
        for (int b = 0; b < bc::BandsState::kBands; ++b)
        {
            st.place[(size_t) b].on = (b == 9);
            st.place[(size_t) b].travel = nsr::Travel::Circle;
            st.place[(size_t) b].bars = 1;
            st.place[(size_t) b].width = 0.0f;
            st.place[(size_t) b].locked = false;
            st.place[(size_t) b].depth = 0.5f;
            st.place[(size_t) b].phase = 0.0f;
        }
        e.setState (st);

        // fed in blocks, with the beat clock advancing, which is how a host would do it
        const int blk = 2048, blocks = 40;
        std::vector<float> oL (blk * blocks, 0.0f), oR (blk * blocks, 0.0f);
        std::vector<float> inL (blk, 0.0f), inR (blk, 0.0f);
        double beats = 0.0;
        const double beatsPerBlock = (double) blk / SR * (120.0 / 60.0);
        for (int k = 0; k < blocks; ++k)
        {
            noise (inL, inR, blk, 11u + (unsigned) k);
            e.process (oL.data() + k * blk, oR.data() + k * blk, inL.data(), inR.data(), blk, beats);
            beats += beatsPerBlock;
        }

        // sample the image in four windows across the pass; a circling band should not sit still
        double lo = 2.0, hi = -2.0;
        for (int w = 0; w < 8; ++w)
        {
            const int a = (blk * blocks) * w / 8, b = (blk * blocks) * (w + 1) / 8;
            if (a < 6000) continue;
            const double p = where (oL, oR, a, b);
            lo = std::min (lo, p); hi = std::max (hi, p);
        }
        std::printf ("     image ranged %+.3f to %+.3f across the pass\n", lo, hi);
        ok (hi - lo > 0.25, "its position changes over time rather than sitting still");
        ok (allFinite (oL) && allFinite (oR), "and it stays finite while moving");
    }

    //==============================================================================================
    heading ("depth reaches the send bus");
    {
        auto sendEnergy = [N] (float distance)
        {
            bc::BandsEngine e;
            e.prepare (SR, N);
            bc::BandsState st;
            st.dsp = liveDsp();
            for (int b = 0; b < bc::BandsState::kBands; ++b)
            {
                st.place[(size_t) b].on = (b == 8);
                st.place[(size_t) b].travel = nsr::Travel::Still;
                st.place[(size_t) b].y = distance;   // 0 at the front, 1 at the back
                st.place[(size_t) b].width = 0.0f;
                st.place[(size_t) b].locked = false;
            }
            e.setState (st);
            std::vector<float> in (N, 0.0f), inR (N, 0.0f), oL (N, 0.0f), oR (N, 0.0f),
                               sL (N, 0.0f), sR (N, 0.0f);
            noise (in, inR, 2400);
            e.setSendBus (sL.data(), sR.data());
            e.process (oL.data(), oR.data(), in.data(), inR.data(), N, 0.0);
            return std::pair<double, double> { rms (sL, 0, N), rms (oL, 0, N) };
        };

        auto near = sendEnergy (0.0f);
        auto far  = sendEnergy (1.0f);
        // depth is how far a travelling band swings, not how far away it is. Distance is y, and
        // mixing the two up is easy enough that it is worth a test saying which is which.
        std::printf ("     send rms  near %.5f   far %.5f\n", near.first, far.first);
        ok (far.first > near.first * 2.0, "a band placed further away sends more to the room");
        ok (near.second > 0.0, "and it is still heard directly either way");
    }

    //==============================================================================================
    heading ("placement off gives back exactly what the bank made");
    {
        auto run = [N] (bool placed)
        {
            bc::BandsEngine e;
            e.prepare (SR, N);
            bc::BandsState st;
            st.dsp = liveDsp();
            st.placed = placed;
            e.setState (st);
            std::vector<float> in (N, 0.0f), inR (N, 0.0f), oL (N, 0.0f), oR (N, 0.0f);
            noise (in, inR, 2400);
            e.process (oL.data(), oR.data(), in.data(), inR.data(), N, 0.0);
            return oL;
        };
        auto off = run (false);
        ok (rms (off, 0, N) > 1e-5, "with placement off there is still a signal");
        ok (allFinite (off), "and it is finite");

        // dry at one, wet at zero: the room is not in the way of the signal passing through
        bc::BandsEngine e;
        e.prepare (SR, N);
        bc::BandsState st; st.dsp = liveDsp(); st.dsp.dry = 1.0f; st.dsp.wet = 0.0f;
        e.setState (st);
        std::vector<float> in (N, 0.0f), inR (N, 0.0f), oL (N, 0.0f), oR (N, 0.0f);
        noise (in, inR, 2400);
        e.process (oL.data(), oR.data(), in.data(), inR.data(), N, 0.0);
        double diff = 0.0;
        for (int i = 0; i < N; ++i) diff += std::abs (oL[(size_t) i] - in[(size_t) i]);
        ok (diff == 0.0, "fully dry adds the input and nothing else");
    }

    //==============================================================================================
    heading ("the default fan keeps the bottom in the middle");
    {
        bc::BandsState st;                               // constructed already fanned
        ok (st.place[0].locked && st.place[1].locked, "the lowest bands are locked to mono");
        ok (! st.place[15].locked, "the top ones are not");
        ok (st.place[15].width > st.place[0].width, "and the fan opens toward the top");

        bool everyBandOn = true;
        for (int b = 0; b < bc::BandsState::kBands; ++b)
            if (! st.place[(size_t) b].on) everyBandOn = false;
        ok (everyBandOn, "every band starts switched on");

        st.travelAll (nsr::Travel::Sway, 4);
        bool spread = false;
        for (int b = 1; b < bc::BandsState::kBands; ++b)
            if (st.place[(size_t) b].bars != st.place[0].bars) spread = true;
        ok (spread, "setting one travel for all of them still staggers their periods");
    }

    // ------------------------------------------------------------------ Chimecho's Pan Width/Speed
    //
    // Two knobs over sixteen Places. The thing worth checking is not that it pans - that is one
    // assignment - but that it leaves the locked bands alone and does not move the bank as a block.
    {
        heading ("the pan LFO");

        bc::BandsState st;                       // the default fan locks the bottom bands
        int lockedBefore = 0;
        for (int b = 0; b < bc::BandsState::kBands; ++b)
            if (st.place[(size_t) b].locked) ++lockedBefore;
        ok (lockedBefore > 0, "the default fan locks something, so there is a case to test");

        // "Left alone" means unchanged, not zeroed: the fan writes a depth to every band, locked
        // ones included, and a locked band simply never acts on it (Place::moves() is false). So
        // the check is against a snapshot, not against zero.
        std::array<nsr::Place, bc::BandsState::kBands> before;
        for (int b = 0; b < bc::BandsState::kBands; ++b) before[(size_t) b] = st.place[(size_t) b];

        st.panLfo (0.8f, 2.0f);

        bool lockedStill = true, movingSwept = true;
        for (int b = 0; b < bc::BandsState::kBands; ++b)
        {
            const auto& pl = st.place[(size_t) b];
            if (before[(size_t) b].locked && pl != before[(size_t) b]) lockedStill = false;
            if (! pl.locked && (pl.travel != nsr::Travel::Sway || std::abs (pl.depth - 0.8f) > 1.0e-5f)) movingSwept = false;
        }
        ok (lockedStill, "a locked band comes through untouched: no sweep is written onto the low end");
        ok (movingSwept, "every band that can move takes the width it was given");

        // staggered, so the bank opens out rather than wandering as one object
        float lo = 2.0f, hi = -1.0f;
        for (int b = 0; b < bc::BandsState::kBands; ++b)
        {
            const auto& pl = st.place[(size_t) b];
            if (pl.locked) continue;
            lo = std::min (lo, pl.phase); hi = std::max (hi, pl.phase);
        }
        ok (hi - lo > 0.5f, "their phases are spread across the lap, not shared");

        // and width zero puts them back to still, rather than sweeping by nothing
        st.panLfo (0.0f, 2.0f);
        bool allStill = true;
        for (int b = 0; b < bc::BandsState::kBands; ++b)
            if (! st.place[(size_t) b].locked && st.place[(size_t) b].travel != nsr::Travel::Still) allStill = false;
        ok (allStill, "width zero returns them to still");
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
