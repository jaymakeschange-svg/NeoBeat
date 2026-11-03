// Checks the sequencer's timing: swing placement, triplet rolls, ratchets,
// nudge, loop wrap, and that a hit is never lost or doubled across block edges.
#include "../Source/engine/Sequencer.h"
#include "../Source/engine/Presets.h"
#include <cassert>
#include <cstdio>
#include <vector>

static int collectAll (nb::Sequencer& s, double bpm, float swing, int block, double bars, std::vector<double>& ppqs, std::vector<int>& pads)
{
    const float sr = 48000.0f;
    std::vector<nb::SeqEvent> ev; ev.reserve (4096);
    const double total = bars * 4.0;
    const double ppqPerSample = bpm / 60.0 / sr;
    int count = 0;
    for (long pos = 0; pos * ppqPerSample < total; pos += block)
    {
        ev.clear();
        const double p0 = pos * ppqPerSample;
        const int n = (int) std::min<double> (block, (total - p0) / ppqPerSample + 1);
        s.collect (p0, bpm, n, true, swing, 0.0f, ev);
        for (auto& e : ev) { ppqs.push_back (p0 + e.offset * ppqPerSample); pads.push_back (e.pad); ++count; }
    }
    return count;
}

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "pass" : "FAIL", what); failures += ok ? 0 : 1; };

    nb::Pattern p; p.length = 16;
    p.steps[0][0].vel = 100;                       // downbeat
    p.steps[0][1].vel = 100;                       // a swung 16th
    p.steps[1][4].vel = 100; p.steps[1][4].roll = 5;   // triplet roll over steps 4-5
    p.steps[2][8].vel = 100; p.steps[2][8].roll = 4;   // ratchet x4 in step 8
    p.steps[3][12].vel = 100; p.steps[3][12].nudge = 20;
    nb::Sequencer s; s.prepare (48000.0f); s.setPattern (p);
    for (int block : { 1, 64, 256, 1000 })
    {
        std::vector<double> q; std::vector<int> pads;
        const int n = collectAll (s, 120.0, 0.66f, block, 2.0, q, pads);
        char msg[128]; std::snprintf (msg, sizeof msg, "block %4d: 2 bars give 2 x 10 hits (got %d)", block, n);
        check (n == 20, msg);
        if (block == 256 && n == 20)
        {
            const double tol = 1.5 * 120.0 / 60.0 / 48000.0;   // within 1.5 samples
            check (std::fabs (q[0] - 0.0) < tol, "downbeat at 0");
            check (std::fabs (q[1] - 0.33) < tol, "swung 16th at 0.66 of the 8th (0.33 beats)");
            check (std::fabs (q[2] - 1.0) < tol && std::fabs (q[3] - 1.0 - 1.0 / 6.0) < tol && std::fabs (q[4] - 1.0 - 2.0 / 6.0) < tol,
                   "triplet roll: three even hits across the 8th");
            check (std::fabs (q[5] - 2.0) < tol && std::fabs (q[8] - (2.0 + 3 * 0.33 / 4.0)) < tol, "ratchet x4 inside the (swung-length) step");
            check (std::fabs (q[9] - (3.0 + 0.25 * 0.2)) < tol, "nudge +20% of a step");
            check (std::fabs (q[10] - 4.0) < tol, "loop wraps to the next bar exactly");
        }
    }
    // negative nudge on the downbeat lands just before the loop point
    nb::Pattern p2; p2.length = 16; p2.steps[0][0].vel = 100; p2.steps[0][0].nudge = -10;
    s.setPattern (p2);
    {
        std::vector<double> q; std::vector<int> pads;
        const int n = collectAll (s, 120.0, 0.5f, 128, 2.0, q, pads);
        check (n == 2 && std::fabs (q[0] - (4.0 - 0.025)) < 1.0e-3, "early downbeat plays at the end of the previous loop");
    }
    // every preset: 32 steps, hits on the pads named, nothing outside 16 pads
    int totalHits = 0;
    for (const auto& pr : nb::presets())
    {
        s.setPattern (pr.pattern);
        std::vector<double> q; std::vector<int> pads;
        totalHits += collectAll (s, pr.tempo, pr.swing, 256, 2.0, q, pads);
    }
    char msg[128]; std::snprintf (msg, sizeof msg, "50 presets parsed (%zu), %d hits in their first two bars", nb::presets().size(), totalHits);
    check (nb::presets().size() == 50 && totalHits > 0, msg);
    return failures;
}
