#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Decay: x0.25 .. x4 with x1 at the centre of the knob.
juce::NormalisableRange<float> decayRange()
{
    return { 0.25f, 4.0f,
             [] (float lo, float hi, float v) { return lo * std::pow (hi / lo, v); },
             [] (float lo, float hi, float x) { return std::log (x / lo) / std::log (hi / lo); },
             [] (float lo, float hi, float x) { return juce::jlimit (lo, hi, x); } };
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout NeoBeatProcessor::createLayout (const nb::Library& lib)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-30.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (0.5f, 0.75f, 0.001f), 0.64f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo (standalone)", NormalisableRange<float> (60.0f, 200.0f, 0.1f), 120.0f,
                                                       AudioParameterFloatAttributes().withLabel ("BPM")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Play Mode", StringArray { "Host", "Key", "Pads" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "run", 1 }, "Run (standalone)", false));

    StringArray soundNames;
    for (const auto& s : lib.sounds) soundNames.add (s.name);
    const auto& kit = nb::kits()[0];
    for (int p = 0; p < kPads; ++p)
    {
        const auto& kp = kit.pads[(size_t) p];
        const int si = std::max (0, lib.find (kp.sound));
        const float hollow = kp.hollow >= 0.0f ? kp.hollow : lib.sounds[(size_t) si].defaultHollow;
        const String pre = "p" + String (p + 1) + "_", nm = "Pad " + String (p + 1) + " ";
        auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pre + "sound", 1 }, nm + "Sound", soundNames, si));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "tune", 1 }, nm + "Tune", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), kp.tune,
                                                           AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "decay", 1 }, nm + "Decay", decayRange(), kp.decay,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "x" + String (v, 2); })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "tone", 1 }, nm + "Tone", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), kp.tone, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "drive", 1 }, nm + "Drive", NormalisableRange<float> (0.0f, 1.0f, 0.001f), kp.drive, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "hollow", 1 }, nm + "Hollow", NormalisableRange<float> (0.0f, 1.0f, 0.001f), hollow, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "evolve", 1 }, nm + "Evolve", NormalisableRange<float> (0.0f, 1.0f, 0.001f), kp.evolve, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "pan", 1 }, nm + "Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), kp.pan, pct));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "level", 1 }, nm + "Level", NormalisableRange<float> (-30.0f, 6.0f, 0.1f), kp.levelDb,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pre + "choke", 1 }, nm + "Choke Group", 0, 4, kp.choke));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pre + "mute", 1 }, nm + "Mute", false));
    }
    return layout;
}

NeoBeatProcessor::NeoBeatProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      engine (std::make_unique<nb::Engine>()),
      apvts (*this, nullptr, "NEOBEAT", createLayout (engine->lib()))
{
    formats.registerBasicFormats();
    masterParam = apvts.getRawParameterValue ("master");
    swingParam = apvts.getRawParameterValue ("swing");
    tempoParam = apvts.getRawParameterValue ("tempo");
    modeParam = apvts.getRawParameterValue ("mode");
    runParam = apvts.getRawParameterValue ("run");
    for (int p = 0; p < kPads; ++p)
    {
        auto get = [&] (const char* n) { return apvts.getRawParameterValue (paramId (p, n)); };
        padParams[(size_t) p] = { get ("sound"), get ("tune"), get ("decay"), get ("tone"), get ("drive"), get ("hollow"),
                                  get ("evolve"), get ("pan"), get ("level"), get ("choke"), get ("mute") };
        pendingAudition[(size_t) p].store (0.0f);
        useSample[(size_t) p].store (false);
        sampleRms[(size_t) p] = 0.05f;
    }
    pattern.load (nb::presets()[0].pattern);
    events.reserve (4096);
}

const juce::String NeoBeatProcessor::getProgramName (int index)
{
    if (index < 0 || index >= nb::kNumPresets) return {};
    return nb::presets()[(size_t) index].name;
}

void NeoBeatProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    engine->prepare ((float) sampleRate);
    sequencer.prepare ((float) sampleRate);
    seenVersion = 0;
    tapScratch.assign ((size_t) std::max (samplesPerBlock, 1) * 2, 0.0f);
    for (auto& o : pendingOffs) o = {};
    heldKeys = 0;
}

