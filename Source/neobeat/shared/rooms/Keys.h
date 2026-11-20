// Batchcut 3.0 - KEYS: sounds made into an instrument MidiJaQ can play (saved as a .NJQ instrument).
// Framework-free.
//
//   three ways to play     Tuned: pitched sounds spread over the keyboard, each covering the keys nearest its
//                          pitch, transposed (loudness sets velocity layers, repeats of the same sound become
//                          round robins). Resonator: any sound, tuned or not (a drill, a tyre screech), struck
//                          into a resonator tuned to the key. Chops: a different sound on each key, as it is.
//   sustain                As recorded (then the release); Loop (a loop found in the sound's body, crossfaded);
//                          Hold (the sound's body frozen: overlapping grains around one spot, as Friction
//                          Stretch's hold sticks)
//   shaping                attack, release, glide (and mono)
//   KeysPlayer             the instrument on the audio thread, from MIDI
#pragma once

#include "Kit.h"
#include "../../gen/Rng.h"
#include "../InstrumentPlay.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bc
{
// The instrument itself, its voices and its resonator are shared code: MidiJaQ plays a .NJQ instrument
// with the very same player (neobeat/shared/InstrumentPlay.h). What lives here is the Batchcut half:
// making an instrument out of a pool of sounds.
using KeysMode = njq::PlayMode;
using KeysSustain = njq::Sustain;
using KeyZone = njq::ZoneT<PoolItem>;
using Instrument = njq::InstrumentT<PoolItem>;
using KeysNote = njq::PlayNote;
using KeysPlayer = njq::PlayerT<PoolItem>;
using Resonator = njq::Resonator;
using njq::resonatorRing;
using njq::resonatorCorner;
inline const char* keysModeName (KeysMode m) { return njq::playModeName (m); }
inline const char* keysSustainName (KeysSustain s) { return njq::sustainName (s); }

namespace keysdetail
{
inline float midiOf (float hz) { return 69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / 440.0f); }
inline float hzOf (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline bool pitched (const PoolItem& it) { return it.f.valid && it.f.pitchStrength >= 0.35f && it.f.pitchHz >= 25.0f && it.f.pitchHz <= 4200.0f; }
inline float monoAt (const PoolItem& s, int i) { return s.r.size() == s.l.size() ? 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]) : s.l[(size_t) i]; }

// where the body of a sound is: after its attack has peaked, before it has died to -30 dB
inline void body (const PoolItem& s, int& from, int& to)
{
    const int n = (int) s.l.size(), w = std::max (16, (int) (0.005 * s.sr));
    std::vector<float> env ((size_t) (n / w + 1), 0.0f);
    for (int i = 0; i < n; ++i) env[(size_t) (i / w)] = std::max (env[(size_t) (i / w)], std::fabs (monoAt (s, i)));
    const int pk = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const float top = std::max (1.0e-6f, env[(size_t) pk]);
    int end = pk; while (end + 1 < (int) env.size() && env[(size_t) end + 1] > top * 0.0316f) ++end;
    from = std::min (n - 1, (pk + 1) * w + (int) (0.01 * s.sr));
    to = std::max (from, end * w);
}

// A loop in the sound's body: a whole number of periods long (pitched) or about 120 ms (not), where the
// sound just before the end is most like the sound just before the start, so the loop's crossfade is smooth.
inline bool findLoop (const PoolItem& s, float rootNote, int& start, int& end)
{
    start = end = -1;
    int a, b; body (s, a, b);
    const int n = (int) s.l.size();
    if (b - a < (int) (0.06 * s.sr)) { a = std::max (0, n / 4); b = n - (int) (0.01 * s.sr); }
    a = std::max (a, (int) (0.02 * s.sr));       // room before the start for the crossfade
    const double period = pitched (s) ? s.sr / (double) hzOf (rootNote) : 0.0;
    int len = (int) (0.12 * s.sr);
    if (period > 8.0) len = std::max (2, (int) std::lround (std::max (1.0, std::round (0.12 * s.sr / period)) * period));
    if (b - a < len + (int) (0.02 * s.sr)) { len = std::max ((int) (0.02 * s.sr), (b - a) / 2); if (period > 8.0) len = std::max (2, (int) std::lround (std::max (1.0, std::floor (len / period)) * period)); }
    const int w = std::max (32, std::min (len / 2, (int) (0.01 * s.sr)));
    double best = -2.0; int bestS = -1;
    const int step = std::max (1, (int) ((b - a) / 200));
    for (int st = a + w; st + len + w < std::min (b, n - 1); st += step)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int k = -w; k < 0; ++k) { const float x = monoAt (s, st + k), y = monoAt (s, st + len + k); xy += x * y; xx += x * x; yy += y * y; }
        const double c = xy / std::sqrt (std::max (1.0e-18, xx * yy));
        // early in the body is better: the loop holds more of the sound's life
        const double score = c - 0.15 * (double) (st - a) / std::max (1, b - a);
        if (score > best) { best = score; bestS = st; }
    }
    if (bestS < 0) return false;
    start = bestS; end = bestS + len;
    return end < n - 1;
}

