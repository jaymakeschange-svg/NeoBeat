// BatchCut - the processor.
#include "Processor.h"
#include "State.h"
#include "Editor.h"
#include "neobeat/shared/learner/FitJob.h"
#include "neobeat/shared/learner/MaterialFile.h"
#ifndef BATCHCUT_VERSION
 #define BATCHCUT_VERSION "3.0.0"
#endif

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
    fitJob.reset();                 // cancels a fit still running, and waits for its threads
    breedJob.reset();               // likewise a generation
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

        // MATERIAL: the learned body, struck from the room. Only while that room is showing, so
        // it costs nothing anywhere else.
        if (materialLive.load() && material.valid())
        {
            const float hit = materialPending.exchange (0.0f);
            if (hit > 0.0f) materialVoice.strike (hit);
            if (materialVoice.isRinging())
                materialVoice.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        }
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






// ---------------------------------------------------------------- playback



int64_t BcProcessor::playhead() const
{
    auto d = session.doc();
    if (! d || d.get() != playDoc) return -1;
    const int64_t p = preview.position();
    return p < 0 ? -1 : p + playOffset;
}

// ---------------------------------------------------------------- recording the input




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
    fitCollect();
    breedCollect();
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

namespace
{
// A rendered sound, ready for the pool: analysed the same way an imported file is, so everything
// downstream treats it identically.
std::shared_ptr<bc::PoolItem> poolItemFrom (std::vector<float> l, std::vector<float> r, double sr,
                                            const juce::String& name, const juce::String& source)
{
    auto it = std::make_shared<bc::PoolItem>();
    it->l = std::move (l); it->r = std::move (r); it->sr = sr;
    it->name = name.toStdString(); it->source = source.toStdString();
    bc::analyseItem (*it, nbkstore::learner());
    return it;
}
}

// ------------------------------------------------------------------ MaterialLab
// The learner fits a physical model to a recording and writes it out as a material file. These
// three turn that file into sound: open one, strike it, or send a pool sound through it.
//
// Everything made here lands in the pool as an ordinary sound, which is the point - cutting,
// classifying, kit building and .NJQ export all work on it without knowing where it came from.

// A material file's every parameter (the learner's engine2), for breeding: false if the file lacks any.
static bool bodyFromFile (const juce::File& file, ml::Body& b)
{
    const auto j = juce::JSON::parse (file);
    auto* params = j.getProperty ("params", {}).getDynamicObject();
    if (params == nullptr) return false;
    auto vec = [params] (const char* key, std::vector<double>& v) { v.clear(); if (auto* a = params->getProperty (key).getArray()) for (const auto& q : *a) v.push_back ((double) q); return ! v.empty(); };
    bool ok = vec ("f", b.f) && vec ("a", b.a) && vec ("rho", b.rho) && vec ("delta", b.delta) && vec ("beta", b.beta) && vec ("eps", b.eps) && vec ("glide", b.glide) && vec ("tau_g", b.tau_g);
    for (auto* v : { &b.a, &b.rho, &b.delta, &b.beta, &b.eps, &b.glide, &b.tau_g }) ok = ok && v->size() == b.f.size();
    double* laws[] = { &b.b1, &b.b3, &b.C, &b.gamma, &b.tau_c, &b.G, &b.tau_a, &b.cx_amt, &b.cx_t, &b.cx_f, &b.lb_m, &b.lb_rate, &b.lb_f, &b.tau_n };
    for (int k = 0; k < ml::kGlobalKeys; ++k)
    {
        const auto v = params->getProperty (ml::globalKeyName (k));
        if (v.isVoid()) return false;
        *laws[k] = (double) v;
    }
    std::vector<double> nz, dn, dr;
    if (! vec ("noise", nz) || nz.size() < (size_t) ml::kBands) return false;
    if (! vec ("dn", dn)) dn.assign ((size_t) ml::kBands, 1e-6);                 // as engine2.complete fills them
    if (! vec ("dn_rho", dr)) dr.assign ((size_t) ml::kBands, 0.0);
    if (dn.size() < (size_t) ml::kBands || dr.size() < (size_t) ml::kBands) return false;
    for (int i = 0; i < ml::kBands; ++i) { b.noise[(size_t) i] = nz[(size_t) i]; b.dn[(size_t) i] = dn[(size_t) i]; b.dn_rho[(size_t) i] = dr[(size_t) i]; }
    return ok;
}

