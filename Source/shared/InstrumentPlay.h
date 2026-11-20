#pragma once
// Playing a .NJQ instrument: the voice Batchcut's KEYS room built and MidiJaQ plays.
//
// This is the playing half only — making an instrument out of a pool of sounds lives in Batchcut. The
// sound type is left open (anything with l, r and sr), so Batchcut plays straight out of its pool while
// MidiJaQ plays what it read from a file, and neither copies the audio.
//
// Kept identical in every source tree. No JUCE here.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace njq
{
enum class PlayMode : uint8_t { Tuned = 0, Resonator = 1, Chops = 2 };
enum class Sustain : uint8_t { AsRecorded = 0, Loop = 1, Hold = 2 };

inline const char* playModeName (PlayMode m) { return m == PlayMode::Tuned ? "Tuned" : m == PlayMode::Resonator ? "Resonator" : "Chops"; }
inline const char* sustainName (Sustain s) { return s == Sustain::AsRecorded ? "As recorded" : s == Sustain::Loop ? "Loop" : "Hold"; }

// the resonator's knobs: RING is how long a note rings (50 ms to 6 s), BRIGHT where its damping filter's
// corner sits (6 to 60 times the note's pitch)
inline double resonatorRing (float ring01) { return 0.05 * std::pow (120.0, (double) std::max (0.0f, std::min (1.0f, ring01))); }
inline double resonatorCorner (float bright01) { return 6.0 * std::pow (10.0, (double) std::max (0.0f, std::min (1.0f, bright01))); }

// what the player needs of a sound: one or two channels and the rate they were recorded at
template <class S>
inline float monoAt (const S& s, int i) { return s.r.size() == s.l.size() ? 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]) : s.l[(size_t) i]; }

inline float noteHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

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

// ------------------------------------------------------------------ the instrument
template <class S>
struct ZoneT
{
    std::shared_ptr<const S> sound;
    float root = 60.0f;                       // the note (fractional) the sound is at its own speed: its pitch
    int lowKey = 0, highKey = 127, lowVel = 1, highVel = 127;
    int rr = 0, rrCount = 1;                  // round robin: which of how many alike sounds in its cell
    int loopStart = -1, loopEnd = -1;         // in samples (-1: no loop found)
    int holdAt = 0;                           // where Hold freezes the sound (samples)
    float gainDb = 0.0f;                      // as loud as the others
    bool tunedSound = false;                  // the sound has a pitch of its own, so Hold can stay in tune
    bool operator== (const ZoneT& o) const noexcept
    {
        return sound == o.sound && root == o.root && lowKey == o.lowKey && highKey == o.highKey && lowVel == o.lowVel && highVel == o.highVel
            && rr == o.rr && rrCount == o.rrCount && loopStart == o.loopStart && loopEnd == o.loopEnd && holdAt == o.holdAt
            && gainDb == o.gainDb && tunedSound == o.tunedSound;
    }
};

template <class S>
struct InstrumentT
{
    std::string name = "My keys";
    PlayMode mode = PlayMode::Tuned;
    Sustain sustain = Sustain::AsRecorded;
    float attackMs = 2.0f, releaseMs = 300.0f, glideMs = 0.0f;
    bool mono = false;
    float resonance = 0.7f, brightness = 0.5f;   // Resonator: how long it rings, how bright
    std::vector<ZoneT<S>> zones;
    bool operator== (const InstrumentT& o) const noexcept
    {
        return name == o.name && mode == o.mode && sustain == o.sustain && attackMs == o.attackMs && releaseMs == o.releaseMs && glideMs == o.glideMs
            && mono == o.mono && resonance == o.resonance && brightness == o.brightness && zones == o.zones;
    }
};

// `fine` shifts the pitch without moving the key: the zone picked and the key held are still
// `note`, so a layer detuned a few cents stays the same sound, played a hair sharp or flat.
struct PlayNote { int offset = 0, note = 60; float velocity = 0.8f; bool on = true; float fine = 0.0f; };

