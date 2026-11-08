#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProjectIO.h"

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
      ws (std::make_unique<nb::Workstation>()),
      apvts (*this, nullptr, "NEOBEAT", createLayout (ws->kit().lib()))
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
        useSample[(size_t) p].store (false);
        sampleRms[(size_t) p].store (0.05f);
    }
    pattern.load (nb::presets()[0].pattern);
    midiIn.reserve (1024);
    midiOut.reserve (1024);
    startTimer (60);
}

NeoBeatProcessor::~NeoBeatProcessor()
{
    stopTimer();
    ws->render().stop();
}

const juce::String NeoBeatProcessor::getProgramName (int index)
{
    if (index < 0 || index >= nb::numPatterns()) return {};
    return nb::patternPreset (index).name;
}

void NeoBeatProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (samplesPerBlock, 32);
    ws->prepare ((float) sampleRate, std::max (maxBlock, 4096));
    ws->startRenderer();
    seenVersion = 0;
    tapScratch.assign ((size_t) std::max (maxBlock, 4096), 0.0f);
    submittedHash = 0;   // render again for this sample rate
    prepared = true;
}

bool NeoBeatProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void NeoBeatProcessor::readParameters() noexcept
{
    auto& eng = ws->kit();
    ws->masterBase = 0.35f * juce::Decibels::decibelsToGain (masterParam->load());
    const int numSounds = (int) eng.lib().sounds.size();
    for (int p = 0; p < kPads; ++p)
    {
        auto& pp = padParams[(size_t) p];
        auto& ps = eng.pads[(size_t) p];
        ps.sound = juce::jlimit (0, numSounds - 1, (int) pp.sound->load());
        ps.tune = pp.tune->load(); ps.decay = pp.decay->load(); ps.tone = pp.tone->load(); ps.drive = pp.drive->load();
        ps.hollow = pp.hollow->load(); ps.evolve = pp.evolve->load(); ps.pan = pp.pan->load(); ps.levelDb = pp.level->load();
        ps.choke = (int) pp.choke->load();
        ps.useSample = useSample[(size_t) p].load();
        ps.sample = ps.useSample ? std::atomic_load (&liveSamples[(size_t) p]) : nullptr;
        ps.sampleRms = sampleRms[(size_t) p].load();
    }
}

std::array<nb::PadSettings, NeoBeatProcessor::kPads> NeoBeatProcessor::padSettingsNow() const
{
    std::array<nb::PadSettings, kPads> a {};
    const int numSounds = (int) ws->kit().lib().sounds.size();
    for (int p = 0; p < kPads; ++p)
    {
        const auto& pp = padParams[(size_t) p];
        auto& ps = a[(size_t) p];
        ps.sound = juce::jlimit (0, numSounds - 1, (int) pp.sound->load());
        ps.tune = pp.tune->load(); ps.decay = pp.decay->load(); ps.tone = pp.tone->load(); ps.drive = pp.drive->load();
        ps.hollow = pp.hollow->load(); ps.evolve = pp.evolve->load(); ps.pan = pp.pan->load(); ps.levelDb = pp.level->load();
        ps.choke = (int) pp.choke->load();
        ps.useSample = useSample[(size_t) p].load() && samples[(size_t) p] != nullptr;
        ps.sample = ps.useSample ? samples[(size_t) p] : nullptr;
        ps.sampleRms = sampleRms[(size_t) p].load();
    }
    return a;
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
    if (n <= 0 || buffer.getNumChannels() < 2) { midi.clear(); return; }
    if (! prepared) { midi.clear(); return; }
    readParameters();

    const uint32_t v = pattern.version.load (std::memory_order_acquire);
    if (v != seenVersion) { pattern.snapshot (patternCopy); ws->setPattern (patternCopy); seenVersion = v; }

    nb::Transport tr;
    tr.bpm = tempoParam->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.bpm = *b;
            if (auto q = pos->getPpqPosition()) { tr.hostPpq = *q; tr.hostPlaying = pos->getIsPlaying(); tr.hostHasPpq = true; }
        }
    if (! (tr.bpm > 1.0 && tr.bpm < 1000.0)) tr.bpm = 120.0;
    hostSynced.store (tr.hostPlaying);
    displayBpm.store (tr.bpm);

    nb::BlockParams bp;
    bp.mode = (int) modeParam->load();
    bp.run = runParam->load() > 0.5f;
    bp.swing = swingParam->load();
    bp.tapPad = selectedPad.load();
    for (int p = 0; p < kPads; ++p) bp.mute[(size_t) p] = padParams[(size_t) p].mute->load() > 0.5f;

    midiIn.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (midiIn.size() >= midiIn.capacity()) break;
        if (m.isNoteOn()) midiIn.push_back ({ meta.samplePosition, 0, m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff()) midiIn.push_back ({ meta.samplePosition, 1, m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff()) midiIn.push_back ({ meta.samplePosition, 2, 0, 0.0f });
    }
    midi.clear();
    midiOut.clear();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    if ((int) tapScratch.size() < n) return;
    ws->process (L, R, n, tr, bp, midiIn.data(), (int) midiIn.size(), midiOut, tapScratch.data());
    for (const auto& o : midiOut)
        midi.addEvent (o.on ? juce::MidiMessage::noteOn (10, o.note, (juce::uint8) o.velocity) : juce::MidiMessage::noteOff (10, o.note), juce::jlimit (0, n - 1, o.offset));
    playStep.store (ws->playStep.load());
    pushTap (tapScratch.data(), n);
}

