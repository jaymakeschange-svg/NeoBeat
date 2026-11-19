// MidiJaQ, listened to rather than reasoned about.
//
// Every other test here checks arithmetic: that weights sum to one, that a curve loops without a step. Not
// one of them can hear a voice drop 12 dB when it is put in the room, an Orb whose eight corners hold the
// same material, or an effect chain running away. All three shipped. This renders the plugin's real output
// and measures it, and every claim the features make about what you will hear is a check in here.
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "neobeat/shared/NjqInstrument.h"

static int failures = 0;
#define CHECK(c, ...) do { char m_[400]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } std::fflush (stdout); } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 512;

// ---------------------------------------------------------------- what a rendered stretch of audio is like
struct Sound
{
    float peak = 0.0f;
    float rms = 0.0f;
    float rmsL = 0.0f, rmsR = 0.0f;
    float dc = 0.0f;
    float firstHalfRms = 0.0f, lastHalfRms = 0.0f;   // for spotting a runaway
    // How much the level moves about from one short window to the next, in dB. A control that is cycling
    // makes the sound change as it plays; its average level may well be the same as a static setting, so
    // the average cannot see it and this can.
    float movement = 0.0f;
    // How alike one bar is to the next, 1 being identical. A line with nothing moving plays the same bar
    // for ever, so this sits at 1; anything that really changes the music as it plays pulls it down. This
    // is the measure that can see a cycling knob, because the average level and the spread cannot.
    float barToBar = 1.0f;
    int clipped = 0;
    bool silent() const noexcept { return peak < 1.0e-5f; }
    float balanceDb() const noexcept { return 20.0f * std::log10 (std::max (1.0e-7f, rmsL) / std::max (1.0e-7f, rmsR)); }
    float db() const noexcept { return 20.0f * std::log10 (std::max (1.0e-7f, rms)); }
};

static float dbOf (float a) { return 20.0f * std::log10 (std::max (1.0e-7f, a)); }

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

// Render some seconds of the plugin and describe what came out. `touch` is called every block, so a test can
// move a control while it plays - which is where the clicks and the runaways live.
static Sound listen (MjqProcessor& p, Transport& t, double seconds,
                     std::function<void (double)> touch = {})
{
    p.setPlayHead (&t);                   // a host tells the plugin where the song is; so must this

    const int blocks = (int) (seconds * kSr / kBlock);
    juce::AudioBuffer<float> buf (2, kBlock);
    double sumSq = 0.0, sumSqL = 0.0, sumSqR = 0.0, sum = 0.0;
    double firstSq = 0.0, lastSq = 0.0;
    long long n = 0, firstN = 0, lastN = 0;
    Sound s;
    std::vector<double> windows;          // one RMS per block, to see the sound move
    std::vector<float> kept;              // the audio itself, for comparing one bar with the next

    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (touch) touch ((double) b * kBlock / kSr);
        p.processBlock (buf, midi);

        for (int c = 0; c < 2; ++c)
        {
            const auto* x = buf.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = x[i];
                if (! std::isfinite (v)) { s.clipped = 1 << 20; continue; }
                const float a = std::fabs (v);
                s.peak = std::max (s.peak, a);
                if (a > 0.999f) ++s.clipped;
                sumSq += (double) v * v;
                sum += v;
                (c == 0 ? sumSqL : sumSqR) += (double) v * v;
                ++n;
                if (b < blocks / 2) { firstSq += (double) v * v; ++firstN; }
                else                { lastSq  += (double) v * v; ++lastN; }
            }
        }
        {
            double w = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const auto* x = buf.getReadPointer (c);
                for (int i = 0; i < kBlock; ++i) w += (double) x[i] * x[i];
            }
            windows.push_back (std::sqrt (w / (double) (kBlock * 2)));
        }
        {
            const auto* x = buf.getReadPointer (0);
            kept.insert (kept.end(), x, x + kBlock);
        }
        if (t.playing) t.ppq += (double) kBlock * t.bpm / (60.0 * kSr);
    }

    // the spread of the windows, in dB: how much the sound moves while it plays
    if (windows.size() > 4)
    {
        double mean = 0.0;
        int counted = 0;
        for (double w : windows) if (w > 1.0e-6) { mean += 20.0 * std::log10 (w); ++counted; }
        if (counted > 2)
        {
            mean /= (double) counted;
            double var = 0.0;
            for (double w : windows) if (w > 1.0e-6) { const double d = 20.0 * std::log10 (w) - mean; var += d * d; }
            s.movement = (float) std::sqrt (var / (double) counted);
        }
    }

    // one bar against the next, at 120 bpm: two seconds
    {
        const size_t bar = (size_t) (2.0 * kSr);
        if (kept.size() >= bar * 3)
        {
            const float* a1 = kept.data() + bar;          // the second bar, once it is going
            const float* a2 = kept.data() + bar * 2;      // and the third
            double dot = 0.0, e1 = 0.0, e2 = 0.0;
            for (size_t i = 0; i < bar; ++i) { dot += (double) a1[i] * a2[i]; e1 += (double) a1[i] * a1[i]; e2 += (double) a2[i] * a2[i]; }
            s.barToBar = (e1 > 1.0e-9 && e2 > 1.0e-9) ? (float) (dot / std::sqrt (e1 * e2)) : 1.0f;
        }
    }

    s.rms = n ? (float) std::sqrt (sumSq / (double) n) : 0.0f;
    s.rmsL = n ? (float) std::sqrt (sumSqL / (double) (n / 2)) : 0.0f;
    s.rmsR = n ? (float) std::sqrt (sumSqR / (double) (n / 2)) : 0.0f;
    s.dc = n ? (float) (sum / (double) n) : 0.0f;
    s.firstHalfRms = firstN ? (float) std::sqrt (firstSq / (double) firstN) : 0.0f;
    s.lastHalfRms = lastN ? (float) std::sqrt (lastSq / (double) lastN) : 0.0f;
    return s;
}