inline float loudnessDb (const PoolItem& s) { return s.lufs > -100.0f ? s.lufs : s.f.rmsDb; }

// A pitch to a fraction of a cent: the period near `approxHz` where the sound best matches itself one period
// later, between whole samples by a parabola through the best three. (An instrument is only as in tune as the
// pitches its zones are tuned from.)
inline double preciseHz (const float* x, int n, double sr, double approxHz)
{
    if (approxHz <= 0.0 || n < 64) return approxHz;
    const double lag0 = sr / approxHz;
    const int lo = std::max (2, (int) std::floor (lag0 * 0.97) - 1), hi = (int) std::ceil (lag0 * 1.03) + 1;
    const int w = n - hi - 1;
    if (w < 3 * (int) lag0) return approxHz;
    std::vector<double> r ((size_t) (hi - lo + 1), -1.0);
    for (int lag = lo; lag <= hi; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < w; ++i) { xy += (double) x[i] * x[i + lag]; xx += (double) x[i] * x[i]; yy += (double) x[i + lag] * x[i + lag]; }
        r[(size_t) (lag - lo)] = xy / std::sqrt (std::max (1.0e-30, xx * yy));
    }
    int k = (int) (std::max_element (r.begin(), r.end()) - r.begin());
    if (k <= 0 || k >= (int) r.size() - 1) return approxHz;
    const double a = r[(size_t) k - 1], b = r[(size_t) k], c = r[(size_t) k + 1], den = a - 2.0 * b + c;
    const double d = std::fabs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
    return sr / ((double) (lo + k) + std::max (-0.5, std::min (0.5, d)));
}
inline double preciseHz (const PoolItem& s, double approxHz)
{
    int a, b; body (s, a, b);
    if (b - a < (int) (0.08 * s.sr)) { a = std::min ((int) s.l.size() - 1, (int) (0.01 * s.sr)); b = (int) s.l.size(); }
    b = std::min (b, a + (int) (0.3 * s.sr));
    std::vector<float> m ((size_t) std::max (0, b - a));
    for (int i = a; i < b; ++i) m[(size_t) (i - a)] = monoAt (s, i);
    return preciseHz (m.data(), (int) m.size(), s.sr, approxHz);
}
} // namespace keysdetail

// How loud a sound rings in the resonator (on A2, ringing a second, half bright): RMS and peak in dB over
// 0.6 s. Exciters are levelled by it, so a click and a long drill ring about alike.
inline void resonatorLevel (const PoolItem& s, float& rmsDb, float& peakDb)
{
    auto r = std::make_unique<Resonator>();
    const double sr = s.sr > 0.0 ? s.sr : 48000.0;
    r->tune (sr, 57.0, resonatorCorner (0.5f), 1.0);
    const int n = (int) (0.6 * sr), len = (int) s.l.size();
    double e = 0.0; float pk = 0.0f;
    for (int i = 0; i < n; ++i) { const float y = r->process (i < len ? keysdetail::monoAt (s, i) : 0.0f); e += (double) y * y; pk = std::max (pk, std::fabs (y)); }
    rmsDb = (float) (10.0 * std::log10 (e / std::max (1, n) + 1.0e-20)); peakDb = 20.0f * std::log10 (pk + 1.0e-10f);
}

