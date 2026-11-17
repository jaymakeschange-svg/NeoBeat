#include "Processor.h"
#include "Editor.h"

MjqProcessor::MjqProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    line = session.current().tracks[0].line;
    rebuildSession (true);
    message = "JAQ writes a line; DIALECT moves its notes, TRAFFIC how many play.";
}

void MjqProcessor::rebuildSession (bool full)
{
    auto& track = session.current().tracks[(size_t) session.selectedTrack];
    track.line = line;
    track.line.partIndex = session.selectedTrack;
    session.touched[(size_t) session.selected] = true;
    auto old = std::atomic_load (&prepared);
    auto next = old != nullptr ? std::make_shared<Prepared> (*old) : std::make_shared<Prepared>();
    next->state = session;
    auto prepareTrack = [&] (int s, int t)
    {
        const auto& tr = next->state.slots[(size_t) s].tracks[(size_t) t];
        next->passes[(size_t) s][(size_t) t] = mjq::generatedPass (tr);
        auto& rounds = next->evolving[(size_t) s][(size_t) t];
        if (tr.generator.kind == mjq::Generator::HABIT || tr.generator.kind == mjq::Generator::DRIFT)
        {
            std::vector<mjq::Pass> prepared;
            prepared.reserve (mjq::kEvolutionRounds);
            prepared.push_back (next->passes[(size_t) s][(size_t) t]);
            for (int r = 1; r < mjq::kEvolutionRounds; ++r)
                prepared.push_back (mjq::generatedPass (tr, (uint64_t) r));
            rounds = std::make_shared<const std::vector<mjq::Pass>> (std::move (prepared));
        }
        else rounds.reset();
    };
    if (full || old == nullptr)
    {
        for (int s = 0; s < mjq::kSessionSlots; ++s)
            for (int t = 0; t < mjq::kSessionTracks; ++t) prepareTrack (s, t);
    }
    else
    {
        for (int t = 0; t < mjq::kSessionTracks; ++t) prepareTrack (session.selected, t);
    }
    const auto& preview = next->passes[(size_t) session.selected][(size_t) session.selectedTrack];
    std::atomic_store (&shown, std::shared_ptr<const mjq::Pass> (std::make_shared<mjq::Pass> (preview)));
    std::atomic_store (&prepared, std::shared_ptr<const Prepared> (std::move (next)));
    version.fetch_add (1);
}

void MjqProcessor::lineChanged() { rebuildSession(); }

void MjqProcessor::chooseSlot (int index)
{
    session.current().tracks[(size_t) session.selectedTrack].line = line;
    session.selected = juce::jlimit (0, mjq::kSessionSlots - 1, index);
    line = session.current().tracks[(size_t) session.selectedTrack].line;
    rebuildSession();
}

void MjqProcessor::chooseTrack (int index)
{
    session.current().tracks[(size_t) session.selectedTrack].line = line;
    session.selectedTrack = juce::jlimit (0, mjq::kSessionTracks - 1, index);
    line = session.current().tracks[(size_t) session.selectedTrack].line;
    line.partIndex = session.selectedTrack;
    rebuildSession();
}

void MjqProcessor::changeWriter (mjq::Generator writer)
{
    session.current().tracks[(size_t) session.selectedTrack].generator.kind = writer;
    rebuildSession();
}

void MjqProcessor::changeChord (int bar, const mjq::Chord& chord)
{
    session.current().chords.bars[(size_t) juce::jlimit (0, mjq::kChordBars - 1, bar)] = chord;
    rebuildSession();
}

void MjqProcessor::changeChain (int length)
{
    session.chainLength = juce::jlimit (1, mjq::kSessionSlots, length);
    rebuildSession();
}

void MjqProcessor::changeTrackEnabled (bool enabled)
{
    session.current().tracks[(size_t) session.selectedTrack].enabled = enabled;
    rebuildSession();
}