// a plain tuned tone, so what is heard is the plugin and not the sample
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
        const double tt = (double) i / kSr;
        const double env = std::min (1.0, tt / 0.005) * (0.6 + 0.4 * std::exp (-tt * 1.5));
        z.l[(size_t) i] = (float) (0.5 * env * (std::sin (6.283185307179586 * hz * tt) + 0.25 * std::sin (12.566370614359172 * hz * tt)));
    }
    z.r = z.l;
    z.holdAt = (int) (0.3 * kSr);
    z.loopStart = (int) (0.4 * kSr);
    z.loopEnd = (int) (0.4 * kSr + std::round (kSr / hz) * 20.0);
    return z;
}

static juce::File writeInstrument (const juce::File& dir, const juce::String& name, float root)
{
    njq::InstrumentFile f;
    f.name = name;
    f.madeWith = "test_audio";
    f.mode = 0;            // tuned
    // Played once, not looped. A looping sustain rings on for ever, so any two bars of it correlate
    // perfectly whatever the notes do, and a test that compares one bar with the next would see nothing.
    f.sustain = 0;
    f.releaseMs = 120.0f;
    auto z = tone (name, root);
    z.lowKey = 0; z.highKey = 127;
    f.zones.push_back (z);
    const auto file = dir.getChildFile (name + ".njqi");
    juce::String err;
    njq::saveInstrument (file, f, err);
    return file;
}