bool BcProcessor::materialOpen (const juce::File& file, juce::String& error)
{
    nsr::Material m;
    if (! m.load (file, error)) return false;
    material = std::move (m);
    loadedBodyValid = bodyFromFile (file, loadedBody);
    if (! breedBusy()) { breedKept.clear(); breedChildren.clear(); breedKeptFlags.clear(); ++breedGeneration; }   // breeding starts again from here
    materialVoice.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0, 256);
    materialVoice.setMaterial (material);
    lastMessage = "Opened " + material.name + " - " + juce::String (material.numModes) + " modes, rings "
                + juce::String (material.ringSeconds(), 2) + " s";
    roomsNotify();
    return true;
}

int BcProcessor::materialRender (int hits, juce::String& error)
{
    if (! material.valid()) { error = "Open a material first."; return 0; }

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const int n = (int) (std::min (6.0f, material.ringSeconds() + 0.1f) * sr);
    int made = 0;

    for (int h = 0; h < hits; ++h)
    {
        // Each hit is a different hit. The strike lands at a different velocity and the pickups
        // move a little, so no two are the same waveform - which is the whole reason to render
        // from a model rather than copy a sample.
        nsr::MaterialVoice v;
        v.prepare (sr, 256);
        nsr::Pickups pk;
        pk.width = 0.25f + 0.25f * (float) h / (float) std::max (1, hits - 1);
        v.setMaterial (material, pk);
        v.strike (0.55f + 0.45f * (float) ((h * 7) % 11) / 10.0f);

        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        v.process (l.data(), r.data(), n);

        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = std::max (peak, std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
        if (peak < 1.0e-5f) continue;
        const float g = 0.89f / peak;
        for (int i = 0; i < n; ++i) { l[(size_t) i] *= g; r[(size_t) i] *= g; }

        const auto nm = material.name + " " + juce::String (h + 1);
        kit.pool.push_back (poolItemFrom (std::move (l), std::move (r), sr, nm, "material:" + material.name));
        ++made;
    }

    if (made == 0) { error = "Nothing came out - the material may be silent."; return 0; }
    kitChanged();
    stateChanged();
    lastMessage = "Rendered " + juce::String (made) + (made == 1 ? " hit" : " hits") + " into the pool";
    roomsNotify();
    return made;
}

bool BcProcessor::materialThrough (int poolIndex, float mix, juce::String& error)
{
    if (! material.valid()) { error = "Open a material first."; return false; }
    if (! juce::isPositiveAndBelow (poolIndex, (int) kit.pool.size()))
    { error = "Pick a sound from the pool to send through it."; return false; }

    const auto src = kit.pool[(size_t) poolIndex];
    if (src == nullptr || src->l.empty()) { error = "That sound is empty."; return false; }

    const double sr = src->sr > 1000.0 ? src->sr : (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    // long enough for the body to ring on past the end of the sound
    const int tail = (int) (std::min (4.0f, material.ringSeconds()) * sr);
    const int n = (int) src->l.size() + tail;

    std::vector<float> in ((size_t) n, 0.0f);
    for (size_t i = 0; i < src->l.size(); ++i)
        in[i] = src->r.empty() ? src->l[i] : 0.5f * (src->l[i] + src->r[i]);

    nsr::MaterialVoice v;
    v.prepare (sr, 256);
    v.setMaterial (material);

    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    v.process (l.data(), r.data(), n, in.data(), 1.0f);

    // Resonators have a lot of gain at their peaks, so match the wet to the dry before mixing;
    // otherwise "mix" would really be a volume control.
    auto rmsOf = [] (const std::vector<float>& x) {
        double s = 0.0; for (float y : x) s += (double) y * y;
        return std::sqrt (s / std::max<size_t> (1, x.size()));
    };
    const double dry = rmsOf (in), wet = rmsOf (l);
    const float match = wet > 1.0e-9 ? (float) (dry / wet) : 1.0f;

    const float m = juce::jlimit (0.0f, 1.0f, mix);
    for (int i = 0; i < n; ++i)
    {
        const float d = in[(size_t) i];
        l[(size_t) i] = d * (1.0f - m) + l[(size_t) i] * match * m;
        r[(size_t) i] = d * (1.0f - m) + r[(size_t) i] * match * m;
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max (peak, std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i])));
    if (peak < 1.0e-6f) { error = "Nothing came out of it."; return false; }
    if (peak > 0.89f) { const float g = 0.89f / peak; for (int i = 0; i < n; ++i) { l[(size_t) i] *= g; r[(size_t) i] *= g; } }

    const auto nm = juce::String (src->name) + " through " + material.name;
    kit.pool.push_back (poolItemFrom (std::move (l), std::move (r), sr, nm, "material:" + material.name));
    kitChanged();
    stateChanged();
    lastMessage = "Added " + nm;
    roomsNotify();
    return true;
}

