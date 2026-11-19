// BatchCut - the processor.
#include "Processor.h"
#include "State.h"
#include "Editor.h"

int bcExpandChord (int chord, int root, int scaleIndex, const bc::KeysNote& in, bc::KeysNote* out, int room);   // KeysProc.cpp

juce::AudioProcessorValueTreeState::ParameterLayout BcProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "level", 1 }, "Preview level",
                                                        juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    // in a DAW the track's own sound passes through Batchcut; the standalone app never plays its input back
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "thru", 1 }, "Input through", juce::StringArray { "Off", "On" }, 1));
    return l;
}

BcProcessor::BcProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    level = apvts.getRawParameterValue ("level");
    thru = apvts.getRawParameterValue ("thru");
    formats.registerBasicFormats();
    session.settings.outDir = exportDir().getFullPathName().toStdString();

    session.prober = [this] (const std::string& path, bc::FileInfo& info, std::string& err)
    {
        const juce::File f (juce::String::fromUTF8 (path.c_str()));
        if (! f.existsAsFile()) { err = "the file is missing"; return false; }
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) { err = "not an audio file Batchcut can read"; return false; }
        if (r->sampleRate < 1000.0 || r->lengthInSamples <= 0) { err = "the file is empty"; return false; }
        info.sr = r->sampleRate; info.length = r->lengthInSamples; info.channels = (int) r->numChannels;
        return true;
    };
    session.loader = [this] (const std::string& path, int64_t start, int64_t len, bc::Audio& out, std::string& err)
    {
        const juce::File f (juce::String::fromUTF8 (path.c_str()));
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) { err = f.existsAsFile() ? "could not read the file" : "the file is missing"; return false; }
        len = std::max<int64_t> (0, std::min<int64_t> (len, (int64_t) r->lengthInSamples - start));
        if (len <= 0 || len > (int64_t) std::numeric_limits<int>::max()) { err = "nothing to read"; return false; }
        const bool stereo = r->numChannels > 1;
        juce::AudioBuffer<float> buf (stereo ? 2 : 1, (int) len);
        if (! r->read (&buf, 0, (int) len, start, true, stereo)) { err = "could not read the file"; return false; }
        out.sr = r->sampleRate;
        out.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
        if (stereo) out.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len); else out.r.clear();
        for (auto* v : { &out.l, &out.r }) for (auto& x : *v) if (! std::isfinite (x)) x = 0.0f;
        return true;
    };
    session.writer = [] (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double sr, int bits, std::string& err)
    {
        return writeWav (juce::File (juce::String::fromUTF8 (path.c_str())), l, r, sr, bits, err);
    };
    session.makeDir = [] (const std::string& dir) { return juce::File (juce::String::fromUTF8 (dir.c_str())).createDirectory().wasOk(); };
    session.onChange = [this] { triggerAsyncUpdate(); };
    session.start();
    preview.startWorker();
    startTimerHz (20);
}

BcProcessor::~BcProcessor()
{
    stopTimer();
    session.onChange = nullptr;
    session.stop();
    preview.stopWorker();
    cancelPendingUpdate();
}

bool BcProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BcProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (recorder.isRecording()) stopRecording();   // the rate may change: what was recorded so far is kept
    hostRate = sampleRate > 0 ? sampleRate : 48000.0;
    recorder.prepare (hostRate);
    kitPlayer.prepare (hostRate);
    kitSetPattern (kit.pattern);
    loopPlayer.prepare (hostRate);
    loopsChanged();
    keysPlayer.prepare (hostRate);
    keysChanged();
    scratch.assign ((size_t) std::max (1, samplesPerBlock), 0.0f);
}

void BcProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // the keys played (LOOPS: Trigger and Slices; KEYS), the on-screen keys among them, and where the host is
    keyboard.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    std::array<bc::LoopNote, 128> notes; int numNotes = 0;
    for (const auto meta : midi)
    {
        if (numNotes >= (int) notes.size()) break;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) notes[(size_t) numNotes++] = { meta.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), true };
        else if (m.isNoteOff()) notes[(size_t) numNotes++] = { meta.samplePosition, m.getNoteNumber(), 0.0f, false };
    }
    midi.clear();
    bc::LoopHost host;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto ppq = pos->getPpqPosition()) { host.valid = true; host.ppq = *ppq; host.playing = pos->getIsPlaying(); }
            if (const auto bpm = pos->getBpm()) host.bpm = *bpm; else host.valid = false;
        }
    hostBpm.store (host.valid && wrapperType != wrapperType_Standalone ? host.bpm : 0.0);
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n <= 0 || outCh <= 0) return;
    if (inCh > 0 && recorder.isRecording()) recorder.push (buffer.getReadPointer (0), inCh > 1 ? buffer.getReadPointer (1) : nullptr, n);
    const bool passThrough = wrapperType != wrapperType_Standalone && thru->load() > 0.5f && inCh > 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        if (! passThrough) buffer.clear (ch, 0, n);
        else if (ch >= inCh && ch < outCh) buffer.copyFrom (ch, 0, buffer, 0, 0, n);   // a mono input to both sides
        else if (ch >= outCh) buffer.clear (ch, 0, n);
    }
    const float gain = juce::Decibels::decibelsToGain (level->load(), -100.0f);
    if (outCh >= 2)
    {
        preview.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, hostRate, gain);
        kitPlayer.level.store (gain);
        kitPlayer.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        // the keys go to LOOPS or to KEYS, whichever of the two was shown last
        const bool toKeys = midiTarget.load() == 3;
        std::array<bc::KeysNote, 512> kn; int numKn = 0;
        if (toKeys)
            for (int i = 0; i < numNotes; ++i)
            {
                const bc::KeysNote e { notes[(size_t) i].offset, notes[(size_t) i].note, notes[(size_t) i].velocity, notes[(size_t) i].on };
                numKn += bcExpandChord (keysChord.load(), chordRoot.load(), chordScale.load(), e, kn.data() + numKn, (int) kn.size() - numKn);
            }
        loopPlayer.level.store (gain);
        loopPlayer.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, host, toKeys ? nullptr : notes.data(), toKeys ? 0 : numNotes);
        keysPlayer.level.store (gain);
        keysPlayer.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, kn.data(), numKn);
    }
    else
    {
        // a mono output: the preview's two sides summed (in pieces of the scratch buffer's size)
        float* L = buffer.getWritePointer (0);
        const int cap = (int) scratch.size();
        for (int o = 0; o < n; o += cap)
        {
            const int m = std::min (cap, n - o);
            std::fill (scratch.begin(), scratch.begin() + m, 0.0f);
            preview.process (scratch.data(), scratch.data(), m, hostRate, 0.5f * gain);   // both sides into one: their average
            for (int i = 0; i < m; ++i) L[o + i] += scratch[(size_t) i];
        }
    }
}

// ---------------------------------------------------------------- files

bool BcProcessor::isAudioFile (const juce::File& f)
{
    static const juce::StringArray ext { ".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac", ".ogg", ".mp3" };
    return f.existsAsFile() && ext.contains (f.getFileExtension().toLowerCase());
}

int BcProcessor::addPaths (const juce::StringArray& paths)
{
    std::vector<std::string> add;
    for (const auto& p : paths)
    {
        const juce::File f (p);
        if (f.isDirectory())
        {
            auto found = f.findChildFiles (juce::File::findFiles, true, "*", juce::File::FollowSymlinks::noCycles);
            found.sort();
            for (const auto& c : found) if (isAudioFile (c) && add.size() < 2000) add.push_back (c.getFullPathName().toStdString());
        }
        else if (isAudioFile (f)) add.push_back (f.getFullPathName().toStdString());
    }
    const size_t before = session.files.size();
    session.addFiles (add);
    const int added = (int) (session.files.size() - before);
    if (added > 0 && ! session.doc()) open ((int) before, 0);
    stateChanged();
    sendChangeMessage();
    return added;
}