// ------------------------------------------------------------------ the render inputs

void NeoBeatProcessor::timerCallback()
{
    if (! prepared) return;
    // what the renders depend on: pads, pattern, tempo, swing, the project, the files
    nb::Hasher h;
    for (int p = 0; p < kPads; ++p)
    {
        const auto& pp = padParams[(size_t) p];
        for (auto* a : { pp.sound, pp.tune, pp.decay, pp.tone, pp.drive, pp.hollow, pp.evolve, pp.choke }) h.f (a->load());
        h.i (useSample[(size_t) p].load()).p (samples[(size_t) p].get());
    }
    h.u (pattern.version.load()).d (std::round (displayBpm.load() * 10.0)).f (swingParam->load()).u (projectVersion.load()).i ((int64_t) files.size());
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (h.h == submittedHash) return;
    if (h.h != pendingHash) { pendingHash = h.h; pendingSince = now; return; }
    if (now - pendingSince < 140) return;
    auto in = std::make_unique<nb::RenderInputs>();
    in->project = project;
    in->pads = padSettingsNow();
    pattern.snapshot (in->pattern);
    in->bpm = displayBpm.load();
    in->swing = swingParam->load();
    in->files = files;
    ws->render().submit (std::move (in));
    submittedHash = h.h;
}

// ------------------------------------------------------------------ editor actions

void NeoBeatProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void NeoBeatProcessor::loadPreset (int index)
{
    if (index < 0 || index >= nb::numPatterns()) return;
    const auto p = nb::patternPreset (index);
    pattern.load (p.pattern);
    setParam ("swing", p.swing);
    setParam ("tempo", p.tempo);
    currentPreset.store (index);
}

void NeoBeatProcessor::loadKit (int index)
{
    if (index < 0 || index >= (int) nb::kits().size()) return;
    const auto& lib = ws->kit().lib();
    for (int p = 0; p < kPads; ++p)
    {
        const auto ps = nb::kitPadSettings (lib, index, p);
        setParam (paramId (p, "sound"), (float) ps.sound);
        setParam (paramId (p, "tune"), ps.tune); setParam (paramId (p, "decay"), ps.decay); setParam (paramId (p, "tone"), ps.tone);
        setParam (paramId (p, "drive"), ps.drive); setParam (paramId (p, "hollow"), ps.hollow); setParam (paramId (p, "evolve"), ps.evolve);
        setParam (paramId (p, "pan"), ps.pan); setParam (paramId (p, "level"), ps.levelDb); setParam (paramId (p, "choke"), (float) ps.choke);
        clearSample (p);
    }
    currentKit.store (index);
}

