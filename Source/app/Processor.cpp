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
        const float ox = next->state.orbX, oy = next->state.orbY;
        next->passes[(size_t) s][(size_t) t] = mjq::generatedPass (tr, 0, ox, oy);
        auto& rounds = next->evolving[(size_t) s][(size_t) t];
        if (tr.generator.kind == mjq::Generator::HABIT || tr.generator.kind == mjq::Generator::DRIFT)
        {
            std::vector<mjq::Pass> prepared;
            prepared.reserve (mjq::kEvolutionRounds);
            prepared.push_back (next->passes[(size_t) s][(size_t) t]);
            for (int r = 1; r < mjq::kEvolutionRounds; ++r)
                prepared.push_back (mjq::generatedPass (tr, (uint64_t) r, ox, oy));
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

// the LEVEL knob sets the chosen voice's level; the others keep theirs, so a multi stays balanced
void MjqProcessor::setInstrumentLevel (float level)
{
    changeTrackLevel (session.selectedTrack, level);
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
    return openInstrument (session.selectedTrack, file, error);
}

bool MjqProcessor::openInstrument (int track, const juce::File& file, juce::String& error)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    mjq::Instrument ins;
    if (! mjq::loadInstrument (file, ins, error)) { message = "Not opened: " + error; return false; }

    auto shared = std::make_shared<const mjq::Instrument> (std::move (ins));
    if (auto old = std::atomic_load (&loadedTracks[(size_t) track])) keep.push_back (old);   // the audio thread may still be in it
    std::atomic_store (&loadedTracks[(size_t) track], shared);
    voicesFor (track).setInstrument (shared);

    // the path travels in the session, so a multi is saved and reopened whole
    for (int p = 0; p < mjq::kSessionSlots; ++p)
        session.slots[(size_t) p].tracks[(size_t) track].instrumentFile = file.getFullPathName().toStdString();
    if (track == 0) instrumentFile = file.getFullPathName();
    message = "Voice " + juce::String (track + 1) + ": " + mjq::describe (*shared);
    version.fetch_add (1);
    return true;
}

void MjqProcessor::closeInstrument() { closeInstrument (session.selectedTrack); }

void MjqProcessor::closeInstrument (int track)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    if (auto old = std::atomic_load (&loadedTracks[(size_t) track])) keep.push_back (old);
    std::atomic_store (&loadedTracks[(size_t) track], std::shared_ptr<const mjq::Instrument>());
    auto& v = voicesFor (track);
    v.setInstrument (nullptr);
    v.letGo.store (true);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
        session.slots[(size_t) p].tracks[(size_t) track].instrumentFile.clear();
    if (track == 0) instrumentFile.clear();
    message = "Voice " + juce::String (track + 1) + " has no instrument, so it makes no sound.";
    version.fetch_add (1);
}

juce::String MjqProcessor::instrumentFileFor (int track) const
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    return juce::String (session.current().tracks[(size_t) track].instrumentFile);
}

void MjqProcessor::changeTrackRange (int track, int lowKey, int highKey)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    lowKey = juce::jlimit (0, 127, lowKey);
    highKey = juce::jlimit (lowKey, 127, highKey);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
    {
        auto& tr = session.slots[(size_t) p].tracks[(size_t) track];
        tr.lowKey = lowKey; tr.highKey = highKey;
    }
    lineChanged();
}

// The joystick. Every voice's Orb reads from the same position, so one hand moves the whole multi; what each
// voice does with it is its own, because the corners belong to the voice.
void MjqProcessor::moveOrb (float x, float y)
{
    const float r = std::sqrt (x * x + y * y);
    if (r > 1.0f) { x /= r; y /= r; }          // the joystick stays in its circle
    session.orbX = x;
    session.orbY = y;
    lineChanged();
}

void MjqProcessor::setOrbOn (int track, bool on)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
        session.slots[(size_t) p].tracks[(size_t) track].orb.on = on;
    lineChanged();
}

