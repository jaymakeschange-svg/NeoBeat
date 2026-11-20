// Depth, listened to.
//
// The Stereoroom's back-to-front axis is the half of it that is easy to fake. Making something
// quieter and duller does not put it further away - it puts a quiet muffled copy in the same
// place. What actually tells the ear about distance is the ratio of the sound to the room around
// it: at the front you hear the thing, at the back you hear the thing plus the space it is in.
//
// So this asks four things. Does a far chop really come back with more room than a near one, once
// their levels are matched - because if the only difference is level, the axis is a volume knob
// with extra steps. Does the room decay rather than run away, which a feedback network has to be
// held to. Does it arrive from the side the chop is on. And does the net under the output leave
// the mono sum alone, since a safety net that changes the sum is not a safety net.

#include "neobeat/shared/StereoRoom.h"
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

static double rms (const std::vector<float>& v, int from, int to)
{
    from = std::max (0, from); to = std::min ((int) v.size(), to);
    if (to <= from) return 0.0;
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / (double) (to - from));
}
static double db (double a, double b) { return 20.0 * std::log10 (std::max (1e-12, a) / std::max (1e-12, b)); }

int main()
{
    // ---------------------------------------------------------------- the room on its own
    std::printf ("the room decays, and does not run away\n");
    {
        nsr::RoomVerb verb;
        verb.prepare (kSr);
        verb.set (0.5f, 1.5f, 5000.0f);

        const int n = (int) (6.0 * kSr);
        std::vector<float> inL ((size_t) n, 0.0f), inR ((size_t) n, 0.0f);
        std::vector<float> outL ((size_t) n, 0.0f), outR ((size_t) n, 0.0f);
        inL[0] = inR[0] = 1.0f;                        // one impulse, then nothing for six seconds
        verb.process (inL.data(), inR.data(), outL.data(), outR.data(), n);

        const double first  = rms (outL, (int) (0.05 * kSr), (int) (0.25 * kSr));
        const double atOne  = rms (outL, (int) (1.40 * kSr), (int) (1.60 * kSr));
        const double atFive = rms (outL, (int) (5.00 * kSr), (int) (5.50 * kSr));

        CHECK (first > 1e-5, "an impulse gives a tail (%.2e)", first);
        CHECK (atOne < first, "it is quieter a second and a half later (%+.1f dB)", db (atOne, first));
        CHECK (atFive < atOne, "and quieter still at five seconds (%+.1f dB)", db (atFive, atOne));
        CHECK (atFive < first * 0.01, "it has died away rather than sustaining (%+.1f dB down)", db (atFive, first));

        // the asked-for RT60 was 1.5 s, so a second and a half in it should be about 60 dB down
        const double fall = db (atOne, first);
        std::printf ("     1.5 s after the impulse: %+.1f dB (asked for -60)\n", fall);
        CHECK (fall < -25.0 && fall > -95.0, "the tail is roughly as long as it was set (%+.1f dB)", fall);

        for (int i = 0; i < n; ++i)
            if (! std::isfinite (outL[(size_t) i]) || std::fabs (outL[(size_t) i]) > 8.0f)
            { CHECK (false, "it stayed finite and bounded (sample %d is %g)", i, (double) outL[(size_t) i]); break; }
        CHECK (std::isfinite (outL[(size_t) n - 1]) && std::fabs (outL[(size_t) n - 1]) < 8.0f,
               "it stayed finite and bounded to the end");
    }

    std::printf ("\nthe room comes back from the side the sound is on\n");
    {
        nsr::RoomVerb verb;
        verb.prepare (kSr);
        verb.set (0.5f, 1.2f, 5000.0f);

        const int n = (int) (0.5 * kSr);
        std::vector<float> inL ((size_t) n, 0.0f), inR ((size_t) n, 0.0f);
        std::vector<float> outL ((size_t) n, 0.0f), outR ((size_t) n, 0.0f);
        inL[0] = 1.0f;                                 // sent only from the left
        verb.process (inL.data(), inR.data(), outL.data(), outR.data(), n);

        const double l = rms (outL, 0, n), r = rms (outR, 0, n);
        std::printf ("     left-only send returns %+.1f dB left over right\n", db (l, r));
        CHECK (l > 1e-5 && r > 1e-6, "both sides answer, so it is a room and not a hard pan");
        CHECK (db (l, r) > 2.0, "but more of it comes back from the side it was sent from (%+.1f dB)", db (l, r));
    }

    // ---------------------------------------------------------------- the axis itself
    std::printf ("\nfar is not just quiet: the room-to-sound ratio is the cue\n");
    {
        // One chop, rendered twice through the same room - once at the front, once at the back -
        // and measured after the chop has finished, where only the room is left.
        auto makeChop = []
        {
            auto it = std::make_shared<bc::PoolItem>();
            it->name = "chop"; it->sr = kSr;
            const int len = (int) (0.10 * kSr);
            it->l.assign ((size_t) len, 0.0f);
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) i / kSr;
                it->l[(size_t) i] = (float) (0.5 * std::sin (6.283185307 * 440.0 * t) * std::exp (-6.0 * t));
            }
            it->r = it->l;
            it->f.valid = true; it->f.low = 0.1f;
            return it;
        };
        auto chop = makeChop();

        // One hit, on the first step of a one-bar loop, so the chop sounds once and then the
        // window after it holds only the room.
        bc::LoopSet set;
        set.numLayers = 1; set.bpm = 120.0; set.swing = 0.5f;
        {
            auto& L = set.layers[0];
            L.bars = 1; L.perBar = 4; L.hits.clear();
            bc::LoopHit h; h.step = 0; h.len = 1; h.sound = chop;
            L.hits.push_back (h);
        }
        auto snap = bc::makeSnapshot (set);

        auto renderAt = [&] (float y, std::vector<float>& outL, std::vector<float>& outR)
        {
            nsr::RoomVerb verb; verb.prepare (kSr); verb.set (0.5f, 1.2f, 5000.0f);

            auto places = std::make_shared<bc::LoopPlacing>();
            places->places.assign (1, nsr::Place());
            places->places[0].on = true;
            places->places[0].x = 0.0f;
            places->places[0].y = y;                   // the one thing that differs
            places->places[0].width = 0.5f;
            places->stride = 1;
            places->index.assign ((size_t) bc::kLoopLayers, -1);
            places->index[0] = 0;

            bc::LoopPlayer player;
            player.prepare (kSr);
            player.setSnapshot (snap);
            player.playing.store (true);               // the player is stopped until told otherwise
            player.setPlacing (places);

            const int n = (int) (2.0 * kSr);
            outL.assign ((size_t) n, 0.0f); outR.assign ((size_t) n, 0.0f);
            std::vector<float> sl ((size_t) kBlock), sr ((size_t) kBlock);

            bc::LoopHost host; host.valid = false; host.bpm = 120.0;

            for (int at = 0; at + kBlock <= n; at += kBlock)
            {
                std::fill (sl.begin(), sl.end(), 0.0f);
                std::fill (sr.begin(), sr.end(), 0.0f);
                player.setSendBus (sl.data(), sr.data());
                player.process (outL.data() + at, outR.data() + at, kBlock, host);
                verb.process (sl.data(), sr.data(), outL.data() + at, outR.data() + at, kBlock);
            }
        };

        std::vector<float> nearL, nearR, farL, farR;
        renderAt (0.0f, nearL, nearR);
        renderAt (1.0f, farL, farR);

        // the chop itself, then what is left once it has gone
        const int chopTo = (int) (0.12 * kSr);
        const double nearDirect = rms (nearL, 0, chopTo), farDirect = rms (farL, 0, chopTo);
        const double nearTail = rms (nearL, (int) (0.20 * kSr), (int) (0.70 * kSr));
        const double farTail  = rms (farL,  (int) (0.20 * kSr), (int) (0.70 * kSr));

        CHECK (nearDirect > 1e-4 && farDirect > 1e-4, "both sound at all (%.4f near, %.4f far)", nearDirect, farDirect);
        CHECK (farDirect < nearDirect, "the far one is quieter (%+.1f dB)", db (farDirect, nearDirect));

        // the cue proper: how much room each comes with, relative to itself. This is the number a
        // volume knob cannot change, and the reason the axis is worth having.
        const double nearRatio = db (nearTail, nearDirect);
        const double farRatio  = db (farTail,  farDirect);
        std::printf ("     room against sound: near %+.1f dB, far %+.1f dB\n", nearRatio, farRatio);
        CHECK (farRatio > nearRatio + 6.0,
               "the far chop comes back with far more room, level for level (%+.1f dB against %+.1f dB)",
               farRatio, nearRatio);
    }

    // ---------------------------------------------------------------- the net
    std::printf ("\nthe net under the output leaves the mono sum alone\n");
    {
        nsr::BassMono net;
        net.prepare (kSr);

        const int n = (int) (1.0 * kSr);
        std::vector<float> l ((size_t) n), r ((size_t) n), wantSum ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / kSr;
            const double low  = 0.4 * std::sin (6.283185307 *  60.0 * t);   // out of phase: sides only
            const double high = 0.3 * std::sin (6.283185307 * 3000.0 * t);
            l[(size_t) i] = (float) ( low + high);
            r[(size_t) i] = (float) (-low + high);
            wantSum[(size_t) i] = l[(size_t) i] + r[(size_t) i];
        }
        const double sideLowBefore = rms (l, 0, n);

        net.process (l.data(), r.data(), n, 120.0f);

        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, (double) std::fabs ((l[(size_t) i] + r[(size_t) i]) - wantSum[(size_t) i]));
        CHECK (worst < 1e-5, "the mono sum is untouched, sample for sample (worst %.2e)", worst);

        // and the out-of-phase low end really has been moved into the middle
        std::vector<float> side ((size_t) n);
        for (int i = 0; i < n; ++i) side[(size_t) i] = 0.5f * (l[(size_t) i] - r[(size_t) i]);
        const double sideAfter = rms (side, (int) (0.1 * kSr), n);
        std::printf ("     the side now reads %+.1f dB against the input\n", db (sideAfter, sideLowBefore));
        CHECK (sideAfter < sideLowBefore * 0.7, "the low end is no longer out in the sides (%+.1f dB)",
               db (sideAfter, sideLowBefore));
    }

    std::printf ("\n%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
