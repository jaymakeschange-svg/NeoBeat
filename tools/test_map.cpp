// The MAP, listened to.
//
// The MAP room draws marks on a grid: a row is a degree of the key, and each mark carries its own
// source, so one pattern holds keys and chops together. This asks whether those marks actually
// play. A grid that only draws is a picture of a sequencer.
//
// It also asks the two questions that would make the room a liar: does a mark on a higher row
// sound higher (the row IS the degree, so it must), and does the pattern come round again - a
// sequencer that plays its first bar and stops is not a sequencer.

#include "app/Processor.h"
#include "neobeat/gen/Scale.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 256;

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
    std::vector<float> wave;          // mono, one entry per sample
    bool silent() const noexcept { return peak < 1.0e-4f; }
};

// Render `seconds` with the transport rolling from `fromBeat`.
static Heard play (MjqProcessor& p, double seconds, double fromBeat = 0.0, bool rolling = true)
{
    Transport t;
    t.ppq = fromBeat;
    t.playing = rolling;
    p.setPlayHead (&t);

    const int blocks = (int) (seconds * kSr / kBlock);
    juce::AudioBuffer<float> buf (2, kBlock);
    Heard h;
    double sumSq = 0.0; long long n = 0;

    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        const auto* l = buf.getReadPointer (0);
        const auto* r = buf.getReadPointer (1);
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = 0.5f * (l[i] + r[i]);
            h.wave.push_back (v);
            if (! std::isfinite (v)) continue;
            h.peak = std::max (h.peak, std::fabs (v));
            sumSq += (double) v * v; ++n;
        }
        if (t.playing) t.ppq += (double) kBlock * t.bpm / (60.0 * kSr);
    }
    h.rms = n ? (float) std::sqrt (sumSq / (double) n) : 0.0f;
    return h;
}

// The strongest frequency in a window, by counting rising zero crossings. Crude, but it tells a
// fifth from a unison, which is all that is being asked.
static double pitchOf (const std::vector<float>& w, int from, int to)
{
    from = std::max (0, from); to = std::min ((int) w.size(), to);
    if (to - from < 64) return 0.0;
    double peak = 0.0;
    for (int i = from; i < to; ++i) peak = std::max (peak, (double) std::fabs (w[(size_t) i]));
    if (peak < 1.0e-4) return 0.0;
    const double gate = 0.25 * peak;
    int crossings = 0; bool above = false;
    for (int i = from; i < to; ++i)
    {
        const double v = w[(size_t) i];
        if (! above && v > gate) { ++crossings; above = true; }
        else if (above && v < -gate) above = false;
    }
    return (double) crossings * kSr / (double) (to - from);
}

// A plain sawtooth sound, so the pitch is easy to read.
static std::shared_ptr<bc::PoolItem> tone (double hz, double seconds = 0.5)
{
    auto it = std::make_shared<bc::PoolItem>();
    it->name = "tone";
    it->sr = kSr;
    const int n = (int) (seconds * kSr);
    it->l.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double ph = std::fmod (t * hz, 1.0);
        const double env = std::min (1.0, 40.0 * t) * std::exp (-2.0 * t);
        it->l[(size_t) i] = (float) (0.5 * (2.0 * ph - 1.0) * env);
    }
    it->r = it->l;
    it->f.valid = true;
    it->f.pitchHz = (float) hz;
    it->f.pitchStrength = 0.9f;
    it->f.low = 0.2f;
    return it;
}

static std::unique_ptr<MjqProcessor> fresh()
{
    auto p = std::make_unique<MjqProcessor>();
    p->setPlayConfigDetails (0, 2, kSr, kBlock);
    p->prepareToPlay (kSr, kBlock);
    return p;
}