// Put this voice's line, as it stands, into a corner: its seed and the controls it is playing with.
void MjqProcessor::putOrbCorner (int track, int corner)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    corner = juce::jlimit (0, mjq::kOrbCorners - 1, corner);
    const auto& from = session.current().tracks[(size_t) track].line;

    for (int p = 0; p < mjq::kSessionSlots; ++p)
    {
        auto& c = session.slots[(size_t) p].tracks[(size_t) track].orb.corners[(size_t) corner];
        const auto wasName = c.name;
        c = mjq::OrbCorner {};
        c.set = true;
        c.name = wasName;
        c.seed = from.seed;
        c.dialect = from.dialect; c.traffic = from.traffic; c.accent = from.accent;
        c.gate = from.gate; c.swing = from.swing; c.transpose = from.transpose;
    }
    message = "Corner " + juce::String (corner + 1) + " holds voice " + juce::String (track + 1) + "'s line.";
    version.fetch_add (1);
    lineChanged();
}

void MjqProcessor::clearOrbCorner (int track, int corner)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    corner = juce::jlimit (0, mjq::kOrbCorners - 1, corner);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
        session.slots[(size_t) p].tracks[(size_t) track].orb.corners[(size_t) corner] = mjq::OrbCorner {};
    lineChanged();
}

// Travel to a corner: the joystick goes there, so the voice plays that corner outright.
void MjqProcessor::recallOrbCorner (int, int corner)
{
    float cx = 0.0f, cy = 0.0f;
    mjq::orbCornerAt (juce::jlimit (0, mjq::kOrbCorners - 1, corner), cx, cy);
    moveOrb (cx, cy);
}

mjq::OrbBlend MjqProcessor::orbBlend (int track) const
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    return mjq::orbRead (session.current().tracks[(size_t) track].orb, session.orbX, session.orbY);
}

void MjqProcessor::changeTrackLevel (int track, float level)
{
    track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
    level = juce::jlimit (0.0f, 2.0f, level);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
        session.slots[(size_t) p].tracks[(size_t) track].level = level;
    lineChanged();
}

void MjqProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    hostSampleRate = sampleRate;
    playingSlot = -1;
    player.prepare (sampleRate);
    player.allKeysUp();
    for (auto& p : otherPlayers) { p.prepare (sampleRate); p.allKeysUp(); }
    voices.prepare (sampleRate);
    for (auto& v : otherVoices) v.prepare (sampleRate);
    // every voice of the multi gets its own instrument back
    for (int t = 0; t < mjq::kSessionTracks; ++t)
        if (auto cur = std::atomic_load (&loadedTracks[(size_t) t]))
            voicesFor (t).setInstrument (cur);

    // the Orb's effect stack: a chain of its own for every voice, drawing on one shared set of pools
    for (int t = 0; t < mjq::kSessionTracks; ++t) { roomAir[(size_t) t].prepare (sampleRate); roomReady[(size_t) t] = false; }

    const int block = std::max (64, maxBlock);
    fxPools.prepare ((float) sampleRate);
    fxScratch.setSize (2, block, false, false, true);
    for (int t = 0; t < mjq::kSessionTracks; ++t)
    {
        voiceBuf[(size_t) t].setSize (2, block, false, false, true);
        voiceBuf[(size_t) t].clear();
        for (auto& fx : orbFx[(size_t) t]) fx.prepare ((float) sampleRate, &fxPools);
        orbFxLast[(size_t) t] = {};
    }
}

void MjqProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();                                   // MidiJaQ is an instrument: it makes sound, not MIDI

    // The keys held down, for Follow, Bass, Parts and the Gate clock. A key only reaches the voices whose
    // range covers it, which is what makes the multi a multi: hold low and the bass answers, hold in the
    // middle where two ranges overlap and both do, each on its own instrument.
    const int heldSlot = session.selected;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isAllNotesOff())
        {
            player.allKeysUp();
            for (auto& p : otherPlayers) p.allKeysUp();
            continue;
        }
        if (! msg.isNoteOn() && ! msg.isNoteOff()) continue;
        const int note = msg.getNoteNumber();
        int reached[mjq::kSessionTracks];
        const int n = session.voicesFor (heldSlot, note, reached, mjq::kSessionTracks);
        for (int i = 0; i < n; ++i)
        {
            auto& p = reached[i] == 0 ? player : otherPlayers[(size_t) (reached[i] - 1)];
            if (msg.isNoteOn()) p.keyDown (note, msg.getVelocity());
            else                p.keyUp (note);
        }
    }
    midi.clear();                                    // what came in played the lines; nothing is passed on

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

        // MidiJaQ no longer sends MIDI out: a note goes to its track's own instrument and nowhere else.
        auto emit = [&] (int track, const mjq::MidiOut& e)
        {
            if (numNotes[track] < 1024)
                notes[track][numNotes[track]++] = { start + e.sample, e.note, (float) e.velocity / 127.0f, e.on };
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

            // a cycling knob is laid over the line as it plays. DIALECT, TRAFFIC, ACCENT, GATE and SWING are
            // read at playing time, so this is heard at once rather than waiting for the line to be rewritten.
            const mjq::Line* linePtr = &tr.line;
            mjq::Line cycled;
            if (tr.randomiser.any())
            {
                const auto now = mjq::cycleRead (tr.randomiser, host.valid ? beat : seq.positionInPass());
                cycled = tr.line;
                if (now.live[(size_t) mjq::Cycled::Dialect]) cycled.dialect = now.value[(size_t) mjq::Cycled::Dialect];
                if (now.live[(size_t) mjq::Cycled::Traffic]) cycled.traffic = now.value[(size_t) mjq::Cycled::Traffic];
                if (now.live[(size_t) mjq::Cycled::Accent])  cycled.accent  = now.value[(size_t) mjq::Cycled::Accent];
                if (now.live[(size_t) mjq::Cycled::Gate])    cycled.gate    = now.value[(size_t) mjq::Cycled::Gate];
                if (now.live[(size_t) mjq::Cycled::Swing])   cycled.swing   = now.value[(size_t) mjq::Cycled::Swing];
                linePtr = &cycled;
            }

            const int count = seq.process (*linePtr, passes[(size_t) track], clockMode, segmentTime,
                                           segment, out, 256, &slot.chords, &tr.generator,
                                           host.valid ? beat : seq.positionInPass(),
                                           snapshot->evolving[(size_t) slotNumber][(size_t) track].get());
            for (int j = 0; j < count; ++j) emit (track, out[j]);
        }
        start += segment;
    }

    // Each track plays its own instrument, at its own level, and they sum into the one output. Keep the
    // events in sample order for the shared voice player, which Batchcut uses too.
    const auto& playing = snapshot->state.slots[(size_t) juce::jlimit (0, mjq::kSessionSlots - 1, playingSlot)];
    shownBeat.store (host.valid ? host.ppq : player.positionInPass(), std::memory_order_relaxed);
    const int numSamples = audio.getNumSamples();
    const bool stereoOut = audio.getNumChannels() > 1;

    for (int track = 0; track < mjq::kSessionTracks; ++track)
    {
        const auto& tr = playing.tracks[(size_t) track];
        const auto blend = (tr.orb.on && tr.orb.anySet())
                         ? mjq::orbRead (tr.orb, snapshot->state.orbX, snapshot->state.orbY)
                         : mjq::OrbBlend {};
        bool anyFx = false;
        for (const auto& slot : blend.stack) anyFx = anyFx || slot.on();
        const bool inRoom = tr.place.on;

        auto& v = voicesFor (track);
        const bool silent = numNotes[track] == 0 && v.active.load() == 0;
        if (silent && ! anyFx) continue;         // an effect with a tail still has to be run out

        std::sort (notes[track], notes[track] + numNotes[track], [] (const mjq::PlayNote& a, const mjq::PlayNote& b)
        {
            if (a.offset != b.offset) return a.offset < b.offset;
            return ! a.on && b.on; // a retrigger releases the earlier voice before its new attack
        });
        v.level.store (tr.level);

        if (! anyFx && ! inRoom)
        {
            // nothing stacked and standing at the front: straight into the output, as before
            v.process (audio.getWritePointer (0), stereoOut ? audio.getWritePointer (1) : audio.getWritePointer (0),
                       numSamples, notes[track], numNotes[track]);
            continue;
        }

        // the voice into a buffer of its own, then through its stack, then into the output
        auto& buf = voiceBuf[(size_t) track];
        if (buf.getNumSamples() < numSamples) buf.setSize (2, numSamples, false, false, true);
        buf.clear (0, 0, numSamples);
        v.process (buf.getWritePointer (0), buf.getWritePointer (1), numSamples, notes[track], numNotes[track]);

        for (int s = 0; s < mjq::kOrbStack; ++s)
        {
            const auto& want = blend.stack[(size_t) s];
            if (! want.on()) continue;

            auto& fx = orbFx[(size_t) track][(size_t) s];
            if (! (want == orbFxLast[(size_t) track][(size_t) s]))
            {
                nb::LiveSpec spec;
                spec.kind = 1;                               // the library; Bounce Delay is the hit path
                spec.fx = (nb::MatrixFx) juce::jlimit (0, (int) nb::MatrixFx::Count - 1, want.fx);
                for (int k = 0; k < mjq::kOrbFxParams && k < (int) spec.p.size(); ++k) spec.p[(size_t) k] = want.p[(size_t) k];
                spec.version = ++orbFxVersion;
                fx.update (spec);
                orbFxLast[(size_t) track][(size_t) s] = want;
            }

            if (fxScratch.getNumSamples() < numSamples) fxScratch.setSize (2, numSamples, false, false, true);
            fx.process (buf.getReadPointer (0), buf.getReadPointer (1),
                        fxScratch.getWritePointer (0), fxScratch.getWritePointer (1), numSamples, nb::HitView {}, false);

            // the slot's mix is how far in the effect is, so one arriving fades in rather than jumping
            const float wet = juce::jlimit (0.0f, 1.0f, want.mix), dry = 1.0f - wet;
            for (int c = 0; c < 2; ++c)
            {
                auto* b = buf.getWritePointer (c);
                const auto* w = fxScratch.getReadPointer (c);
                for (int i = 0; i < numSamples; ++i) b[i] = dry * b[i] + wet * w[i];
            }
        }

        // the stereo room: where this voice stands, and where its drawn curve has moved it to. The place is
        // worked out once a block and glided to across it, so a voice crossing the picture does not zip.
        if (inRoom)
        {
            const double beat = host.valid ? host.ppq : player.positionInPass();
            const auto want = mjq::roomGain (tr.place, beat, playing.tracks[(size_t) track].line.perBar);
            auto& air = roomAir[(size_t) track];
            air.set (want.cutoff);

            auto& was = roomWas[(size_t) track];
            if (! roomReady[(size_t) track]) { was = want; roomReady[(size_t) track] = true; }
            const float stepL = (want.l - was.l) / (float) std::max (1, numSamples);
            const float stepR = (want.r - was.r) / (float) std::max (1, numSamples);

            auto* bl = buf.getWritePointer (0);
            auto* br = buf.getWritePointer (1);
            float gl = was.l, gr = was.r;
            for (int i = 0; i < numSamples; ++i)
            {
                float l = bl[i] * gl, r = br[i] * gr;
                air.process (l, r);
                bl[i] = l; br[i] = r;
                gl += stepL; gr += stepR;
            }
            was = want;
        }

        audio.addFrom (0, 0, buf, 0, 0, numSamples);
        if (stereoOut) audio.addFrom (1, 0, buf, 1, 0, numSamples);
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
    root.setAttribute ("orbX", session.orbX);
    root.setAttribute ("orbY", session.orbY);
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
            // the multi: this voice's own instrument, the keys it answers to, and its level
            if (! tr.instrumentFile.empty()) tn->setAttribute ("instrument", juce::String (tr.instrumentFile));
            tn->setAttribute ("lowKey", tr.lowKey);
            tn->setAttribute ("highKey", tr.highKey);
            tn->setAttribute ("level", tr.level);

            // the randomiser: any control left cycling
            if (tr.randomiser.any())
            {
                auto* qn = tn->createNewChildElement ("CYCLES");
                for (int k = 0; k < (int) mjq::Cycled::Count; ++k)
                {
                    const auto& c = tr.randomiser.cycles[(size_t) k];
                    if (! c.on) continue;
                    auto* cn = qn->createNewChildElement ("CYCLE");
                    cn->setAttribute ("what", k);
                    cn->setAttribute ("rate", c.rate);
                    cn->setAttribute ("swing", c.swing);
                    cn->setAttribute ("extremity", c.extremity);
                    cn->setAttribute ("home", c.home);
                    cn->setAttribute ("seed", juce::String (c.seed));
                }
            }

            // the stereo room: where this voice stands, and the curve that moves it
            if (tr.place.on || tr.place.x != 0.0f || tr.place.y != 0.35f || tr.place.move != mjq::RoomMove::None)
            {
                auto* rn = tn->createNewChildElement ("ROOM");
                rn->setAttribute ("on", tr.place.on ? 1 : 0);
                rn->setAttribute ("x", tr.place.x);
                rn->setAttribute ("y", tr.place.y);
                rn->setAttribute ("width", tr.place.width);
                rn->setAttribute ("move", (int) tr.place.move);
                rn->setAttribute ("depth", tr.place.depth);
                rn->setAttribute ("bars", tr.place.bars);
                juce::String drawn;
                for (int k = 0; k < mjq::kRoomCurve; ++k) drawn += juce::String (tr.place.curve[(size_t) k], 4) + (k + 1 < mjq::kRoomCurve ? " " : "");
                rn->setAttribute ("curve", drawn);
            }

            // the Orb: its eight corners, and the effects stacked on each
            if (tr.orb.on || tr.orb.anySet())
            {
                auto* on = tn->createNewChildElement ("ORB");
                on->setAttribute ("on", tr.orb.on ? 1 : 0);
                on->setAttribute ("grip", tr.orb.grip);
                on->setAttribute ("salt", juce::String (tr.orb.salt));
                for (int c = 0; c < mjq::kOrbCorners; ++c)
                {
                    const auto& corner = tr.orb.corners[(size_t) c];
                    if (! corner.set) continue;
                    auto* cn = on->createNewChildElement ("CORNER");
                    cn->setAttribute ("i", c);
                    if (! corner.name.empty()) cn->setAttribute ("name", juce::String (corner.name));
                    cn->setAttribute ("seed", juce::String (corner.seed));
                    cn->setAttribute ("dialect", corner.dialect);
                    cn->setAttribute ("traffic", corner.traffic);
                    cn->setAttribute ("accent", corner.accent);
                    cn->setAttribute ("gate", corner.gate);
                    cn->setAttribute ("swing", corner.swing);
                    cn->setAttribute ("transpose", corner.transpose);
                    for (int s = 0; s < mjq::kOrbStack; ++s)
                    {
                        const auto& slot = corner.stack[(size_t) s];
                        if (slot.fx < 0) continue;
                        auto* fn = cn->createNewChildElement ("FX");
                        fn->setAttribute ("slot", s);
                        fn->setAttribute ("fx", slot.fx);
                        fn->setAttribute ("mix", slot.mix);
                        for (int k = 0; k < mjq::kOrbFxParams; ++k)
                            fn->setAttribute ("p" + juce::String (k), slot.p[(size_t) k]);
                    }
                }
            }
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
        restored.orbX = (float) juce::jlimit (-1.0, 1.0, root->getDoubleAttribute ("orbX", 0.0));
        restored.orbY = (float) juce::jlimit (-1.0, 1.0, root->getDoubleAttribute ("orbY", 0.0));
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
                // the multi. A project from before it was written has no range: the voice takes the whole
                // keyboard, which is what it did then.
                tr.instrumentFile = tn->getStringAttribute ("instrument").toStdString();
                tr.lowKey = juce::jlimit (0, 127, tn->getIntAttribute ("lowKey", 0));
                tr.highKey = juce::jlimit (tr.lowKey, 127, tn->getIntAttribute ("highKey", 127));
                tr.level = (float) juce::jlimit (0.0, 2.0, tn->getDoubleAttribute ("level", 0.8));

                tr.randomiser = mjq::Randomiser {};
                if (auto* qn = tn->getChildByName ("CYCLES"))
                    for (auto* cn : qn->getChildWithTagNameIterator ("CYCLE"))
                    {
                        const int k = cn->getIntAttribute ("what", -1);
                        if (k < 0 || k >= (int) mjq::Cycled::Count) continue;
                        auto& c = tr.randomiser.cycles[(size_t) k];
                        c.on = true;
                        c.rate = (float) juce::jlimit (0.03125, 16.0, cn->getDoubleAttribute ("rate", 1.0));
                        c.swing = (float) juce::jlimit (0.5, 0.9, cn->getDoubleAttribute ("swing", 0.5));
                        c.extremity = (float) juce::jlimit (0.0, 1.0, cn->getDoubleAttribute ("extremity", 0.5));
                        c.home = (float) cn->getDoubleAttribute ("home", 0.5);
                        c.seed = (uint64_t) cn->getStringAttribute ("seed").getLargeIntValue();
                        if (c.seed == 0) c.seed = 1;
                    }

                tr.place = mjq::Place {};
                if (auto* rn = tn->getChildByName ("ROOM"))
                {
                    tr.place.on = rn->getBoolAttribute ("on", false);
                    tr.place.x = (float) juce::jlimit (-1.0, 1.0, rn->getDoubleAttribute ("x", 0.0));
                    tr.place.y = (float) juce::jlimit (0.0, 1.0, rn->getDoubleAttribute ("y", 0.35));
                    tr.place.width = (float) juce::jlimit (0.0, 1.0, rn->getDoubleAttribute ("width", 0.5));
                    tr.place.move = (mjq::RoomMove) juce::jlimit (0, 3, rn->getIntAttribute ("move", 0));
                    tr.place.depth = (float) juce::jlimit (0.0, 1.0, rn->getDoubleAttribute ("depth", 0.5));
                    tr.place.bars = juce::jlimit (1, 8, rn->getIntAttribute ("bars", 1));
                    const auto drawn = juce::StringArray::fromTokens (rn->getStringAttribute ("curve"), " ", {});
                    for (int k = 0; k < mjq::kRoomCurve && k < drawn.size(); ++k)
                        tr.place.curve[(size_t) k] = (float) juce::jlimit (-1.0, 1.0, drawn[k].getDoubleValue());
                }

                tr.orb = mjq::Orb {};
                if (auto* on = tn->getChildByName ("ORB"))
                {
                    tr.orb.on = on->getBoolAttribute ("on", false);
                    tr.orb.grip = (float) juce::jlimit (0.05, 12.0, on->getDoubleAttribute ("grip", 3.0));
                    if (on->hasAttribute ("salt")) tr.orb.salt = (uint64_t) on->getStringAttribute ("salt").getLargeIntValue();
                    for (auto* cn : on->getChildWithTagNameIterator ("CORNER"))
                    {
                        const int i = cn->getIntAttribute ("i", -1);
                        if (i < 0 || i >= mjq::kOrbCorners) continue;
                        auto& corner = tr.orb.corners[(size_t) i];
                        corner.set = true;
                        corner.name = cn->getStringAttribute ("name").toStdString();
                        corner.seed = (uint64_t) cn->getStringAttribute ("seed").getLargeIntValue();
                        corner.dialect = (float) cn->getDoubleAttribute ("dialect", 0.5);
                        corner.traffic = (float) cn->getDoubleAttribute ("traffic", 0.5);
                        corner.accent = (float) cn->getDoubleAttribute ("accent", 0.5);
                        corner.gate = (float) cn->getDoubleAttribute ("gate", 1.0);
                        corner.swing = (float) cn->getDoubleAttribute ("swing", 0.5);
                        corner.transpose = cn->getIntAttribute ("transpose", 0);
                        for (auto* fn : cn->getChildWithTagNameIterator ("FX"))
                        {
                            const int s = fn->getIntAttribute ("slot", -1);
                            if (s < 0 || s >= mjq::kOrbStack) continue;
                            auto& slot = corner.stack[(size_t) s];
                            slot.fx = fn->getIntAttribute ("fx", -1);
                            slot.mix = (float) juce::jlimit (0.0, 1.0, fn->getDoubleAttribute ("mix", 1.0));
                            for (int k = 0; k < mjq::kOrbFxParams; ++k)
                                slot.p[(size_t) k] = (float) fn->getDoubleAttribute ("p" + juce::String (k), 0.0);
                        }
                    }
                }
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

    // Open every voice's instrument. A project saved before the multi existed names one instrument for the
    // whole plugin, in `wants`; that one goes to voice 1, which is where it played from.
    juce::StringArray missing;
    for (int t = 0; t < mjq::kSessionTracks; ++t)
    {
        auto path = juce::String (session.current().tracks[(size_t) t].instrumentFile);
        if (path.isEmpty() && t == 0) path = wants;
        if (path.isEmpty()) { closeInstrument (t); continue; }
        const juce::File f (path);
        juce::String err;
        if (f.existsAsFile()) openInstrument (t, f, err);
        else
        {
            for (int p = 0; p < mjq::kSessionSlots; ++p)
                session.slots[(size_t) p].tracks[(size_t) t].instrumentFile = path.toStdString();
            if (t == 0) instrumentFile = path;
            missing.add (f.getFileName());
        }
    }
    if (! missing.isEmpty())
    {
        message = (missing.size() == 1 ? "The instrument " : "The instruments ")
                + missing.joinIntoString (", ") + " were not where they were: open them again.";
        version.fetch_add (1);
    }
}

juce::AudioProcessorEditor* MjqProcessor::createEditor() { return new MjqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MjqProcessor(); }