void MjqProcessor::setInstrumentLevel (float level)
{
    voices.level.store (level);
    for (auto& v : otherVoices) v.level.store (level);
}

void MjqProcessor::roll()
{
    mark();
    mjq::generate (line, (uint32_t) seeds.nextInt ({ 1, 1000000 }));
    lineChanged();
}

void MjqProcessor::rollLane (mjq::Lane lane)
{
    mark();
    mjq::writeLane (line, lane, (uint32_t) seeds.nextInt ({ 1, 1000000 }));
    lineChanged();
}

void MjqProcessor::mark()
{
    undoStack.push_back ({ session.selected, session.selectedTrack, line });
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool MjqProcessor::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back ({ session.selected, session.selectedTrack, line });
    const auto prev = undoStack.back();
    undoStack.pop_back();
    chooseSlot (prev.slot);
    chooseTrack (prev.track);
    line = prev.line;
    lineChanged();
    return true;
}

bool MjqProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back ({ session.selected, session.selectedTrack, line });
    const auto next = redoStack.back();
    redoStack.pop_back();
    chooseSlot (next.slot);
    chooseTrack (next.track);
    line = next.line;
    lineChanged();
    return true;
}

bool MjqProcessor::openInstrument (const juce::File& file, juce::String& error)
{
    mjq::Instrument ins;
    if (! mjq::loadInstrument (file, ins, error)) { message = "Not opened: " + error; return false; }

    auto shared = std::make_shared<const mjq::Instrument> (std::move (ins));
    if (auto old = std::atomic_load (&loaded)) keep.push_back (old);   // the audio thread may still be in it
    std::atomic_store (&loaded, shared);
    voices.setInstrument (shared);
    for (auto& v : otherVoices) v.setInstrument (shared);
    instrumentFile = file.getFullPathName();
    message = mjq::describe (*shared);
    version.fetch_add (1);
    return true;
}

void MjqProcessor::closeInstrument()
{
    if (auto old = std::atomic_load (&loaded)) keep.push_back (old);
    std::atomic_store (&loaded, std::shared_ptr<const mjq::Instrument>());
    voices.setInstrument (nullptr);
    voices.letGo.store (true);
    for (auto& v : otherVoices) { v.setInstrument (nullptr); v.letGo.store (true); }
    instrumentFile.clear();
    message = "No instrument: MidiJaQ is sending MIDI to whatever you point it at.";
    version.fetch_add (1);
}

void MjqProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate;
    playingSlot = -1;
    player.prepare (sampleRate);
    player.allKeysUp();
    for (auto& p : otherPlayers) { p.prepare (sampleRate); p.allKeysUp(); }
    voices.prepare (sampleRate);
    for (auto& v : otherVoices) v.prepare (sampleRate);
    if (auto cur = std::atomic_load (&loaded))
    {
        voices.setInstrument (cur);
        for (auto& v : otherVoices) v.setInstrument (cur);
    }
}

void MjqProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();                                   // MidiJaQ makes MIDI; its instrument comes later

    // the keys held down, for Follow, Bass and the Gate clock
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        auto key = [&] (mjq::Player& p)
        {
            if (msg.isNoteOn())            p.keyDown (msg.getNoteNumber(), msg.getVelocity());
            else if (msg.isNoteOff())      p.keyUp (msg.getNoteNumber());
            else if (msg.isAllNotesOff())  p.allKeysUp();
        };
        key (player);
        for (auto& p : otherPlayers) key (p);
    }
    midi.clear();                                    // what came in played the line; it is not passed on

    auto snapshot = std::atomic_load (&prepared);
    if (snapshot == nullptr) return;

    mjq::HostTime host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.valid = true;
            host.playing = pos->getIsPlaying();
            host.ppq = pos->getPpqPosition().orFallback (0.0);
            host.bpm = pos->getBpm().orFallback (120.0);
        }

    // Split at bar boundaries so changing a slot never fires notes from the next slot early.
    // Every line has its own player and MIDI channel; the players share the host clock.
    mjq::PlayNote notes[mjq::kSessionTracks][1024];
    int numNotes[mjq::kSessionTracks] {};
    const int total = audio.getNumSamples();
    const double bpm = host.bpm > 1.0 ? host.bpm : 120.0;
    const double beatPerSample = bpm / (60.0 * std::max (1.0, hostSampleRate));
    int start = 0;
    while (start < total)
    {
        const double beat = host.ppq + start * beatPerSample;
        const uint64_t bar = (uint64_t) std::floor (std::max (0.0, beat) / 4.0);
        const int slotNumber = (host.valid && host.playing && snapshot->state.chainLength > 1)
                             ? snapshot->state.slotAtBar (bar)
                             : snapshot->state.selected;
        const auto& slot = snapshot->state.slots[(size_t) slotNumber];
        const auto& passes = snapshot->passes[(size_t) slotNumber];

        int segment = total - start;
        if (host.valid && host.playing && snapshot->state.chainLength > 1)
        {
            const double nextBar = ((double) bar + 1.0) * 4.0;
            segment = std::min (segment, std::max (1, (int) std::ceil ((nextBar - beat) / beatPerSample - 1.0e-7)));
        }

        auto emit = [&] (int track, const mjq::MidiOut& e)
        {
            const int sample = start + e.sample;
            midi.addEvent (e.on ? juce::MidiMessage::noteOn (track + 1, e.note, (juce::uint8) e.velocity)
                                : juce::MidiMessage::noteOff (track + 1, e.note), sample);
            if (numNotes[track] < 1024)
                notes[track][numNotes[track]++] = { sample, e.note, (float) e.velocity / 127.0f, e.on };
        };

        if (slotNumber != playingSlot)
        {
            for (int track = 0; track < mjq::kSessionTracks; ++track)
            {
                auto& seq = track == 0 ? player : otherPlayers[(size_t) (track - 1)];
                mjq::MidiOut released[64];
                const int count = seq.releaseAllNow (released, 64);
                for (int j = 0; j < count; ++j) emit (track, released[j]);
                seq.reset();
            }
            playingSlot = slotNumber;
        }

        mjq::HostTime segmentTime = host;
        if (host.valid) segmentTime.ppq = beat;
        for (int track = 0; track < mjq::kSessionTracks; ++track)
        {
            auto& seq = track == 0 ? player : otherPlayers[(size_t) (track - 1)];
            if (! slot.tracks[(size_t) track].enabled)
            {
                mjq::MidiOut released[64];
                const int count = seq.releaseAllNow (released, 64);
                for (int j = 0; j < count; ++j) emit (track, released[j]);
                seq.reset();
                continue;
            }
            mjq::MidiOut out[256];
            const auto& tr = slot.tracks[(size_t) track];
            const int count = seq.process (tr.line, passes[(size_t) track], clockMode, segmentTime,
                                           segment, out, 256, &slot.chords, &tr.generator,
                                           host.valid ? beat : seq.positionInPass(),
                                           snapshot->evolving[(size_t) slotNumber][(size_t) track].get());
            for (int j = 0; j < count; ++j) emit (track, out[j]);
        }
        start += segment;
    }

    // One instrument plays the notes from all active lines. Keep the events in sample order for the
    // shared voice player, which is also used by Batchcut.
    for (int track = 0; track < mjq::kSessionTracks; ++track)
    {
        std::sort (notes[track], notes[track] + numNotes[track], [] (const mjq::PlayNote& a, const mjq::PlayNote& b)
        {
            if (a.offset != b.offset) return a.offset < b.offset;
            return ! a.on && b.on; // a retrigger releases the earlier voice before its new attack
        });
        auto& v = track == 0 ? voices : otherVoices[(size_t) (track - 1)];
        v.process (audio.getWritePointer (0), audio.getNumChannels() > 1 ? audio.getWritePointer (1) : audio.getWritePointer (0),
                   audio.getNumSamples(), notes[track], numNotes[track]);
    }

    lastStep.store (player.stepNow (snapshot->passes[(size_t) snapshot->state.selected][0]));
}

