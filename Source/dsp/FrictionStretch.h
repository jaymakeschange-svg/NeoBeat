// NeoBeat — Friction Stretch: the hit pulled off like duct tape off a roll. Each jump on the swung grid is a
// yank; while the tape peels, the line where the glue lets go sticks and slips hundreds to thousands of
// times a second (the rip), and every one of those micro-slips fires a grain of the hit's own audio, read
// where the playhead is (a grain stream). Between yanks it creeps: sparse crackle. The glue's own dry
// crackle, the roll's hollow core (a learned MaterialLab body) and the loose strip of tape between hand
// and roll (it tightens while stuck, slackens on the yank) ring with it. Rendered per hit, offline.
//
// Underneath, the earlier engine is still there (Freeze: the spectral freeze and jump, with its lines;
// Tape: a varispeed read):
//
//   The clock.  A friction2 contact (the learned rubber's tab, bristle and Stribeck grip) is pulled so
//   that it slips exactly when the grid says (the hand pulls just fast enough for the spring to reach the
//   sticking force on time). The tab's own motion is the playhead: still while it sticks (or creeping,
//   with Creep), then a jump with the shape of a real slip - it speeds up, overshoots a hair and is caught.
//   Slip sets how long the jump takes (a stiff tab: a cut; a soft, heavy one: a zip). Grit roughens the
//   surface, so the slips wander off the grid. Stretch sets how far each jump goes: the hit lasts Stretch
//   times longer.
//   The sound.  The hit is analysed with an FFT, frame by frame. Wherever the playhead stops, that frame
//   keeps sounding (its partials keep turning at their own frequencies: a smooth spectral freeze); when it
//   jumps, the partials are picked up again at the new spot (cut) or slid through (zip). Grain keeps the
//   pitch; Tape lets it follow the playhead's speed, like dragged tape (silent while stuck, chirping on
//   the jump).
//   The lines.  Up to six lines drawn on the sonogram, each a band of the spectrum gripped on its own:
//   Hold fade lets a stuck sound die away (the clamped rubber loses its ring) and Snap kicks it back up
//   on every slip, so the jumps become a swung stutter.
//   its frequency range (with a soft edge), Offset (sideways: the band runs later), Tilt (its angle: the
//   top of the band runs later or earlier than the bottom, so the jump sweeps across the band), Spray
//   (every frequency in it slips at its own random moment), and its level and pan.
#pragma once

#include "SwingGrid.h"
#include "Friction2.h"
#include "MaterialBody.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

namespace nb
{
struct StretchLine
{
    bool on = true;
    float fLo = 20.0f, fHi = 20000.0f;   // the frequencies it grips
    float edge = 0.3f;                    // octaves of soft edge
    float offset = 0.0f;                  // seconds the band runs late
    float tilt = 0.0f;                    // seconds the top of the band runs later (+) or earlier (-) than its bottom
    float spray = 0.0f;                   // 0..1 of a step: each frequency slips at its own moment
    float level = 1.0f, pan = 0.0f;
};

struct StretchParams
{
    RateSpec rate { true, 0.25, 125.0, 0.62f };   // a jump every 1/16 (or every 125 ms), swing bias 62%
    float stretch = 4.0f;      // the hit lasts this many times longer
    float creep = 0.15f;       // 0: stuck still between yanks .. 1: creeping (more crackle, slow-motion freeze)
    float holdFade = 0.0f;     // 0: a stuck sound holds .. 1: it dies away before the next slip (a clamped rubber)
    float snap = 0.0f;         // each slip kicks the sound back up (an accent on every jump)
    float slipMs = 70.0f;      // how long a yank takes: 40-150 ms tears the tape off; 1-3 ms is a cut
    float grit = 0.25f;        // a rough surface: the yanks wander off the grid, the rip crackles unevenly
    float tape = 0.0f;         // 0 grain (pitch held) .. 1 tape (pitch follows the playhead)
    float attackMs = 15.0f;    // the hit plays as it is this long before it first sticks
    std::vector<StretchLine> lines;   // empty: one line over everything
    float wet = 1.0f;
    uint32_t seed = 1;
    // ---- the duct tape
    float peel = 1.0f;         // the peel: the hit streamed as grains by the adhesive letting go
    float freeze = 0.25f;      // the spectral freeze underneath (the lines shape this layer)
    float screechHz = 900.0f;  // micro-slips a second at full yank speed: the pitch of the rip
    float adhesive = 0.4f;     // the glue's own dry crackle on each micro-slip
    float grainMs = 8.0f;      // grain length: longer keeps more of the hit's tone in the rip
    float rasp = 0.3f;         // 0: every grain from the same spot (a pitched screech) .. 1: scattered (a noisy rasp)
    float roll = 0.5f;         // the roll's hollow core ringing with each yank
    int rollBody = 5;          // which learned body is the roll (5 = PVC pipe, 6 = tube clank, 1 = bucket)
    float rollSize = 1.0f;     // > 1: a bigger, lower roll
    float strip = 0.35f;       // the loose strip of tape: its pitch rises while stuck, drops on the yank
    float width = 0.6f;        // how far the crackle sprays across the field
    // ---- the accent's shape
    double gateBeats = 0.0, gateMs = 0.0;   // how long it plays (follows the Tempo/ms switch); 0 = all of it
    float decayMs = 400.0f;    // how fast it falls away once the gate closes (to -60 dB)
};

// ---------------------------------------------------------------- the clock: one friction2 contact
struct FrictionClock
{
    std::vector<float> x;               // the tab's position at the output rate, from the hit (t = 0)
    std::vector<double> onset, restick; // seconds