// ------------------------------------------------------------------ fitting any sound
// The learner's fit, run in Batchcut (docs/PROPOSED-FEATURES.md, stage 7). The user decided that a
// sound gets several fits side by side and every one goes into the pool, closest first: the fit is
// path-dependent, so fits started a rounding step apart end as different materials, and the spread
// is the first set of variants.

juce::File BcProcessor::materialsDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("MARBELITE_MATERIALS_DIR", {});
    return env.isNotEmpty() ? juce::File (env)
                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Marbelite").getChildFile ("Materials");
}

bool BcProcessor::fitStartFile (const juce::File& audio, juce::String& error)
{
    if (fitJob != nullptr) { error = "A fit is already running - let it finish, or Cancel it first."; return false; }
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (audio));
    if (r == nullptr) { error = audio.getFileName() + " is not an audio file Batchcut can read."; return false; }
    // the first minute is plenty: the fit takes the first strike and the 1.5 s after it
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    if (len <= 0 || r->sampleRate < 1000.0 || r->numChannels == 0) { error = audio.getFileName() + " is empty."; return false; }
    juce::AudioBuffer<float> buf ((int) r->numChannels, len);
    if (! r->read (&buf, 0, len, 0, true, true)) { error = audio.getFileName() + " could not be read."; return false; }
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
    return fitStart (std::move (mono), r->sampleRate, audio.getFileNameWithoutExtension(), audio.getFullPathName(), error);
}