void BcProcessor::open (int file, int part)
{
    if (file < 0 || file >= (int) session.files.size()) return;
    stopPlayback();
    openFile = file; openPart = std::max (0, part);
    viewStart = viewEnd = 0.0;   // the editor fits the new part
    session.open (file, openPart);
    stateChanged();
}

void BcProcessor::removeFiles (std::vector<int> rows)
{
    std::sort (rows.begin(), rows.end());
    rows.erase (std::unique (rows.begin(), rows.end()), rows.end());
    rows.erase (std::remove_if (rows.begin(), rows.end(), [this] (int r) { return r < 0 || r >= (int) session.files.size(); }), rows.end());
    if (rows.empty()) return;
    const auto d = session.doc();
    const int openRow = d ? d->file : -1;
    const bool openGoes = openRow >= 0 && std::binary_search (rows.begin(), rows.end(), openRow);
    if (openGoes) stopPlayback();
    for (auto r = rows.rbegin(); r != rows.rend(); ++r)
    {
        session.removeFile (*r);
        if (openFile == *r) openFile = -1; else if (openFile > *r) --openFile;
    }
    const int n = (int) rows.size();
    lastMessage = "Took " + juce::String (n) + (n == 1 ? " file" : " files") + " off the list";
    stateChanged();
    sendChangeMessage();
    if (openGoes && ! session.files.empty())
    {
        // the first file left after the one that was open
        const int below = (int) (std::lower_bound (rows.begin(), rows.end(), openRow) - rows.begin());
        open (std::min (openRow - below, (int) session.files.size() - 1), 0);
    }
}

void BcProcessor::clearFiles()
{
    const int n = (int) session.files.size();
    if (n == 0) return;
    stopPlayback();
    session.clearFiles();
    openFile = -1; openPart = 0; viewStart = viewEnd = 0.0;
    lastMessage = "Took all " + juce::String (n) + (n == 1 ? " file" : " files") + " off the list (the files themselves are untouched; the kit pool keeps its sounds)";
    stateChanged();
    sendChangeMessage();
}

// ---------------------------------------------------------------- playback

void BcProcessor::playEvent (const bc::Event& e)
{
    auto d = session.doc();
    if (! d || ! d->audio || ! d->an) return;
    std::vector<float> l, r;
    bc::renderEvent (*d->audio, *d->an, d->layers, e, session.settings.exp, -1, l, r);
    preview.playClip (std::move (l), std::move (r), d->audio->sr);
    playDoc = d.get(); playOffset = e.start; playingEvent = true;
}

void BcProcessor::playRange (int64_t from, int64_t to, bool loop)
{
    auto d = session.doc();
    if (! d || ! d->audio || ! d->an) return;
    preview.play (d->audio, d->an, d->layers, from, to, loop);
    playDoc = d.get(); playOffset = 0; playingEvent = false;
}

int64_t BcProcessor::playhead() const
{
    auto d = session.doc();
    if (! d || d.get() != playDoc) return -1;
    const int64_t p = preview.position();
    return p < 0 ? -1 : p + playOffset;
}

// ---------------------------------------------------------------- recording the input

juce::File BcProcessor::recordingsDir() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Batchcut").getChildFile ("Recordings");
}

// the assigned WAV folder (Settings). Until one is assigned: Music/Batchcut/One-shots, as before.
juce::File BcProcessor::exportDir() const
{
    return shared::Settings::load().wavs();
}

// the assigned MIDI folder (Settings). Until one is assigned: Documents/Marbelite/MIDI.
juce::File BcProcessor::midiDir() const
{
    return shared::Settings::load().midis();
}

bool BcProcessor::startRecording()
{
    if (getTotalNumInputChannels() <= 0) { lastMessage = "No input to record: give Batchcut an input (in the standalone app: Options, Audio settings)"; return false; }
    recorder.prepare (hostRate);
    recorder.start();
    lastMessage = "Recording";
    return true;
}

