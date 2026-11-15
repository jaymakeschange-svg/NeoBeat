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
#include "../neobeat/gen/Rng.h"
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
enum class KeysMode : uint8_t { Tuned = 0, Resonator = 1, Chops = 2 };
// the resonator's knobs: RING is how long a note rings (50 ms to 6 s), BRIGHT where its damping filter's
// corner sits (6 to 60 times the note's pitch)
inline double resonatorRing (float ring01) { return 0.05 * std::pow (120.0, (double) std::max (0.0f, std::min (1.0f, ring01))); }
inline double resonatorCorner (float bright01) { return 6.0 * std::pow (10.0, (double) std::max (0.0f, std::min (1.0f, bright01))); }
enum class KeysSustain : uint8_t { AsRecorded = 0, Loop = 1, Hold = 2 };
inline const char* keysModeName (KeysMode m) { return m == KeysMode::Tuned ? "Tuned" : m == KeysMode::Resonator ? "Resonator" : "Chops"; }
inline const char* keysSustainName (KeysSustain s) { return s == KeysSustain::AsRecorded ? "As recorded" : s == KeysSustain::Loop ? "Loop" : "Hold"; }

struct KeyZone
{
    std::shared_ptr<const PoolItem> sound;
    float root = 60.0f;                       // the note (fractional) the sound is at its own speed: its pitch
    int lowKey = 0, highKey = 127, lowVel = 1, highVel = 127;
    int rr = 0, rrCount = 1;                  // round robin: which of how many alike sounds in its cell
    int loopStart = -1, loopEnd = -1;         // in samples (-1: no loop found)
    int holdAt = 0;                           // where Hold freezes the sound (samples)
    float gainDb = 0.0f;                      // as loud as the others
    bool operator== (const KeyZone& o) const noexcept
    {
        return sound == o.sound && root == o.root && lowKey == o.lowKey && highKey == o.highKey && lowVel == o.lowVel && highVel == o.highVel
            && rr == o.rr && rrCount == o.rrCount && loopStart == o.loopStart && loopEnd == o.loopEnd && holdAt == o.holdAt && gainDb == o.gainDb;
    }
};

struct Instrument
{
    std::string name = "My keys";
    KeysMode mode = KeysMode::Tuned;
    KeysSustain sustain = KeysSustain::AsRecorded;
    float attackMs = 2.0f, releaseMs = 300.0f, glideMs = 0.0f;
    bool mono = false;
    float resonance = 0.7f, brightness = 0.5f;   // Resonator: how long it rings, how bright
    std::vector<KeyZone> zones;
    bool operator== (const Instrument& o) const noexcept
    {
        return name == o.name && mode == o.mode && sustain == o.sustain && attackMs == o.attackMs && releaseMs == o.releaseMs && glideMs == o.glideMs
            && mono == o.mono && resonance == o.resonance && brightness == o.brightness && zones == o.zones;
    }
};

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

// ------------------------------------------------------------------ the resonator
// A tuned delay line with a damping filter in it, struck by a sound. The line is one period long, less what
// the damping filter and the in-between read lag at that pitch (else it goes flat: half a semitone up high).
// The filter's corner is a multiple of the note's pitch (dark is dark, yet a high note still rings). The
// feedback makes the fundamental die away in `ring` seconds, allowing for what the filter and the read take
// from it each time round, so a high note rings as long as a low one (dark and high, not quite). The
// strike's level follows (a long ring is not louder than a short one); a high-pass keeps out what lies well
// below the note (a kick's boom would only blur it); and what is heard is only what rings.
struct Resonator
{
    static constexpr int kLen = 4096;                    // a power of two: the longest period at 48 kHz is 12 Hz
    std::array<float, kLen> line {};
    int wpos = 0;
    double delay = 100.0, tunedFor = -1000.0, tunedCorner = -1.0, tunedRing = -1.0;
    float lp = 0.0f, lpB = 0.5f, fb = 0.9f, inGain = 0.35f, hpA = 0.99f, hx1 = 0.0f, hy1 = 0.0f, hx2 = 0.0f, hy2 = 0.0f;

    void reset() noexcept { line.fill (0.0f); wpos = 0; lp = hx1 = hy1 = hx2 = hy2 = 0.0f; tunedFor = -1000.0; }