// ------------------------------------------------------------------ the line in the project
juce::String MjqProcessor::toXml (const mjq::Line& L, mjq::ClockMode clock, const juce::String& instrumentFile)
{
    juce::XmlElement root ("MIDIJAQ");
    root.setAttribute ("version", 1);
    root.setAttribute ("seed", (int) L.seed);
    root.setAttribute ("steps", L.numSteps);
    root.setAttribute ("offset", L.offset);
    root.setAttribute ("direction", (int) L.direction);
    root.setAttribute ("mode", (int) L.mode);
    root.setAttribute ("partIndex", L.partIndex);
    root.setAttribute ("clock", (int) clock);
    root.setAttribute ("dialect", L.dialect);
    root.setAttribute ("traffic", L.traffic);
    root.setAttribute ("accent", L.accent);
    root.setAttribute ("gate", L.gate);
    root.setAttribute ("root", L.root);
    root.setAttribute ("scale", L.scaleIndex);
    root.setAttribute ("transpose", L.transpose);
    root.setAttribute ("octLow", L.octLow);
    root.setAttribute ("octHigh", L.octHigh);
    root.setAttribute ("base", L.base);
    root.setAttribute ("tie", L.tie);
    root.setAttribute ("perBar", L.perBar);
    root.setAttribute ("swing", L.swing);
    juce::String locks;
    for (int i = 0; i < mjq::numLanes; ++i) locks << (L.locked[(size_t) i] ? "1" : "0");
    root.setAttribute ("locks", locks);
    if (instrumentFile.isNotEmpty()) root.setAttribute ("instrument", instrumentFile);

    for (int i = 0; i < mjq::kMaxSteps; ++i)
    {
        const auto& s = L.steps[(size_t) i];
        auto* e = root.createNewChildElement ("STEP");
        e->setAttribute ("w", (int) s.weight);
        e->setAttribute ("f", (int) s.freeDegree);
        e->setAttribute ("a", (int) s.acidDegree);
        e->setAttribute ("d", (int) s.dialectAt);
        e->setAttribute ("o", (int) s.octave);
        e->setAttribute ("ac", (int) s.accent);
        e->setAttribute ("g", (int) s.gate);
        e->setAttribute ("r", (int) s.roll);
        e->setAttribute ("sl", s.slide ? 1 : 0);
    }
    return root.toString();
}

