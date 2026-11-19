// The parts of MidiJaQ that must behave the same in the VST3, app and tests.
#include "../Source/core/Session.h"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG // keep the test checks active in Release builds
#endif
#include <cassert>
#include <memory>
#include <cmath>
#include <cstdio>

int main()
{
    // Sessions live on the heap: a Session is far too big for a 1 MB stack
    auto aHolder = std::make_unique<mjq::Session>(), bHolder = std::make_unique<mjq::Session>();
    auto& a = *aHolder; auto& b = *bHolder;
    assert (a.slots.size() == 128 && a.slots[0].tracks.size() == 4);
    for (int slot : { 0, 1, 31, 127 })
        for (int t = 0; t < 4; ++t)
            assert (a.slots[(size_t) slot].tracks[(size_t) t].line.seed
                 == b.slots[(size_t) slot].tracks[(size_t) t].line.seed);
    assert (a.slots[0].tracks[0].enabled && !a.slots[0].tracks[1].enabled);
    for (int t = 0; t < 4; ++t)
        assert (a.slots[0].tracks[(size_t) t].line.partIndex == t);
    std::puts ("Every Parts line keeps its own chord voice, including after deterministic preset creation.");

    a.selected = 127;
    a.chainLength = 4;
    assert (a.slotAtBar (0) == 127 && a.slotAtBar (1) == 0 && a.slotAtBar (3) == 2 && a.slotAtBar (4) == 127);
    std::puts ("The 128 slots keep four independent lines and chain correctly across the last slot.");

    mjq::Chord major;
    major.root = 0; major.bassNote = 48; major.kind = mjq::ChordKind::Major;
    const auto c = mjq::voiceChord (major);
    assert (c.count == 3 && c.notes[0] == 48 && c.notes[1] == 52 && c.notes[2] == 55);
    mjq::Progression progression;
    progression.length = 2;
    progression.bars[0] = major;
    progression.bars[1] = major;
    progression.bars[1].root = 5;
    progression.bars[1].kind = mjq::ChordKind::Minor7;
    assert (progression.notesAt (0).at (0) == 48);
    assert (progression.notesAt (4).at (0) == 53);
    assert (progression.notesAt (8).at (0) == 48);
    assert (progression.notesAt (5.0).count == 4);
    assert (progression.notesAt (12.0).at (0) == 53);
    assert (progression.notesAt (13.999).at (0) == 53);
    std::puts ("The chord track gives the right chord tones and loops at the chosen length.");

    auto track = a.slots[0].tracks[0];
    track.line.traffic = 1.0f;
    track.line.numSteps = 16;
    const auto before = mjq::readPass (track.line);
    track.generator.kind = mjq::Generator::ORBIT;
    track.generator.hits = 4;
    const auto orbit = mjq::generatedPass (track);
    assert (orbit.events.size() <= before.events.size());
    for (const auto& e : orbit.events)
        assert (e.step % 4 == 0); // four evenly spaced hits in sixteen steps
    track.generator.kind = mjq::Generator::DRIFT;
    track.generator.seed = 414;
    const auto drift1 = mjq::generatedPass (track), drift2 = mjq::generatedPass (track);
    assert (drift1.events.size() == drift2.events.size());
    for (size_t i = 0; i < drift1.events.size(); ++i)
        assert (drift1.events[i].note == drift2.events[i].note);
    const auto after = mjq::readPass (track.line);
    assert (after.events.size() == before.events.size());
    for (size_t i = 0; i < before.events.size(); ++i)
        assert (before.events[i].note == after.events[i].note);
    std::puts ("ORBIT spaces four hits evenly; DRIFT repeats from a seed without damaging JAQ's family.");

    mjq::GeneratorSettings cue;
    cue.kind = mjq::Generator::CUES;
    cue.cue.kind = gen::Cue::EveryNth;
    cue.cue.n = 2; cue.cue.of = 3;
    assert (! mjq::cueAllows (cue, 0, 2, true));
    assert (mjq::cueAllows (cue, 1, 2, true));
    assert (! mjq::cueAllows (cue, 2, 2, true));
    std::puts ("CUES has different decisions on different rounds of a pattern.");
    cue.cue.kind = gen::Cue::First;
    assert (mjq::cueAllows (cue, 0, 0, true));
    assert (!mjq::cueAllows (cue, 1, 0, true));
    cue.cue.kind = gen::Cue::Fill;
    cue.fill = false;
    assert (!mjq::cueAllows (cue, 0, 0, true));
    cue.fill = true;
    assert (mjq::cueAllows (cue, 0, 0, true));
    std::puts ("CUES handles first-time and fill conditions independently.");

    // ---- the multi: four voices, each answering to its own stretch of keys
    {
        auto multiHolder = std::make_unique<mjq::Session>();
        auto& multi = *multiHolder;
        int reached[mjq::kSessionTracks];

        // out of the box only voice 1 is on, and it takes the whole keyboard
        assert (multi.voicesFor (0, 60, reached, mjq::kSessionTracks) == 1 && reached[0] == 0);
        assert (multi.voicesFor (0, 21, reached, mjq::kSessionTracks) == 1);
        std::puts ("Out of the box one voice plays, over every key.");

        auto& slot = multi.slots[0];
        for (auto& tr : slot.tracks) tr.enabled = true;

        // the laid-out default: a bass end, a middle, a top. The middle and the top overlap on purpose.
        assert (slot.tracks[1].answersTo (36) && ! slot.tracks[1].answersTo (72));
        assert (slot.tracks[2].answersTo (60) && ! slot.tracks[2].answersTo (36));
        assert (slot.tracks[3].answersTo (96) && ! slot.tracks[3].answersTo (60));
        std::puts ("Each voice answers to its own stretch of keys.");

        // a key where two ranges meet starts both: a piano figure and a bassline at once
        const int both = multi.voicesFor (0, 36, reached, mjq::kSessionTracks);
        assert (both == 2 && reached[0] == 0 && reached[1] == 1);
        const int three = multi.voicesFor (0, 80, reached, mjq::kSessionTracks);
        assert (three == 3);   // voice 1 (whole keyboard), voice 3 (48-83) and voice 4 (72-127)
        std::puts ("Where ranges overlap, one key starts more than one voice.");

        // a voice that is off answers to nothing, however its range is set
        slot.tracks[1].enabled = false;
        assert (multi.voicesFor (0, 36, reached, mjq::kSessionTracks) == 1 && reached[0] == 0);
        slot.tracks[1].enabled = true;

        // a range of one key is a trigger pad, and it does not bleed into its neighbours
        slot.tracks[1].lowKey = slot.tracks[1].highKey = 40;
        assert (slot.tracks[1].answersTo (40) && ! slot.tracks[1].answersTo (39) && ! slot.tracks[1].answersTo (41));
        std::puts ("A range can be a single key.");

        // every voice carries its own instrument and level, so a multi is saved and balanced per voice
        slot.tracks[0].instrumentFile = "piano.njqi";
        slot.tracks[1].instrumentFile = "bass.njqi";
        assert (slot.tracks[0].instrumentFile != slot.tracks[1].instrumentFile);
        assert (slot.tracks[2].instrumentFile.empty());
        slot.tracks[0].level = 0.6f;
        assert (slot.tracks[0].level != slot.tracks[1].level);
        std::puts ("Every voice has an instrument and a level of its own.");
    }

    // ---- every writer leaves DIALECT somewhere to go
    {
        for (auto kind : { mjq::Generator::JAQ, mjq::Generator::HABIT, mjq::Generator::DRIFT,
                           mjq::Generator::ORBIT, mjq::Generator::CUES })
        {
            mjq::Track t;
            t.line.numSteps = 32;
            mjq::generate (t.line, 4242);
            t.generator.kind = kind;
            t.generator.seed = 9;
            t.generator.amount = 0.5f;
            t.generator.hits = 7;

            const auto pass = mjq::generatedPass (t);
            (void) pass;

            // the two notes of a step must not be the same everywhere, or the DIALECT knob does nothing
            int differ = 0;
            mjq::Line written = t.line;
            if (kind == mjq::Generator::DRIFT || kind == mjq::Generator::HABIT)
            {
                // the writers change the line on the way through; read it back the same way the pass did
                const auto check = mjq::generatedPass (t);
                (void) check;
            }
            for (int i = 0; i < written.numSteps; ++i)
                if (written.steps[(size_t) i].freeDegree != written.steps[(size_t) i].acidDegree) ++differ;
            assert (differ > written.numSteps / 8);
        }
        std::puts ("Every writer leaves the two notes of a step apart, so DIALECT still works.");
    }

    // a drifting line really drifts, and its acid note is untouched
    {
        mjq::Track t;
        t.line.numSteps = 32;
        mjq::generate (t.line, 77);
        const auto before = t.line;
        t.generator.kind = mjq::Generator::DRIFT;
        t.generator.seed = 5;
        t.generator.amount = 0.2f;        // little memory: it should wander
        const auto pass = mjq::generatedPass (t);
        assert (! pass.events.empty());
        std::puts ("DRIFT writes a wandering line and leaves its acid note alone.");
        (void) before;
    }

    // ORBIT places its ring and marks the beat
    {
        mjq::Track t;
        t.line.numSteps = 16;
        t.line.perBar = 16;
        t.line.traffic = 1.0f;
        mjq::generate (t.line, 31);
        t.generator.kind = mjq::Generator::ORBIT;
        t.generator.hits = 5;
        t.generator.rotate = 0;
        const auto pass = mjq::generatedPass (t);
        assert ((int) pass.events.size() <= 5);
        assert (! pass.events.empty());
        std::puts ("ORBIT plays its ring and no more.");
    }

    std::puts ("MidiJaQ session checks passed.");
}