    // cheap to call every sample: it only works when the note (a glide), the corner or the ring moved
    void tune (double sr, double note, double corner, double ring) noexcept
    {
        if (std::fabs (note - tunedFor) <= 5.0e-4 && corner == tunedCorner && ring == tunedRing) return;
        tunedFor = note; tunedCorner = corner; tunedRing = ring;
        const double hz = std::max (8.0, 440.0 * std::exp2 ((note - 69.0) / 12.0)), w = 6.283185307179586 * hz / sr;
        const double fc = std::min (0.45 * sr, hz * corner);
        const double b = 1.0 - std::exp (-6.283185307179586 * fc / sr), a = 1.0 - b;
        const double lpLag = std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w;
        const double lpMag = b / std::sqrt (1.0 - 2.0 * a * std::cos (w) + a * a);
        const double want = std::max (2.0, std::min (kLen - 8.0, sr / hz - lpLag));
        double x = want;
        for (int it = 0; it < 4; ++it)
        {
            const double di = std::floor (x), d = x - di;
            x -= di + std::atan2 (d * std::sin (w), (1.0 - d) + d * std::cos (w)) / w - want;
        }
        x = std::max (2.0, std::min (kLen - 8.0, x));
        const double d = x - std::floor (x);
        const double readMag = std::sqrt ((1.0 - d) * (1.0 - d) + d * d + 2.0 * d * (1.0 - d) * std::cos (w));
        const double g = std::pow (10.0, -3.0 / (hz * std::max (0.01, ring)));
        const double feedback = std::min (0.9995, g / std::max (1.0e-6, lpMag * readMag));
        const double each = feedback * lpMag * readMag;   // what is left of the fundamental each time round
        delay = x; lpB = (float) b; fb = (float) feedback;
        inGain = (float) (1.5 * std::sqrt (std::max (1.0e-6, 1.0 - each * each)));
        hpA = (float) (1.0 / (1.0 + 6.283185307179586 * 0.6 * hz / sr));
    }

    float process (float x) noexcept
    {
        const float h1 = hpA * (hy1 + x - hx1); hx1 = x; hy1 = h1;
        const float h2 = hpA * (hy2 + h1 - hx2); hx2 = h1; hy2 = h2;
        const int di = (int) delay; const float df = (float) (delay - di);
        const float y0 = line[(size_t) ((wpos - di) & (kLen - 1))], y1 = line[(size_t) ((wpos - di - 1) & (kLen - 1))];
        lp += lpB * (y0 + df * (y1 - y0) - lp);
        const float ringing = fb * lp;
        line[(size_t) wpos] = inGain * h2 + ringing; wpos = (wpos + 1) & (kLen - 1);
        return ringing;
    }
};

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
                KeyZone z; z.sound = cl[k]; z.root = note[cl[k].get()];
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
struct KeysNote { int offset = 0, note = 60; float velocity = 0.8f; bool on = true; };

class KeysPlayer
{
public:
    static constexpr int kVoices = 24;
    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& v : voices) v = Voice();
        held.fill (false); lastNote = -1; glideFrom = -1.0f;
    }
    // message thread: the instrument changed (the caller keeps the old one alive a while)
    void setInstrument (std::shared_ptr<const Instrument> i) { std::atomic_store (&next, std::move (i)); }
    std::atomic<float> level { 0.8f };
    std::atomic<int> active { 0 };                          // voices sounding (for the display)
    std::atomic<bool> letGo { false };                      // message thread: release every key (leaving the room)

    void process (float* l, float* r, int n, const KeysNote* notes, int numNotes) noexcept
    {
        if (auto i = std::atomic_exchange (&next, std::shared_ptr<const Instrument>())) { ins = std::move (i); for (auto& v : voices) v.z = nullptr; }
        if (letGo.exchange (false)) { for (auto& v : voices) if (v.z != nullptr && v.stage < 2) v.stage = 2; held.fill (false); }
        int at = 0, ni = 0;
        while (at < n)
        {
            const int end = ni < numNotes ? std::max (at, std::min (n, notes[ni].offset)) : n;
            if (end > at) render (l + at, r + at, end - at);
            if (end >= n) break;
            if (ins != nullptr) note (notes[ni]);
            at = end; ++ni;
        }
        int a = 0; for (const auto& v : voices) a += v.z != nullptr ? 1 : 0;
        active.store (a);
    }

