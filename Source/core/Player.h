#pragma once
// MidiJaQ's clock and its MIDI out: it turns a pass into note-ons and note-offs at the right samples.
//
// The clock has three modes. Pattern starts the line again whenever the transport starts; Song puts the
// line where the song is, so a line is in the same place whenever you play from a bar; Gate plays only
// while keys are held. In every mode the line is read from a snapshot, so the audio thread never waits
// for the editor.
//
// No JUCE here: the plugin turns these events into a juce::MidiBuffer, and the tests read them directly.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Jaq.h"
#include "Session.h"

namespace mjq
{
enum class ClockMode : uint8_t { Pattern = 0, Song, Gate };

// What the host tells us at the top of a block.
struct HostTime
{
    double ppq = 0.0;       // the song position, in quarter notes
    double bpm = 120.0;
    bool playing = false;
    bool valid = false;     // false in a standalone app with no host
};

// One MIDI note event on its way out.
struct MidiOut
{
    int sample = 0;
    int note = 60;
    int velocity = 100;
    bool on = true;
};

class Player
{
public:
    static constexpr int kMaxHeld = 16;          // keys held at once, and so copies of a line in Keys mode

    void prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }

    // Everything stops: anything sounding is let go by the caller through `release`.
    void reset() noexcept { playhead = 0.0; wasPlaying = false; }