bool MjqProcessor::fromXml (const juce::String& xml, mjq::Line& L, mjq::ClockMode& clock, juce::String& instrumentFile)
{
    auto root = juce::parseXML (xml);
    if (root == nullptr || ! root->hasTagName ("MIDIJAQ")) return false;

    L = mjq::Line();
    L.seed = (uint32_t) root->getIntAttribute ("seed", 1);
    L.numSteps = juce::jlimit (1, mjq::kMaxSteps, root->getIntAttribute ("steps", 16));
    L.offset = root->getIntAttribute ("offset", 0);
    L.direction = (mjq::Direction) juce::jlimit (0, 2, root->getIntAttribute ("direction", 0));
    L.mode = (mjq::Mode) juce::jlimit (0, 3, root->getIntAttribute ("mode", 0));
    L.partIndex = juce::jlimit (0, 3, root->getIntAttribute ("partIndex", 0));
    clock = (mjq::ClockMode) juce::jlimit (0, 2, root->getIntAttribute ("clock", 0));
    L.dialect = (float) root->getDoubleAttribute ("dialect", 0.5);
    L.traffic = (float) root->getDoubleAttribute ("traffic", 0.5);
    L.accent = (float) root->getDoubleAttribute ("accent", 0.5);
    L.gate = (float) root->getDoubleAttribute ("gate", 1.0);
    L.root = juce::jlimit (0, 11, root->getIntAttribute ("root", 0));
    L.scaleIndex = juce::jlimit (0, gen::numScales() - 1, root->getIntAttribute ("scale", 3));
    L.transpose = juce::jlimit (-48, 48, root->getIntAttribute ("transpose", 0));
    L.octLow = juce::jlimit (-4, 4, root->getIntAttribute ("octLow", -1));
    L.octHigh = juce::jlimit (L.octLow, 4, root->getIntAttribute ("octHigh", 2));
    L.base = juce::jlimit (0, 120, root->getIntAttribute ("base", 48));
    L.tie = root->getBoolAttribute ("tie", true);
    L.perBar = juce::jlimit (1, 64, root->getIntAttribute ("perBar", 16));
    L.swing = (float) root->getDoubleAttribute ("swing", 0.5);
    instrumentFile = root->getStringAttribute ("instrument");
    const auto locks = root->getStringAttribute ("locks");
    for (int i = 0; i < mjq::numLanes && i < locks.length(); ++i) L.locked[(size_t) i] = locks[i] == '1';

    int i = 0;
    for (auto* e : root->getChildWithTagNameIterator ("STEP"))
    {
        if (i >= mjq::kMaxSteps) break;
        auto& s = L.steps[(size_t) i++];
        s.weight = (uint16_t) juce::jlimit (0, 65535, e->getIntAttribute ("w", 0));
        s.freeDegree = (int16_t) e->getIntAttribute ("f", 0);
        s.acidDegree = (int16_t) e->getIntAttribute ("a", 0);
        s.dialectAt = (uint16_t) juce::jlimit (0, 1000, e->getIntAttribute ("d", 500));
        s.octave = (int8_t) juce::jlimit (-4, 4, e->getIntAttribute ("o", 0));
        s.accent = (uint8_t) juce::jlimit (0, 2, e->getIntAttribute ("ac", 1));
        s.gate = (uint8_t) juce::jlimit (0, 2, e->getIntAttribute ("g", 1));
        s.roll = (uint8_t) juce::jlimit (1, 4, e->getIntAttribute ("r", 1));
        s.slide = e->getIntAttribute ("sl", 0) != 0;
    }
    return true;
}

bool MjqProcessor::savePatterns (const juce::File& destination, juce::String& error)
{
    juce::MemoryBlock project;
    getStateInformation (project);
    auto state = juce::parseXML (juce::String::fromUTF8 ((const char*) project.getData(), (int) project.getSize()));
    if (state == nullptr) { error = "Could not read the saved pattern."; return false; }
    juce::XmlElement manifest ("NJQ");
    manifest.setAttribute ("version", 2);
    manifest.setAttribute ("kind", "pattern");
    manifest.setAttribute ("name", destination.getFileNameWithoutExtension());
    manifest.setAttribute ("madeWith", "MidiJaQ");
    manifest.addChildElement (state.release());
    const auto xml = manifest.toString();
    juce::ZipFile::Builder zip;
    zip.addEntry (new juce::MemoryInputStream (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true),
                  9, "manifest.xml", juce::Time::getCurrentTime());
    const auto tmp = destination.getSiblingFile (destination.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
        { tmp.deleteFile(); error = "Could not save " + destination.getFileName(); return false; }
        out.flush();
    }
    if (! tmp.moveFileTo (destination))
    { tmp.deleteFile(); error = "Could not finish saving " + destination.getFileName(); return false; }
    message = "Saved " + destination.getFileName();
    version.fetch_add (1);
    return true;
}

bool MjqProcessor::loadPatterns (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile()) { error = "The file is not there."; return false; }
    juce::ZipFile zip (file);
    auto* entry = zip.getEntry ("manifest.xml");
    if (entry == nullptr) { error = "Not a .NJQ file."; return false; }
    std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*entry));
    if (! stream) { error = "Could not read the pattern."; return false; }
    auto manifest = juce::parseXML (stream->readEntireStreamAsString());
    if (! manifest || ! manifest->hasTagName ("NJQ") || manifest->getStringAttribute ("kind") != "pattern")
    { error = "This is not a MidiJaQ pattern."; return false; }
    auto* state = manifest->getChildByName ("MIDIJAQ_SESSION");
    if (! state) { error = "No MidiJaQ pattern was found."; return false; }
    const auto data = state->toString();
    setStateInformation (data.toRawUTF8(), data.getNumBytesAsUTF8());
    message = "Opened " + file.getFileName();
    version.fetch_add (1);
    return true;
}

void MjqProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    session.current().tracks[(size_t) session.selectedTrack].line = line;
    juce::XmlElement root ("MIDIJAQ_SESSION");
    root.setAttribute ("version", 2);
    root.setAttribute ("selected", session.selected);
    root.setAttribute ("selectedTrack", session.selectedTrack);
    root.setAttribute ("chain", session.chainLength);
    root.setAttribute ("clock", (int) clockMode);
    root.setAttribute ("instrument", instrumentFile);
    for (int s = 0; s < mjq::kSessionSlots; ++s)
    {
        if (! session.touched[(size_t) s]) continue;
        const auto& slot = session.slots[(size_t) s];
        auto* sn = root.createNewChildElement ("SLOT");
        sn->setAttribute ("index", s);
        sn->setAttribute ("chordLength", slot.chords.length);
        sn->setAttribute ("beatsPerChord", slot.chords.beatsPerChord);
        for (int c = 0; c < mjq::kChordBars; ++c)
        {
            auto* cn = sn->createNewChildElement ("CHORD");
            const auto& chord = slot.chords.bars[(size_t) c];
            cn->setAttribute ("bar", c);
            cn->setAttribute ("root", chord.root);
            cn->setAttribute ("kind", (int) chord.kind);
            cn->setAttribute ("bass", chord.bassNote);
            cn->setAttribute ("inversion", chord.inversion);
        }
        for (int t = 0; t < mjq::kSessionTracks; ++t)
        {
            const auto& tr = slot.tracks[(size_t) t];
            auto* tn = sn->createNewChildElement ("TRACK");
            tn->setAttribute ("index", t);
            tn->setAttribute ("enabled", tr.enabled);
            tn->setAttribute ("generator", (int) tr.generator.kind);
            tn->setAttribute ("seed", (int) tr.generator.seed);
            tn->setAttribute ("amount", tr.generator.amount);
            tn->setAttribute ("hits", tr.generator.hits);
            tn->setAttribute ("rotate", tr.generator.rotate);
            tn->setAttribute ("cue", (int) tr.generator.cue.kind);
            tn->setAttribute ("cueN", (int) tr.generator.cue.n);
            tn->setAttribute ("cueOf", (int) tr.generator.cue.of);
            tn->setAttribute ("cueChance", tr.generator.cue.chance);
            tn->setAttribute ("fill", tr.generator.fill);
            if (auto legacy = juce::parseXML (toXml (tr.line, clockMode, {})))
                tn->addChildElement (legacy.release());
        }
    }
    const auto xml = root.toString();
    dest.replaceAll (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
}

void MjqProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    const juce::String contents = juce::String::fromUTF8 ((const char*) data, size);
    auto root = juce::parseXML (contents);
    if (root == nullptr) return;
    juce::String wants;
    if (root->hasTagName ("MIDIJAQ"))
    {
        // The original projects must open as before, with their line in slot one.
        mjq::Line oldLine;
        auto clock = clockMode;
        if (! fromXml (contents, oldLine, clock, wants)) return;
        session = mjq::Session();
        session.selected = 0;
        session.selectedTrack = 0;
        line = oldLine;
        clockMode = clock;
    }
    else if (root->hasTagName ("MIDIJAQ_SESSION"))
    {
        mjq::Session restored;
        restored.selected = juce::jlimit (0, 127, root->getIntAttribute ("selected"));
        restored.selectedTrack = juce::jlimit (0, 3, root->getIntAttribute ("selectedTrack"));
        restored.chainLength = juce::jlimit (1, 128, root->getIntAttribute ("chain", 1));
        clockMode = (mjq::ClockMode) juce::jlimit (0, 2, root->getIntAttribute ("clock"));
        wants = root->getStringAttribute ("instrument");
        for (auto* sn : root->getChildWithTagNameIterator ("SLOT"))
        {
            const int s = sn->getIntAttribute ("index", -1);
            if (s < 0 || s >= 128) continue;
            restored.touched[(size_t) s] = true;
            auto& slot = restored.slots[(size_t) s];
            slot.chords.length = juce::jlimit (1, 8, sn->getIntAttribute ("chordLength", 4));
            slot.chords.beatsPerChord = juce::jlimit (1, 16, sn->getIntAttribute ("beatsPerChord", 4));
            for (auto* cn : sn->getChildWithTagNameIterator ("CHORD"))
            {
                const int b = cn->getIntAttribute ("bar", -1);
                if (b < 0 || b >= 8) continue;
                auto& chord = slot.chords.bars[(size_t) b];
                chord.root = juce::jlimit (0, 11, cn->getIntAttribute ("root"));
                chord.kind = (mjq::ChordKind) juce::jlimit (0, 8, cn->getIntAttribute ("kind"));
                chord.bassNote = juce::jlimit (0, 120, cn->getIntAttribute ("bass", 48));
                chord.inversion = juce::jlimit (0, 3, cn->getIntAttribute ("inversion"));
            }
            for (auto* tn : sn->getChildWithTagNameIterator ("TRACK"))
            {
                const int t = tn->getIntAttribute ("index", -1);
                if (t < 0 || t >= 4) continue;
                auto& tr = slot.tracks[(size_t) t];
                tr.enabled = tn->getBoolAttribute ("enabled", t == 0);
                tr.generator.kind = (mjq::Generator) juce::jlimit (0, 4, tn->getIntAttribute ("generator"));
                tr.generator.seed = (uint32_t) tn->getIntAttribute ("seed", 1);
                tr.generator.amount = (float) tn->getDoubleAttribute ("amount", 0.5);
                tr.generator.hits = juce::jlimit (0, 32, tn->getIntAttribute ("hits", 5));
                tr.generator.rotate = tn->getIntAttribute ("rotate");
                tr.generator.cue.kind = (gen::Cue::Kind) juce::jlimit (0, 8, tn->getIntAttribute ("cue"));
                tr.generator.cue.n = (uint8_t) juce::jlimit (1, 64, tn->getIntAttribute ("cueN", 1));
                tr.generator.cue.of = (uint8_t) juce::jlimit (1, 64, tn->getIntAttribute ("cueOf", 2));
                tr.generator.cue.chance = (float) juce::jlimit (0.0, 1.0, tn->getDoubleAttribute ("cueChance", 0.5));
                tr.generator.fill = tn->getBoolAttribute ("fill", false);
                if (auto* ln = tn->getChildByName ("MIDIJAQ"))
                {
                    mjq::ClockMode dummy = clockMode;
                    juce::String ignored;
                    fromXml (ln->toString(), tr.line, dummy, ignored);
                    tr.line.partIndex = t;
                }
            }
        }
        session = std::move (restored);
        line = session.current().tracks[(size_t) session.selectedTrack].line;
    }
    else return;
    playingSlot = -1;
    rebuildSession (true);
    if (wants.isEmpty() && instrumentFile.isNotEmpty()) closeInstrument();
    if (wants.isNotEmpty() && wants != instrumentFile)
    {
        const juce::File f (wants);
        juce::String err;
        if (f.existsAsFile()) openInstrument (f, err);
        else { instrumentFile = wants; message = "The instrument " + f.getFileName() + " was not where it was: open it again."; version.fetch_add (1); }
    }
}

juce::AudioProcessorEditor* MjqProcessor::createEditor() { return new MjqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MjqProcessor(); }
