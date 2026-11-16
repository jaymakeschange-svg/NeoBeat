// Opening a .NJQ instrument in MidiJaQ and playing it: the file, the zones, and the sound that comes out.
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "core/Inst.h"

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

static constexpr double kSr = 48000.0;

// a plain tone with a little body to it, so a zone has something to loop and to hold
static njq::Zone tone (const juce::String& name, float rootNote, double seconds = 1.0)
{
    njq::Zone z;
    z.name = name;
    z.source = "test";
    z.sampleRate = kSr;
    z.root = rootNote;
    z.tuned = true;
    const double hz = 440.0 * std::exp2 ((rootNote - 69.0f) / 12.0);
    const int n = (int) (seconds * kSr);
    z.l.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        const double env = std::min (1.0, t / 0.005) * (0.6 + 0.4 * std::exp (-t * 1.5));
        z.l[(size_t) i] = (float) (0.5 * env * (std::sin (6.283185307179586 * hz * t) + 0.25 * std::sin (12.566370614359172 * hz * t)));
    }
    z.r = z.l;
    z.holdAt = (int) (0.3 * kSr);
    z.loopStart = (int) (0.4 * kSr);
    z.loopEnd = (int) (0.4 * kSr + std::round (kSr / hz) * 20.0);
    return z;
}

// the pitch of a stretch of output, by autocorrelation
static double pitchOf (const std::vector<float>& x, int from, int to)
{
    const int n = to - from;
    const int lagLo = (int) (kSr / 2000.0), lagHi = std::min (n / 2, (int) (kSr / 40.0));
    std::vector<double> rr ((size_t) std::max (0, lagHi - lagLo), 0.0);
    double best = 0.0;
    for (int lag = lagLo; lag < lagHi; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < n - lag; ++i) s += (double) x[(size_t) (from + i)] * x[(size_t) (from + i + lag)];
        s /= (double) (n - lag);                      // per sample, or a short lag wins just by having more terms
        rr[(size_t) (lag - lagLo)] = s;
        best = std::max (best, s);
    }
    int bestLag = 0;
    for (int lag = lagLo + 1; lag + 1 < lagHi; ++lag)  // the lowest pitch that correlates nearly as well: no octave errors
    {
        const double v = rr[(size_t) (lag - lagLo)];
        if (v >= 0.85 * best && v >= rr[(size_t) (lag - lagLo - 1)] && v >= rr[(size_t) (lag - lagLo + 1)]) { bestLag = lag; break; }
    }
    if (bestLag <= 0) return 0.0;
    // between samples, so the answer is good to a fraction of a cent
    auto corr = [&] (double lag)
    {
        const int L = (int) lag; const double f = lag - L;
        double s = 0.0;
        for (int i = 0; i < n - L - 1; ++i)
            s += (double) x[(size_t) (from + i)] * ((1.0 - f) * x[(size_t) (from + i + L)] + f * x[(size_t) (from + i + L + 1)]);
        return s;
    };
    const double a = corr (bestLag - 1.0), b = corr (bestLag), c = corr (bestLag + 1.0);
    const double d = (a - c) / (2.0 * (a - 2.0 * b + c) + 1e-20);
    return kSr / ((double) bestLag + juce::jlimit (-1.0, 1.0, d));
}
static double cents (double hz, double want) { return 1200.0 * std::log2 (std::max (1.0, hz) / want); }