bool NeoBeatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void NeoBeatProcessor::readParameters() noexcept
{
    engine->master = 0.5f * juce::Decibels::decibelsToGain (masterParam->load());
    const int numSounds = (int) engine->lib().sounds.size();
    for (int p = 0; p < kPads; ++p)
    {
        auto& pp = padParams[(size_t) p];
        auto& ps = engine->pads[(size_t) p];
        ps.sound = juce::jlimit (0, numSounds - 1, (int) pp.sound->load());
        ps.tune = pp.tune->load();
        ps.decay = pp.decay->load();
        ps.tone = pp.tone->load();
        ps.drive = pp.drive->load();
        ps.hollow = pp.hollow->load();
        ps.evolve = pp.evolve->load();
        ps.pan = pp.pan->load();
        ps.levelDb = pp.level->load();
        ps.choke = (int) pp.choke->load();
        ps.useSample = useSample[(size_t) p].load();
        ps.sample = ps.useSample ? std::atomic_load (&liveSamples[(size_t) p]) : nullptr;
        ps.sampleRms = sampleRms[(size_t) p];
    }
}

void NeoBeatProcessor::pushTap (const float* data, int n) noexcept
{
    const auto scope = tapFifo.write (n);
    if (scope.blockSize1 > 0) std::copy (data, data + scope.blockSize1, tapBuffer.data() + scope.startIndex1);
    if (scope.blockSize2 > 0) std::copy (data + scope.blockSize1, data + scope.blockSize1 + scope.blockSize2, tapBuffer.data() + scope.startIndex2);
}

void NeoBeatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;
    readParameters();

    const uint32_t v = pattern.version.load (std::memory_order_acquire);
    if (v != seenVersion)
    {
        pattern.snapshot (patternCopy);
        sequencer.setPattern (patternCopy);
        seenVersion = v;
    }

    // Transport
    double bpm = tempoParam->load(), hostPpq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; hostPlaying = pos->getIsPlaying(); }
        }
    hostSynced.store (hostPlaying);
    displayBpm.store (bpm);
    const int mode = (int) modeParam->load();
    const bool run = runParam->load() > 0.5f;
    const float swing = swingParam->load();

    // MIDI in: pads, key-transpose, panic
    struct Trigger { int offset; int pad; float velocity; float semis; bool fromSequencer; };
    std::array<Trigger, 512> triggers;
    int numTriggers = 0;
    const bool keysWereHeld = heldKeys > 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (note >= kPadNoteBase && note < kPadNoteBase + kPads && numTriggers < (int) triggers.size())
                triggers[(size_t) numTriggers++] = { at, note - kPadNoteBase, m.getFloatVelocity(), 0.0f, false };
            else if (note >= kKeyRoot && mode == 1)
            {
                ++heldKeys;
                keySemis = note - kKeyRoot;
            }
        }
        else if (m.isNoteOff())
        {
            if (m.getNoteNumber() >= kKeyRoot && mode == 1) heldKeys = std::max (0, heldKeys - 1);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldKeys = 0;
            engine->allNotesOff();
        }
    }
    midi.clear();
    if (mode == 1 && heldKeys > 0 && ! keysWereHeld) keyPpq = 0.0;   // a fresh key press starts the pattern from the top

    for (int p = 0; p < kPads; ++p)
    {
        const float a = pendingAudition[(size_t) p].exchange (0.0f);
        if (a > 0.0f && numTriggers < (int) triggers.size()) triggers[(size_t) numTriggers++] = { 0, p, a, 0.0f, false };
    }

    // Sequencer
    events.clear();
    bool seqOn = false;
    double ppq0 = 0.0;
    float semis = 0.0f;
    if (mode == 0)
    {
        if (hostPlaying) { seqOn = true; ppq0 = hostPpq; }
        else if (run) { seqOn = true; ppq0 = internalPpq; }
    }
    else if (mode == 1 && heldKeys > 0)
    {
        seqOn = true;
        ppq0 = hostPlaying ? hostPpq : keyPpq;
        semis = (float) keySemis;
    }
    if (seqOn) sequencer.collect (ppq0, bpm, n, true, swing, semis, events);
    const double ppqPerBlock = bpm / 60.0 * n / sampleRate;
    if (mode == 0 && ! hostPlaying && run) internalPpq += ppqPerBlock;
    else if (! run) internalPpq = 0.0;
    if (mode == 1 && heldKeys > 0 && ! hostPlaying) keyPpq += ppqPerBlock;
    playStep.store (seqOn ? sequencer.stepAt (ppq0) : -1);

    for (const auto& e : events)
        if (numTriggers < (int) triggers.size())
            triggers[(size_t) numTriggers++] = { e.offset, e.pad, e.velocity, semis, true };
    std::stable_sort (triggers.begin(), triggers.begin() + numTriggers, [] (const Trigger& a, const Trigger& b) { return a.offset < b.offset; });

    // Render in segments between hits
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    if ((int) tapScratch.size() < n) return;   // host broke its promised block size; stay silent rather than allocate
    const int tapPad = selectedPad.load();
    int done = 0;
    for (int i = 0; i < numTriggers; ++i)
    {
        const auto& t = triggers[(size_t) i];
        if (t.offset > done)
        {
            engine->render (L + done, R + done, t.offset - done, tapScratch.data() + done, tapPad);
            done = t.offset;
        }
        if (t.fromSequencer && padParams[(size_t) t.pad].mute->load() > 0.5f) continue;
        engine->trigger (t.pad, t.velocity, t.semis);
        if (t.fromSequencer)
        {
            const int note = kPadNoteBase + t.pad;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (t.velocity * 127.0f))), t.offset);
            for (auto& o : pendingOffs)
                if (o.note < 0) { o.note = note; o.samplesLeft = t.offset + (int) (0.06 * sampleRate); break; }
        }
    }
    if (n > done) engine->render (L + done, R + done, n - done, tapScratch.data() + done, tapPad);
    for (auto& o : pendingOffs)
    {
        if (o.note < 0) continue;
        if (o.samplesLeft < n) { midi.addEvent (juce::MidiMessage::noteOff (10, o.note), std::max (0, o.samplesLeft)); o.note = -1; }
        else o.samplesLeft -= n;
    }
    pushTap (tapScratch.data(), n);
}

