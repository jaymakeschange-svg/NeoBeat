// NeoBeat — Bounce Delay: every repeat is the hit bouncing like a real ball, landing on the swung grid.
//
//   Timing.  The gaps shrink like a ball's: each is `bounciness` of the last, starting from Rate (a note
//   value or ms). Each landing snaps to the nearest point of the swung grid (Grid, with the swing bias),
//   never before the last one, so once the real gaps get shorter than the grid the tail becomes a roll at
//   the grid's step, until the ball has all but stopped (its real gap is a small part of a step).
//   Energy falls the same way: each bounce is `bounciness` of the last.
//   Space.  The ball travels from Distance start to end (at a steady speed, so most of the way is covered
//   by the long early bounces). Each bounce sounds where it landed: quieter and duller with distance
//   (1/r, air), narrower far away and wider up close, panned at random within Pan random of the centre,
//   and with more of the room the further it is.
//   Pitch.  The ball moves while it sounds: away and toward, and up and down on each arc. That motion is
//   a Doppler shift; Speed is how fast it travels, so more Speed bends harder (well past real speeds).
//
// Rendered per hit, offline (the hit is the pad's own sound, captured once): plan() places the bounces
// (also what the window draws as spheres), render() plays them.
#pragma once

#include "SwingGrid.h"
#include "MaterialSpace.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace nb
{
struct BounceParams
{
    RateSpec rate { true, 1.0, 400.0, 0.62f };   // the first gap: a quarter note (or 400 ms), and the swing bias
    double gridBeats = 0.25;                      // the bounces snap to this (tempo mode), 1/16 by default
    double gridMs = 60.0;                         // ... or this (ms mode)
    float bounciness = 0.78f;   // each gap, and each bounce's energy, this fraction of the last
    int maxBounces = 32;
    float distStart = 1.5f, distEnd = 10.0f;      // metres
    float speed = 0.3f;         // 0..1: how fast it travels (the pitch bend); above ~0.5 impossibly fast
    float panRandom = 0.5f;     // 0: every bounce at the centre; 1: anywhere across the field
    float panCenter = 0.0f;
    float width = 1.0f;         // the hit's own stereo width, up close
    float room = 0.25f;         // how much of the room each bounce excites
    float wet = 1.0f;
    float tail = 0.9f;          // seconds each bounce may ring
    uint32_t seed = 1;
};

struct Bounce { double t; float dist, pan, gain, width; };   // one landing (the window draws these)

class BounceDelay
{
public:
    static constexpr float kSpeedOfSound = 343.0f, kGravity = 9.81f, kEar = 1.2f;

    std::vector<Bounce> plan (const BounceParams& p, double bpm, double hitTime, double barStart = 0.0) const
    {
        RateSpec gr = p.rate; gr.beats = p.gridBeats; gr.ms = p.gridMs;
        const SwingGrid grid = SwingGrid::make (gr, bpm, hitTime, barStart);
        const double e = std::min (0.97, std::max (0.2, (double) p.bounciness));
        double gap = std::max (grid.step, p.rate.stepSeconds (bpm)), phys = 0.0, amp = 1.0;
        int64_t last = grid.nearest (hitTime);
        std::vector<Bounce> out;
        std::mt19937 rng (p.seed * 2654435761u + 17u);
        std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
        for (int k = 1; k <= p.maxBounces; ++k)
        {
            phys += gap;
            amp *= std::pow (e, 0.6);                          // each bounce a bit softer (a ball's impacts, heard)
            int64_t idx = grid.nearest (hitTime + phys);
            if (idx <= last) idx = last + 1;                    // never before the last landing: the roll
            last = idx;
            Bounce b;
            b.t = grid.timeOf (idx);
            b.gain = (float) amp;
            b.pan = std::min (1.0f, std::max (-1.0f, p.panCenter + p.panRandom * uni (rng)));
            out.push_back (b);
            gap *= e;
            if (amp < 0.003 || gap < 0.12 * grid.step) break;   // it has all but stopped
        }
        // the path: a steady speed from start to end over the whole bounce
        const double span = out.empty() ? 1.0 : std::max (1.0e-3, out.back().t - hitTime);
        const float rNear = std::max (0.5f, std::min (p.distStart, p.distEnd));
        for (auto& b : out)
        {
            const float u = (float) ((b.t - hitTime) / span);
            b.dist = std::max (0.3f, p.distStart + (p.distEnd - p.distStart) * u);
            b.gain *= std::pow (rNear / std::max (b.dist, rNear * 0.5f), 0.7f);
            b.width = p.width * std::min (1.6f, std::max (0.08f, std::sqrt (1.0f / b.dist) * 1.2f));
        }
        return out;
    }

    // Adds the bounces of one hit into out (out[0] is time 0). hit starts at hitTime. roomSend: mono, adds.
    void render (const BounceParams& p, const std::vector<Bounce>& bounces, double hitTime,
                 const float* hitL, const float* hitR, int hitLen, float fs,
                 float* outL, float* outR, float* roomSend, int outLen) const
    {
        if (bounces.empty() || hitLen < 4) return;
        // the ball's flight: landing points in the plane (the hit itself is the first, at the start distance)
        struct Pt { double t; float x, y; };
        std::vector<Pt> path;
        path.push_back ({ hitTime, p.distStart * std::sin (p.panCenter * 1.4f), p.distStart * std::cos (p.panCenter * 1.4f) });
        for (const auto& b : bounces) path.push_back ({ b.t, b.dist * std::sin (b.pan * 1.4f), b.dist * std::cos (b.pan * 1.4f) });
        const float kappa = 1.0f + 79.0f * p.speed * p.speed;   // how much faster than life it travels
        auto distanceAt = [&] (double t) -> double
        {
            if (t <= path.front().t) return std::hypot ((double) path.front().x, (double) path.front().y, (double) kEar);
            size_t i = 1;
            while (i < path.size() && path[i].t < t) ++i;
            if (i >= path.size()) { const auto& q = path.back(); return std::hypot ((double) q.x, (double) q.y, (double) kEar); }
            const auto& a = path[i - 1]; const auto& b = path[i];
            const double G = std::max (1.0e-4, b.t - a.t), s = t - a.t, u = s / G;
            const double x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
            const double z = 0.5 * kGravity * G * s - 0.5 * kGravity * s * s;   // the arc: up and back down in G
            return std::hypot (x, y, z - kEar);
        };
        const double h = 1.0 / fs;
        for (const auto& b : bounces)
        {
            const int64_t start = (int64_t) std::llround (b.t * fs);
            const int len = (int) std::min ((double) p.tail * fs, (double) outLen - (double) start);
            if (len <= 0 || start < 0) continue;
            const float fc = std::min (0.45f * fs, std::max (1500.0f, 20000.0f / (1.0f + (std::max (b.dist, 1.0f) - 1.0f) / 12.0f)));
            const float air = b.dist > 1.0f ? 1.0f - std::exp (-6.2831853f * fc / fs) : 1.0f;
            const float gl = std::sqrt (std::max (0.0f, 0.5f * (1.0f - b.pan))) * 1.41421356f;
            const float gr = std::sqrt (std::max (0.0f, 0.5f * (1.0f + b.pan))) * 1.41421356f;
            const float g = b.gain * p.wet;
            const float send = p.room * b.gain * std::sqrt (std::max (0.5f, std::min (p.distStart, p.distEnd)) / b.dist);
            const int fadeLen = (int) (0.05f * fs);
            double q = 0.0, dPrev = distanceAt (b.t);
            float lpL = 0, lpR = 0;
            for (int n = 0; n < len; ++n)
            {
                const double t = b.t + (n + 1) * h;
                const double d = distanceAt (t);
                const double vr = (d - dPrev) * fs; dPrev = d;          // m/s, away is positive
                const double rho = std::min (4.0, std::max (0.25, 1.0 / (1.0 + kappa * vr / kSpeedOfSound)));
                if (q >= hitLen - 2) break;
                float l = cubic (hitL, hitLen, q), r = cubic (hitR, hitLen, q);
                q += rho;
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * b.width;
                l = mid + side; r = mid - side;
                lpL += air * (l - lpL); lpR += air * (r - lpR);
                float env = 1.0f;
                if (n > len - fadeLen) env = (float) (len - n) / (float) fadeLen;
                const int64_t o = start + n;
                if (o >= outLen) break;
                outL[o] += g * env * lpL * gl;
                outR[o] += g * env * lpR * gr;
                if (roomSend) roomSend[o] += send * env * 0.5f * (lpL + lpR);
            }
        }
    }

private:
    static float cubic (const float* x, int n, double pos) noexcept
    {
        const int i = (int) pos; const float f = (float) (pos - i);
        auto at = [&] (int k) { return x[std::min (n - 1, std::max (0, k))]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};
} // namespace nb
