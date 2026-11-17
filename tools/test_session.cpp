// The parts of MidiJaQ that must behave the same in the VST3, app and tests.
#include "../Source/core/Session.h"
#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG // keep the test checks active in Release builds
#endif
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    mjq::Session a, b;
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
    std::puts ("MidiJaQ session checks passed.");
}