// ------------------------------------------------------------------ editor actions

void NeoBeatProcessor::loadPreset (int index)
{
    if (index < 0 || index >= nb::kNumPresets) return;
    const auto& p = nb::presets()[(size_t) index];
    pattern.load (p.pattern);
    if (auto* s = apvts.getParameter ("swing")) s->setValueNotifyingHost (s->convertTo0to1 (p.swing));
    if (auto* t = apvts.getParameter ("tempo")) t->setValueNotifyingHost (t->convertTo0to1 (p.tempo));
    currentPreset.store (index);
}

void NeoBeatProcessor::loadKit (int index)
{
    if (index < 0 || index >= (int) nb::kits().size()) return;
    const auto& kit = nb::kits()[(size_t) index];
    const auto& lib = engine->lib();
    for (int p = 0; p < kPads; ++p)
    {
        const auto& kp = kit.pads[(size_t) p];
        const int si = std::max (0, lib.find (kp.sound));
        auto set = [&] (const char* name, float value)
        {
            if (auto* prm = apvts.getParameter (paramId (p, name))) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        };
        set ("sound", (float) si);
        set ("tune", kp.tune); set ("decay", kp.decay); set ("tone", kp.tone); set ("drive", kp.drive);
        set ("hollow", kp.hollow >= 0.0f ? kp.hollow : lib.sounds[(size_t) si].defaultHollow);
        set ("evolve", kp.evolve); set ("pan", kp.pan); set ("level", kp.levelDb); set ("choke", (float) kp.choke);
        clearSample (p);
    }
    currentKit.store (index);
}

bool NeoBeatProcessor::loadSample (int pad, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20.0));
    juce::AudioBuffer<float> b ((int) std::max (1u, reader->numChannels), len);
    reader->read (&b, 0, len, 0, true, true);
    auto data = std::make_shared<nb::SampleData>();
    data->sampleRate = (float) reader->sampleRate;
    data->path = file.getFullPathName().toStdString();
    data->left.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    const float* right = b.getNumChannels() > 1 ? b.getReadPointer (1) : b.getReadPointer (0);
    data->right.assign (right, right + len);
    double sum = 0.0; int last = 1;
    for (int i = 0; i < len; ++i)
    {
        sum += 0.5 * ((double) data->left[(size_t) i] * data->left[(size_t) i] + (double) data->right[(size_t) i] * data->right[(size_t) i]);
        if (std::fabs (data->left[(size_t) i]) > 1.0e-4f) last = i + 1;
    }
    sampleRms[(size_t) pad] = (float) std::sqrt (sum / std::max (last, 1));
    std::shared_ptr<const nb::SampleData> ptr = data;
    keepAlive.push_back (ptr);
    samples[(size_t) pad] = ptr;
    std::atomic_store (&liveSamples[(size_t) pad], ptr);
    useSample[(size_t) pad].store (true);
    return true;
}