juce::File BcProcessor::stopRecording()
{
    if (! recorder.isRecording()) return {};
    const bool lost = recorder.overflowed();
    const double sr = recorder.rate();
    std::vector<float> l, r;
    recorder.stop (&l, &r);
    if (l.size() < (size_t) (0.05 * sr)) { lastMessage = "Nothing recorded"; return {}; }
    const auto dir = recordingsDir();
    dir.createDirectory();
    auto f = dir.getNonexistentChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);
    std::string err;
    if (! writeWav (f, l, r, sr, 24, err)) { lastMessage = "The recording could not be saved: " + juce::String (err); return {}; }
    lastMessage = "Saved " + f.getFileName() + (lost ? " (the 30-minute limit was reached)" : "");
    addPaths ({ f.getFullPathName() });
    const std::string p = f.getFullPathName().toStdString();
    for (int i = 0; i < (int) session.files.size(); ++i) if (session.files[(size_t) i].info.path == p) open (i, 0);
    return f;
}

// ---------------------------------------------------------------- writing

bool BcProcessor::writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, double sr, int bits, std::string& err)
{
    if (l.empty() || sr <= 0) { err = "nothing to write"; return false; }
    // written beside the target and moved into place, so a half-written file never has the real name
    const auto tmp = f.getSiblingFile (f.getFileName() + ".part");
    tmp.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (tmp);
    if (static_cast<juce::FileOutputStream*> (os.get())->failedToOpen()) { err = "could not write " + f.getFullPathName().toStdString(); return false; }
    juce::WavAudioFormat wav;
    const auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (bits)
                          .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr) { tmp.deleteFile(); err = "could not write " + f.getFullPathName().toStdString(); return false; }
    const float* ch[2] = { l.data(), r.size() == l.size() ? r.data() : l.data() };
    const bool ok = w->writeFromFloatArrays (ch, 2, (int) l.size());
    w.reset();
    if (! ok) { tmp.deleteFile(); err = "could not write " + f.getFullPathName().toStdString() + " (is the disk full?)"; return false; }
    if (! tmp.moveFileTo (f)) { tmp.deleteFile(); err = "could not replace " + f.getFullPathName().toStdString(); return false; }
    return true;
}

juce::File BcProcessor::renderToTemp (const bc::Event& e)
{
    auto d = session.doc();
    if (! d || ! d->audio || ! d->an || d->file < 0 || d->file >= (int) session.files.size()) return {};
    std::vector<float> l, r;
    bc::renderEvent (*d->audio, *d->an, d->layers, e, session.settings.exp, -1, l, r);
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Batchcut");
    dir.createDirectory();
    const auto t = (double) (e.onset + d->offset) / d->audio->sr;
    const juce::String stem = juce::String::fromUTF8 (session.files[(size_t) d->file].info.name.c_str())
                            + "_" + juce::String (t, 3).replaceCharacter ('.', '_') + "s";
    const auto f = dir.getChildFile (juce::File::createLegalFileName (stem) + ".wav");
    std::string err;
    return writeWav (f, l, r, d->audio->sr, session.settings.exp.bits, err) ? f : juce::File();
}

// ---------------------------------------------------------------- state

void BcProcessor::refreshStateCache()
{
    juce::XmlElement root ("BATCHCUT");
    root.setAttribute ("version", 1);
    if (auto params = apvts.copyState().createXml()) root.addChildElement (params.release());
    root.addChildElement (bcstate::writeSession (session).release());
    auto* v = root.createNewChildElement ("VIEW");
    v->setAttribute ("file", openFile); v->setAttribute ("part", openPart);
    v->setAttribute ("start", viewStart); v->setAttribute ("end", viewEnd);
    if (kitXmlVersion != kitVersion || kitXmlCache == nullptr) { kitXmlCache = kitToXml(); kitXmlVersion = kitVersion; }   // (its sounds are encoded only when the kit changed)
    root.addChildElement (new juce::XmlElement (*kitXmlCache));
    if (loopsXmlVersion != loopsVersion || loopsXmlCache == nullptr) { loopsXmlCache = loopsToXml(); loopsXmlVersion = loopsVersion; }
    root.addChildElement (new juce::XmlElement (*loopsXmlCache));
    if (keysXmlVersion != keysVersion || keysXmlCache == nullptr) { keysXmlCache = keysToXml(); keysXmlVersion = keysVersion; }
    root.addChildElement (new juce::XmlElement (*keysXmlCache));
    juce::MemoryBlock mb;
    copyXmlToBinary (root, mb);
    const juce::ScopedLock sl (stateLock);
    stateCache = std::move (mb);
    stateDirty = false;
}

void BcProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // the session belongs to the message thread: built there, and only copied here when a host asks from elsewhere
    if (juce::MessageManager::existsAndIsCurrentThread()) refreshStateCache();
    const juce::ScopedLock sl (stateLock);
    dest = stateCache;
}

void BcProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("BATCHCUT")) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) { applyState (*xml); return; }
    { const juce::ScopedLock sl (stateLock); pendingState = std::move (xml); }
    triggerAsyncUpdate();
}

void BcProcessor::applyState (const juce::XmlElement& root)
{
    if (auto* p = root.getChildByName ("PARAMS")) apvts.replaceState (juce::ValueTree::fromXml (*p));
    stopPlayback();
    if (recorder.isRecording()) stopRecording();
    if (auto* k = root.getChildByName ("KIT")) kitFromXml (*k);
    if (auto* lp = root.getChildByName ("LOOPS")) loopsFromXml (*lp);
    if (auto* ks = root.getChildByName ("KEYS")) keysFromXml (*ks);
    if (auto* s = root.getChildByName ("SESSION"))
    {
        std::vector<std::string> paths;
        bcstate::readSession (*s, session, paths);
        if (session.settings.outDir.empty()) session.settings.outDir = exportDir().getFullPathName().toStdString();
        session.close();
        session.refresh();
    }
    openFile = -1; openPart = 0; viewStart = viewEnd = 0.0;
    if (auto* v = root.getChildByName ("VIEW"))
    {
        const int f = v->getIntAttribute ("file", -1), part = v->getIntAttribute ("part", 0);
        if (f >= 0 && f < (int) session.files.size())
        {
            open (f, part);
            viewStart = std::max (0.0, v->getDoubleAttribute ("start")); viewEnd = std::max (viewStart, v->getDoubleAttribute ("end"));
        }
    }
    stateDirty = true;
    sendChangeMessage();
}

void BcProcessor::handleAsyncUpdate()
{
    std::unique_ptr<juce::XmlElement> st;
    { const juce::ScopedLock sl (stateLock); st = std::move (pendingState); }
    if (st != nullptr) applyState (*st);
    const auto before = session.doc();
    session.applyResults();
    kitTakeGathered();
    if (session.doc() != before) stateDirty = true;
    sendChangeMessage();
}

void BcProcessor::timerCallback()
{
    recorder.service();
    preview.collect();
    if (playDoc != nullptr && ! preview.playing()) { playDoc = nullptr; playingEvent = false; }
    // a host that asks for the state from another thread still gets a recent one
    if (++ticks % 20 == 0 && stateDirty) refreshStateCache();
    // LOOPS: the snapshots the player may still be reading stay alive 15 s; in a DAW the loop takes the song's tempo
    const auto now = juce::Time::getMillisecondCounter();
    while (loopKeep.size() > 1 && now - loopKeep.front().first > 15000) loopKeep.pop_front();
    while (keysKeep.size() > 1 && now - keysKeep.front().first > 30000) keysKeep.pop_front();
    chordRoot.store (loops.root); chordScale.store (loops.scale);
    const double hb = hostBpm.load();
    if (hb > 1.0 && std::fabs (hb - loops.bpm) > 0.01) { loops.bpm = hb; loopsSaved.bpm = hb; ++loopsVersion; stateDirty = true; }
}

juce::AudioProcessorEditor* BcProcessor::createEditor() { return new BcEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BcProcessor(); }