bool BcProcessor::fitStartPool (int poolIndex, juce::String& error)
{
    if (fitJob != nullptr) { error = "A fit is already running - let it finish, or Cancel it first."; return false; }
    if (! juce::isPositiveAndBelow (poolIndex, (int) kit.pool.size())) { error = "Pick a sound from the pool to fit."; return false; }
    const auto src = kit.pool[(size_t) poolIndex];
    if (src == nullptr || src->l.empty()) { error = "That sound is empty."; return false; }
    std::vector<float> mono (src->l.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = src->r.empty() ? src->l[i] : 0.5f * (src->l[i] + src->r[i]);
    const juce::String name (src->name);
    return fitStart (std::move (mono), src->sr > 1000.0 ? src->sr : 48000.0, name, "pool:" + name, error);
}

bool BcProcessor::fitStart (std::vector<float> mono, double sr, const juce::String& name, const juce::String& from, juce::String& error)
{
    if (breedJob != nullptr) { error = "Wait for the generation to finish first, or Cancel it."; return false; }
    float peak = 0.0f;
    for (float v : mono) peak = std::max (peak, std::fabs (v));
    if (peak < 1.0e-5f) { error = "That sound is silent - there is nothing to fit."; return false; }
    // one core is left for the host and the audio; four fits use at most four
    const int threads = juce::jlimit (1, kFitsPerSound, juce::SystemStats::getNumCpus() - 1);
    fitJob = std::make_unique<ml::FitJob> (std::move (mono), sr, kFitsPerSound, threads, 300, 6.0);
    fitSource = name.isNotEmpty() ? name : juce::String ("sound");
    fitFrom = from;
    ++fitGeneration;
    lastMessage = "Fitting " + fitSource + " - " + juce::String (kFitsPerSound) + " fits on " + juce::String (threads)
                + (threads == 1 ? " core" : " cores");
    roomsNotify();
    return true;
}

void BcProcessor::fitCancel()
{
    if (fitJob != nullptr) fitJob->cancel();
}

double BcProcessor::fitProgress() const { return fitJob != nullptr ? fitJob->progress() : 0.0; }

double BcProcessor::fitSecondsLeft() const
{
    if (fitJob == nullptr) return -1.0;
    const double p = fitJob->progress(), t = fitJob->elapsed();
    return p > 0.05 ? t * (1.0 - p) / p : -1.0;
}

void BcProcessor::fitCollect()
{
    if (fitJob == nullptr || ! fitJob->finished()) return;
    const auto job = std::move (fitJob);            // fitBusy() is false from here on
    ++fitGeneration;
    if (job->wasCancelled()) { lastMessage = "Fitting " + fitSource + " cancelled."; roomsNotify(); return; }
    if (! job->error().empty()) { lastMessage = fitSource + ": " + juce::String (job->error()); roomsNotify(); return; }
    const auto results = job->results();
    if (results.empty()) { lastMessage = "Nothing came of fitting " + fitSource + "."; roomsNotify(); return; }
    // what breeding grows toward from now on
    breedTarget = job->clip(); breedHasTarget = breedTarget.ok(); breedTargetName = fitSource;
    breedKept.clear(); breedChildren.clear(); breedKeptFlags.clear();

    // one folder per job, so a second fit of the same sound keeps the first's materials
    const auto base = materialsDir();
    const auto legal = juce::File::createLegalFileName (fitSource);
    auto dir = base.getChildFile (legal);
    for (int i = 2; dir.exists() && ! dir.findChildFiles (juce::File::findFiles, false, "*.json").isEmpty(); ++i)
        dir = base.getChildFile (legal + " " + juce::String (i));
    const bool canWrite = dir.createDirectory().wasOk();

    int made = 0, saved = 0;
    fitBest = juce::File();
    for (size_t k = 0; k < results.size(); ++k)
    {
        const auto& f = results[k];
        const auto name = fitSource + " fit " + juce::String ((int) k + 1);
        if (canWrite)
        {
            const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".json");
            ml::MaterialInfo info;
            info.name = name.toStdString(); info.source = fitFrom.toStdString(); info.madeBy = "Batchcut " BATCHCUT_VERSION;
            info.rank = (int) k + 1; info.of = (int) results.size();
            info.loss = f.loss; info.startLoss = f.startLoss; info.seconds = f.seconds;
            info.noiseFloor = job->clip().noiseFloor; info.nSamples = (int) job->clip().x.size();
            if (file.replaceWithText (juce::String::fromUTF8 (ml::materialJson (f.params, info).c_str())))
            {
                ++saved;
                if (k == 0) fitBest = file;
            }
        }
        float peak = 0.0f;
        for (float v : f.strike) peak = std::max (peak, std::fabs (v));
        if (peak < 1.0e-5f) continue;
        std::vector<float> l (f.strike.size());
        const float g = 0.89f / peak;
        for (size_t i = 0; i < l.size(); ++i) l[i] = f.strike[i] * g;
        auto r = l;
        kit.pool.push_back (poolItemFrom (std::move (l), std::move (r), ml::kFitRate, name, "material:" + name));
        ++made;
    }
    fitFolder = saved > 0 ? dir : juce::File();
    if (made > 0) { kitChanged(); stateChanged(); }
    lastMessage = "Fitted " + fitSource + ": " + juce::String (made) + (made == 1 ? " sound" : " sounds") + " in the pool, closest "
                + juce::String (results.front().loss, 2) + " (from " + juce::String (results.front().startLoss, 2) + ")"
                + (saved > 0 ? ", materials in " + dir.getFileName()
                             : ", but the materials could not be saved in " + base.getFullPathName());
    roomsNotify();
}