// ------------------------------------------------------------------ the voices
template <class S>
class PlayerT
{
public:
    using Zone = ZoneT<S>;
    using Instrument = InstrumentT<S>;
    static constexpr int kVoices = 24;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& v : voices) v = Voice();
        held.fill (false); lastNote = -1;
    }
    // message thread: the instrument changed (the caller keeps the old one alive a while). A null one puts
    // the instrument away: what is sounding is released first, and dropped once it has died away.
    void setInstrument (std::shared_ptr<const Instrument> i)
    {
        if (i == nullptr) { closing.store (true); return; }
        closing.store (false);
        std::atomic_store (&next, std::move (i));
    }
    bool hasInstrument() const noexcept { return ins != nullptr; }

    std::atomic<float> level { 0.8f };
    std::atomic<int> active { 0 };                          // voices sounding (for the display)
    std::atomic<bool> letGo { false };                      // message thread: release every key
    std::atomic<bool> closing { false };                    // message thread: put the instrument away

    void process (float* l, float* r, int n, const PlayNote* notes, int numNotes) noexcept
    {
        if (auto i = std::atomic_exchange (&next, std::shared_ptr<const Instrument>())) { ins = std::move (i); for (auto& v : voices) v.z = nullptr; dropping = false; }
        if (closing.exchange (false)) { for (auto& v : voices) if (v.z != nullptr && v.stage < 2) v.stage = 2; held.fill (false); dropping = true; }
        if (letGo.exchange (false)) { for (auto& v : voices) if (v.z != nullptr && v.stage < 2) v.stage = 2; held.fill (false); }
        int at = 0, ni = 0;
        while (at < n)
        {
            const int end = ni < numNotes ? std::max (at, std::min (n, notes[ni].offset)) : n;
            if (end > at) render (l + at, r + at, end - at);
            if (end >= n) break;
            if (ins != nullptr && ! dropping) note (notes[ni]);   // on its way out: no new notes, only the tail
            at = end; ++ni;
        }
        int a = 0; for (const auto& v : voices) a += v.z != nullptr ? 1 : 0;
        active.store (a);
        if (dropping && a == 0) { ins.reset(); dropping = false; }
    }

private:
    struct Grain { double pos = 0.0; int age = 0; bool on = false; };
    struct Voice
    {
        const Zone* z = nullptr;
        int note = 60; float vel = 1.0f, gain = 1.0f;
        double pos = 0.0, inc = 1.0, targetNote = 60.0, curNote = 60.0;
        float env = 0.0f; int stage = 0;                  // 0 attack, 1 held, 2 released
        int sinceOn = 0, holdClock = -1;                  // holdClock: samples since Hold began (-1: not yet)
        Resonator res;                                    // Resonator mode
        std::array<Grain, 4> grains {}; int nextGrain = 0; uint32_t rng = 1u;
        uint64_t order = 0;
    };

    const Zone* pick (int key, int vel)
    {
        const Zone* first = nullptr; int count = 0;
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

    void note (const PlayNote& nt) noexcept
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
        const Zone* z = pick (key, vel);
        if (z == nullptr || z->sound == nullptr || z->sound->l.size() < 4) return;
        if (ins->mono)
        {
            // one voice: a new key glides (or jumps) the sounding one
            for (auto& v : voices)
                if (v.z != nullptr && v.stage < 2)
                {
                    v.note = key; v.targetNote = (double) key + (double) nt.fine;
                    if (ins->glideMs <= 0.5f) v.curNote = v.targetNote;
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
        use->targetNote = (double) key + (double) nt.fine;
        use->curNote = (ins->glideMs > 0.5f && lastNote >= 0) ? (double) lastNote + (double) nt.fine : use->targetNote;
        use->rng = 0x9e3779b9u ^ (uint32_t) (counter * 2654435761u);
        if (ins->mode == PlayMode::Resonator) use->res.reset();   // tuned on its first sample
        lastNote = key;
    }

    float sampleAt (const S& s, double p) const noexcept
    {
        const int len = (int) s.l.size();
        const int i0 = std::max (0, std::min (len - 2, (int) p)); const float f = (float) (p - i0);
        const float a = monoAt (s, i0), b = monoAt (s, i0 + 1);
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
                if (ins->mode == PlayMode::Tuned) inc = rate * std::exp2 ((v.curNote - (double) v.z->root) / 12.0);
                // the envelope
                if (v.stage == 0) { v.env += att; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
                else if (v.stage == 2) { v.env *= rel; if (v.env < 1.0e-4f) { v.z = nullptr; break; } }
                float x = 0.0f;
                const bool sustaining = v.stage < 2;
                if (ins->sustain == Sustain::Hold && sustaining && v.pos >= (double) v.z->holdAt && ins->mode != PlayMode::Resonator)
                {
                    // the body frozen: grains of 80 ms from spots around the hold point, overlapping by three quarters
                    // (their windows add up to one, so the hold is as loud as the sound was); it starts with four
                    // grains already under way, so the hand-over from the sound itself is not heard
                    const int quarter = std::max (1, grainLen / 4);
                    // a tuned sound's grains are kept in phase with each other (each starts a whole number of periods
                    // from the one before, near the hold point), so they add up cleanly and the hold stays in tune;
                    // any other sound's grains come from anywhere within 10 ms of the hold point
                    const double period = v.z->tunedSound ? s.sr / (double) noteHz (v.z->root) : 0.0;
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
                    const bool looping = ins->sustain == Sustain::Loop && sustaining && v.z->loopEnd > v.z->loopStart + xfade && ins->mode != PlayMode::Resonator;
                    if (looping && v.pos >= (double) v.z->loopEnd) v.pos -= (double) (v.z->loopEnd - v.z->loopStart);
                    if (v.pos >= len - 2)
                    {
                        if (ins->mode != PlayMode::Resonator) { v.z = nullptr; break; }
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
                    if (ins->mode == PlayMode::Resonator)
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
    bool dropping = false;                                  // the instrument goes once the last voice has
};
} // namespace njq