std::shared_ptr<const nb::SampleData> NeoBeatProcessor::loadFile (const juce::File& file)
{
    const std::string key = file.getFullPathName().toStdString();
    if (auto it = files.find (key); it != files.end()) return it->second;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return nullptr;
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    juce::AudioBuffer<float> b ((int) std::max (1u, reader->numChannels), len);
    reader->read (&b, 0, len, 0, true, true);
    auto data = std::make_shared<nb::SampleData>();
    data->sampleRate = (float) reader->sampleRate;
    data->path = key;
    data->left.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    const float* right = b.getNumChannels() > 1 ? b.getReadPointer (1) : b.getReadPointer (0);
    data->right.assign (right, right + len);
    for (auto* ch : { &data->left, &data->right }) for (auto& x : *ch) if (! std::isfinite (x)) x = 0.0f;
    std::shared_ptr<const nb::SampleData> ptr = data;
    files[key] = ptr;
    keepAlive.push_back (ptr);
    return ptr;
}

int NeoBeatProcessor::loadGridFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return 0;
    auto found = folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    found.sort();
    std::vector<std::string> paths;
    for (const auto& f : found)
    {
        if ((int) paths.size() >= nb::GridPattern::kSize) break;
        if (loadFile (f)) paths.push_back (f.getFullPathName().toStdString());
    }
    if (paths.empty()) return 0;
    project.grid.folder = folder.getFullPathName().toStdString();
    project.grid.files = paths;
    project.grid.useKit = false;
    projectChanged();
    return (int) paths.size();
}

bool NeoBeatProcessor::loadSample (int pad, const juce::File& file)
{
    auto ptr = loadFile (file);
    if (! ptr) return false;
    double sum = 0.0; int last = 1;
    const int len = (int) ptr->left.size();
    for (int i = 0; i < len; ++i)
    {
        sum += 0.5 * ((double) ptr->left[(size_t) i] * ptr->left[(size_t) i] + (double) ptr->right[(size_t) i] * ptr->right[(size_t) i]);
        if (std::fabs (ptr->left[(size_t) i]) > 1.0e-4f) last = i + 1;
    }
    sampleRms[(size_t) pad].store ((float) std::sqrt (sum / std::max (last, 1)));
    samples[(size_t) pad] = ptr;
    std::atomic_store (&liveSamples[(size_t) pad], ptr);
    useSample[(size_t) pad].store (true);
    projectChanged();
    return true;
}

void NeoBeatProcessor::clearSample (int pad)
{
    useSample[(size_t) pad].store (false);
    std::atomic_store (&liveSamples[(size_t) pad], std::shared_ptr<const nb::SampleData>());
    samples[(size_t) pad].reset();
    projectChanged();
}

juce::String NeoBeatProcessor::padName (int pad) const
{
    if (useSample[(size_t) pad].load() && samples[(size_t) pad]) return juce::File (samples[(size_t) pad]->path).getFileNameWithoutExtension();
    const int si = juce::jlimit (0, (int) ws->kit().lib().sounds.size() - 1, (int) padParams[(size_t) pad].sound->load());
    return ws->kit().lib().sounds[(size_t) si].name;
}

juce::String NeoBeatProcessor::padGroup (int pad) const
{
    if (useSample[(size_t) pad].load()) return "Sample";
    const int si = juce::jlimit (0, (int) ws->kit().lib().sounds.size() - 1, (int) padParams[(size_t) pad].sound->load());
    return ws->kit().lib().sounds[(size_t) si].group;
}

void NeoBeatProcessor::reloadProjectFiles()
{
    for (const auto& f : project.grid.files) loadFile (juce::File (f));
    for (const auto& z : project.keymap.zones) if (! z.file.empty()) loadFile (juce::File (z.file));
    if (! project.matrix.sample.file.empty()) loadFile (juce::File (project.matrix.sample.file));
}

// ------------------------------------------------------------------ user presets

juce::File NeoBeatProcessor::userRoot()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Marbelite").getChildFile ("NeoBeat").getChildFile ("Presets");
}