// Builds the zones from a set of sounds. Tuned: the pitched ones, by pitch; their loudness makes velocity
// layers and alike repeats round robins. Chops: one sound a key from C2 up. Resonator: every sound is an
// exciter, struck at its own speed (the resonator gives the pitch), alike ones as round robins.
inline void buildInstrument (Instrument& ins, const std::vector<std::shared_ptr<const PoolItem>>& sounds)
{
    using namespace keysdetail;
    ins.zones.clear();
    std::vector<std::shared_ptr<const PoolItem>> use;
    for (const auto& s : sounds) if (s && s->f.valid && s->l.size() > 32 && (ins.mode != KeysMode::Tuned || pitched (*s))) use.push_back (s);
    if (use.empty()) return;
    // every sound as loud as the others (the loudest velocity layer is where they meet)
    auto gainOf = [] (const PoolItem& s) { return nbk::matchGainDb ({ s.lufs, s.peakDb }); };
    if (ins.mode == KeysMode::Chops)
    {
        for (int i = 0; i < (int) use.size() && i < 80; ++i)
        {
            KeyZone z; z.sound = use[(size_t) i]; z.lowKey = z.highKey = 48 + i; z.root = (float) (48 + i); z.gainDb = gainOf (*z.sound);
            z.tunedSound = pitched (*z.sound);
            findLoop (*z.sound, z.root, z.loopStart, z.loopEnd);
            int a, b; body (*z.sound, a, b); z.holdAt = a;
            ins.zones.push_back (z);
        }
        return;
    }
    if (ins.mode == KeysMode::Resonator)
    {
        // each exciter on every key, as round robins
        for (int i = 0; i < (int) use.size() && i < 16; ++i)
        {
            KeyZone z; z.sound = use[(size_t) i]; z.lowKey = 0; z.highKey = 127; z.root = 60.0f; z.rr = i; z.rrCount = std::min<int> (16, (int) use.size());
            z.tunedSound = pitched (*z.sound);
            // levelled by how it rings (about -18 dB, its peak under -6 dB), not by how loud it is itself
            float rmsDb = -200.0f, peakDb = -200.0f; resonatorLevel (*z.sound, rmsDb, peakDb);
            z.gainDb = rmsDb > -150.0f ? std::max (-24.0f, std::min (36.0f, std::min (-18.0f - rmsDb, -6.0f - peakDb))) : 0.0f;
            int a, b; body (*z.sound, a, b); z.holdAt = a;
            ins.zones.push_back (z);
        }
        return;
    }
    // Tuned: each sound's pitch to a fraction of a cent, then clusters of sounds within half a semitone
    std::map<const PoolItem*, float> note;
    for (const auto& s : use) note[s.get()] = midiOf ((float) preciseHz (*s, s->f.pitchHz));
    std::stable_sort (use.begin(), use.end(), [&] (const auto& x, const auto& y) { return note[x.get()] < note[y.get()]; });
    std::vector<std::vector<std::shared_ptr<const PoolItem>>> clusters;
    std::vector<float> roots;
    for (const auto& s : use)
    {
        const float nt = note[s.get()];
        if (! clusters.empty() && nt - roots.back() < 0.5f) { clusters.back().push_back (s); continue; }
        clusters.push_back ({ s }); roots.push_back (nt);
    }
    for (size_t c = 0; c < clusters.size(); ++c)
    {
        // the keys nearest this pitch: half way to the next pitch on each side (the ends reach the keyboard's ends)
        const float r = roots[c];
        const int lo = c == 0 ? 0 : (int) std::floor ((roots[c - 1] + r) * 0.5f) + 1;
        const int hi = c + 1 == clusters.size() ? 127 : (int) std::floor ((r + roots[c + 1]) * 0.5f);
        auto& cl = clusters[c];
        // loudness makes velocity layers: sounds within 4 dB of each other are one layer (alike repeats, played
        // in turn as round robins); a sound 4 dB louder starts the next layer (four at most)
        std::stable_sort (cl.begin(), cl.end(), [] (const auto& x, const auto& y) { return loudnessDb (*x) < loudnessDb (*y); });
        std::vector<size_t> starts { 0 };
        for (size_t k = 1; k < cl.size(); ++k) if (loudnessDb (*cl[k]) - loudnessDb (*cl[starts.back()]) >= 4.0f) starts.push_back (k);
        while (starts.size() > 4)
        {
            // merge the two layers closest in loudness
            size_t best = 1; float d = 1e9f;
            for (size_t k = 1; k < starts.size(); ++k) { const float dk = loudnessDb (*cl[starts[k]]) - loudnessDb (*cl[starts[k - 1]]); if (dk < d) { d = dk; best = k; } }
            starts.erase (starts.begin() + (std::ptrdiff_t) best);
        }
        const int layers = (int) starts.size();
        for (int v = 0; v < layers; ++v)
        {
            const size_t a = starts[(size_t) v], b = v + 1 < layers ? starts[(size_t) v + 1] : cl.size();
            const int vlo = v == 0 ? 1 : 1 + 127 * v / layers, vhi = v + 1 == layers ? 127 : 127 * (v + 1) / layers;
            for (size_t k = a; k < b; ++k)
            {
                KeyZone z; z.sound = cl[k]; z.root = note[cl[k].get()]; z.tunedSound = true;   // the Tuned build takes only pitched sounds
                z.lowKey = std::max (0, lo); z.highKey = std::min (127, std::max (lo, hi)); z.lowVel = vlo; z.highVel = vhi;
                z.rr = (int) (k - a); z.rrCount = (int) (b - a);
                // softer layers keep their own (lower) level against the loudest: velocity still means something
                z.gainDb = gainOf (*cl[k]) - (layers > 1 ? (loudnessDb (*cl.back()) - loudnessDb (*cl[k])) * 0.5f : 0.0f);
                findLoop (*z.sound, z.root, z.loopStart, z.loopEnd);
                int s0, s1; body (*z.sound, s0, s1); z.holdAt = s0;
                ins.zones.push_back (z);
            }
        }
    }
}

// ------------------------------------------------------------------ the player (audio thread)
} // namespace bc