void NeoBeatProcessor::clearSample (int pad)
{
    useSample[(size_t) pad].store (false);
    std::atomic_store (&liveSamples[(size_t) pad], std::shared_ptr<const nb::SampleData>());
    samples[(size_t) pad].reset();
}

juce::String NeoBeatProcessor::padName (int pad) const
{
    if (useSample[(size_t) pad].load() && samples[(size_t) pad]) return juce::File (samples[(size_t) pad]->path).getFileNameWithoutExtension();
    const int si = juce::jlimit (0, (int) engine->lib().sounds.size() - 1, (int) padParams[(size_t) pad].sound->load());
    return engine->lib().sounds[(size_t) si].name;
}

juce::String NeoBeatProcessor::padGroup (int pad) const
{
    if (useSample[(size_t) pad].load()) return "Sample";
    const int si = juce::jlimit (0, (int) engine->lib().sounds.size() - 1, (int) padParams[(size_t) pad].sound->load());
    return engine->lib().sounds[(size_t) si].group;
}

// ------------------------------------------------------------------ state

void NeoBeatProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* pat = xml->createNewChildElement ("PATTERN");
    pat->setAttribute ("length", pattern.length.load());
    for (int t = 0; t < nb::Pattern::kTracks; ++t)
    {
        juce::String row;
        for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
            row << juce::String::toHexString ((int) PatternStore::pack (pattern.get (t, s))) << (s + 1 < nb::Pattern::kMaxSteps ? " " : "");
        pat->setAttribute ("t" + juce::String (t), row);
    }
    auto* meta = xml->createNewChildElement ("META");
    meta->setAttribute ("preset", currentPreset.load());
    meta->setAttribute ("kit", currentKit.load());
    meta->setAttribute ("version", "0.1.0");
    auto* smp = xml->createNewChildElement ("SAMPLES");
    for (int p = 0; p < kPads; ++p)
        if (useSample[(size_t) p].load() && samples[(size_t) p]) smp->setAttribute ("p" + juce::String (p), juce::String (samples[(size_t) p]->path));
    copyXmlToBinary (*xml, destData);
}

void NeoBeatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    nb::Pattern p;
    if (auto* pat = xml->getChildByName ("PATTERN"))
    {
        p.length = juce::jlimit (1, nb::Pattern::kMaxSteps, pat->getIntAttribute ("length", 32));
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
        {
            juce::StringArray cells = juce::StringArray::fromTokens (pat->getStringAttribute ("t" + juce::String (t)), " ", "");
            for (int s = 0; s < nb::Pattern::kMaxSteps && s < cells.size(); ++s)
                p.steps[(size_t) t][(size_t) s] = PatternStore::unpack ((uint32_t) cells[s].getHexValue32());
        }
        pattern.load (p);
    }
    if (auto* meta = xml->getChildByName ("META"))
    {
        currentPreset.store (meta->getIntAttribute ("preset", 0));
        currentKit.store (meta->getIntAttribute ("kit", 0));
    }
    std::unique_ptr<juce::XmlElement> smpCopy;
    if (auto* smp = xml->getChildByName ("SAMPLES")) smpCopy = std::make_unique<juce::XmlElement> (*smp);
    // strip our own children before handing the tree to the parameter state
    for (auto* name : { "PATTERN", "META", "SAMPLES" })
        if (auto* c = xml->getChildByName (name)) xml->removeChildElement (c, true);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    for (int pad = 0; pad < kPads; ++pad)
    {
        clearSample (pad);
        if (smpCopy != nullptr && smpCopy->hasAttribute ("p" + juce::String (pad)))
        {
            const juce::File f (smpCopy->getStringAttribute ("p" + juce::String (pad)));
            if (f.existsAsFile()) loadSample (pad, f);
        }
    }
}

juce::AudioProcessorEditor* NeoBeatProcessor::createEditor() { return new NeoBeatEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeoBeatProcessor(); }