// plays one note and returns the left channel
static std::vector<float> play (const mjq::Instrument& ins, int note, int velocity, int samples, int offSample = -1)
{
    auto shared = std::make_shared<const mjq::Instrument> (ins);
    mjq::InstrumentPlayer p;
    p.prepare (kSr);
    p.setInstrument (shared);
    p.level.store (1.0f);
    std::vector<float> l ((size_t) samples, 0.0f), r ((size_t) samples, 0.0f);
    const int block = 512;
    for (int at = 0; at < samples; at += block)
    {
        const int n = std::min (block, samples - at);
        mjq::PlayNote notes[2];
        int count = 0;
        if (at == 0) notes[count++] = { 0, note, (float) velocity / 127.0f, true };
        if (offSample >= at && offSample < at + n) notes[count++] = { offSample - at, note, 0.0f, false };
        p.process (l.data() + at, r.data() + at, n, notes, count);
    }
    return l;
}
static float peak (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    for (int i = from; i < to && i < (int) x.size(); ++i) m = std::max (m, std::fabs (x[(size_t) i]));
    return m;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;   // the FLAC readers register themselves here
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mjq_inst_test");
    dir.deleteRecursively();
    dir.createDirectory();

    std::printf ("1. a .NJQ instrument written and read back\n");
    njq::InstrumentFile f;
    f.name = "Three tones";
    f.madeWith = "test_inst";
    f.mode = 0;                       // tuned
    f.sustain = 1;                    // loop
    f.releaseMs = 120.0f;
    f.zones.push_back (tone ("low", 48.0f));
    f.zones.push_back (tone ("middle", 60.0f));
    f.zones.push_back (tone ("high", 72.0f));
    f.zones[0].lowKey = 0;  f.zones[0].highKey = 53;
    f.zones[1].lowKey = 54; f.zones[1].highKey = 65;
    f.zones[2].lowKey = 66; f.zones[2].highKey = 127;

    const auto file = dir.getChildFile ("Three tones.njq");
    juce::String err;
    const bool wrote = njq::saveInstrument (file, f, err);
    CHECK (wrote && juce::File (file.getFullPathName()).getSize() > 1000, "written (%d bytes) %s", (int) juce::File (file.getFullPathName()).getSize(), err.toRawUTF8());

    mjq::Instrument ins;
    const bool opened = mjq::loadInstrument (file, ins, err);
    CHECK (opened && ins.zones.size() == 3, "MidiJaQ opens it: %d sounds %s", (int) ins.zones.size(), err.toRawUTF8());
    if (! opened) { std::printf ("FAIL: %d failure(s)\n", ++failures); return 1; }

    bool kept = ins.name == "Three tones" && ins.mode == njq::PlayMode::Tuned && ins.sustain == njq::Sustain::Loop
             && std::fabs (ins.releaseMs - 120.0f) < 0.01f;
    for (size_t i = 0; kept && i < ins.zones.size(); ++i)
    {
        const auto& z = ins.zones[i];
        const auto& o = f.zones[i];
        kept = std::fabs (z.root - o.root) < 1e-4f && z.lowKey == o.lowKey && z.highKey == o.highKey
            && z.loopStart == o.loopStart && z.loopEnd == o.loopEnd && z.holdAt == o.holdAt && z.tunedSound
            && z.sound != nullptr && (int) z.sound->l.size() == (int) o.l.size() && z.sound->sr == o.sampleRate;
    }
    CHECK (kept, "with every zone's pitch, keys, loop and hold point");
    std::printf ("     %s\n", mjq::describe (ins).toRawUTF8());

    std::printf ("2. it plays, and it is in tune\n");
    {
        double worst = 0.0; int worstNote = 0;
        for (const int note : { 36, 48, 55, 60, 64, 72, 79 })
        {
            const auto out = play (ins, note, 100, (int) (0.9 * kSr));
            const double want = 440.0 * std::exp2 ((note - 69) / 12.0);
            const double got = pitchOf (out, (int) (0.3 * kSr), (int) (0.8 * kSr));
            if (std::fabs (cents (got, want)) > std::fabs (worst)) { worst = cents (got, want); worstNote = note; }
        }
        CHECK (std::fabs (worst) < 5.0, "C1 to G5 in tune within five cents (worst %.2f at %s)", worst, gen::noteName (worstNote).c_str());

        const auto loud = play (ins, 60, 127, (int) (0.3 * kSr));
        const auto soft = play (ins, 60, 30, (int) (0.3 * kSr));
        CHECK (peak (loud, 0, 14400) > peak (soft, 0, 14400) * 1.5f, "velocity is heard (%.2f against %.2f)", peak (loud, 0, 14400), peak (soft, 0, 14400));

        // the loop holds it as long as the key is down, and the release lets it go
        const int off = (int) (1.4 * kSr);
        const auto held = play (ins, 60, 100, (int) (2.0 * kSr), off);
        CHECK (peak (held, off - 4800, off) > 0.05f, "a loop holds the note past the end of the sound (%.2f at 1.4 s)", peak (held, off - 4800, off));
        CHECK (peak (held, off + 9600, (int) (2.0 * kSr)) < 0.002f, "and the release lets it go (%.4f a fifth of a second after)", peak (held, off + 9600, (int) (2.0 * kSr)));
    }

    std::printf ("3. the zones pick the right sound\n");
    {
        // each zone gets its own key range; a Chops instrument plays one sound a key
        mjq::Instrument chops = ins;
        chops.mode = njq::PlayMode::Chops;
        chops.sustain = njq::Sustain::AsRecorded;
        for (int i = 0; i < 3; ++i) { chops.zones[(size_t) i].lowKey = chops.zones[(size_t) i].highKey = 48 + i; }
        bool right = true;
        for (int i = 0; i < 3; ++i)
        {
            const auto out = play (chops, 48 + i, 100, (int) (0.4 * kSr));
            const double want = 440.0 * std::exp2 ((chops.zones[(size_t) i].root - 69.0f) / 12.0);
            right = right && std::fabs (cents (pitchOf (out, 2400, 14400), want)) < 5.0;     // as recorded: its own pitch
        }
        CHECK (right, "Chops: each key plays its own sound at its own speed");

        const auto nothing = play (chops, 90, 100, 9600);
        CHECK (peak (nothing, 0, 9600) < 1e-6f, "and a key with no sound on it stays quiet");
    }

    std::printf ("4. what is not an instrument\n");
    {
        njq::Kit kit;
        kit.name = "A kit";
        const auto kitFile = dir.getChildFile ("A kit.njq");
        juce::String e2;
        CHECK (njq::save (kitFile, kit, e2), "a NeoBeat sequence written for the test %s", e2.toRawUTF8());
        mjq::Instrument no;
        const bool refused = ! mjq::loadInstrument (kitFile, no, e2);
        CHECK (refused && e2.contains ("not an instrument"), "opening it as an instrument is refused: \"%s\"", e2.toRawUTF8());

        mjq::Instrument none;
        juce::String e3;
        CHECK (! mjq::loadInstrument (dir.getChildFile ("nothing here.njq"), none, e3) && e3.isNotEmpty(), "and so is a file that is not there");
    }

    // given a real file (one Batchcut wrote), open that too and play a few keys through it
    if (argc > 1)
    {
        std::printf ("5. %s\n", argv[1]);
        mjq::Instrument real;
        juce::String e;
        const bool ok = mjq::loadInstrument (juce::File (juce::String (argv[1])), real, e);
        CHECK (ok, "opened: %s", ok ? mjq::describe (real).toRawUTF8() : e.toRawUTF8());
        if (ok)
        {
            int sounded = 0;
            for (const int note : { 40, 48, 55, 60, 67, 72 })
                if (peak (play (real, note, 100, (int) (0.5 * kSr)), 0, (int) (0.5 * kSr)) > 0.001f) ++sounded;
            CHECK (sounded >= 3, "and %d of six keys across it play", sounded);
        }
    }

    dir.deleteRecursively();
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
