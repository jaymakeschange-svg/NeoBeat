// BatchCut - test recordings for the host test and the snapshots (written as real WAV files).
#pragma once

#include <random>
#include "app/Processor.h"

namespace testaudio
{
struct Made { juce::File file; std::vector<double> truth; double sr = 48000.0; };

struct Gen { std::mt19937 rng { 5 }; std::normal_distribution<float> nd; float white() { return nd (rng); } float uni (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng); } };

inline void clank (std::vector<float>& x, size_t at, float amp, double f0, double decay, double sr, Gen& g)
{
    const double ratios[] = { 1.0, 1.61, 2.32, 3.71, 4.93, 6.4 };
    for (size_t i = 0; i < (size_t) (sr * 0.7) && at + i < x.size(); ++i)
    {
        const double t = (double) i / sr; double v = 0;
        for (int k = 0; k < 6; ++k) v += std::exp (-decay * (1 + 0.6 * k) * t) * std::sin (6.283185307 * f0 * ratios[k] * t + k) / (1 + 0.5 * k);
        v += 0.6 * g.white() * std::exp (-300.0 * t);
        x[at + i] += (float) (amp * std::min (1.0, t * 4000.0) * v * 0.4);
    }
}
inline void thud (std::vector<float>& x, size_t at, float amp, double sr)
{
    double ph = 0;
    for (size_t i = 0; i < (size_t) (sr * 0.3) && at + i < x.size(); ++i) { const double t = (double) i / sr; ph += 6.283185307 * (60 + 90 * std::exp (-t * 30)) / sr; x[at + i] += (float) (amp * std::exp (-t * 12) * std::sin (ph) * std::min (1.0, t * 3000)); }
}
inline void hat (std::vector<float>& x, size_t at, float amp, double sr, Gen& g)
{
    double hp = 0, prev = 0;
    for (size_t i = 0; i < (size_t) (sr * 0.06) && at + i < x.size(); ++i) { const double t = (double) i / sr; const double w = g.white(); hp = 0.6 * (hp + w - prev); prev = w; x[at + i] += (float) (amp * 0.5 * std::exp (-t * 70) * hp); }
}
inline void snare (std::vector<float>& x, size_t at, float amp, double sr, Gen& g)
{
    for (size_t i = 0; i < (size_t) (sr * 0.2) && at + i < x.size(); ++i) { const double t = (double) i / sr; x[at + i] += (float) (amp * std::min (1.0, t * 3000) * (0.5 * std::exp (-t * 30) * std::sin (6.283185307 * 190 * t) + 0.45 * std::exp (-t * 22) * g.white())); }
}
inline void pinkNoise (std::vector<float>& x, float rms, Gen& g)
{
    double b0 = 0, b1 = 0, b2 = 0, lp = 0, e = 0; std::vector<float> n (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double w = g.white(); b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        lp += 0.003 * (g.white() - lp);
        n[i] = (float) (b0 + b1 + b2 + w * 0.1848 + 20.0 * lp); e += (double) n[i] * n[i];
    }
    const float k = rms / (float) std::sqrt (e / (double) std::max<size_t> (1, x.size()) + 1e-30);
    for (size_t i = 0; i < x.size(); ++i) x[i] += k * n[i];
}

inline bool write (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    std::string err; return BcProcessor::writeWav (f, l, r.empty() ? l : r, sr, 24, err);
}

// a yard: clanks and thuds in traffic noise, 20 s, 48 kHz stereo
inline Made field (const juce::File& dir)
{
    Made m; m.sr = 48000.0; m.file = dir.getChildFile ("yard clanks.wav");
    Gen g; std::vector<float> l ((size_t) (20 * m.sr), 0.0f);
    double t = 0.8; int k = 0;
    while (t < 19.0) { if (k++ % 3 == 2) thud (l, (size_t) (t * m.sr), 0.5f, m.sr); else clank (l, (size_t) (t * m.sr), g.uni (0.08f, 0.4f), 760.0, 14.0, m.sr, g); m.truth.push_back (t); t += g.uni (0.9f, 1.7f); }
    pinkNoise (l, 0.02f, g);
    auto r = l; for (auto& v : r) v *= 0.9f;
    write (m.file, l, r, m.sr);
    return m;
}

// a drum loop: 8 bars at 120 BPM, kick, snare and eighth hats, 44.1 kHz
inline Made loop (const juce::File& dir)
{
    Made m; m.sr = 44100.0; m.file = dir.getChildFile ("drum loop 120.wav");
    Gen g; std::vector<float> l ((size_t) (16.5 * m.sr), 0.0f);
    for (int b = 0; b < 64; ++b)
    {
        const double t = 0.25 + b * 0.25; const size_t at = (size_t) (t * m.sr);
        if (b % 8 == 0 || b % 8 == 5) { thud (l, at, 0.7f, m.sr); m.truth.push_back (t); }
        else if (b % 8 == 4) { snare (l, at, 0.5f, m.sr, g); m.truth.push_back (t); }
        else { hat (l, at, 0.35f, m.sr, g); m.truth.push_back (t); }
    }
    for (auto& v : l) v += 0.0005f * g.white();
    write (m.file, l, {}, m.sr);
    return m;
}

// a long take: 9.5 minutes at 16 kHz, mono, a clank every 3 s (opens in two parts)
inline Made longTake (const juce::File& dir)
{
    Made m; m.sr = 16000.0; m.file = dir.getChildFile ("long take.wav");
    Gen g; std::vector<float> l ((size_t) (9.5 * 60 * m.sr), 0.0f);
    for (double t = 1.0; t < 9.5 * 60 - 1; t += 3.0) { clank (l, (size_t) (t * m.sr), 0.3f, 600.0, 14.0, m.sr, g); m.truth.push_back (t); }
    for (auto& v : l) v += 0.004f * g.white();
    write (m.file, l, {}, m.sr);
    return m;
}
} // namespace testaudio
