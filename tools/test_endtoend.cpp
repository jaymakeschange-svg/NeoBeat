// MidiJaQ, given the files it is actually going to be given.
//
// Batchcut's MakePatches writes a real .NJQ and a real .NJQi through Batchcut's own saving code.
// This opens them through MidiJaQ's own loading code and plays them, which is the one test that
// covers the join between the two plugins. Everything else in this tree tests MidiJaQ alone, and
// a format that drifts apart between the two would pass all of it.
//
//   test_endtoend <folder holding kit.NJQ and keys.NJQi>
//
// What it asks:
//   * both files open
//   * a key plays at that key's pitch, and the next octave up plays an octave up
//   * every part of the keyboard answers
//   * a sequence and the keys play together, both audible
//   * a stacked layer thickens what the keys play

#include "app/Processor.h"
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 512;

struct Transport final : public juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setPpqPosition (ppq);
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        return p;
    }
};

struct Heard
{
    float peak = 0.0f, rms = 0.0f;
    std::vector<float> wave;
    bool silent() const noexcept { return peak < 1.0e-4f; }
    float db() const noexcept { return 20.0f * std::log10 (std::max (1.0e-7f, rms)); }
};

// Render, with the option of holding keys down. `keysDown` is sent as note-ons at the start and
// note-offs at the end, which is what a player does.
static Heard play (MjqProcessor& p, Transport& t, double seconds,
                   const std::vector<int>& keysDown = {}, bool sequencerRunning = true)
{
    p.setPlayHead (&t);
    t.playing = sequencerRunning;

    const int blocks = (int) (seconds * kSr / kBlock);
    juce::AudioBuffer<float> buf (2, kBlock);
    Heard h;
    double sumSq = 0.0;
    long long n = 0;

    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 1) for (int k : keysDown) midi.addEvent (juce::MidiMessage::noteOn (1, k, 0.8f), 0);
        if (b == blocks - 2) for (int k : keysDown) midi.addEvent (juce::MidiMessage::noteOff (1, k), 0);
        p.processBlock (buf, midi);

        const auto* l = buf.getReadPointer (0);
        const auto* r = buf.getReadPointer (1);
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = 0.5f * (l[i] + r[i]);
            h.wave.push_back (v);
            for (float c : { l[i], r[i] })
            {
                if (! std::isfinite (c)) continue;
                h.peak = std::max (h.peak, std::fabs (c));
                sumSq += (double) c * c; ++n;
            }
        }
        if (t.playing) t.ppq += (double) kBlock * t.bpm / (60.0 * kSr);
    }
    h.rms = n ? (float) std::sqrt (sumSq / (double) n) : 0.0f;
    return h;
}

// The pitch of what was heard, by autocorrelation over the part that is ringing. Returns 0 if
// there is nothing pitched there.
static float pitchHz (const std::vector<float>& w, size_t from, size_t len)
{
    if (w.size() < from + len) return 0.0f;
    const float* x = w.data() + from;
    double energy = 0.0;
    for (size_t i = 0; i < len; ++i) energy += (double) x[i] * x[i];
    if (energy < 1.0e-8) return 0.0f;

    const int lagLow = (int) (kSr / 1200.0), lagHigh = (int) (kSr / 40.0);
    double best = 0.0; int bestLag = 0;
    for (int lag = lagLow; lag < lagHigh && lag < (int) len / 2; ++lag)
    {
        double dot = 0.0, e = 0.0;
        for (size_t i = 0; i + (size_t) lag < len; ++i) { dot += (double) x[i] * x[i + (size_t) lag]; e += (double) x[i + (size_t) lag] * x[i + (size_t) lag]; }
        const double norm = dot / std::sqrt (std::max (1.0e-12, e * energy));
        if (norm > best) { best = norm; bestLag = lag; }
    }
    return (best > 0.3 && bestLag > 0) ? (float) (kSr / (double) bestLag) : 0.0f;
}

static float centsApart (float hz, float want)
{
    if (hz < 1.0f || want < 1.0f) return 9999.0f;
    return 1200.0f * (float) std::log2 ((double) hz / (double) want);
}
static float hzOfNote (int n) { return 440.0f * (float) std::exp2 (((double) n - 69.0) / 12.0); }