juce::StringArray NeoBeatProcessor::userPresets (const juce::String& section) const
{
    juce::StringArray names;
    for (const auto& f : userRoot().getChildFile (section).findChildFiles (juce::File::findFiles, false, "*.nbp"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

std::unique_ptr<juce::XmlElement> NeoBeatProcessor::sectionXml (const juce::String& section)
{
    auto e = std::make_unique<juce::XmlElement> ("NEOBEAT_PRESET");
    e->setAttribute ("section", section);
    if (section == "OneShot") io::writeOneShot (*e->createNewChildElement ("ONESHOT"), project.matrix.oneShot);
    else if (section == "Sample") io::writeSample (*e->createNewChildElement ("SAMPLE"), project.matrix.sample);
    else if (section == "Sequencer") io::writeSeq (*e->createNewChildElement ("SEQ"), project.matrix.seq);
    else if (section == "Grid") io::writeGrid (*e->createNewChildElement ("GRID"), project.grid);
    else if (section == "KeyMap") io::writeKeyMap (*e->createNewChildElement ("KEYMAP"), project.keymap);
    else if (section == "Mod") io::writeMod (*e->createNewChildElement ("MOD"), project.mod);
    else if (section == "Pattern")
    {
        nb::Pattern p; pattern.snapshot (p);
        auto* pe = e->createNewChildElement ("PATTERN"); io::writePattern (*pe, p);
        pe->setAttribute ("swing", (double) swingParam->load()); pe->setAttribute ("tempo", (double) tempoParam->load());
    }
    else if (section == "Kit")
    {
        auto* ke = e->createNewChildElement ("KIT");
        auto state = apvts.copyState();
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto c = state.getChild (i);
            const auto id = c.getProperty ("id").toString();
            if (id.startsWith ("p")) ke->setAttribute (id, (double) c.getProperty ("value"));
        }
        for (int p = 0; p < kPads; ++p) if (useSample[(size_t) p].load() && samples[(size_t) p]) ke->setAttribute ("sample" + juce::String (p), juce::String (samples[(size_t) p]->path));
    }
    return e;
}

void NeoBeatProcessor::applySectionXml (const juce::String& section, const juce::XmlElement& e)
{
    if (section == "OneShot") { if (auto* c = e.getChildByName ("ONESHOT")) io::readOneShot (*c, project.matrix.oneShot); }
    else if (section == "Sample") { if (auto* c = e.getChildByName ("SAMPLE")) io::readSample (*c, project.matrix.sample); }
    else if (section == "Sequencer") { if (auto* c = e.getChildByName ("SEQ")) io::readSeq (*c, project.matrix.seq); }
    else if (section == "Grid") { if (auto* c = e.getChildByName ("GRID")) io::readGrid (*c, project.grid); }
    else if (section == "KeyMap") { if (auto* c = e.getChildByName ("KEYMAP")) io::readKeyMap (*c, project.keymap); }
    else if (section == "Mod") { if (auto* c = e.getChildByName ("MOD")) io::readMod (*c, project.mod); }
    else if (section == "Pattern")
    {
        if (auto* c = e.getChildByName ("PATTERN"))
        {
            nb::Pattern p; io::readPattern (*c, p); pattern.load (p);
            setParam ("swing", (float) c->getDoubleAttribute ("swing", 0.62)); setParam ("tempo", (float) c->getDoubleAttribute ("tempo", 120.0));
        }
    }
    else if (section == "Kit")
    {
        if (auto* c = e.getChildByName ("KIT"))
        {
            for (int i = 0; i < c->getNumAttributes(); ++i)
            {
                const auto name = c->getAttributeName (i);
                if (name.startsWith ("sample")) continue;
                setParam (name, (float) c->getDoubleAttribute (name));
            }
            for (int p = 0; p < kPads; ++p)
            {
                const auto path = c->getStringAttribute ("sample" + juce::String (p));
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (p, juce::File (path));
                else clearSample (p);
            }
        }
    }
    reloadProjectFiles();
    projectChanged();
}

bool NeoBeatProcessor::saveUserPreset (const juce::String& section, const juce::String& name)
{
    const auto dir = userRoot().getChildFile (section);
    if (! dir.createDirectory()) return false;
    const auto safe = juce::File::createLegalFileName (name.trim().isEmpty() ? "Untitled" : name.trim());
    // keep the samples: copy every referenced file next to the presets
    const auto smpDir = userRoot().getChildFile ("Samples");
    smpDir.createDirectory();
    auto keepFile = [&] (std::string& path)
    {
        if (path.empty()) return;
        const juce::File f (path);
        if (! f.existsAsFile() || f.isAChildOf (smpDir)) return;
        const auto dst = smpDir.getChildFile (f.getFileName());
        if (! dst.existsAsFile()) f.copyFileTo (dst);
        if (dst.existsAsFile()) { files[dst.getFullPathName().toStdString()] = files[path]; path = dst.getFullPathName().toStdString(); }
    };
    if (section == "Sample") keepFile (project.matrix.sample.file);
    if (section == "Grid") for (auto& f : project.grid.files) keepFile (f);
    if (section == "KeyMap") for (auto& z : project.keymap.zones) keepFile (z.file);
    auto e = sectionXml (section);
    if (section == "Kit")
        if (auto* k = e->getChildByName ("KIT"))
            for (int p = 0; p < kPads; ++p)
            {
                const auto attr = "sample" + juce::String (p);
                if (! k->hasAttribute (attr)) continue;
                std::string path = k->getStringAttribute (attr).toStdString();
                keepFile (path);
                k->setAttribute (attr, juce::String (path));
            }
    return e->writeTo (dir.getChildFile (safe + ".nbp"));
}

bool NeoBeatProcessor::loadUserPreset (const juce::String& section, const juce::String& name)
{
    const auto f = userRoot().getChildFile (section).getChildFile (name + ".nbp");
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("NEOBEAT_PRESET")) return false;
    applySectionXml (section, *xml);
    return true;
}

// ------------------------------------------------------------------ state

void NeoBeatProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    nb::Pattern p; pattern.snapshot (p);
    io::writePattern (*xml->createNewChildElement ("PATTERN"), p);
    auto* meta = xml->createNewChildElement ("META");
    meta->setAttribute ("preset", currentPreset.load());
    meta->setAttribute ("kit", currentKit.load());
    meta->setAttribute ("version", "2.0.0");
    auto* smp = xml->createNewChildElement ("SAMPLES");
    for (int pad = 0; pad < kPads; ++pad)
        if (useSample[(size_t) pad].load() && samples[(size_t) pad]) smp->setAttribute ("p" + juce::String (pad), juce::String (samples[(size_t) pad]->path));
    xml->addChildElement (io::toXml (project).release());
    copyXmlToBinary (*xml, destData);
}

void NeoBeatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    if (auto* pat = xml->getChildByName ("PATTERN")) { nb::Pattern p; io::readPattern (*pat, p); pattern.load (p); }
    if (auto* meta = xml->getChildByName ("META"))
    {
        currentPreset.store (juce::jlimit (0, nb::numPatterns() - 1, meta->getIntAttribute ("preset", 0)));
        currentKit.store (juce::jlimit (0, (int) nb::kits().size() - 1, meta->getIntAttribute ("kit", 0)));
    }
    if (auto* pr = xml->getChildByName ("PROJECT")) io::fromXml (*pr, project);
    else project = nb::Project();
    std::unique_ptr<juce::XmlElement> smpCopy;
    if (auto* smp = xml->getChildByName ("SAMPLES")) smpCopy = std::make_unique<juce::XmlElement> (*smp);
    for (auto* name : { "PATTERN", "META", "SAMPLES", "PROJECT" })
        while (auto* c = xml->getChildByName (name)) xml->removeChildElement (c, true);
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
    reloadProjectFiles();
    projectChanged();
}

juce::AudioProcessorEditor* NeoBeatProcessor::createEditor() { return new NeoBeatEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeoBeatProcessor(); }