    // due: when each slip should happen (seconds from the hit); the contact runs until tEnd
    void run (const FrictionMaterial& m, const std::vector<double>& due, double tStart, double tEnd,
              float slipMs, float grit, uint32_t seed, float fs)
    {
        const int os = 4; const double fsc = fs * os, dt = 1.0 / fsc;
        const int n = (int) std::ceil (tEnd * fs) + 2;
        x.assign ((size_t) n, 0.0f); onset.clear(); restick.clear();
        // the tab: a slip is about half its period
        const double wt = 3.14159265358979 / std::max (0.0004, slipMs / 1000.0);
        const double mt = 1.0 / (wt * wt), ct = 2.0 * m.zeta_tab * std::sqrt (mt);
        const double wb = 2.0 * 3.14159265358979 * std::min ((double) m.f_bristle, 0.19 / dt);
        const double s0 = mt * wb * wb, s1 = 2.0 * m.zeta_bristle * mt * wb;
        const double Fc = m.mu_ratio, vSlip = wt * (1.0 - Fc), vs = m.v_s * vSlip, s2 = m.viscous / vSlip;
        const double rough = 0.12 * std::min (1.0f, std::max (0.0f, grit));
        std::mt19937 rng (seed * 977u + 5u); std::normal_distribution<double> gn (0.0, 1.0);
        const double ra = 1.0 - std::exp (-2.0 * 3.14159265358979 * m.rough_hz / fsc), rNorm = std::sqrt ((2.0 - ra) / ra);
        double rState = 0.0;
        double vt = 0, xt = 0, sst = 0, z = 0, thr = 0.98, V = 0;
        int64_t lag = 0, peakAt = 0, lastOn = -1, lowStart = 0;
        double sstPeak = -1e9; int lowRun = 0; bool slipping = false;
        size_t next = 0;
        const int64_t steps = (int64_t) std::ceil (tEnd * fsc);
        const int64_t startStep = (int64_t) (tStart * fsc);
        const double vff = 2.0 * (1.0 - Fc) / std::max (1e-3, due.size() > 1 ? (due.back() - due.front()) / (due.size() - 1) : 0.1);
        for (int64_t i = 0; i < steps; ++i)
        {
            const double t = i * dt;
            rState += ra * (gn (rng) - rState);
            const double nEff = 1.0 + rough * rState * rNorm;
            if (i < startStep) { if ((i % os) == 0) x[(size_t) (i / os)] = 0.0f; continue; }
            // the hand: while stuck, just fast enough to reach the sticking force when the next slip is due
            if (next < due.size())
            {
                if (! slipping)
                {
                    const double rem = due[next] - (double) lag * dt - t;
                    V = rem <= 0.0 ? 20.0 * vff + 5.0 : std::min (5000.0, std::max (0.0, (thr * nEff - sst) / std::max (rem, 2.0 * dt)));
                }
                else V = vff;
            }
            else V = 0.0;
            // LuGre contact on a rigid surface, the tab a mass on the hand's spring
            const double vr = vt, xx = vr / vs;
            const double g = nEff * (Fc + (1.0 - Fc) * std::exp (-xx * xx)) + 1e-12;
            const double a = s0 * std::fabs (vr) / g;
            const double zss = g * (vr > 0 ? 1.0 : (vr < 0 ? -1.0 : 0.0)) / s0;
            const double zNew = zss + (z - zss) * std::exp (-a * dt);
            const double zd = (zNew - z) / dt;
            const double F = s0 * zNew + s1 * zd + s2 * vr * nEff;
            z = zNew;
            vt += dt * (sst - ct * vt - F) / mt;
            xt += dt * vt;
            sst += dt * (V - vt);
            // slips
            const double av = std::fabs (vt);
            if (! slipping)
            {
                if (sst > sstPeak) { sstPeak = sst; peakAt = i; }
                if (av > 2.0 * vs)
                {
                    slipping = true; lastOn = i;
                    onset.push_back (t);
                    const double th = sstPeak / std::max (nEff, 1e-3);
                    if (th > 0.3 && th < 2.0) thr += 0.4 * (th - thr);
                    const int64_t lg = i - peakAt;
                    if (lg >= 0 && lg < (int64_t) (0.25 * slipMs / 1000.0 * fsc) + 64) lag += (lg - lag) / 2;
                    sstPeak = -1e9;
                    // the next slip due is the next grid point still ahead (a small early slip on a rough
                    // surface does not use up the grid)
                    const double ahead = t + 0.3 * (next + 1 < due.size() ? due[next + 1] - due[next] : 0.05);
                    while (next < due.size() && due[next] <= ahead) ++next;
                }
            }
            else
            {
                if (av < 0.5 * vs) { if (lowRun++ == 0) lowStart = i; } else lowRun = 0;
                if (lowRun > (int) std::max (0.0003 * fsc, 0.08 * slipMs / 1000.0 * fsc)) { slipping = false; lowRun = 0; restick.push_back (lowStart * dt); }
            }
            if ((i % os) == 0) x[(size_t) (i / os)] = (float) xt;
        }
        (void) lastOn;
        if (restick.size() < onset.size()) restick.push_back (tEnd);
    }
};

// ---------------------------------------------------------------- the effect
class FrictionStretch
{
public:
    static constexpr int kN = 2048, kBins = kN / 2 + 1, kHa = 64, kHs = 256;