    // The keys held down, for Follow and Bass and for the Gate clock.
    void keyDown (int note, int velocity) noexcept
    {
        if (numHeld >= (int) held.size() || note < 0 || note > 127) return;
        for (int i = 0; i < numHeld; ++i) if (held[(size_t) i] == note) return;
        held[(size_t) numHeld] = note;
        vel[(size_t) numHeld] = std::max (1, std::min (127, velocity));
        ++numHeld;
    }
    void keyUp (int note) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i] == note)
            {
                for (int j = i; j + 1 < numHeld; ++j) { held[(size_t) j] = held[(size_t) j + 1]; vel[(size_t) j] = vel[(size_t) j + 1]; }
                --numHeld;
                return;
            }
    }
    void allKeysUp() noexcept { numHeld = 0; }
    int keysHeld() const noexcept { return numHeld; }
    int keyVelocity() const noexcept { return numHeld > 0 ? vel[0] : 100; }

    bool running() const noexcept { return wasPlaying; }
    double positionInPass() const noexcept { return playhead; }
    // Where the line is now, as a step of the pass (for the lights in the editor).
    int stepNow (const Pass& pass) const noexcept
    {
        if (pass.events.empty() || pass.beats <= 0.0) return -1;
        const double p = std::fmod (playhead, pass.beats);
        int best = -1;
        for (const auto& e : pass.events) if (e.start <= p + 1.0e-9) best = e.step;
        return best;
    }

    // Writes one block's events. Returns how many it wrote. `release` is filled with the notes that must
    // be let go because the transport stopped or the line changed under us.
    int process (const Line& L, const Pass& pass, ClockMode mode, const HostTime& host, int numSamples,
                 MidiOut* out, int maxOut, const Progression* progression = nullptr,
                 const GeneratorSettings* writer = nullptr, double chordBeat = 0.0,
                 const std::vector<Pass>* evolving = nullptr, bool reapply = false)
    {
        int written = 0;
        if (out == nullptr || maxOut <= 0 || numSamples <= 0) return 0;

        const bool gated = mode == ClockMode::Gate;
        const bool play = gated ? numHeld > 0 : (host.valid ? host.playing : true);
        const double bpm = host.bpm > 1.0 ? host.bpm : 120.0;
        const double blockBeats = (double) numSamples * bpm / (60.0 * sr);

        auto emit = [&] (int sample, int note, int velocity, bool on)
        {
            if (written < maxOut) out[written++] = { std::max (0, std::min (numSamples - 1, sample)), note, velocity, on };
        };

        // the transport (or the last key) went away: everything sounding is let go
        if (! play)
        {
            if (wasPlaying) { releaseAll (emit, 0); wasPlaying = false; }
            if (mode == ClockMode::Pattern || gated) playhead = 0.0;
            return written;
        }

        // the start of a block: where the line is
        double from = playhead;
        if (! wasPlaying)
        {
            from = (mode == ClockMode::Song && host.valid) ? std::fmod (std::max (0.0, host.ppq), std::max (1.0e-6, pass.beats)) : 0.0;
            wasPlaying = true;
        }
        else if (mode == ClockMode::Song && host.valid)
        {
            // follow the song, but only jump when the host really moved (a loop, or the user dragging)
            const double want = std::fmod (std::max (0.0, host.ppq), std::max (1.0e-6, pass.beats));
            const double drift = std::fabs (want - std::fmod (from, std::max (1.0e-6, pass.beats)));
            if (drift > 0.02 && drift < pass.beats - 0.02)
            {
                releaseAll (emit, 0);                       // the song jumped: nothing may hang over the cut
                from = want;
            }
        }
        const double to = from + blockBeats;

        // the notes that stop inside this block
        for (int i = 0; i < numSounding; )
        {
            if (sounding[(size_t) i].endBeat < to)
            {
                const int s = (int) std::floor ((sounding[(size_t) i].endBeat - from) / std::max (1.0e-9, blockBeats) * numSamples);
                emit (s, sounding[(size_t) i].note, 0, false);
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
            }
            else ++i;
        }

        // the notes that start inside it
        if (! pass.events.empty() && pass.beats > 0.0)
        {
            const long long k0 = (long long) std::floor (from / pass.beats);
            const long long k1 = (long long) std::floor (to / pass.beats);
            const long long songRound = mode == ClockMode::Song && host.valid
                ? (long long) std::floor (std::max (0.0, host.ppq) / pass.beats) : 0;
            for (long long k = k0; k <= k1; ++k)
            {
                const double base = (double) k * pass.beats;
                const uint64_t round = (uint64_t) std::max (0ll,
                    mode == ClockMode::Song && host.valid ? songRound + (k - k0) : k);
                const auto& currentPass = evolving != nullptr && ! evolving->empty()
                    ? (*evolving)[(size_t) (round % evolving->size())] : pass;
                bool prevPassed = true;
                for (const auto& e : currentPass.events)
                {
                    const bool permitted = writer == nullptr || cueAllows (*writer, round, e.step, prevPassed);
                    prevPassed = permitted;
                    if (! permitted) continue;

                    // A pass bakes in what TRAFFIC, DIALECT, ACCENT and GATE were when it was written. A
                    // cycling knob moves them while it plays, so when one is live they are worked out again
                    // here from the step the event remembers: traffic says whether it sounds at all,
                    // dialect which of its two notes it takes, accent how hard and gate how long. The pass
                    // for a cycling line is written at the widest setting the cycle can reach, so there is
                    // always a step here to drop rather than one missing that should have sounded.
                    int liveNote = e.note, liveVelocity = e.velocity;
                    double liveLength = e.length;
                    if (reapply)
                    {
                        if (! stepOn (L, e.step)) continue;
                        liveNote = noteOf (L, e.step);
                        liveVelocity = velocityOf (L, e.step);
                        liveLength = e.length * (double) std::max (0.05f, L.gate) / (double) std::max (0.05f, currentPass.gate);
                    }

                    const double t = base + e.start;
                    if (t < from || t >= to) continue;
                    ChordNotes chord;
                    const int* pitchKeys = held.data();
                    int numPitchKeys = numHeld;
                    if (progression != nullptr && (L.mode == Mode::Bass || L.mode == Mode::Parts))
                    {
                        chord = progression->notesAt (chordBeat + t - from);
                        pitchKeys = chord.notes.data();
                        numPitchKeys = chord.count;
                    }
                    // In Keys mode the line keeps its shape and moves to each key held, so one event can
                    // sound several times at once - a triad gives three copies of the figure. Every other
                    // mode sounds one note, which keeps this loop the same shape for all of them.
                    int pitches[kMaxHeld];
                    int numPitches = 1;
                    if (L.mode == Mode::Keys && numHeld > 0)
                    {
                        numPitches = 0;
                        for (int i = 0; i < numHeld && numPitches < kMaxHeld; ++i)
                        {
                            const int p = liveNote + keysShift (L, held[(size_t) i]) + L.transpose;
                            if (p >= 0 && p <= 127) pitches[numPitches++] = p;
                        }
                        if (numPitches == 0) continue;      // every copy fell off the keyboard
                    }
                    else
                    {
                        Event live = e;
                        live.note = liveNote;
                        pitches[0] = notePlayed (L, live, pitchKeys, numPitchKeys);
                    }

                    const int velocity = (L.mode == Mode::Free || numHeld == 0 || (progression != nullptr && (L.mode == Mode::Bass || L.mode == Mode::Parts)))
                                       ? liveVelocity
                                       : std::max (1, std::min (127, (int) std::lround ((double) liveVelocity * (double) keyVelocity() / 100.0)));
                    const int s = (int) std::floor ((t - from) / std::max (1.0e-9, blockBeats) * numSamples);

                    for (int c = 0; c < numPitches; ++c)
                    {
                        const int note = pitches[c];

                        // a note already sounding at this pitch is let go first, unless this one slides into it
                        for (int i = 0; i < numSounding; ++i)
                            if (sounding[(size_t) i].note == note)
                            {
                                emit (std::max (0, s - 1), note, 0, false);
                                sounding[(size_t) i] = sounding[(size_t) --numSounding];
                                break;
                            }

                        emit (s, note, velocity, true);
                        if (numSounding < (int) sounding.size()) sounding[(size_t) numSounding++] = { note, t + std::max (1.0e-4, liveLength) };
                    }
                }
            }
        }

        playhead = to;
        if (pass.beats > 0.0 && playhead > 1.0e7) playhead = std::fmod (playhead, pass.beats);   // never let it drift out of precision
        return written;
    }

    // Let everything sounding go (the editor leaving, the plugin being stopped).
    int releaseAllNow (MidiOut* out, int maxOut)
    {
        int written = 0;
        auto emit = [&] (int sample, int note, int velocity, bool on)
        {
            if (written < maxOut) out[written++] = { sample, note, velocity, on };
        };
        releaseAll (emit, 0);
        return written;
    }

private:
    struct Sounding { int note; double endBeat; };

    template <typename Emit>
    void releaseAll (Emit&& emit, int sample)
    {
        for (int i = 0; i < numSounding; ++i) emit (sample, sounding[(size_t) i].note, 0, false);
        numSounding = 0;
    }

    double sr = 48000.0;
    double playhead = 0.0;
    bool wasPlaying = false;

    std::array<Sounding, 64> sounding {};
    int numSounding = 0;
    std::array<int, kMaxHeld> held {}, vel {};
    int numHeld = 0;
};
} // namespace mjq