// A plain tuned sine, for the questions about pitch. The .NJQi Batchcut builds from drum one-shots
// is the right thing to test the format with and the wrong thing to measure pitch on.
static juce::File writeTuned (const juce::File& dir)
{
    njq::InstrumentFile f;
    f.name = "tuned"; f.madeWith = "test_endtoend";
    f.mode = 0; f.sustain = 1; f.releaseMs = 80.0f;
    njq::Zone z;
    z.name = "sine"; z.source = "test"; z.sampleRate = kSr; z.root = 60.0f; z.tuned = true;
    z.lowKey = 0; z.highKey = 127;
    const double hz = 440.0 * std::exp2 ((60.0 - 69.0) / 12.0);
    const int n = (int) (1.5 * kSr);
    z.l.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double env = std::min (1.0, t / 0.004);
        z.l[(size_t) i] = (float) (0.5 * env * std::sin (6.283185307179586 * hz * t));
    }
    z.r = z.l;
    z.holdAt = (int) (0.5 * kSr);
    z.loopStart = (int) (0.6 * kSr);
    z.loopEnd = (int) (0.6 * kSr + std::round (kSr / hz) * 24.0);
    f.zones.push_back (std::move (z));
    const auto file = dir.getChildFile ("tuned.njqi");
    juce::String err;
    njq::saveInstrument (file, f, err);
    return file;
}