private:
    struct Grain { double pos = 0.0; int age = 0; bool on = false; };
    struct Voice
    {
        const KeyZone* z = nullptr;
        int note = 60; float vel = 1.0f, gain = 1.0f;
        double pos = 0.0, inc = 1.0, targetNote = 60.0, curNote = 60.0;
        float env = 0.0f; int stage = 0;                  // 0 attack, 1 held, 2 released
        int sinceOn = 0, holdClock = -1;                  // holdClock: samples since Hold began (-1: not yet)
        Resonator res;                                    // Resonator mode
        std::array<Grain, 4> grains {}; int nextGrain = 0; uint32_t rng = 1u;
        uint64_t order = 0;
    };

    const KeyZone* pick (int key, int vel)
    {
        const KeyZone* first = nullptr; int count = 0;
        for (const auto& z : ins->zones) if (key >= z.lowKey && key <= z.highKey && vel >= z.lowVel && vel <= z.highVel) { if (first == nullptr) first = &z; ++count; }
        if (first == nullptr)
        {
            // no zone for this velocity: the nearest one for this key
            for (const auto& z : ins->zones) if (key >= z.lowKey && key <= z.highKey) { first = &z; break; }
            if (first == nullptr) return nullptr;
            count = 1;
        }
        // round robin: the next alike sound each time this cell plays
        const int cell = std::max (0, std::min (127, key));
        const int want = rrNext[(size_t) cell]++ % std::max (1, first->rrCount);
        for (const auto& z : ins->zones)
            if (key >= z.lowKey && key <= z.highKey && vel >= z.lowVel && vel <= z.highVel && z.rr == want) return &z;
        return first;
    }

    void note (const KeysNote& nt) noexcept
    {
        const int key = std::max (0, std::min (127, nt.note));
        if (! nt.on)
        {
            held[(size_t) key] = false;
            for (auto& v : voices) if (v.z != nullptr && v.note == key && v.stage < 2) v.stage = 2;
            if (ins->mono)
            {
                // back to a key still held, if any
                for (int k = 127; k >= 0; --k) if (held[(size_t) k]) { for (auto& v : voices) if (v.z != nullptr && v.stage == 2 && v.note == key) { v.stage = 1; v.note = k; v.targetNote = k; } break; }
            }
            return;
        }
        held[(size_t) key] = true;
        const int vel = std::max (1, std::min (127, (int) std::lround (nt.velocity * 127.0f)));
        const KeyZone* z = pick (key, vel);
        if (z == nullptr || z->sound == nullptr || z->sound->l.size() < 4) return;
        if (ins->mono)
        {
            // one voice: a new key glides (or jumps) the sounding one
            for (auto& v : voices)
                if (v.z != nullptr && v.stage < 2)
                {
                    v.note = key; v.targetNote = key;
                    if (ins->glideMs <= 0.5f) v.curNote = key;
                    return;
                }
        }
        Voice* use = nullptr;
        for (auto& v : voices) if (v.z == nullptr) { use = &v; break; }
        if (use == nullptr) { use = &voices[0]; for (auto& v : voices) if (v.order < use->order) use = &v; }
        // (each field reset in place: a voice holds a delay line too big to build on the audio thread's stack)
        use->pos = 0.0; use->env = 0.0f; use->stage = 0; use->sinceOn = 0; use->holdClock = -1; use->nextGrain = 0;
        for (auto& g : use->grains) g = Grain();
        use->z = z; use->note = key; use->vel = (float) vel / 127.0f; use->order = ++counter;
        use->gain = std::pow (10.0f, z->gainDb / 20.0f) * (0.25f + 0.75f * use->vel);
        use->targetNote = key; use->curNote = (ins->glideMs > 0.5f && lastNote >= 0) ? (double) lastNote : (double) key;
        use->rng = 0x9e3779b9u ^ (uint32_t) (counter * 2654435761u);
        if (ins->mode == KeysMode::Resonator) use->res.reset();   // tuned on its first sample
        lastNote = key;
    }

    float sampleAt (const PoolItem& s, double p) const noexcept
    {
        const int len = (int) s.l.size();
        const int i0 = std::max (0, std::min (len - 2, (int) p)); const float f = (float) (p - i0);
        const float a = keysdetail::monoAt (s, i0), b = keysdetail::monoAt (s, i0 + 1);
        return a + f * (b - a);
    }

    void render (float* l, float* r, int n) noexcept
    {
        if (ins == nullptr) return;
        const float lv = level.load();
        const float att = 1.0f / std::max (1.0f, ins->attackMs * 0.001f * (float) sr);
        const float rel = std::exp (-6.9f / std::max (1.0f, ins->releaseMs * 0.001f * (float) sr));   // -60 dB over the release
        const float glide = ins->glideMs > 0.5f ? 1.0f - std::exp (-1.0f / (ins->glideMs * 0.001f * (float) sr)) : 1.0f;
        const int xfade = std::max (8, (int) (0.012 * sr)), grainLen = std::max (64, (int) (0.08 * sr));
        const double corner = resonatorCorner (ins->brightness), ring = resonatorRing (ins->resonance);
        for (auto& v : voices)
        {
            if (v.z == nullptr) continue;
            const auto& s = *v.z->sound; const int len = (int) s.l.size();
            const double rate = s.sr / sr;
            for (int i = 0; i < n; ++i)
            {
                v.curNote += (v.targetNote - v.curNote) * glide;
                if (std::fabs (v.targetNote - v.curNote) < 1.0e-5) v.curNote = v.targetNote;
                double inc = rate;
                if (ins->mode == KeysMode::Tuned) inc = rate * std::exp2 ((v.curNote - (double) v.z->root) / 12.0);
                // the envelope
                if (v.stage == 0) { v.env += att; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
                else if (v.stage == 2) { v.env *= rel; if (v.env < 1.0e-4f) { v.z = nullptr; break; } }
                float x = 0.0f;
                const bool sustaining = v.stage < 2;
                if (ins->sustain == KeysSustain::Hold && sustaining && v.pos >= (double) v.z->holdAt && ins->mode != KeysMode::Resonator)
                {
                    // the body frozen: grains of 80 ms from spots around the hold point, overlapping by three quarters
                    // (their windows add up to one, so the hold is as loud as the sound was); it starts with four
                    // grains already under way, so the hand-over from the sound itself is not heard
                    const int quarter = std::max (1, grainLen / 4);
                    // a tuned sound's grains are kept in phase with each other (each starts a whole number of periods
                    // from the one before, near the hold point), so they add up cleanly and the hold stays in tune;
                    // any other sound's grains come from anywhere within 10 ms of the hold point
                    const double period = keysdetail::pitched (s) ? s.sr / (double) keysdetail::hzOf (v.z->root) : 0.0;
                    const double lo = 0.0, hi = (double) len - 2.0 - grainLen * inc;
                    auto spawn = [&] (int age, double ref)
                    {
                        auto& g = v.grains[(size_t) (v.nextGrain++ % (int) v.grains.size())];
                        v.rng = v.rng * 1664525u + 1013904223u;
                        const double u = (double) (v.rng >> 8) / 16777216.0 - 0.5;
                        double p;
                        if (period > 2.0)
                        {
                            const double back = std::round (((double) v.z->holdAt + age * inc - ref) / period) + std::round (u * 4.0);
                            p = ref + back * period;
                            while (p > hi + age * inc && p - period >= lo) p -= period;
                            while (p < lo + age * inc && p + period <= hi + age * inc) p += period;
                        }
                        else p = std::max (lo, std::min (hi, (double) v.z->holdAt + u * 0.02 * s.sr)) + age * inc;
                        g.on = true; g.age = age; g.pos = p;
                    };
                    if (v.holdClock < 0) { for (int k = 3; k >= 0; --k) spawn (k * quarter, v.pos); v.holdClock = 0; }
                    else if (v.holdClock % quarter == 0)
                    {
                        // in phase with the youngest grain still sounding
                        double ref = v.pos; int youngest = 1 << 30;
                        for (const auto& g : v.grains) if (g.on && g.age < youngest) { youngest = g.age; ref = g.pos; }
                        spawn (0, ref);
                    }
                    ++v.holdClock;
                    for (auto& g : v.grains)
                    {
                        if (! g.on) continue;
                        const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) g.age / (float) grainLen);
                        x += 0.5f * w * sampleAt (s, g.pos);
                        g.pos += inc; if (++g.age >= grainLen || g.pos >= len - 2) g.on = false;
                    }
                }
                else
                {
                    // a loop: past its end the read goes back to its start; over the last 12 ms before the end the
                    // start's run-up is faded in, so at the seam it is already all start and nothing is heard
                    const bool looping = ins->sustain == KeysSustain::Loop && sustaining && v.z->loopEnd > v.z->loopStart + xfade && ins->mode != KeysMode::Resonator;
                    if (looping && v.pos >= (double) v.z->loopEnd) v.pos -= (double) (v.z->loopEnd - v.z->loopStart);
                    if (v.pos >= len - 2)
                    {
                        if (ins->mode != KeysMode::Resonator) { v.z = nullptr; break; }
                    }
                    else x = sampleAt (s, v.pos);
                    if (looping)
                    {
                        const double toEnd = (double) v.z->loopEnd - v.pos;
                        if (toEnd < xfade && toEnd > 0.0)
                        {
                            const float t = (float) (1.0 - toEnd / xfade);
                            x = (1.0f - t) * x + t * sampleAt (s, v.pos - (v.z->loopEnd - v.z->loopStart));
                        }
                    }
                    v.pos += inc;
                    if (ins->mode == KeysMode::Resonator)
                    {
                        // struck into the tuned delay line; it follows a glide, and the knobs as they move
                        v.res.tune (sr, v.curNote, corner, ring);
                        x = v.res.process (x);
                    }
                }
                const float out = lv * v.gain * v.env * x;
                l[i] += out; r[i] += out;
                ++v.sinceOn;
            }
        }
    }

    double sr = 48000.0;
    std::shared_ptr<const Instrument> next, ins;
    std::array<Voice, kVoices> voices {};
    std::array<bool, 128> held {};
    std::array<uint32_t, 128> rrNext {};
    uint64_t counter = 0;
    int lastNote = -1;
    float glideFrom = -1.0f;
};
} // namespace bc