// ------------------------------------------------------------------ breeding (7b)

bool BcProcessor::breedStart (bool grow, juce::String& error)
{
    if (breedJob != nullptr) { error = "A generation is already on its way - let it finish, or Cancel it."; return false; }
    if (fitJob != nullptr) { error = "Wait for the fit to finish first."; return false; }
    ml::BreedRequest q;
    q.mode = grow ? ml::BreedRequest::Grow : ml::BreedRequest::Wander;
    bool keptBody = false;
    for (const auto& k : breedKept) keptBody = keptBody || k.isBody;
    for (const auto& k : breedKept) if (k.isBody || ! grow) q.parents.push_back (k);
    if (! keptBody && loadedBodyValid)
    {
        ml::Source s; s.name = material.name.toStdString(); s.isBody = true; s.body = loadedBody;
        q.parents.insert (q.parents.begin(), std::move (s));
    }
    if (breedHasTarget && ! grow)
    {
        ml::Source s; s.name = breedTargetName.toStdString(); s.sound = breedTarget.x;
        q.parents.push_back (std::move (s));
    }
    bool anyBody = false;
    for (const auto& p : q.parents) anyBody = anyBody || p.isBody;
    if (grow && ! breedHasTarget) { error = "Grow climbs toward a sound: drop one to fit first."; return false; }
    if (grow && ! anyBody) { error = "Grow breeds materials: load or keep one first."; return false; }
    if (q.parents.empty()) { error = "Load a material or fit a sound first."; return false; }
    q.hasTarget = breedHasTarget;
    if (breedHasTarget) q.target = breedTarget;
    q.reach = juce::jlimit (0.0, 1.0, (double) breedReach);
    q.generations = 12;
    q.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
    q.threads = juce::jlimit (1, 4, juce::SystemStats::getNumCpus() - 1);
    breedGrowing = grow; breedFromScratch = false;
    breedJob = std::make_unique<ml::BreedJob> (std::move (q));
    ++breedGeneration;
    lastMessage = grow ? "Growing toward " + breedTargetName : "Wandering";
    roomsNotify();
    return true;
}

bool BcProcessor::breedScratch (juce::String& error)
{
    if (breedJob != nullptr) { error = "A generation is already on its way - let it finish, or Cancel it."; return false; }
    if (fitJob != nullptr) { error = "Wait for the fit to finish first."; return false; }
    ml::BreedRequest q;
    q.mode = ml::BreedRequest::Scratch;
    q.hasTarget = breedHasTarget;
    if (breedHasTarget) q.target = breedTarget;
    q.reach = juce::jlimit (0.0, 1.0, (double) breedReach);
    q.seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
    q.threads = juce::jlimit (1, 4, juce::SystemStats::getNumCpus() - 1);
    breedGrowing = false; breedFromScratch = true;
    breedJob = std::make_unique<ml::BreedJob> (std::move (q));
    ++breedGeneration;
    lastMessage = "Making from scratch";
    roomsNotify();
    return true;
}

void BcProcessor::breedCancel() { if (breedJob != nullptr) breedJob->cancel(); }
double BcProcessor::breedProgress() const { return breedJob != nullptr ? breedJob->progress() : 0.0; }
double BcProcessor::breedSecondsLeft() const
{
    if (breedJob == nullptr) return -1.0;
    const double p = breedJob->progress(), t = breedJob->elapsed();
    return p > 0.05 ? t * (1.0 - p) / p : -1.0;
}

