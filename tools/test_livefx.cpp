#include "../Source/dsp/LiveFx.h"
#include "../Source/dsp/MatrixFx.h"
#include "../Source/engine/Sequencer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace nb;
static double rmsOf (const std::vector<float>& v, size_t a, size_t b) { double s = 0; b = std::min (b, v.size()); for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return b > a ? std::sqrt (s / (double) (b - a)) : 0.0; }
int main()
{
    const float sr = 48000.0f;
    // a hit: a decaying tone + noise burst, 0.4 s
    const int n = (int) (0.4f * sr);
    std::vector<float> hl ((size_t) n), hr ((size_t) n);
    uint32_t x = 12345;
    for (int i = 0; i < n; ++i)
    {
        x = x * 1664525u + 1013904223u;
        const float nz = ((float) (x >> 8) / 8388608.0f - 1.0f);
        const float t = (float) i / sr;
        const float v = 0.6f * std::sin (6.2831853f * 180.0f * t) * std::exp (-t * 9.0f) + 0.3f * nz * std::exp (-t * 30.0f) + 0.2f * std::sin (6.2831853f * 1320.0f * t) * std::exp (-t * 14.0f);
        hl[(size_t) i] = v; hr[(size_t) i] = v * 0.9f;
    }
    LiveFxPools pool; pool.prepare (sr);
    LiveFx fx; fx.prepare (sr, &pool);
    const auto& info = matrixFxInfo();
    int bad = 0;
    for (int f = 0; f < (int) info.size(); ++f)
    {
        LiveSpec s; s.kind = 1; s.fx = (MatrixFx) f; s.seed = 7; s.version = 1;
        for (size_t k = 0; k < info[(size_t) f].params.size() && k < 8; ++k) s.p[k] = info[(size_t) f].params[k].def;
        float known = 0.0f; double first = 0.0;
        for (int pass = 0; pass < 2; ++pass)
        {
        fx.start (s, 120.0, 0.0, known, known > 0.0f);
        const int lat = fx.latency();
        const int total = n + (int) (9.0f * sr);
        std::vector<float> ol, orr; ol.reserve ((size_t) total); orr.reserve ((size_t) total);
        const int B = 256;
        std::vector<float> il ((size_t) B), ir ((size_t) B), bl ((size_t) B), br ((size_t) B);
        const auto t0 = std::chrono::steady_clock::now();
        int64_t pos = 0; int produced = 0;
        while (! fx.finished() && produced < total)
        {
            for (int i = 0; i < B; ++i) { const int64_t q = pos + i; il[(size_t) i] = q < n ? hl[(size_t) q] : 0.0f; ir[(size_t) i] = q < n ? hr[(size_t) q] : 0.0f; }
            HitView hv { hl.data(), hr.data(), n, true };
            fx.process (il.data(), ir.data(), bl.data(), br.data(), B, hv, pos >= n);
            pos += B;
            for (int i = 0; i < B; ++i) { ol.push_back (bl[(size_t) i]); orr.push_back (br[(size_t) i]); }
            produced += B;
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        // offline reference
        FxSettings fs = FxSettings::defaults ((MatrixFx) f); fs.seed = 7;
        std::vector<float> rl, rr;
        MatrixFxEngine eng; eng.process ((MatrixFx) f, fs, hl.data(), hr.data(), n, sr, rl, rr);
        // both channels, always: the input is deliberately lopsided (the right is 0.9 of
        // the left), so anything that treats one channel differently from the other shows
        // up as a balance that no longer matches the offline render's balance.
        bool nan = false; float pk = 0;
        for (size_t i = 0; i < ol.size(); ++i)
        {
            if (! std::isfinite (ol[i]) || ! std::isfinite (orr[i])) nan = true;
            pk = std::max (pk, std::max (std::fabs (ol[i]), std::fabs (orr[i])));
        }
        const double liveR = rmsOf (ol, (size_t) lat, (size_t) lat + (size_t) n), refR = rmsOf (rl, 0, (size_t) n), inR = rmsOf (hl, 0, (size_t) n);
        const double liveTail = rmsOf (ol, (size_t) lat + (size_t) n, ol.size()), refTail = rmsOf (rl, (size_t) n, rl.size());
        const double liveRr = rmsOf (orr, (size_t) lat, (size_t) lat + (size_t) n), refRr = rmsOf (rr, 0, (size_t) n);
        const double liveBal = 20.0 * std::log10 (std::max (1e-9, liveR) / std::max (1e-9, liveRr));
        const double refBal  = 20.0 * std::log10 (std::max (1e-9, refR)  / std::max (1e-9, refRr));
        // the tail must be decaying, not growing: a feedback path that runs away can sit
        // under the peak limit for the length of this render and still be wrong.
        const size_t tailFrom = (size_t) lat + (size_t) n, tailMid = tailFrom + (ol.size() - tailFrom) / 2;
        const double tailEarly = rmsOf (ol, tailFrom, tailMid), tailLate = rmsOf (ol, tailMid, ol.size());
        const double growth = 20.0 * std::log10 (std::max (1e-9, tailLate) / std::max (1e-9, tailEarly));
        const double cost = ms / (1000.0 * produced / sr) * 100.0;
        known = fx.measuredGain();
        if (pass == 0) { first = liveR; continue; }
        const double dB = 20.0 * std::log10 (liveR / std::max (1e-9, refR));
        const bool ok = ! nan && pk < 1.6f && std::fabs (dB) < (f == (int) MatrixFx::Shimmer ? 4.0 : 2.5) && fx.finished()
                     && std::fabs (liveBal - refBal) < 3.0   // the same stereo picture as the render
                     && growth < 3.0;                        // a tail that decays
        if (! ok) ++bad;
        std::printf ("%-24s %s first %.4f (%+.1f dB) 2nd %+.1f dB |", info[(size_t) f].name, ok ? "ok " : "BAD", first, 20.0 * std::log10 (first / std::max (1e-9, refR)), dB); std::printf (" lat %4d  in %.4f live %.4f ref %.4f | tail live %.4f (%.2fs) ref %.4f (%.2fs) | pk %.2f cost %.1f%%\n", lat, inR, liveR, refR,
                     liveTail, (double) (ol.size() - lat - n) / sr, refTail, (double) ((int) rl.size() - n) / sr, pk, cost);
        std::printf ("%-24s    balance live %+.1f dB ref %+.1f dB (%+.1f) | tail %+.1f dB over its own length\n", "", liveBal, refBal, liveBal - refBal, growth);
        }
    }
    // Bounce Delay
    {
        LiveSpec s; s.kind = 2; s.version = 1;
        fx.start (s, 120.0, 0.0);
        std::vector<float> ol, orr; const int B = 256; std::vector<float> il ((size_t) B), ir ((size_t) B), bl ((size_t) B), br ((size_t) B);
        int64_t pos = 0;
        while (! fx.finished() && pos < 20 * 48000)
        {
            for (int i = 0; i < B; ++i) { const int64_t q = pos + i; il[(size_t) i] = q < n ? hl[(size_t) q] : 0.0f; ir[(size_t) i] = q < n ? hr[(size_t) q] : 0.0f; }
            HitView hv { hl.data(), hr.data(), n, true };
            fx.process (il.data(), ir.data(), bl.data(), br.data(), B, hv, pos >= n);
            pos += B; for (int i = 0; i < B; ++i) { ol.push_back (bl[(size_t) i]); orr.push_back (br[(size_t) i]); }
        }
        BounceDelay b; const auto plan = b.plan (s.bounce, 120.0, 0.0, 0.0);
        const int len = n + (int) (((plan.empty() ? 0.0 : plan.back().t) + s.bounce.tail + 0.2) * sr);
        std::vector<float> rl ((size_t) len, 0.0f), rr ((size_t) len, 0.0f);
        for (int i = 0; i < n; ++i) { rl[(size_t) i] = hl[(size_t) i]; rr[(size_t) i] = hr[(size_t) i]; }
        b.render (s.bounce, plan, 0.0, hl.data(), hr.data(), n, sr, rl.data(), rr.data(), nullptr, len);
        std::printf ("Bounce Delay: live len %.2fs rms %.4f | ref len %.2fs rms %.4f bounces %d finished %d\n", (double) ol.size() / sr, rmsOf (ol, 0, ol.size()), (double) rl.size() / sr, rmsOf (rl, 0, rl.size()), (int) plan.size(), (int) fx.finished());
        double diff = 0, ref = 0; for (size_t i = 0; i < std::min (ol.size(), rl.size()); ++i) { diff += (double) (ol[i] - rl[i]) * (ol[i] - rl[i]); ref += (double) rl[i] * rl[i]; }
        const double apart = 10.0 * std::log10 (diff / std::max (1e-20, ref) + 1e-20);
        std::printf ("  difference from the rendered version: %.1f dB\n", apart);

        // this stretch used to print and assert nothing, so a broken Bounce Delay read as a
        // pass. Bounce Delay throws the repeats side to side on purpose, so the test is that
        // it matches its own offline render's stereo picture - not that it sits centred.
        float pk = 0.0f; bool nan = false;
        for (size_t i = 0; i < ol.size(); ++i)
        {
            if (! std::isfinite (ol[i]) || ! std::isfinite (orr[i])) nan = true;
            pk = std::max (pk, std::max (std::fabs (ol[i]), std::fabs (orr[i])));
        }
        const double liveBal = 20.0 * std::log10 (std::max (1e-9, rmsOf (ol, 0, ol.size())) / std::max (1e-9, rmsOf (orr, 0, orr.size())));
        const double refBal  = 20.0 * std::log10 (std::max (1e-9, rmsOf (rl, 0, rl.size()))  / std::max (1e-9, rmsOf (rr, 0, rr.size())));
        const double early = rmsOf (ol, (size_t) n, (size_t) n + (ol.size() - (size_t) n) / 2);
        const double late  = rmsOf (ol, (size_t) n + (ol.size() - (size_t) n) / 2, ol.size());
        const double growth = 20.0 * std::log10 (std::max (1e-9, late) / std::max (1e-9, early));
        std::printf ("  balance live %+.1f dB ref %+.1f dB | repeats fade %+.1f dB | peak %.2f | finished %d\n",
                     liveBal, refBal, growth, pk, (int) fx.finished());
        int bounceBad = 0;
        if (nan)                              { std::printf ("  BAD: not finite\n"); ++bounceBad; }
        if (pk >= 1.6f)                       { std::printf ("  BAD: peak %.2f\n", pk); ++bounceBad; }
        if (! fx.finished())                  { std::printf ("  BAD: never finished\n"); ++bounceBad; }
        if (std::fabs (liveBal - refBal) > 3.0) { std::printf ("  BAD: a different stereo picture from the render\n"); ++bounceBad; }
        if (growth > 1.0)                     { std::printf ("  BAD: the repeats grow instead of fading\n"); ++bounceBad; }
        if (apart > -12.0)                    { std::printf ("  BAD: too far from the rendered version\n"); ++bounceBad; }
        bad += bounceBad;
    }
    std::printf (bad == 0 ? "LIVE FX PASS\n" : "LIVE FX FAIL (%d)\n", bad);
    return bad;
}