// Put one sound on the keys instrument, covering the whole keyboard, so any degree sounds.
static void giveItAnInstrument (MjqProcessor& p, double hz)
{
    // built the way the room builds it, so the test exercises the real path
    p.keys = {};
    bc::buildInstrument (p.keys, { tone (hz) });
    p.keysChanged();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::printf ("a mark on the grid sounds\n");
    {
        auto p = fresh();
        giveItAnInstrument (*p, 220.0);

        const auto quiet = play (*p, 1.0);
        CHECK (quiet.silent(), "an empty map is silent (peak %.5f)", quiet.peak);

        p->map = {};
        p->map.steps = 16; p->map.perBar = 16;
        p->map.marks.push_back ({ 0, 0, 2, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->mapChanged();

        const auto heard = play (*p, 1.0);
        CHECK (! heard.silent(), "one keys mark plays (peak %.4f)", heard.peak);
    }

    std::printf ("\nthe row is the degree\n");
    {
        auto p = fresh();
        giveItAnInstrument (*p, 220.0);
        p->map = {};
        p->map.steps = 16; p->map.perBar = 16;
        p->map.root = 0; p->map.scale = 0; p->map.octave = 4;
        // two marks a bar apart: the tonic, then four degrees above it
        p->map.marks.push_back ({ 0, 0, 3, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->map.marks.push_back ({ 8, 4, 3, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->mapChanged();

        const auto heard = play (*p, 2.0);
        // step 0 lands at beat 0, step 8 at beat 2 - a quarter-note each at 16ths in 4/4
        const int a = (int) (0.10 * kSr), b = (int) (0.30 * kSr);
        const int c = (int) (1.10 * kSr), d = (int) (1.30 * kSr);
        const double lo = pitchOf (heard.wave, a, b), hi = pitchOf (heard.wave, c, d);

        // What the MAP says those rows are, against what came out. The sound is tuned from 220 Hz,
        // so the note it should sound at is arithmetic, not a guess - and the first run of this
        // test guessed, assuming scale 0 was major when it is chromatic.
        const auto mask = gen::scale (p->map.scale).mask;
        auto hzOf = [] (int note) { return 220.0 * std::pow (2.0, (note - 57) / 12.0); };
        const double wantLo = hzOf (mjq::noteOfRow (0, mask, p->map.root, p->map.octave));
        const double wantHi = hzOf (mjq::noteOfRow (4, mask, p->map.root, p->map.octave));
        CHECK (lo > 20.0 && hi > 20.0, "both marks sound (%.0f Hz then %.0f Hz)", lo, hi);
        CHECK (std::fabs (lo / wantLo - 1.0) < 0.04, "row 0 is the degree it says (%.0f Hz, wanted %.0f)", lo, wantLo);
        CHECK (std::fabs (hi / wantHi - 1.0) < 0.04, "row 4 is the degree it says (%.0f Hz, wanted %.0f)", hi, wantHi);
        CHECK (hi > lo * 1.05, "the higher row is the higher note (%.0f Hz over %.0f Hz)", hi, lo);
    }

    std::printf ("\nthe pattern comes round again\n");
    {
        auto p = fresh();
        giveItAnInstrument (*p, 330.0);
        p->map = {};
        p->map.steps = 16; p->map.perBar = 16;
        p->map.marks.push_back ({ 0, 0, 2, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->mapChanged();

        // The pattern is sixteen 16ths: four beats, so the mark comes round at beats 0, 4, 8, 12.
        // The first run of this test listened from beat 6 for two beats - a window with no mark in
        // it at all - and called that a failure of the code.
        const auto late = play (*p, 1.0, 8.0);
        CHECK (! late.silent(), "the mark plays again four bars later (peak %.4f)", late.peak);
    }

    std::printf ("\nthe transport holds the map\n");
    {
        auto p = fresh();
        giveItAnInstrument (*p, 220.0);
        p->map = {};
        p->map.steps = 16; p->map.perBar = 16;
        for (int s = 0; s < 16; ++s)
            p->map.marks.push_back ({ s, s % 7, 1, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->mapChanged();

        const auto stopped = play (*p, 1.0, 0.0, false);
        CHECK (stopped.silent(), "a stopped transport does not run the map (peak %.5f)", stopped.peak);

        const auto rolling = play (*p, 1.0, 0.0, true);
        CHECK (! rolling.silent(), "a rolling transport does (peak %.4f)", rolling.peak);
    }

    std::printf ("\nclearing the marks stops the sound\n");
    {
        auto p = fresh();
        giveItAnInstrument (*p, 220.0);
        p->map = {};
        p->map.steps = 16; p->map.perBar = 16;
        for (int s = 0; s < 16; s += 2)
            p->map.marks.push_back ({ s, 0, 1, 0.9f, mjq::MarkSource::Keys, 0, 1, false });
        p->mapChanged();
        const auto before = play (*p, 1.0);

        p->map.marks.clear();
        p->mapChanged();
        play (*p, 1.0, 4.0);                      // the release tails of the notes already struck
        const auto after = play (*p, 1.0, 6.0);
        CHECK (! before.silent() && after.silent(),
               "marks on then off: peak %.4f then %.5f", before.peak, after.peak);
    }

    std::printf ("\n%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