static std::unique_ptr<MjqProcessor> fresh()
{
    auto p = std::make_unique<MjqProcessor>();
    p->setPlayConfigDetails (0, 2, kSr, kBlock);
    p->prepareToPlay (kSr, kBlock);
    return p;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File dir (argc > 1 ? juce::String (argv[1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("patches").getFullPathName());
    const auto njq = dir.getChildFile ("kit.NJQ");
    const auto njqi = dir.getChildFile ("keys.NJQi");

    std::printf ("the files Batchcut wrote\n");
    CHECK (njq.existsAsFile(), "kit.NJQ is there (%.0f KB)", (double) njq.getSize() / 1024.0);
    CHECK (njqi.existsAsFile(), "keys.NJQi is there (%.0f KB)", (double) njqi.getSize() / 1024.0);
    if (failures > 0) { std::printf ("\nFAIL: run Batchcut's MakePatches first\n"); return 1; }

    std::printf ("\nopening them in MidiJaQ\n");
    {
        auto p = fresh();
        juce::String err;
        CHECK (p->openInstrument (0, njqi, err), "the .NJQi opens (%s)", err.isEmpty() ? "no error" : err.toRawUTF8());
        juce::String err2;
        CHECK (p->openInstrument (1, njq, err2), "the .NJQ opens on another voice (%s)", err2.isEmpty() ? "no error" : err2.toRawUTF8());
    }

    std::printf ("\nthe kit plays as a drum rack\n");
    {
        // a kit laid out one lane to a key from C1 up: the keys with a sound in them should sound,
        // and two different lanes should not be the same sound
        auto hit = [&] (int key)
        {
            auto p = fresh();
            juce::String err;
            p->openInstrument (0, njq, err);
            p->line.mode = mjq::Mode::Follow;
            p->lineChanged();
            Transport t;
            return play (*p, t, 1.5, { key }, true);
        };
        const auto kick = hit (36), snare = hit (38);
        CHECK (! kick.silent(), "C1 plays the first lane (%.1f dB)", kick.db());
        CHECK (! snare.silent(), "D1 plays another lane (%.1f dB)", snare.db());
        double ea = 0.0, eb = 0.0, d = 0.0;
        const size_t n = std::min (kick.wave.size(), snare.wave.size());
        for (size_t i = 0; i < n; ++i) { ea += (double) kick.wave[i] * kick.wave[i]; eb += (double) snare.wave[i] * snare.wave[i]; }
        if (ea > 1e-12 && eb > 1e-12)
        {
            const double g = std::sqrt (ea / eb);
            for (size_t i = 0; i < n; ++i) { const double e2 = (double) kick.wave[i] - g * snare.wave[i]; d += e2 * e2; }
        }
        const float unlike = (ea > 1e-12) ? (float) (10.0 * std::log10 (std::max (1e-12, d) / ea)) : 0.0f;
        CHECK (unlike > -12.0f, "two lanes are two different sounds (%.1f dB apart)", unlike);
    }

    std::printf ("\nkeys play as keys\n");
    {
        const auto tuned = writeTuned (dir);

        // Keys mode: the line moves to the key held, so an octave up should play an octave up.
        auto hold = [&] (int key, mjq::Mode mode, bool running)
        {
            auto p = fresh();
            juce::String err;
            p->openInstrument (0, tuned, err);
            p->line.mode = mode;
            p->line.numSteps = 16; p->line.traffic = 1.0f; p->line.gate = 1.0f;
            mjq::generate (p->line, 99);
            p->line.base = 60;
            p->lineChanged();
            Transport t;
            return play (*p, t, 2.5, { key }, running);
        };

        const size_t from = (size_t) (0.6 * kSr), len = (size_t) (0.8 * kSr);

        const auto kLow = hold (60, mjq::Mode::Keys, true), kHigh = hold (72, mjq::Mode::Keys, true);
        const float hzKl = pitchHz (kLow.wave, from, len), hzKh = pitchHz (kHigh.wave, from, len);
        CHECK (! kLow.silent() && ! kHigh.silent(), "Keys mode sounds on a held key (%.1f dB, %.1f dB)", kLow.db(), kHigh.db());
        std::printf ("     Keys mode: C3 %.1f Hz, C4 %.1f Hz\n", hzKl, hzKh);
        if (hzKl > 0.0f && hzKh > 0.0f)
            CHECK (std::fabs (centsApart (hzKh, hzKl) - 1200.0f) < 60.0f,
                   "a key an octave up moves the line an octave up (%.0f cents)", centsApart (hzKh, hzKl));
        else CHECK (false, "something pitched came out of Keys mode (%.1f, %.1f Hz)", hzKl, hzKh);

        // Follow mode: the held keys give the pitches, so the key itself should be what is heard.
        const auto fLow = hold (60, mjq::Mode::Follow, true), fHigh = hold (72, mjq::Mode::Follow, true);
        const float hzFl = pitchHz (fLow.wave, from, len), hzFh = pitchHz (fHigh.wave, from, len);
        std::printf ("     Follow mode: holding C3 gives %.1f Hz (C3 is %.1f), C4 gives %.1f Hz (C4 is %.1f)\n",
                     hzFl, hzOfNote (60), hzFh, hzOfNote (72));
        // Follow takes its pitches from the held keys but still applies the line's own octave lane
        // (-1 to +2), so the key is the root rather than the exact note. What has to hold is that
        // moving the key moves what is heard by the same amount.
        CHECK (hzFl > 0.0f && hzFh > 0.0f && std::fabs (centsApart (hzFh, hzFl) - 1200.0f) < 60.0f,
               "a key an octave up plays an octave up in Follow (%.0f cents)", centsApart (hzFh, hzFl));

        // and with the transport stopped, which is how you audition a sound
        // Stopped, there is no line for a key to steer, so the key plays the sound at its own pitch:
        // an instrument you can play and audition without starting the transport.
        const auto sLow = hold (60, mjq::Mode::Follow, false), sHigh = hold (72, mjq::Mode::Follow, false);
        CHECK (! sLow.silent(), "a held key sounds with the transport stopped (%.1f dB)", sLow.db());
        const float hzSl = pitchHz (sLow.wave, from, len), hzSh = pitchHz (sHigh.wave, from, len);
        std::printf ("     stopped: C3 gives %.1f Hz (want %.1f), C4 gives %.1f Hz (want %.1f)\n",
                     hzSl, hzOfNote (60), hzSh, hzOfNote (72));
        CHECK (hzSl > 0.0f && std::fabs (centsApart (hzSl, hzOfNote (60))) < 50.0f,
               "stopped, C3 plays C3 (%.0f cents off)", centsApart (hzSl, hzOfNote (60)));
        CHECK (hzSh > 0.0f && std::fabs (centsApart (hzSh, hzOfNote (72))) < 50.0f,
               "stopped, C4 plays C4 (%.0f cents off)", centsApart (hzSh, hzOfNote (72)));
    }

    std::printf ("\nthe whole keyboard answers\n");
    {
        int sounded = 0, tried = 0;
        for (int key = 36; key <= 96; key += 12)
        {
            auto p = fresh();
            juce::String err;
            p->openInstrument (0, njqi, err);
            p->line.mode = mjq::Mode::Keys;
            p->lineChanged();
            Transport t;
            const auto h = play (*p, t, 1.2, { key }, true);
            ++tried;
            if (! h.silent()) ++sounded;
        }
        CHECK (sounded == tried, "every octave from C1 to C6 sounds (%d of %d)", sounded, tried);
    }

    std::printf ("\na sequence and the keys together\n");
    {
        juce::String err;
        // the sequence on its own
        auto s = fresh();
        s->openInstrument (0, njqi, err);
        s->line.numSteps = 16; s->line.traffic = 1.0f; s->line.gate = 1.0f;
        mjq::generate (s->line, 4242);
        s->lineChanged();
        Transport t1;
        const auto seqOnly = play (*s, t1, 4.0, {}, true);

        // the keys on their own
        auto k = fresh();
        k->openInstrument (0, njqi, err);
        k->line.mode = mjq::Mode::Keys;
        k->lineChanged();
        Transport t2;
        const auto keysOnly = play (*k, t2, 4.0, { 48, 55 }, true);

        // and a voice sequencing while another voice is played by hand
        auto both = fresh();
        both->openInstrument (0, njqi, err);
        both->openInstrument (1, writeTuned (dir), err);
        both->line.numSteps = 16; both->line.traffic = 1.0f; both->line.gate = 1.0f;
        mjq::generate (both->line, 4242);
        both->lineChanged();
        for (int p = 0; p < mjq::kSessionSlots; ++p)
        {
            auto& tr = both->song.slots[(size_t) p].tracks[1];
            tr.line.mode = mjq::Mode::Keys;     // voice 2 answers the keyboard
            tr.lowKey = 0; tr.highKey = 127;
        }
        both->lineChanged();
        Transport t3;
        const auto together = play (*both, t3, 4.0, { 48, 55 }, true);

        CHECK (! seqOnly.silent(), "the sequence plays on its own (%.1f dB)", seqOnly.db());
        CHECK (! keysOnly.silent(), "the keys play on their own (%.1f dB)", keysOnly.db());
        CHECK (! together.silent(), "both at once plays (%.1f dB)", together.db());
        CHECK (together.db() > seqOnly.db() + 0.5f && together.db() > keysOnly.db() + 0.5f,
               "both at once is fuller than either alone (%.1f dB against %.1f and %.1f)",
               together.db(), seqOnly.db(), keysOnly.db());
    }

    std::printf ("\nstacking a layer on what the keys play\n");
    {
        juce::String err;
        auto one = fresh();
        one->openInstrument (0, njqi, err);
        one->line.mode = mjq::Mode::Keys;
        one->lineChanged();
        Transport t1;
        const auto alone = play (*one, t1, 2.5, { 57 }, true);

        auto two = fresh();
        two->openInstrument (0, njqi, err);
        two->openInstrument (1, writeTuned (dir), err);
        two->setTrackMode (1, true, 0);          // voice 2 stacked on voice 1
        two->setTrackStack (1, 0, 9.0f, 11.0f);  // slightly sharp, slightly behind
        two->line.mode = mjq::Mode::Keys;
        two->lineChanged();
        Transport t2;
        const auto stacked = play (*two, t2, 2.5, { 57 }, true);

        CHECK (! stacked.silent(), "the stack sounds (%.1f dB)", stacked.db());
        // level-matched, so turning it up cannot fake it
        const size_t n = std::min (alone.wave.size(), stacked.wave.size());
        double ea = 0.0, eb = 0.0;
        for (size_t i = 0; i < n; ++i) { ea += (double) alone.wave[i] * alone.wave[i]; eb += (double) stacked.wave[i] * stacked.wave[i]; }
        float unlike = 0.0f;
        if (ea > 1.0e-12 && eb > 1.0e-12)
        {
            const double g = std::sqrt (ea / eb);
            double d = 0.0;
            for (size_t i = 0; i < n; ++i) { const double e = (double) alone.wave[i] - g * stacked.wave[i]; d += e * e; }
            unlike = (float) (10.0 * std::log10 (std::max (1.0e-12, d) / ea));
        }
        CHECK (unlike > -12.0f, "the stack changes the sound, not just the level (%.1f dB apart, level for level)", unlike);
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