    // The stretched hit (stereo) for a hit at hitTime on the song (barStart: the grid's origin in tempo mode).
    void render (const StretchParams& P, const FrictionMaterial& rubber, double bpm, double hitTime, double barStart,
                 const float* inL, const float* inR, int inLen, float fs,
                 std::vector<float>& outL, std::vector<float>& outR, const Engine2Material* rollBody = nullptr)
    {
        sr = fs;
        const double Lin = (double) inLen / fs;
        const double attack = std::min (Lin, (double) P.attackMs / 1000.0);
        // the accent's length: the gate (if any), then the decay
        const double gate = P.rate.tempo ? P.gateBeats * 60.0 / std::max (bpm, 1.0) : P.gateMs / 1000.0;
        const double limit = gate > 0.0 ? gate + P.decayMs / 1000.0 + 0.05 : 1.0e9;
        // ---- the yanks on the swung grid, then the friction contact that keeps them
        const SwingGrid grid = SwingGrid::make (P.rate, bpm, hitTime, barStart);
        const double stretch = std::max (1.0f, std::min (64.0f, P.stretch));
        std::vector<double> due;
        {
            int64_t k = grid.atOrAfter (hitTime + attack + 0.02);
            double pos = attack, prev = attack;
            while (pos < Lin && due.size() < 200000)   // no practical cap: 64x at the finest rates still fits
            {
                const double t = grid.timeOf (k++) - hitTime;
                if (t > limit) break;
                pos += (t - prev) / stretch; prev = t;
                due.push_back (t);
            }
        }
        const double outSec = std::min (limit, (due.empty() ? attack : due.back()) + 0.25 + (double) kN / fs);
        const int outLen = (int) std::ceil (outSec * fs);
        clock.run (rubber, due, attack, outSec, P.slipMs, P.grit, P.seed, fs);
        buildPlayhead (P, attack, stretch, outLen);
        buildEnvelope (P, grid.step, attack, outLen);
        // ---- the lines' grip on the spectrum (the freeze layer)
        std::vector<StretchLine> lines = P.lines;
        if (lines.empty()) lines.push_back (StretchLine {});
        double minTau = 0.0;
        for (const auto& l : lines) minTau = std::min ({ minTau, (double) l.offset, (double) l.offset + std::min (0.0f, l.tilt) });
        prepareBins (lines, grid.step, P.seed, minTau);
        outL.assign ((size_t) outLen, 0.0f); outR.assign ((size_t) outLen, 0.0f);
        auto mix = [&] (const std::vector<float>& a, const std::vector<float>& b, float g)
        {
            for (int i = 0; i < outLen; ++i) { outL[(size_t) i] += g * a[(size_t) i]; outR[(size_t) i] += g * b[(size_t) i]; }
        };
        if (P.freeze > 0.0f)
        {
            std::vector<float> gL, gR;
            grain (inL, inLen, outLen, lines, gL, 0); grain (inR, inLen, outLen, lines, gR, 1);
            mix (gL, gR, P.freeze);
        }
        if (P.tape > 0.0f)
        {
            std::vector<float> tL, tR;
            tapeRead (inL, inLen, outLen, lines, minTau, tL, 0); tapeRead (inR, inLen, outLen, lines, minTau, tR, 1);
            mix (tL, tR, P.tape);
        }
        if (P.peel > 0.0f)
        {
            std::vector<float> pL, pR;
            peel (P, inL, inR, inLen, outLen, attack, rollBody, pL, pR);
            mix (pL, pR, P.peel);
        }
        // the gate and the decay
        const double tau = std::max (0.005, P.decayMs / 1000.0 / 6.9);
        for (int i = 0; i < outLen; ++i)
        {
            const double t = (double) i / fs;
            const float g = (float) (gate > 0.0 && t > gate ? std::exp (-(t - gate) / tau) : 1.0) * P.wet;
            outL[(size_t) i] *= g; outR[(size_t) i] *= g;
        }
    }