void BcProcessor::breedCollect()
{
    if (breedJob == nullptr || ! breedJob->finished()) return;
    const auto job = std::move (breedJob);
    ++breedGeneration;
    if (job->wasCancelled()) { lastMessage = breedGrowing ? "Grow cancelled." : breedFromScratch ? "Scratch cancelled." : "Cancelled."; roomsNotify(); return; }
    if (! job->error().empty()) { lastMessage = juce::String (job->error()); roomsNotify(); return; }
    breedChildren = job->results();
    breedKeptFlags.assign (breedChildren.size(), 0);
    ++breedGenerationNumber;
    lastMessage = "Generation " + juce::String (breedGenerationNumber) + ": " + juce::String ((int) breedChildren.size()) + " children"
                + (breedFromScratch ? " from scratch" : "");
    if (! breedChildren.empty() && std::isfinite (breedChildren.front().loss))
        lastMessage << ", the closest " << juce::String (breedChildren.front().loss, 2) << " from " << breedTargetName;
    lastMessage << " - click one to hear it, Keep the ones to breed from";
    roomsNotify();
}

bool BcProcessor::breedPlay (int child)
{
    if (! juce::isPositiveAndBelow (child, (int) breedChildren.size())) return false;
    const auto& c = breedChildren[(size_t) child];
    float peak = 0.0f; for (float v : c.sound) peak = std::max (peak, std::fabs (v));
    if (peak <= 0.0f) return false;
    std::vector<float> l (c.sound.size());
    for (size_t i = 0; i < l.size(); ++i) l[i] = c.sound[i] * (0.89f / peak);
    auto r = l;
    preview.playClip (std::move (l), std::move (r), ml::kFitRate);
    return true;
}

bool BcProcessor::breedKeep (int child, juce::String& error)
{
    if (! juce::isPositiveAndBelow (child, (int) breedChildren.size())) { error = "Pick a child to keep."; return false; }
    if (breedKeptFlags[(size_t) child]) { error = "Already kept."; return false; }
    const auto& c = breedChildren[(size_t) child];
    // a child from scratch is no one's: "scratch g3 2", its material in Materials/scratch
    const bool fromScratch = c.kind == ml::ChildKind::Scratch;
    const auto base = fromScratch ? juce::String ("scratch") : breedTargetName.isNotEmpty() ? breedTargetName : material.name;
    const auto name = base + " g" + juce::String (breedGenerationNumber) + " " + juce::String (child + 1);
    float peak = 0.0f; for (float v : c.sound) peak = std::max (peak, std::fabs (v));
    if (peak < 1.0e-6f) { error = "That one is silent."; return false; }
    std::vector<float> l (c.sound.size());
    for (size_t i = 0; i < l.size(); ++i) l[i] = c.sound[i] * (0.89f / peak);
    auto r = l;
    kit.pool.push_back (poolItemFrom (std::move (l), std::move (r), ml::kFitRate, name, "bred:" + name));
    juce::String saved;
    if (c.isBody)
    {
        auto dir = fitFolder.isDirectory() && ! fromScratch ? fitFolder : materialsDir().getChildFile (juce::File::createLegalFileName (base));
        if (dir.createDirectory().wasOk())
        {
            ml::MaterialInfo info;
            info.name = name.toStdString(); info.source = "bred (" + std::string (ml::kindName (c.kind)) + "): " + c.how; info.madeBy = "Batchcut " BATCHCUT_VERSION;
            info.loss = std::isfinite (c.loss) ? c.loss : 0.0; info.startLoss = info.loss;
            info.noiseFloor = breedHasTarget ? breedTarget.noiseFloor : -70.0; info.nSamples = breedHasTarget ? (int) breedTarget.x.size() : 0;
            const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".json");
            if (file.replaceWithText (juce::String::fromUTF8 (ml::materialJson (c.body, info).c_str()))) saved = ", and its material in " + dir.getFileName();
        }
    }
    ml::Source k = c; k.name = name.toStdString();
    breedKept.push_back (std::move (k));
    breedKeptFlags[(size_t) child] = 1;
    kitChanged(); stateChanged();
    lastMessage = "Kept " + name + " in the pool" + saved + " - the next generation breeds from what you keep";
    roomsNotify();
    return true;
}
