// NeoBeat — an Amaranth Cycle-style voice (the method, from Cycle's tutorials
// and mesh files; no code from the product):
//   * a waveshape drawn as curve nodes (phase, amplitude, sharpness)
//   * every node is the blend of vertices on a (time x key) cube
//   * a scratch envelope warps the time axis; a deformer wiggles node paths
//   * each 8 ms frame is band-limited for its pitch and crossfaded to the next
#pragma once

#include "Filters.h"
#include <array>
#include <complex>
#include <vector>

namespace nb
{
// Small radix-2 complex FFT (in place), enough for 1024-point frames.
inline void fft (std::vector<std::complex<float>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = (inverse ? 2.0f : -2.0f) * kPi / (float) len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (float) n;
}

struct CurveNode { float phase = 0.0f, amp = 0.0f, sharp = 0.0f; };

struct CycleSpec
{
    static constexpr int kMaxNodes = 8;
    int numNodes = 4;
    std::array<CurveNode, kMaxNodes> start {}, end {};   // the cube's time axis (key fixed)
    float scratchTime = 0.25f;   // time axis: 0 -> 1 over this many seconds
    float deform = 0.0f, deformHz = 5.0f, frameMs = 8.0f, bright = 1.0f;
    float aa = 0.01f, ad = 0.3f, as = 0.8f, ar = 0.3f;
};

class CycleVoice
{
public:
    static constexpr int kSize = 1024;

    void prepare (float sampleRate) { sr = sampleRate; buf.assign (kSize, {}); tableA.assign (kSize, 0.0f); tableB.assign (kSize, 0.0f); }

    void start (const CycleSpec& s, float hz, float durSeconds, float gateSeconds, uint32_t seed) noexcept
    {
        spec = s; f0 = hz; dur = durSeconds; gate = gateSeconds; n = 0; phase = 0.0f;
        hop = std::max (16, (int) (spec.frameMs * 0.001f * sr));
        rng = Rng (ls::hash32 (seed * 41u + 13u) | 1u);
        for (auto& w : wig) w = 0.0f;
        wigA = std::exp (-(float) hop / (0.3f * sr));
        wigB = std::sqrt (1.0f - wigA * wigA);
        for (auto& w : wig) w = gauss (rng);
        frame = 0;
        makeFrame (0, tableA);
        makeFrame (1, tableB);
        env.start (spec.aa, spec.ad, spec.as, spec.ar, gate, sr);
    }

    float process() noexcept
    {
        const float t = (float) n / sr;
        if (t >= dur) { ++n; return 0.0f; }
        const int64_t f = n / hop;
        while (frame < f)
        {
            std::swap (tableA, tableB);
            ++frame;
            makeFrame (frame + 1, tableB);
        }
        const float xf = (float) (n - f * hop) / (float) hop;
        const float idx = phase * (float) kSize;
        const int i0 = (int) idx & (kSize - 1), i1 = (i0 + 1) & (kSize - 1);
        const float u = idx - std::floor (idx);
        const float a = tableA[(size_t) i0] + (tableA[(size_t) i1] - tableA[(size_t) i0]) * u;
        const float b = tableB[(size_t) i0] + (tableB[(size_t) i1] - tableB[(size_t) i0]) * u;
        phase += f0 / sr; phase -= std::floor (phase);
        ++n;
        return (a * (1.0f - xf) + b * xf) * env.next();
    }

private:
    void makeFrame (int64_t index, std::vector<float>& out) noexcept
    {
        const float ts = (float) (index * hop) / sr;
        const float u = clampf (ts / std::max (spec.scratchTime, 1.0e-3f), 0.0f, 1.0f);
        // advance the deformer's slow random paths (unit-variance wander)
        for (auto& w : wig) w = wigA * w + wigB * gauss (rng);
        std::array<CurveNode, CycleSpec::kMaxNodes> nodes {};
        const int nn = std::min (spec.numNodes, CycleSpec::kMaxNodes);
        for (int m = 0; m < nn; ++m)
        {
            const auto& a = spec.start[(size_t) m];
            const auto& b = spec.end[(size_t) m];
            float p = a.phase + (b.phase - a.phase) * u;
            float am = a.amp + (b.amp - a.amp) * u;
            const float sh = a.sharp + (b.sharp - a.sharp) * u;
            p += spec.deform * 0.02f * std::sin (kTwoPi * spec.deformHz * ts + (float) m) + spec.deform * 0.01f * wig[(size_t) (2 * m)];
            am += spec.deform * 0.08f * wig[(size_t) (2 * m + 1)];
            p -= std::floor (p);
            nodes[(size_t) m] = { p, clampf (am, -1.2f, 1.2f), clampf (sh, 0.0f, 1.0f) };
        }
        std::sort (nodes.begin(), nodes.begin() + nn, [] (const CurveNode& x, const CurveNode& y) { return x.phase < y.phase; });
        drawCurve (nodes.data(), nn, out);
        // band-limit for the pitch
        const int hmax = std::min ((int) (std::min (18000.0f / std::max (f0, 20.0f), 1000.0f) * spec.bright), kSize / 2 - 1);
        for (int i = 0; i < kSize; ++i) buf[(size_t) i] = { out[(size_t) i], 0.0f };
        fft (buf, false);
        for (int k = hmax + 1; k < kSize - hmax; ++k) buf[(size_t) k] = {};
        buf[0] = {};
        fft (buf, true);
        for (int i = 0; i < kSize; ++i) out[(size_t) i] = buf[(size_t) i].real();
    }

    // Smooth segments that sharpen toward each node (Cycle's sharpness).
    static void drawCurve (const CurveNode* nodes, int nn, std::vector<float>& out) noexcept
    {
        float mean = 0.0f;
        for (int i = 0; i < kSize; ++i)
        {
            float t = (float) i / kSize;
            if (t < nodes[0].phase) t += 1.0f;
            int j = nn - 1;
            for (int q = 0; q < nn; ++q)
                if (nodes[q].phase <= t) j = q;
            const CurveNode& a = nodes[j];
            const CurveNode b = j + 1 < nn ? nodes[j + 1] : CurveNode { nodes[0].phase + 1.0f, nodes[0].amp, nodes[0].sharp };
            const float u = clampf ((t - a.phase) / std::max (b.phase - a.phase, 1.0e-6f), 0.0f, 1.0f);
            const float s = b.sharp;
            const float smooth = 0.5f - 0.5f * std::cos (kPi * u);
            const float sharp = std::pow (u, 1.0f + 6.0f * s);
            const float w = smooth * (1.0f - s) + sharp * s;
            out[(size_t) i] = a.amp + (b.amp - a.amp) * w;
            mean += out[(size_t) i];
        }
        mean /= kSize;
        for (auto& v : out) v -= mean;
    }

    CycleSpec spec;
    Adsr env;
    Rng rng;
    std::array<float, 2 * CycleSpec::kMaxNodes> wig {};
    std::vector<std::complex<float>> buf;
    std::vector<float> tableA, tableB;
    float sr = 48000.0f, f0 = 440.0f, dur = 1.0f, gate = 0.2f, phase = 0.0f, wigA = 0.9f, wigB = 0.4f;
    int hop = 384;
    int64_t n = 0, frame = 0;
};
} // namespace nb