// a processor playing something, with one instrument on voice 1
static std::unique_ptr<MjqProcessor> playing (const juce::File& instrument, uint32_t seed = 1234)
{
    auto p = std::make_unique<MjqProcessor>();
    p->prepareToPlay (kSr, kBlock);
    p->line.numSteps = 16;
    p->line.traffic = 1.0f;          // every step sounds, so a change is easy to hear
    p->line.gate = 1.0f;
    mjq::generate (p->line, seed);
    p->lineChanged();
    juce::String err;
    p->openInstrument (0, instrument, err);
    return p;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mjq_audio_test");
    dir.deleteRecursively();
    dir.createDirectory();

    const auto insA = writeInstrument (dir, "Tone A", 60.0f);
    const auto insB = writeInstrument (dir, "Tone B", 48.0f);

    // ------------------------------------------------------------------ it makes a sound at all
    std::printf ("\nit plays\n");
    {
        Transport t;
        auto p = playing (insA);
        const auto s = listen (*p, t, 2.0);
        CHECK (! s.silent(), "the plugin makes a sound (peak %.3f, %.1f dB)", s.peak, s.db());
        CHECK (s.clipped == 0, "without clipping (%d samples over)", s.clipped);
        CHECK (std::fabs (s.dc) < 0.01f, "and without a DC offset (%.5f)", s.dc);
    }

    // ------------------------------------------------------------------ the room
    std::printf ("\nthe room\n");
    {
        Transport t1, t2;
        auto dry = playing (insA);
        const auto before = listen (*dry, t1, 3.0);

        auto wet = playing (insA);
        wet->session.places[0].on = true;          // straight on, nothing else touched
        wet->lineChanged();
        const auto after = listen (*wet, t2, 3.0);

        const float change = after.db() - before.db();
        CHECK (! after.silent(), "a voice put in the room is still there (%.1f dB)", after.db());
        // the promise the room makes is that going back sounds further away, not quieter. Turning it on at
        // the default place, which is near the front, must therefore not move the level.
        CHECK (std::fabs (change) < 1.5f, "turning the room on does not change the level at the default place (%+.1f dB)", change);
    }
    {
        Transport tl, tr;
        auto left = playing (insA);
        left->session.places[0].on = true;
        left->session.places[0].x = -1.0f;
        left->lineChanged();
        const auto L = listen (*left, tl, 3.0);

        auto right = playing (insA);
        right->session.places[0].on = true;
        right->session.places[0].x = 1.0f;
        right->lineChanged();
        const auto R = listen (*right, tr, 3.0);

        CHECK (L.balanceDb() > 12.0f, "hard left really is on the left (%.1f dB of it)", L.balanceDb());
        CHECK (R.balanceDb() < -12.0f, "hard right really is on the right (%.1f dB)", R.balanceDb());
        CHECK (std::fabs (dbOf (L.rms) - dbOf (R.rms)) < 1.0f, "and the two sides weigh the same");
    }
    {
        Transport tn, tf;
        auto near = playing (insA);
        near->session.places[0].on = true;
        near->session.places[0].y = 0.0f;
        near->lineChanged();
        const auto N = listen (*near, tn, 3.0);

        auto far = playing (insA);
        far->session.places[0].on = true;
        far->session.places[0].y = 1.0f;
        far->lineChanged();
        const auto F = listen (*far, tf, 3.0);

        const float away = N.db() - F.db();
        CHECK (away > 6.0f && away < 24.0f, "the back of the room is further away, and still in the mix (%.1f dB down)", away);
    }
    {
        // a voice moving on a drawn curve must not click: no sample may jump further than the sound itself does
        Transport t;
        auto p = playing (insA);
        auto& place = p->session.places[0];
        place.on = true;
        place.move = mjq::RoomMove::LeftRight;
        place.depth = 1.0f;
        place.bars = 1;
        for (int i = 0; i < mjq::kRoomCurve; ++i)
            place.curve[(size_t) i] = (i < mjq::kRoomCurve / 2) ? -1.0f : 1.0f;    // a hard jump, twice a bar
        p->lineChanged();
        const auto s = listen (*p, t, 4.0);
        CHECK (! s.silent() && s.clipped == 0, "a voice thrown across the room does not clip (%d over)", s.clipped);
        CHECK (std::isfinite (s.rms), "and stays a number");
    }

    // ------------------------------------------------------------------ the Orb
    std::printf ("\nthe Orb\n");
    {
        // the obvious way to use it: play something, put it in a corner, change it, put that in another
        Transport t;
        auto p = playing (insA, 11);

        p->putOrbCorner (0, 0);
        p->line.dialect = 1.0f;
        p->line.gate = 0.25f;
        mjq::generate (p->line, 999);          // a different figure
        p->lineChanged();
        p->putOrbCorner (0, 4);
        p->setOrbOn (0, true);

        p->recallOrbCorner (0, 0);
        const auto atOne = listen (*p, t, 3.0);
        p->recallOrbCorner (0, 4);
        const auto atTwo = listen (*p, t, 3.0);

        CHECK (! atOne.silent() && ! atTwo.silent(), "both corners play");
        const float moved = std::fabs (atOne.db() - atTwo.db());
        CHECK (moved > 1.0f, "and the two corners do not sound the same (%.1f dB apart)", moved);
    }
    {
        // One button, and the Orb has somewhere to go. This is the thing a person does first, and it has
        // to produce a real morph rather than eight copies of the same line.
        Transport t;
        auto p = playing (insA, 77);
        p->scatterOrb (0);
        p->recallOrbCorner (0, 0);
        const auto one = listen (*p, t, 3.0);
        const uint64_t notesOne = p->notesPlayed.load();
        p->recallOrbCorner (0, 4);
        const auto two = listen (*p, t, 3.0);
        const uint64_t notesTwo = p->notesPlayed.load() - notesOne;

        CHECK (! one.silent() && ! two.silent(), "filling the Orb with variations plays at both corners");
        const double spread = (double) notesTwo / (double) std::max<uint64_t> (1, notesOne);
        CHECK (std::fabs (one.db() - two.db()) > 1.0f || spread < 0.8 || spread > 1.25,
               "and opposite corners really are different places (%llu notes against %llu, %.1f against %.1f dB)",
               (unsigned long long) notesOne, (unsigned long long) notesTwo, one.db(), two.db());
    }
    {
        // the effect stack must never run away, however it is driven
        Transport t;
        auto p = playing (insA, 5);
        p->putOrbCorner (0, 0);
        p->putOrbCorner (0, 4);
        auto& orb = p->session.orbs[0];
        orb.on = true;
        for (int s = 0; s < mjq::kOrbStack; ++s)
        {
            orb.corners[0].stack[(size_t) s].fx = s;             // the first four of the library
            orb.corners[0].stack[(size_t) s].mix = 1.0f;
            orb.corners[4].stack[(size_t) s].fx = s + 4;
            orb.corners[4].stack[(size_t) s].mix = 1.0f;
        }
        p->lineChanged();

        // the joystick swept back and forth the whole time, which is the worst case
        const auto s = listen (*p, t, 12.0, [&p] (double sec)
        {
            const float a = (float) std::sin (sec * 2.2);
            p->moveOrb (a, (float) std::cos (sec * 1.7));
        });

        CHECK (std::isfinite (s.rms) && std::isfinite (s.peak), "a swept effect stack stays a number");
        CHECK (s.peak < 4.0f, "and does not run away (peak %.2f)", s.peak);
        const float growth = dbOf (s.lastHalfRms) - dbOf (s.firstHalfRms);
        CHECK (growth < 12.0f, "and is no louder at the end than the start (%+.1f dB)", growth);
    }
    {
        // every effect in the library, on its own, driven continuously: none may run away
        int ranAway = 0;
        juce::String worst;
        float worstPeak = 0.0f;
        const int howMany = (int) nb::matrixFxInfo().size();
        for (int fx = 0; fx < howMany; ++fx)
        {
            Transport t;
            auto p = playing (insA, 3);
            p->putOrbCorner (0, 0);
            auto& orb = p->session.orbs[0];
            orb.on = true;
            orb.corners[0].stack[0].fx = fx;
            orb.corners[0].stack[0].mix = 1.0f;
            p->lineChanged();
            p->recallOrbCorner (0, 0);
            const auto s = listen (*p, t, 6.0);
            const bool bad = ! std::isfinite (s.peak) || s.peak > 4.0f
                           || (dbOf (s.lastHalfRms) - dbOf (s.firstHalfRms)) > 15.0f;
            if (bad)
            {
                ++ranAway;
                if (s.peak > worstPeak) { worstPeak = s.peak; worst = nb::matrixFxInfo()[(size_t) fx].name; }
            }
        }
        CHECK (ranAway == 0, "all %d effects behave when driven continuously (%d ran away, worst %s at %.1f)",
               howMany, ranAway, worst.isEmpty() ? "none" : worst.toRawUTF8(), worstPeak);
    }

    // ------------------------------------------------------------------ the randomiser
    std::printf ("\nthe randomiser\n");
    {
        Transport t1, t2;
        auto still = playing (insA, 21);
        still->line.traffic = 0.25f;          // a quarter of the steps, and it stays there
        still->lineChanged();
        const auto before = listen (*still, t1, 6.0);
        const uint64_t stillNotes = still->notesPlayed.load();

        auto moving = playing (insA, 21);
        moving->line.traffic = 0.25f;
        auto& c = moving->session.randomisers[0].cycles[(size_t) mjq::Cycled::Traffic];
        c.on = true;
        c.home = 0.25f;           // where it was left, and where it lands when switched off
        c.extremity = 0.45f;      // a good swing either side of where it was left
        c.rate = 1.0f;
        c.seed = 4;
        moving->lineChanged();
        const auto after = listen (*moving, t2, 6.0);
        const uint64_t movingNotes = moving->notesPlayed.load();

        CHECK (! after.silent(), "a cycling TRAFFIC still plays (%.1f dB)", after.db());
        // a cycle swinging right across its range averages out to the static setting, so the average level
        // cannot tell them apart: what says it is cycling is that the sound keeps changing as it plays
        // a line with nothing moving plays the same bar for ever; a cycling knob must break that, which is
        // the whole point of it. The average level cannot see this and neither can the spread, because the
        // instrument's own envelope swings far more than the knob does.
        // TRAFFIC decides how many of the steps sound, so the honest measure is how many notes were
        // actually played - not the level, which averages out, and not how alike one bar is to the next,
        // which an instrument's own ringing can hold at 1 whatever the notes do.
        CHECK (stillNotes > 0 && movingNotes > 0, "both play notes (%llu still, %llu cycling)",
               (unsigned long long) stillNotes, (unsigned long long) movingNotes);
        const double ratio = (double) movingNotes / (double) std::max<uint64_t> (1, stillNotes);
        CHECK (ratio < 0.8 || ratio > 1.25,
               "and cycling TRAFFIC really changes how many notes play (%llu against %llu)",
               (unsigned long long) movingNotes, (unsigned long long) stillNotes);
    }
    {
        // switching it off must land back where it was: the same sound as never having touched it
        Transport t1, t2;
        auto plain = playing (insA, 33);
        plain->line.gate = 0.8f;
        plain->lineChanged();
        const auto before = listen (*plain, t1, 4.0);

        auto toggled = playing (insA, 33);
        toggled->line.gate = 0.8f;
        auto& c = toggled->session.randomisers[0].cycles[(size_t) mjq::Cycled::Gate];
        c.on = true; c.home = 0.8f; c.extremity = 1.0f; c.rate = 2.0f;
        toggled->lineChanged();
        (void) listen (*toggled, t2, 2.0);
        c.on = false;
        toggled->lineChanged();
        const auto after = listen (*toggled, t2, 4.0);

        CHECK (std::fabs (after.db() - before.db()) < 0.75f,
               "switching a cycle off sounds like never having used it (%.1f against %.1f dB)", after.db(), before.db());
    }

    // ------------------------------------------------------------------ the multi
    std::printf ("\nthe multi\n");
    {
        Transport t1, t2;
        auto one = playing (insA, 8);
        const auto single = listen (*one, t1, 3.0);

        auto two = playing (insA, 8);
        juce::String err;
        two->openInstrument (1, insB, err);
        for (int p = 0; p < mjq::kSessionSlots; ++p)
        {
            auto& tr = two->session.slots[(size_t) p].tracks[1];
            tr.enabled = true;
            tr.lowKey = 0; tr.highKey = 127;
            tr.line = two->session.slots[(size_t) p].tracks[0].line;
            tr.line.base = 36;
        }
        two->lineChanged();
        const auto both = listen (*two, t2, 3.0);

        CHECK (! both.silent(), "a second voice with its own instrument plays (%.1f dB)", both.db());
        CHECK (both.db() - single.db() > 1.5f,
               "and stacking it is plainly audible (%+.1f dB over one voice)", both.db() - single.db());
    }

    dir.deleteRecursively();
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