    const FrictionClock& lastClock() const noexcept { return clock; }
    const std::vector<float>& lastPlayhead() const noexcept { return head; }   // input seconds, per output sample

private:
    // ---- the playhead: input seconds at each output sample
    void buildPlayhead (const StretchParams& P, double attack, double stretch, int outLen)
    {
        head.assign ((size_t) outLen, 0.0f);
        const float fs = sr;
        const int a0 = (int) (attack * fs);
        for (int i = 0; i < std::min (a0, outLen); ++i) head[(size_t) i] = (float) i / fs;
        const auto& on = clock.onset; const auto& rs = clock.restick; const auto& x = clock.x;
        auto xAt = [&] (double t) { const int i = std::min ((int) x.size() - 1, std::max (0, (int) std::llround (t * fs))); return (double) x[(size_t) i]; };
        double P0 = attack, segStart = attack;
        const float c = std::min (1.0f, std::max (0.0f, P.creep));
        for (size_t k = 0; k < on.size(); ++k)
        {
            const double segEnd = k < rs.size() ? rs[k] : on[k] + 0.01;
            const double prevOn = k == 0 ? attack : on[k - 1];
            const double delta = (on[k] - prevOn) / stretch;
            const double x0 = xAt (segStart), D = std::max (1e-9, xAt (segEnd) - x0);
            const int i0 = (int) std::ceil (segStart * fs), i1 = std::min (outLen, (int) std::ceil (segEnd * fs));
            for (int i = std::max (i0, a0); i < i1; ++i)
            {
                const double t = (double) i / fs;
                const double lam = std::min (1.0, std::max (0.0, (t - segStart) / std::max (1e-6, on[k] - segStart)));
                const double chi = (xAt (t) - x0) / D;
                head[(size_t) i] = (float) (P0 + delta * (c * lam + (1.0 - c) * chi));
            }
            P0 += delta; segStart = segEnd;
        }
        const int iLast = std::max (a0, (int) std::ceil (segStart * fs));
        for (int i = iLast; i < outLen; ++i) head[(size_t) i] = (float) P0;
    }
    // the stuck sound's level: fading while it sticks, kicked on each slip
    void buildEnvelope (const StretchParams& P, double step, double attack, int outLen)
    {
        env.assign ((size_t) outLen, 1.0f);
        const float fade = std::min (1.0f, std::max (0.0f, P.holdFade)), snap = std::max (0.0f, P.snap);
        if (fade <= 0.0f && snap <= 0.0f) return;
        const double tau = fade > 0.0f ? step * (0.04 + 3.0 * (1.0 - fade) * (1.0 - fade)) : 1e9;
        const auto& on = clock.onset; const auto& rs = clock.restick;
        size_t k = 0;
        double lastFree = attack;       // when it last stuck
        for (int i = 0; i < outLen; ++i)
        {
            const double t = (double) i / sr;
            while (k < on.size() && on[k] <= t) { lastFree = k < rs.size() ? rs[k] : on[k]; ++k; }
            float g = 1.0f;
            if (t >= attack)
            {
                const bool slipping = k > 0 && t < lastFree;
                if (slipping)
                {   // the jump: from its stuck level back up, with an accent shaped like the slip
                    const double s0 = on[k - 1], s1 = std::max (s0 + 1e-4, lastFree);
                    const double u = (t - s0) / (s1 - s0);
                    g = (float) (1.0 + snap * std::sin (3.14159265 * u));
                }
                else g = (float) std::exp (-(t - lastFree) / tau);
            }
            env[(size_t) i] = g;
        }
    }
    float envAt (double t) const noexcept
    {
        if (env.empty() || t < 0.0) return 1.0f;
        const int i = std::min ((int) env.size() - 1, (int) (t * sr));
        return env[(size_t) i];
    }
    double headAt (double t) const noexcept
    {
        if (t < 0.0) return -1.0;
        const double s = t * sr; const int i = (int) s;
        if (i >= (int) head.size() - 1) return head.back();
        const double f = s - i;
        return head[(size_t) i] + (head[(size_t) i + 1] - head[(size_t) i]) * f;
    }

    // ---- which line holds each bin, and how late each bin runs
    void prepareBins (const std::vector<StretchLine>& lines, double step, uint32_t seed, double minTau)
    {
        const int L = (int) lines.size();
        weight.assign ((size_t) L * kBins, 0.0f); tau.assign ((size_t) L * kBins, 0.0f);
        std::mt19937 rng (seed * 31u + 7u); std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        for (int b = 0; b < kBins; ++b)
        {
            const float f = std::max (1.0f, (float) b * sr / kN);
            float sum = 0.0f;
            for (int j = 0; j < L; ++j)
            {
                const auto& l = lines[(size_t) j];
                if (! l.on) continue;
                const float lo = std::log2 (std::max (1.0f, l.fLo)), hi = std::log2 (std::max (l.fLo + 1.0f, l.fHi)), lf = std::log2 (f);
                const float e = std::max (0.01f, l.edge);
                const float up = std::min (1.0f, std::max (0.0f, (lf - (lo - 0.5f * e)) / e));
                const float dn = std::min (1.0f, std::max (0.0f, ((hi + 0.5f * e) - lf) / e));
                const float w = up * dn;
                const float u = std::min (1.0f, std::max (0.0f, (lf - lo) / std::max (0.01f, hi - lo)));
                weight[(size_t) (j * kBins + b)] = w;
                tau[(size_t) (j * kBins + b)] = (float) (l.offset + l.tilt * u + l.spray * (float) step * uni (rng) - minTau);
                sum += w;
            }
            if (sum > 1.0f) for (int j = 0; j < L; ++j) weight[(size_t) (j * kBins + b)] /= sum;
        }
    }

    static void fft (std::vector<std::complex<float>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979 / (double) len * (inverse ? 1 : -1);
            for (size_t i = 0; i < n; i += len)
                for (size_t j = 0; j < len / 2; ++j)
                {
                    const std::complex<float> w ((float) std::cos (ang * (double) j), (float) std::sin (ang * (double) j));
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v; a[i + j + len / 2] = u - v;
                }
        }
        if (inverse) for (auto& v : a) v /= (float) n;
    }
    static float princarg (float p) noexcept { return p - 6.28318530718f * std::floor ((p + 3.14159265359f) / 6.28318530718f); }

