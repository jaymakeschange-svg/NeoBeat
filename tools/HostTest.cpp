// MidiJaQ host test: the plugin driven as a DAW would drive it, with its editor open — the MIDI it
// sends, the editing in the grid, undo, and the line in the project. Run under Xvfb.
// Usage: MidiJaQHostTest [seed]
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "neobeat/shared/NjqInstrument.h"
#include "app/Editor.h"

static int failures = 0;
#define CHECK(c, ...) do { char m_[400]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } std::fflush (stdout); } while (0)

// A host that says where the song is.
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

// what a host does: audio blocks, and the message loop in between
static float lastPeak = 0.0f;                     // how loud the plugin's own instrument played

static std::vector<juce::MidiMessage> run (MjqProcessor& p, Transport& t, int blocks, int blockSize = 512)
{
    std::vector<juce::MidiMessage> out;
    juce::AudioBuffer<float> buf (2, blockSize);
    lastPeak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        lastPeak = std::max (lastPeak, buf.getMagnitude (0, blockSize));
        for (const auto m : midi) out.push_back (m.getMessage());
        if (t.playing) t.ppq += (double) blockSize * t.bpm / (60.0 * 48000.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    return out;
}
static int noteOns (const std::vector<juce::MidiMessage>& v)
{
    int n = 0; for (const auto& m : v) if (m.isNoteOn()) ++n; return n;
}
static int noteOffs (const std::vector<juce::MidiMessage>& v)
{
    int n = 0; for (const auto& m : v) if (m.isNoteOff()) ++n; return n;
}

// the real mouse handlers, with events built as JUCE builds them
static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downAt, juce::ModifierKeys mods, int clicks, bool dragged)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downAt, now, clicks, dragged);
}
static void drag (juce::Component& c, const std::vector<juce::Point<float>>& pts, juce::ModifierKeys mods = {})
{
    const auto m = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (mouseAt (c, pts.front(), pts.front(), m, 1, false));
    for (size_t k = 1; k < pts.size(); ++k) c.mouseDrag (mouseAt (c, pts[k], pts.front(), m, 1, true));
    c.mouseUp (mouseAt (c, pts.back(), pts.front(), mods, 1, pts.size() > 1));
}
static void click (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}) { drag (c, { p }, mods); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int seed = argc > 1 ? std::atoi (argv[1]) : 1;
    juce::Random r ((juce::int64) seed);

    auto proc = std::make_unique<MjqProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    Transport host;
    proc->setPlayHead (&host);

    std::printf ("1. it sends MIDI as the host plays\n");
    {
        proc->line.numSteps = 16; proc->line.traffic = 1.0f; proc->line.perBar = 16;
        mjq::generate (proc->line, 2026);
        proc->lineChanged();
        host.ppq = 0.0; host.playing = true;
        const auto m = run (*proc, host, 187);                      // just under a bar at 120 bpm
        const int wanted = (int) proc->pass()->events.size();     // 16 steps, plus the hits of any rolled one
        CHECK (noteOns (m) == wanted && noteOffs (m) >= wanted - 1, "a bar sends every note of the line (%d of %d) and lets them go again (%d)",
               noteOns (m), wanted, noteOffs (m));

        bool inRange = true, channel1 = true;
        for (const auto& x : m)
            if (x.isNoteOnOrOff()) { inRange = inRange && x.getNoteNumber() >= 0 && x.getNoteNumber() <= 127; channel1 = channel1 && x.getChannel() == 1; }
        CHECK (inRange && channel1, "every one of them a real note on channel 1");

        host.playing = false;
        const auto stop = run (*proc, host, 3);
        CHECK (noteOns (stop) == 0, "and the transport stopping sends no more");
    }

    std::printf ("2. the window\n");
    std::unique_ptr<MjqEditor> ed (dynamic_cast<MjqEditor*> (proc->createEditor()));
    CHECK (ed != nullptr, "the editor opens");
    if (ed == nullptr) { std::printf ("FAIL: %d failure(s)\n", ++failures); return 1; }
    ed->setBounds (0, 0, 1120, 480);            // no desktop window: the test drives the components themselves
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

    {
        const int before = proc->version.load();
        auto before3 = proc->line.steps[3];
        // the grid: the accent row of step 4 (the rows start under the ruler, 34 apart)
        auto& grid = *ed->getChildComponent (0);
        juce::ignoreUnused (grid);
        bool found = false;
        for (int i = 0; i < ed->getNumChildComponents(); ++i)
            if (auto* c = ed->getChildComponent (i))
                if (c->getWidth() > 600 && c->getHeight() > 200)
                {
                    const float w = (float) (c->getWidth() - 96) / 16.0f;      // the grid: 96 for the row names
                    const float rowH = (float) (c->getHeight() - 18) / (float) mjq::numLanes;
                    click (*c, { 96.0f + w * 3.5f, 18.0f + rowH * 2.5f });      // the Accent row, step 4
                    found = true;
                    break;
                }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        CHECK (found && proc->version.load() > before && proc->line.steps[3].accent != before3.accent,
               "a click in the grid changes that step's accent (%d to %d)", (int) before3.accent, (int) proc->line.steps[3].accent);

        CHECK (proc->undo() && proc->line.steps[3].accent == before3.accent, "and undo puts it back");
        CHECK (proc->redo() && proc->line.steps[3].accent != before3.accent, "redo again");
    }

    std::printf ("3. JAQ, and the locks\n");
    {
        const auto before = proc->line.steps;
        proc->line.locked[(size_t) mjq::Lane::Rhythm] = true;
        proc->roll();
        bool groove = true, notes = false;
        for (int i = 0; i < mjq::kMaxSteps; ++i)
        {
            groove = groove && proc->line.steps[(size_t) i].weight == before[(size_t) i].weight;
            notes = notes || proc->line.steps[(size_t) i].freeDegree != before[(size_t) i].freeDegree;
        }
        CHECK (groove && notes, "JAQ writes new notes over the locked groove");
        proc->line.locked[(size_t) mjq::Lane::Rhythm] = false;
    }

    std::printf ("4. the line in the project\n");
    {
        proc->line.dialect = 0.8f; proc->line.scaleIndex = 10; proc->line.numSteps = 11;
        proc->line.mode = mjq::Mode::Follow; proc->clockMode = mjq::ClockMode::Song;
        proc->lineChanged();

        juce::MemoryBlock state;
        proc->getStateInformation (state);
        auto other = std::make_unique<MjqProcessor>();
        other->setPlayConfigDetails (0, 2, 48000.0, 512);
        other->prepareToPlay (48000.0, 512);
        other->setStateInformation (state.getData(), (int) state.getSize());

        bool same = other->line.numSteps == proc->line.numSteps && other->line.scaleIndex == proc->line.scaleIndex
                 && std::fabs (other->line.dialect - proc->line.dialect) < 1e-6 && other->line.mode == proc->line.mode
                 && other->clockMode == proc->clockMode && other->line.seed == proc->line.seed;
        for (int i = 0; same && i < mjq::kMaxSteps; ++i)
        {
            const auto &a = other->line.steps[(size_t) i], &b = proc->line.steps[(size_t) i];
            same = a.weight == b.weight && a.freeDegree == b.freeDegree && a.acidDegree == b.acidDegree
                && a.dialectAt == b.dialectAt && a.accent == b.accent && a.gate == b.gate && a.octave == b.octave
                && a.roll == b.roll && a.slide == b.slide;
        }
        CHECK (same, "the project keeps the line, its knobs and its modes (%d bytes)", (int) state.getSize());

        const auto mine = mjq::readPass (proc->line);
        const auto theirs = mjq::readPass (other->line);
        bool plays = mine.events.size() == theirs.events.size();
        for (size_t i = 0; plays && i < mine.events.size(); ++i) plays = mine.events[i].note == theirs.events[i].note && mine.events[i].start == theirs.events[i].start;
        CHECK (plays, "and plays the same notes when it is opened again");
        other->releaseResources();
    }

    std::printf ("5. a .NJQ instrument from Batchcut\n");
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mjq_host_inst");
    {
        dir.deleteRecursively(); dir.createDirectory();
        // an instrument as Batchcut's KEYS room writes them
        njq::InstrumentFile f;
        f.name = "Host tones"; f.mode = 0; f.sustain = 0; f.releaseMs = 150.0f;
        for (int k = 0; k < 3; ++k)
        {
            njq::Zone z;
            z.name = "tone " + juce::String (k); z.sampleRate = 48000.0; z.root = (float) (48 + 12 * k); z.tuned = true;
            z.lowKey = 24 + 12 * k; z.highKey = 35 + 12 * k;
            const double hz = 440.0 * std::exp2 ((z.root - 69.0f) / 12.0);
            z.l.assign (24000, 0.0f);
            for (int i = 0; i < 24000; ++i) z.l[(size_t) i] = (float) (0.5 * std::sin (6.283185307179586 * hz * i / 48000.0) * std::exp (-i / 20000.0));
            z.r = z.l;
            f.zones.push_back (std::move (z));
        }
        const auto file = dir.getChildFile ("Host tones.njq");
        juce::String err;
        CHECK (njq::saveInstrument (file, f, err), "an instrument written for the test %s", err.toRawUTF8());

        proc->line.numSteps = 16; proc->line.traffic = 1.0f; proc->line.transpose = 0; proc->line.base = 36;
        proc->line.mode = mjq::Mode::Free; proc->clockMode = mjq::ClockMode::Pattern;
        mjq::generate (proc->line, 99);
        proc->lineChanged();
        host.playing = true; host.ppq = 0.0;
        run (*proc, host, 20);
        CHECK (lastPeak < 1.0e-6f, "with no instrument the plugin makes no sound of its own (%.6f)", lastPeak);

        const bool opened = proc->openInstrument (file, err);
        CHECK (opened && proc->instrument() != nullptr, "it opens the instrument: %s", proc->message.toRawUTF8());
        run (*proc, host, 100);
        CHECK (lastPeak > 0.01f, "and now plays its own line out loud (peak %.2f)", lastPeak);

        // the project remembers where the instrument came from, and opens it again
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        auto other = std::make_unique<MjqProcessor>();
        other->setPlayConfigDetails (0, 2, 48000.0, 512);
        other->prepareToPlay (48000.0, 512);
        other->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (other->instrument() != nullptr && other->instrument()->zones.size() == 3,
               "a project opened again brings the instrument back (%d sounds)", other->instrument() == nullptr ? 0 : (int) other->instrument()->zones.size());
        other->releaseResources();

        // and a sequence is refused as an instrument, with a message that says what it is
        njq::Kit kit; kit.name = "not an instrument";
        const auto kitFile = dir.getChildFile ("kit.njq");
        juce::String e2;
        njq::save (kitFile, kit, e2);
        const bool refused = ! proc->openInstrument (kitFile, e2);
        const bool kept = proc->instrument() != nullptr && proc->message.contains ("not an instrument");
        CHECK (refused && kept, "a NeoBeat sequence is refused and the one that was open stays: \"%s\"", proc->message.toRawUTF8());

        proc->closeInstrument();
        run (*proc, host, 40);                       // what was sounding dies away over its release
        run (*proc, host, 20);
        CHECK (proc->instrument() == nullptr && lastPeak < 1.0e-6f, "and it can be put away again (%.6f)", lastPeak);
    }

    std::printf ("6. four lines, slots and .NJQ patterns\n");
    {
        proc->chooseSlot (0);
        proc->chooseTrack (0);
        proc->line.mode = mjq::Mode::Free;
        proc->line.traffic = 1.0f;
        proc->lineChanged();
        proc->chooseTrack (1);
        proc->changeTrackEnabled (true);
        proc->line.mode = mjq::Mode::Parts;
        proc->line.traffic = 1.0f;
        proc->lineChanged();
        mjq::Chord c;
        c.root = 0; c.kind = mjq::ChordKind::Major;
        proc->changeChord (0, c);
        host.playing = true; host.ppq = 0.0; proc->clockMode = mjq::ClockMode::Song;
        const auto together = run (*proc, host, 80);
        bool first = false, second = false;
        for (const auto& m : together)
            if (m.isNoteOn()) { first |= m.getChannel() == 1; second |= m.getChannel() == 2; }
        CHECK (first && second, "two lines play at once on their own MIDI channels");

        proc->chooseSlot (5);
        proc->chooseTrack (1);
        proc->line.mode = mjq::Mode::Bass;
        proc->line.traffic = 1.0f;
        proc->changeTrackEnabled (true);
        proc->lineChanged();
        proc->changeChain (2);
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        auto copy = std::make_unique<MjqProcessor>();
        copy->setPlayConfigDetails (0, 2, 48000.0, 512);
        copy->prepareToPlay (48000.0, 512);
        copy->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (copy->session.selected == 5 && copy->session.chainLength == 2
               && copy->session.current().tracks[1].enabled
               && copy->session.current().tracks[1].line.mode == mjq::Mode::Bass,
               "a saved project keeps its slots and chord voices (%d bytes)", (int) state.getSize());

        const auto pattern = dir.getChildFile ("MidiJaQ session.njq");
        juce::String error;
        const bool saved = proc->savePatterns (pattern, error);
        const bool loaded = saved && copy->loadPatterns (pattern, error);
        CHECK (saved && loaded && copy->session.selected == 5 && copy->session.chainLength == 2,
               "a .NJQ file saves and opens the pattern bank: %s", error.toRawUTF8());
        copy->releaseResources();
        proc->chooseTrack (0); proc->chooseSlot (0); proc->changeChain (1);
        proc->clockMode = mjq::ClockMode::Pattern;
    }

    std::printf ("7. a long random session (seed %d)\n", seed);
    {
        proc->line.mode = mjq::Mode::Free;
        for (int i = 0; i < 400; ++i)
        {
            switch (r.nextInt (13))
            {
                case 0: proc->roll(); break;
                case 1: proc->rollLane ((mjq::Lane) r.nextInt (mjq::numLanes)); break;
                case 2: proc->line.locked[(size_t) r.nextInt (mjq::numLanes)] = r.nextBool(); break;
                case 3: proc->line.dialect = r.nextFloat(); proc->line.traffic = r.nextFloat(); proc->lineChanged(); break;
                case 4: proc->line.numSteps = 1 + r.nextInt (mjq::kMaxSteps); proc->line.offset = r.nextInt (40) - 8; proc->lineChanged(); break;
                case 5: proc->line.perBar = (const int[]) { 8, 12, 16, 24 }[r.nextInt (4)]; proc->line.swing = 0.5f + 0.25f * r.nextFloat(); proc->lineChanged(); break;
                case 6: proc->line.scaleIndex = r.nextInt (gen::numScales()); proc->line.root = r.nextInt (12); proc->line.transpose = r.nextInt (97) - 48; proc->lineChanged(); break;
                case 7: proc->line.mode = (mjq::Mode) r.nextInt (3); proc->clockMode = (mjq::ClockMode) r.nextInt (3); proc->lineChanged(); break;
                case 8: proc->line.direction = (mjq::Direction) r.nextInt (3); proc->line.gate = 0.1f + 1.9f * r.nextFloat(); proc->lineChanged(); break;
                case 9: if (r.nextBool()) proc->undo(); else proc->redo(); break;
                case 11:
                {
                    if (r.nextBool()) proc->closeInstrument();
                    else { juce::String e; proc->openInstrument (dir.getChildFile ("Host tones.njq"), e); }
                    break;
                }
                case 10:
                {
                    host.playing = r.nextBool();
                    host.bpm = 40.0 + 200.0 * r.nextFloat();
                    if (r.nextBool()) host.ppq = r.nextFloat() * 400.0;
                    juce::AudioBuffer<float> buf (2, 1 + r.nextInt (1024));
                    juce::MidiBuffer midi;
                    for (int k = 0; k < r.nextInt (4); ++k)
                        midi.addEvent (r.nextBool() ? juce::MidiMessage::noteOn (1, 24 + r.nextInt (80), (juce::uint8) (1 + r.nextInt (127)))
                                                    : juce::MidiMessage::noteOff (1, 24 + r.nextInt (80)), r.nextInt (buf.getNumSamples()));
                    proc->processBlock (buf, midi);
                    bool sane = true;
                    for (const auto m : midi)
                    {
                        const auto msg = m.getMessage();
                        sane = sane && m.samplePosition >= 0 && m.samplePosition < buf.getNumSamples()
                                    && (! msg.isNoteOnOrOff() || (msg.getNoteNumber() >= 0 && msg.getNoteNumber() <= 127));
                    }
                    if (! sane) { CHECK (false, "every event inside the block and in range (step %d)", i); i = 10000; }
                    break;
                }
                default:
                {
                    const auto p = proc->pass();
                    if (p != nullptr) for (const auto& e : p->events) if (e.note < 0 || e.note > 127 || e.start < 0.0 || e.length <= 0.0) { CHECK (false, "a sane pass (step %d)", i); i = 10000; break; }
                    break;
                }
            }
            if (i % 40 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        CHECK (true, "400 random steps with the window open, and nothing came out wrong");
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        CHECK (state.getSize() > 100, "and it still saves (%d bytes)", (int) state.getSize());
    }

    ed.reset();
    dir.deleteRecursively();
    proc->setPlayHead (nullptr);
    proc->releaseResources();
    proc.reset();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