    // ---- grain: the phase vocoder reading the frozen / jumping frames
    void grain (const float* in, int inLen, int outLen, const std::vector<StretchLine>& lines, std::vector<float>& out, int ch)
    {
        // analysis, frame i centred on input sample i * Ha
        const int frames = inLen / kHa + 2;
        std::vector<float> win (kN);
        for (int n = 0; n < kN; ++n) win[(size_t) n] = 0.5f - 0.5f * std::cos (6.28318530718f * (float) n / kN);
        std::vector<float> mag ((size_t) frames * kBins), ph ((size_t) frames * kBins);
        std::vector<std::complex<float>> buf (kN);
        for (int i = 0; i < frames; ++i)
        {
            for (int n = 0; n < kN; ++n)
            {
                const int s = i * kHa + n - kN / 2;
                buf[(size_t) n] = { s >= 0 && s < inLen ? in[s] * win[(size_t) n] : 0.0f, 0.0f };
            }
            fft (buf, false);
            for (int b = 0; b < kBins; ++b) { mag[(size_t) (i * kBins + b)] = std::abs (buf[(size_t) b]); ph[(size_t) (i * kBins + b)] = std::arg (buf[(size_t) b]); }
        }
        const int L = (int) lines.size();
        std::vector<float> phase ((size_t) L * kBins, 0.0f), lastPos ((size_t) L * kBins, -1.0f);
        out.assign ((size_t) outLen + kN, 0.0f);
        const float hopSec = (float) kHs / sr;
        for (int m = 0; m * kHs < outLen + kN / 2; ++m)
        {
            const double tc = (double) m * kHs / sr;   // this output frame's centre
            std::vector<std::complex<float>> Y (kN, { 0.0f, 0.0f });
            for (int j = 0; j < L; ++j)
            {
                const auto& ln = lines[(size_t) j];
                if (! ln.on) continue;
                const float gainC = ln.level * (ch == 0 ? std::sqrt (0.5f * (1.0f - ln.pan)) : std::sqrt (0.5f * (1.0f + ln.pan))) * 1.41421356f;
                for (int b = 0; b < kBins; ++b)
                {
                    const float w = weight[(size_t) (j * kBins + b)];
                    if (w <= 0.0f) continue;
                    const double s = headAt (tc - tau[(size_t) (j * kBins + b)]);
                    float& Phi = phase[(size_t) (j * kBins + b)];
                    float& last = lastPos[(size_t) (j * kBins + b)];
                    if (s < 0.0 || s * sr >= inLen) { last = -1.0f; continue; }
                    const double fpos = s * sr / kHa;
                    const int i0 = std::min (frames - 2, (int) fpos);
                    const float fr = (float) (fpos - i0);
                    const float M = (1.0f - fr) * mag[(size_t) (i0 * kBins + b)] + fr * mag[(size_t) ((i0 + 1) * kBins + b)];
                    const float wb = 6.28318530718f * (float) b / kN;
                    const float dphi = princarg (ph[(size_t) ((i0 + 1) * kBins + b)] - ph[(size_t) (i0 * kBins + b)] - wb * kHa);
                    const float winst = wb + dphi / kHa;
                    const float moved = last < 0.0f ? 1.0e9f : (float) s - last;
                    const float speed = moved / hopSec;
                    if (last < 0.0f || std::fabs (speed) > 3.0f || std::fabs (speed - 1.0f) < 0.02f)
                    {   // a start, a jump (cut) or plain playback: pick the partials up as they were there
                        const int ir = std::min (frames - 1, (int) std::llround (fpos));
                        Phi = ph[(size_t) (ir * kBins + b)];
                        if (std::fabs (speed - 1.0f) < 0.02f) Phi += winst * (float) ((fpos - ir) * kHa);
                    }
                    else Phi = princarg (Phi + winst * kHs);   // frozen or sliding: keep turning at its own rate
                    last = (float) s;
                    Y[(size_t) b] += std::polar (M * w * gainC * envAt (tc - tau[(size_t) (j * kBins + b)]), Phi);
                }
            }
            for (int b = 1; b < kBins - 1; ++b) Y[(size_t) (kN - b)] = std::conj (Y[(size_t) b]);
            fft (Y, true);
            const int start = m * kHs - kN / 2;
            for (int n = 0; n < kN; ++n)
            {
                const int o = start + n;
                if (o >= 0 && o < (int) out.size()) out[(size_t) o] += Y[(size_t) n].real() * win[(size_t) n] / 3.0f * ((float) kHa / kHs) * (float) kHs / kHa;
            }
        }
        out.resize ((size_t) outLen);
    }

    // ---- tape: each band read at the playhead, pitch following its speed (silent while stuck)
    void tapeRead (const float* in, int inLen, int outLen, const std::vector<StretchLine>& lines, double minTau, std::vector<float>& out, int ch)
    {
        out.assign ((size_t) outLen, 0.0f);
        const int L = (int) lines.size();
        constexpr int kSub = 4, kH = kN / 4;
        std::vector<float> win (kN);
        for (int n = 0; n < kN; ++n) win[(size_t) n] = 0.5f - 0.5f * std::cos (6.28318530718f * (float) n / kN);
        const int frames = inLen / kH + kN / kH + 1;
        // the input's spectrum once
        std::vector<std::vector<std::complex<float>>> X ((size_t) frames, std::vector<std::complex<float>> (kN));
        for (int i = 0; i < frames; ++i)
        {
            auto& buf = X[(size_t) i];
            for (int n = 0; n < kN; ++n) { const int s = i * kH + n - kN; buf[(size_t) n] = { s >= 0 && s < inLen ? in[s] * win[(size_t) n] : 0.0f, 0.0f }; }
            fft (buf, false);
        }
        std::mt19937 rng (91u + (uint32_t) ch);
        for (int j = 0; j < L; ++j)
        {
            const auto& ln = lines[(size_t) j];
            if (! ln.on) continue;
            const float gainC = ln.level * (ch == 0 ? std::sqrt (0.5f * (1.0f - ln.pan)) : std::sqrt (0.5f * (1.0f + ln.pan))) * 1.41421356f;
            const float lo = std::log2 (std::max (1.0f, ln.fLo)), hi = std::log2 (std::max (ln.fLo + 1.0f, ln.fHi));
            for (int sb = 0; sb < kSub; ++sb)
            {
                const float a = lo + (hi - lo) * sb / kSub, bnd = lo + (hi - lo) * (sb + 1) / kSub;
                // the band's own signal (the line's weight times this quarter of it)
                std::vector<float> band ((size_t) inLen + 2 * kN, 0.0f);
                for (int i = 0; i < frames; ++i)
                {
                    std::vector<std::complex<float>> Y (kN, { 0.0f, 0.0f });
                    for (int b = 0; b < kBins; ++b)
                    {
                        const float lf = std::log2 (std::max (1.0f, (float) b * sr / kN));
                        const float e = 0.15f;
                        const float m = std::min (1.0f, std::max (0.0f, (lf - (a - 0.5f * e)) / e)) * std::min (1.0f, std::max (0.0f, ((bnd + 0.5f * e) - lf) / e));
                        const float w = weight[(size_t) (j * kBins + b)] * (sb == 0 && lf < a ? 1.0f : (sb == kSub - 1 && lf > bnd ? 1.0f : m));
                        Y[(size_t) b] = X[(size_t) i][(size_t) b] * w;
                    }
                    for (int b = 1; b < kBins - 1; ++b) Y[(size_t) (kN - b)] = std::conj (Y[(size_t) b]);
                    fft (Y, true);
                    for (int n = 0; n < kN; ++n) { const int s = i * kH + n - kN; if (s >= 0 && s < (int) band.size()) band[(size_t) s] += Y[(size_t) n].real() * win[(size_t) n] / 1.5f; }
                }
                const float tauSb = (float) (ln.offset + ln.tilt * (sb + 0.5f) / kSub - minTau);
                float dc = 0.0f;
                double prev = -1.0;
                for (int i = 0; i < outLen; ++i)
                {
                    const double s = headAt ((double) i / sr - tauSb);
                    if (s < 0.0 || s * sr >= inLen) { prev = -1.0; continue; }
                    const double speed = prev < 0.0 ? 1.0 : std::fabs (s - prev) * sr;
                    prev = s;
                    const double pos = s * sr;
                    const int k = (int) pos; const float f = (float) (pos - k);
                    auto at = [&] (int q) { return band[(size_t) std::min (inLen - 1, std::max (0, q))]; };
                    const float y0 = at (k - 1), y1 = at (k), y2 = at (k + 1), y3 = at (k + 2);
                    const float v = ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f + y1);
                    const float y = v * (float) std::sqrt (std::min (1.5, speed));   // tape: no motion, no sound
                    dc += 0.0026f * (y - dc);                                         // ~20 Hz: no thump from a stopped tape
                    out[(size_t) i] += gainC * (y - dc) * envAt ((double) i / sr - tauSb);
                }
            }
        }
        (void) rng;
    }


    // ---- the duct tape: yanks and creep drive the peel front; each micro-slip fires a grain of the hit
    void peel (const StretchParams& P, const float* inL, const float* inR, int inLen, int outLen, double attack,
               const Engine2Material* roll, std::vector<float>& oL, std::vector<float>& oR)
    {
        const float fs = sr;
        oL.assign ((size_t) outLen, 0.0f); oR.assign ((size_t) outLen, 0.0f);
        // how fast the tape comes off (each yank's own speed curve, 0..1) and how taut the loose strip is
        std::vector<float> vel ((size_t) outLen, 0.0f), tens ((size_t) outLen, 0.25f);
        const auto& x = clock.x; const auto& on = clock.onset; const auto& rs = clock.restick;
        for (int i = 0; i + 1 < outLen && i + 1 < (int) x.size(); ++i) vel[(size_t) i] = std::max (0.0f, (x[(size_t) i + 1] - x[(size_t) i]) * fs);
        double prevFree = attack;
        for (size_t k = 0; k < on.size(); ++k)
        {
            const int a = std::max (0, (int) (on[k] * fs) - (int) (0.002 * fs));
            const int b = std::min (outLen, (int) ((k < rs.size() ? rs[k] : on[k] + 0.1) * fs) + (int) (0.002 * fs));
            float pk = 1e-9f; for (int i = a; i < b; ++i) pk = std::max (pk, vel[(size_t) i]);
            for (int i = a; i < b; ++i) vel[(size_t) i] /= pk;
            // tension: builds through the stick, drops through the yank
            const int s0 = (int) (prevFree * fs);
            for (int i = std::max (0, s0); i < std::min (a, outLen); ++i) tens[(size_t) i] = 0.25f + 0.75f * (float) (i - s0) / (float) std::max (1, a - s0);
            for (int i = a; i < b; ++i) tens[(size_t) i] = 1.0f - 0.75f * (float) (i - a) / (float) std::max (1, b - a);
            prevFree = k < rs.size() ? rs[k] : on[k];
        }
        // anything outside a yank is creep (or stuck)
        std::vector<char> inYank ((size_t) outLen, 0);
        for (size_t k = 0; k < on.size(); ++k)
            for (int i = std::max (0, (int) (on[k] * fs) - (int) (0.002 * fs)); i < std::min (outLen, (int) ((k < rs.size() ? rs[k] : on[k] + 0.1) * fs) + (int) (0.002 * fs)); ++i) inYank[(size_t) i] = 1;
        for (int i = 0; i < outLen; ++i) if (! inYank[(size_t) i]) vel[(size_t) i] = 0.0f;
        const float creepV = 0.006f + 0.05f * std::min (1.0f, std::max (0.0f, P.creep));
        // layers: grains of the hit, the glue's crackle, and the excitation that rings the roll and the strip
        std::vector<float> gL ((size_t) outLen, 0.0f), gR ((size_t) outLen, 0.0f), cL ((size_t) outLen, 0.0f), cR ((size_t) outLen, 0.0f), ex ((size_t) outLen, 0.0f);
        std::mt19937 rng (P.seed * 4099u + 11u);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        std::normal_distribution<float> gn (0.0f, 1.0f);
        const float grit = std::min (1.0f, std::max (0.0f, P.grit));
        double tauF = 0.0, theta = 1.0;
        for (int i = (int) (attack * fs); i < outLen; ++i)
        {
            const float v = std::max (vel[(size_t) i], creepV);
            const float rate = P.screechHz * v;
            tauF += rate / fs;
            if (tauF < theta) continue;
            tauF -= theta;
            const float jit = 0.04f + 0.7f * (1.0f - v) * (1.0f - v) * (0.4f + grit);
            theta = std::min (2.5f, std::max (0.25f, 1.0f + jit * gn (rng)));
            // one micro-slip: its energy grows with the speed it lets go at
            const float A = (0.12f + 0.88f * std::sqrt (v)) * (0.6f + 0.65f * uni (rng));
            // creeping: short ticks; tearing: overlapping, near-coherent grains (the rip takes the hit's colour, pitched at the slip rate)
            const float g = std::max (0.0015f, P.grainMs / 1000.0f * (0.25f + 0.75f * v));
            const int gN = (int) (g * fs);
            const float norm = 1.0f / std::sqrt (std::max (1.0f, rate * g));
            const double pos = std::max (0.0, ((double) head[(size_t) i] + (uni (rng) - 0.5f) * 0.002 * P.rasp) * fs);
            const float pan = std::min (1.0f, std::max (-1.0f, (uni (rng) * 2.0f - 1.0f) * P.width));
            const float pl = std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f, pr = std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
            const float dDec = std::max (1.0f, (float) gN / 3.0f);
            if (pos < inLen - 2)
                for (int n = 0; n < gN && i + n < outLen; ++n)
                {
                    const float sh = std::sin (3.14159265f * (float) n / (float) gN);
                    const float w = sh * sh * (1.0f + 1.5f * std::exp (-(float) n / dDec)) * 0.6f * A * norm;   // a smooth grain with a little bite at the front
                    const double q = pos + n;
                    if (q >= inLen - 2) break;
                    gL[(size_t) (i + n)] += w * cubicAt (inL, inLen, q) * pl;
                    gR[(size_t) (i + n)] += w * cubicAt (inR, inLen, q) * pr;
                }
            // the glue's dry crackle: a tick of noise, high and short
            const int cN = (int) (0.0008f * fs);
            float hp = 0.0f, prev = 0.0f;
            const float cp = std::min (1.0f, std::max (-1.0f, (uni (rng) * 2.0f - 1.0f) * P.width * 1.3f));
            const float cl = std::sqrt (0.5f * (1.0f - cp)) * 1.41421356f, cr = std::sqrt (0.5f * (1.0f + cp)) * 1.41421356f;
            for (int n = 0; n < cN && i + n < outLen; ++n)
            {
                const float z = gn (rng); hp = z - prev; prev = z;
                const float w = std::exp (-(float) n / (0.00018f * fs)) * A;
                cL[(size_t) (i + n)] += w * hp * cl; cR[(size_t) (i + n)] += w * hp * cr;
            }
            ex[(size_t) i] += A;
        }
        // the hit's grains set the level; the other layers sit relative to them
        auto rms = [&] (const std::vector<float>& a, const std::vector<float>& b) { double e = 0; for (size_t i = 0; i < a.size(); ++i) e += (double) a[i] * a[i] + (double) b[i] * b[i]; return std::sqrt (e / std::max<size_t> (1, 2 * a.size())); };
        const double ref = std::max (1e-9, rms (gL, gR));
        auto addRel = [&] (const std::vector<float>& a, const std::vector<float>& b, float amount, float relDb)
        {
            const double r = rms (a, b); if (r < 1e-12 || amount <= 0.0f) return;
            const float g = (float) (amount * ref / r * std::pow (10.0, relDb / 20.0));
            for (int i = 0; i < outLen; ++i) { oL[(size_t) i] += g * a[(size_t) i]; oR[(size_t) i] += g * b[(size_t) i]; }
        };
        for (int i = 0; i < outLen; ++i) { oL[(size_t) i] = gL[(size_t) i]; oR[(size_t) i] = gR[(size_t) i]; }
        addRel (cL, cR, P.adhesive, -3.0f);
        // the roll: the learned body's strongest modes, rung by every micro-slip
        if (roll != nullptr && P.roll > 0.0f && roll->modes() > 0)
        {
            std::vector<int> idx ((size_t) roll->modes());
            for (int k = 0; k < roll->modes(); ++k) idx[(size_t) k] = k;
            std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return roll->a[(size_t) a] > roll->a[(size_t) b]; });
            const int K = std::min (24, (int) idx.size());
            std::vector<float> rl ((size_t) outLen, 0.0f), rr ((size_t) outLen, 0.0f);
            for (int kk = 0; kk < K; ++kk)
            {
                const int k = idx[(size_t) kk];
                const float f = roll->f[(size_t) k] / std::max (0.25f, P.rollSize);
                if (f >= 0.45f * fs) continue;
                const float sig = (roll->b1 + roll->b3 * (f / 1000.0f) * (f / 1000.0f)) * std::exp (roll->rho[(size_t) k]);
                const float r = std::exp (-sig / fs), w = 6.2831853f * f / fs;
                const float c1 = 2.0f * r * std::cos (w), c2 = -r * r, amp = roll->a[(size_t) k];
                const float side = (kk % 2 ? 1.0f : -1.0f) * 0.25f;
                float y1 = 0, y2 = 0;
                for (int i = 0; i < outLen; ++i)
                {
                    const float y = c1 * y1 + c2 * y2 + ex[(size_t) i];
                    y2 = y1; y1 = y;
                    rl[(size_t) i] += amp * y * (1.0f - side); rr[(size_t) i] += amp * y * (1.0f + side);
                }
            }
            addRel (rl, rr, P.roll, -4.0f);
        }
        // the loose strip: a taut ribbon whose pitch follows the tension
        if (P.strip > 0.0f)
        {
            std::vector<float> sl ((size_t) outLen, 0.0f);
            for (int h = 1; h <= 4; ++h)
            {
                float y1 = 0, y2 = 0;
                const float sig = 25.0f + 12.0f * h;
                const float r = std::exp (-sig / fs);
                for (int i = 0; i < outLen; ++i)
                {
                    const float f = 150.0f * (1.0f + 2.2f * tens[(size_t) i]) * h * std::sqrt (1.0f + 0.003f * h * h);
                    const float w = 6.2831853f * std::min (f, 0.45f * fs) / fs;
                    const float y = 2.0f * r * std::cos (w) * y1 - r * r * y2 + ex[(size_t) i] / (float) h;
                    y2 = y1; y1 = y;
                    sl[(size_t) i] += y;
                }
            }
            addRel (sl, sl, P.strip, -6.0f);
        }
        // as loud as the hit: the peel over the stretch of the hit's first 300 ms matches that stretch's level
        {
            const int i0 = (int) (attack * fs), i1 = std::min (inLen, i0 + (int) (0.3 * fs));
            double ein = 0, pin = 1e-9; int nin = 0;
            for (int i = i0; i < i1; ++i) { ein += (double) inL[i] * inL[i] + (double) inR[i] * inR[i]; pin = std::max (pin, (double) std::max (std::fabs (inL[i]), std::fabs (inR[i]))); ++nin; }
            const double endPos = (double) i1 / fs;
            double eo = 0, po = 1e-9; int no = 0;
            for (int i = i0; i < outLen && head[(size_t) i] < endPos; ++i) { eo += (double) oL[(size_t) i] * oL[(size_t) i] + (double) oR[(size_t) i] * oR[(size_t) i]; po = std::max (po, (double) std::max (std::fabs (oL[(size_t) i]), std::fabs (oR[(size_t) i]))); ++no; }
            if (nin > 0 && no > 0 && eo > 1e-20)
            {
                const double g = std::min ({ 30.0, std::sqrt ((ein / nin) / (eo / no)), 1.4 * pin / po });
                for (int i = 0; i < outLen; ++i) { oL[(size_t) i] *= (float) g; oR[(size_t) i] *= (float) g; }
            }
        }
        // the hit's own attack, before the tape first sticks
        const int aN = std::min ({ outLen, inLen, (int) (attack * fs) + (int) (0.004 * fs) });
        const int fadeN = std::max (1, (int) (0.004 * fs));
        for (int i = 0; i < aN; ++i)
        {
            const float w = i < aN - fadeN ? 1.0f : (float) (aN - i) / (float) fadeN;
            oL[(size_t) i] += w * inL[i]; oR[(size_t) i] += w * inR[i];
        }
    }
    static float cubicAt (const float* x, int n, double pos) noexcept
    {
        const int i = (int) pos; const float f = (float) (pos - i);
        auto at = [&] (int k) { return x[std::min (n - 1, std::max (0, k))]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        return ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f + y1);
    }

    float sr = 48000.0f;
    FrictionClock clock;
    std::vector<float> head, weight, tau, env;
};
} // namespace nb
