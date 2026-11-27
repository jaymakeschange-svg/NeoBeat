#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProjectIO.h"
#include "../shared/LearnerStore.h"
#include "../shared/Loudness.h"

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
    // IDM Jack, held: on while it is on. A host parameter so it can be mapped and automated.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "jack", 1 }, "IDM Jack", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scene", 1 }, "Scene", StringArray { "A", "B", "C", "D" }, 0));

    StringArray soundNames;
    for (const auto& s : lib.sounds) soundNames.add (s.name);
    const auto& kit = nb::kits()[0];
    for (int p = 0; p < kPads; ++p)
    {
        const auto& kp = kit.pads[(size_t) p];
        const int si = std::max (0, lib.find (kp.sound));
        const float hollow = kp.hollow >= 0.0f ? kp.hollow : lib.sounds[(size_t) si].defaultHollow;
        const String pre = "p" + String (p + 1) + "_", nm = "Lane " + String (p + 1) + " ";
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
        // up to +24 dB: a quiet recording can be brought up to the stock sounds
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "level", 1 }, nm + "Level", NormalisableRange<float> (-30.0f, 24.0f, 0.1f), kp.levelDb,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pre + "choke", 1 }, nm + "Choke Group", 0, 4, kp.choke));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pre + "mute", 1 }, nm + "Mute", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "width", 1 }, nm + "Width", NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.005f ? String ("mono") : String (roundToInt (v * 100.0f)) + "%"; })));
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
    jackParam = apvts.getRawParameterValue ("jack");
    sceneParam = apvts.getRawParameterValue ("scene");
    for (int p = 0; p < kPads; ++p)
    {
        auto get = [&] (const char* n) { return apvts.getRawParameterValue (paramId (p, n)); };
        padParams[(size_t) p] = { get ("sound"), get ("tune"), get ("decay"), get ("tone"), get ("drive"), get ("hollow"),
                                  get ("evolve"), get ("pan"), get ("level"), get ("choke"), get ("mute"), get ("width") };
        useSample[(size_t) p].store (false);
        sampleRms[(size_t) p].store (0.05f);
    }
    pattern.loadScene (0, nb::presets()[0].pattern);
    for (int s = 1; s < nb::kNumScenes; ++s) { nb::Pattern empty; empty.length = nb::presets()[0].pattern.length; pattern.loadScene (s, empty); }
    midiIn.reserve (1024);
    midiOut.reserve (2048);
    registerPadTargets();
    jackNextPass();
    publishLive();
    startTimer (30);
}

// ------------------------------------------------------------------------- the global randomiser
//
// What each pad's roll reaches, and how far. Ranges are read from the host parameters themselves
// rather than written out again here, so the randomiser can never disagree with the parameter it is
// moving. Reach is how much of that range one press may cover, as a window around where the
// parameter already sits.
//
// Left out on purpose: sound (which sound is on the lane is a different decision from how it is
// set), choke and mute (structure, not character).
namespace
{
// reach: how far one press moves a parameter from where it is. lo < hi instead: a fixed window,
// picked fresh each press wherever the parameter was - for a value that must not wander.
struct PadRoll { const char* name; float reach; float lo = 0.0f, hi = 0.0f; };
constexpr PadRoll kPadRoll[] = {
    { "tune",   0.50f },     // a window of +-12 st: two octaves of travel stays musical enough to audition
    { "decay",  0.80f },
    { "tone",   1.00f },
    { "drive",  0.80f },
    { "hollow", 0.80f },
    { "evolve", 1.00f },
    { "pan",    0.60f },
    { "width",  0.70f },
    // Level is a window, not a walk. As a walk (first build: reach 0.2, about +-6 dB from wherever
    // it was) a dozen presses drift some lanes towards silence and pin others at +6 dB - the test
    // snapshot landed one on +6.0 at the first press. A roll that silences a lane or blows it up
    // is not an audition.
    { "level",  1.00f, -9.0f, 1.0f },
};
} // namespace

void NeoBeatProcessor::registerPadTargets()
{
    chaos.clearTargets();
    for (int p = 0; p < kPads; ++p)
    {
        nsr::ChaosTarget t;
        t.room = "LANE " + std::to_string (p + 1);
        t.name = padName (p).toStdString();
        t.reach = 1.0f;
        t.on = true;
        t.apply = [this, p] (float, gen::Rng& rng) { rollPad (p, rng); };
        chaos.addTarget (std::move (t));
    }
}

void NeoBeatProcessor::refreshPadTargetNames()
{
    auto& list = chaos.allTargets();
    for (int p = 0; p < kPads && p < (int) list.size(); ++p)
        list[(size_t) p].name = padName (p).toStdString();
}

void NeoBeatProcessor::includeAll (bool on)
{
    for (auto& t : chaos.allTargets()) if (! t.never) t.on = on;
    jackApply();
}

uint32_t NeoBeatProcessor::jackMask() const noexcept
{
    uint32_t m = 0;
    for (int p = 0; p < kPads; ++p) if (padIncluded (p)) m |= 1u << p;
    return m;
}

void NeoBeatProcessor::jackApply()
{
    jackPlaying = jack;
    if (jack.selfRandomise > 0.001f)
    {
        // seeded by the pass, so the same seed rolls the same settings: a locked pass replays whole
        gen::Rng rng (gen::Rng::mix (jack.seed, 0x5e1f5e1full));
        nsr::jackSelfRandomise (jackPlaying, rng);
    }
    // NeoBeat has no effect parameter for Jack to move (docs/DECISIONS.md), so that axis is off
    // here rather than a switch that does nothing - and at least one axis stays on.
    jackPlaying.axis[(int) nsr::ChaosAxis::Effect] = false;
    bool any = false;
    for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i) any = any || jackPlaying.axis[i];
    if (! any) jackPlaying.axis[(int) nsr::ChaosAxis::Parameter] = true;
    ws->jackSettings.write (jackPlaying);
    ws->jackMask.store (jackMask(), std::memory_order_relaxed);
}

void NeoBeatProcessor::jackNextPass()
{
    if (! jackLockSeed)
        jack.seed = gen::Rng::mix ((uint64_t) juce::Time::currentTimeMillis(), ++jackPasses) | 1u;
    jackApply();
}

void NeoBeatProcessor::jackSetHeld (bool on)
{
    if (auto* p = apvts.getParameter ("jack"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

bool NeoBeatProcessor::padIncluded (int pad) const noexcept
{
    const auto& list = chaos.allTargets();
    return pad >= 0 && pad < (int) list.size() && list[(size_t) pad].on && ! list[(size_t) pad].never;
}

void NeoBeatProcessor::rollPad (int pad, gen::Rng& rng)
{
    // the lane's own parameters, through the host parameters so the host sees the change
    for (const auto& r : kPadRoll)
    {
        const auto id = paramId (pad, r.name);
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        {
            const auto& range = prm->getNormalisableRange();
            const float v = r.lo < r.hi ? rng.unif (std::max (range.start, r.lo), std::min (range.end, r.hi))
                                        : nsr::chaosVisit (rng, getParam (id), range.start, range.end, r.reach);
            setParam (id, v);
        }
    }

    // and the parameters of the effects already on its lane. Which effects are there is left
    // alone: changing the chain is rebuilding the lane, not auditioning it.
    for (auto& c : project.padFx[(size_t) pad].inserts)
    {
        if (c.kind == nb::CellFx::Library)
        {
            const auto& info = nb::matrixFxInfo()[(size_t) c.fx].params;
            for (size_t i = 0; i < info.size() && i < c.settings.p.size(); ++i)
                c.settings.p[i] = nsr::chaosVisit (rng, c.settings.p[i], info[i].lo, info[i].hi, 0.8f);
        }
        else if (c.kind == nb::CellFx::BounceDelayFx)
        {
            auto& b = c.bounce;
            b.bounciness = nsr::chaosVisit (rng, b.bounciness, 0.3f, 0.95f, 0.6f);
            b.distStart  = nsr::chaosVisit (rng, b.distStart, 0.5f, 10.0f, 0.6f);
            b.distEnd    = nsr::chaosVisit (rng, b.distEnd, 1.0f, 30.0f, 0.6f);
            b.speed      = nsr::chaosVisit (rng, b.speed, 0.0f, 1.0f, 0.6f);
            b.panRandom  = nsr::chaosVisit (rng, b.panRandom, 0.0f, 1.0f, 0.8f);
        }
        else if (c.kind == nb::CellFx::FrictionStretchFx)
        {
            auto& s = c.stretch;
            s.stretch  = nsr::chaosVisit (rng, s.stretch, 1.0f, 12.0f, 0.6f);
            s.creep    = nsr::chaosVisit (rng, s.creep, 0.0f, 1.0f, 0.8f);
            s.holdFade = nsr::chaosVisit (rng, s.holdFade, 0.0f, 1.0f, 0.8f);
            s.snap     = nsr::chaosVisit (rng, s.snap, 0.0f, 1.0f, 0.8f);
        }
    }
}

void NeoBeatProcessor::roll()
{
    refreshPadTargetNames();
    chaos.rollOnce();
    projectChanged();               // the lane effects
    undoCommit (kJackRoom);         // one press, one step back
}

NeoBeatProcessor::~NeoBeatProcessor()
{
    stopTimer();
    exportThread.reset();
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
    seenScene.fill (0);
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
        ps.choke = (int) pp.choke->load(); ps.width = pp.width->load();
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
        ps.choke = (int) pp.choke->load(); ps.width = pp.width->load();
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

    for (int s = 0; s < nb::kNumScenes; ++s)
    {
        const uint32_t v = pattern.sceneVersion[(size_t) s].load (std::memory_order_acquire);
        if (v != seenScene[(size_t) s]) { pattern.snapshotScene (s, sceneCopy[(size_t) s]); ws->setScene (s, sceneCopy[(size_t) s]); seenScene[(size_t) s] = v; }
    }

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
    ws->jackHold.store (jackParam->load() > 0.5f, std::memory_order_relaxed);
    bp.swing = swingParam->load();
    bp.tapPad = selectedPad.load();
    bp.scene = juce::jlimit (0, nb::kNumScenes - 1, (int) std::lround (sceneParam->load()));
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
        midi.addEvent (o.on ? juce::MidiMessage::noteOn (juce::jlimit (1, 16, o.channel), o.note, (juce::uint8) o.velocity)
                            : juce::MidiMessage::noteOff (juce::jlimit (1, 16, o.channel), o.note), juce::jlimit (0, n - 1, o.offset));
    playStep.store (ws->playStep.load());
    playScene.store (ws->playScene.load()); queuedScene.store (ws->queuedScene.load()); playLink.store (ws->playLink.load());
    pushTap (tapScratch.data(), n);
}

// ------------------------------------------------------------------ keeping the sound in step with the editor

void NeoBeatProcessor::publishLive()
{
    ws->publishLive (nb::buildLiveState (project));
    publishedVersion = projectVersion.load();
}

void NeoBeatProcessor::projectChanged()
{
    ++projectVersion;
    // the effects hear it at once (the editor's thread only: a host restoring state from another thread waits for the timer)
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) if (mm->isThisTheMessageThread()) publishLive();
}

std::unique_ptr<nb::RenderInputs> NeoBeatProcessor::renderInputs() const
{
    auto in = std::make_unique<nb::RenderInputs>();
    in->project = project;
    in->pads = padSettingsNow();
    for (int s = 0; s < nb::kNumScenes; ++s) pattern.snapshotScene (s, in->scenes[(size_t) s]);
    in->bpm = displayBpm.load();
    in->swing = swingParam->load();
    in->files = files;
    return in;
}

void NeoBeatProcessor::timerCallback()
{
    if (publishedVersion != projectVersion.load()) publishLive();
    // IDM Jack: a pass has ended (released, and any recovery run out) - get the next one ready
    {
        const int st = jackState();
        if (jackSeenState != 0 && st == 0 && ! jackHeldParam()) jackNextPass();
        jackSeenState = st;
    }
    // a new instance (no saved state came from the host) opens with the default sequence, if one was chosen
    if (! startupDone && ++ticks > 12)
    {
        startupDone = true;
        if (! stateRestored.load())
        {
            const auto st = shared::Settings::load();
            const juce::File f (st.startupNjq);
            juce::String err;
            if (st.startupNjq.isNotEmpty() && f.existsAsFile()) loadNjq (f, err);
        }
        undoCommit (0);
        for (auto& h : history) { h.undo.clear(); h.redo.clear(); }
    }
    if (! prepared) return;
    // what the prepared sound depends on: lanes, scenes, tempo, swing, the project, the files
    nb::Hasher h;
    for (int p = 0; p < kPads; ++p)
    {
        const auto& pp = padParams[(size_t) p];
        for (auto* a : { pp.sound, pp.tune, pp.decay, pp.tone, pp.drive, pp.hollow, pp.evolve, pp.choke, pp.width }) h.f (a->load());
        h.i (useSample[(size_t) p].load()).p (samples[(size_t) p].get());
    }
    h.u (pattern.version.load()).d (std::round (displayBpm.load() * 10.0)).f (swingParam->load()).u (projectVersion.load()).i ((int64_t) files.size());
    if (h.h == submittedHash) return;
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (now - lastSubmit < 60) return;        // no waiting for the hand to stop: at most this often
    ws->render().submit (renderInputs());
    submittedHash = h.h; lastSubmit = now;
}

// ------------------------------------------------------------------ editor actions

void NeoBeatProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}
float NeoBeatProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void NeoBeatProcessor::setScene (int s)
{
    s = juce::jlimit (0, nb::kNumScenes - 1, s);
    pattern.edit.store (s);
    setParam ("scene", (float) s);
}
void NeoBeatProcessor::copyScene (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= nb::kNumScenes || to >= nb::kNumScenes) return;
    auto p = std::make_unique<nb::Pattern>(); pattern.snapshotScene (from, *p); pattern.loadScene (to, *p);
    // its nodes come along
    auto& cells = project.matrix.oneShot.cells;
    cells.erase (std::remove_if (cells.begin(), cells.end(), [to] (const nb::OneShotCell& c) { return c.scene == to; }), cells.end());
    const size_t n = cells.size();
    for (size_t i = 0; i < n; ++i) if (cells[i].scene == from) { auto c = cells[i]; c.scene = to; cells.push_back (c); }
    projectChanged();
}
void NeoBeatProcessor::clearScene (int s)
{
    if (s < 0 || s >= nb::kNumScenes) return;
    auto p = std::make_unique<nb::Pattern>(); p->length = pattern.lengthOf (s); pattern.loadScene (s, *p);
    auto& cells = project.matrix.oneShot.cells;
    cells.erase (std::remove_if (cells.begin(), cells.end(), [s] (const nb::OneShotCell& c) { return c.scene == s; }), cells.end());
    projectChanged();
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
        setParam (paramId (p, "width"), 1.0f);
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

void NeoBeatProcessor::setPadSample (int pad, std::shared_ptr<const nb::SampleData> ptr)
{
    if (pad < 0 || pad >= kPads) return;
    if (! ptr) { clearSample (pad); return; }
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
}

bool NeoBeatProcessor::loadSample (int pad, const juce::File& file)
{
    auto ptr = loadFile (file);
    if (! ptr) return false;
    setPadSample (pad, ptr);
    return true;
}

void NeoBeatProcessor::clearSample (int pad)
{
    useSample[(size_t) pad].store (false);
    std::atomic_store (&liveSamples[(size_t) pad], std::shared_ptr<const nb::SampleData>());
    samples[(size_t) pad].reset();
    projectChanged();
}

// a sample by what the state calls it: a file's path, or "njq:<file>#<lane>" for a sound inside a .NJQ
std::shared_ptr<const nb::SampleData> NeoBeatProcessor::resolveSample (const juce::String& key)
{
    if (key.isEmpty()) return nullptr;
    if (auto it = files.find (key.toStdString()); it != files.end()) return it->second;
    if (key.startsWith ("njq:"))
    {
        const auto path = key.substring (4).upToLastOccurrenceOf ("#", false, false);
        const int lane = key.fromLastOccurrenceOf ("#", false, false).getIntValue();
        njq::Kit kit; juce::String err;
        if (lane < 0 || lane >= kPads || ! njq::load (juce::File (path), kit, err)) return nullptr;
        // every lane of that file, while it is open
        for (int i = 0; i < kPads; ++i)
        {
            auto& ln = kit.lanes[(size_t) i];
            if (! ln.hasSample || ln.l.empty()) continue;
            auto d = std::make_shared<nb::SampleData>();
            d->sampleRate = (float) ln.sampleRate; d->left = std::move (ln.l); d->right = std::move (ln.r);
            d->path = ("njq:" + path + "#" + juce::String (i)).toStdString();
            std::shared_ptr<const nb::SampleData> p = d;
            files[d->path] = p; keepAlive.push_back (p);
        }
        if (auto it = files.find (key.toStdString()); it != files.end()) return it->second;
        return nullptr;
    }
    const juce::File f (key);
    return f.existsAsFile() ? loadFile (f) : nullptr;
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

// ------------------------------------------------------------------ undo

namespace
{
juce::MemoryBlock blockOf (const juce::String& s) { return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8()); }
juce::String textOf (const juce::MemoryBlock& b) { return juce::String::fromUTF8 ((const char*) b.getData(), (int) b.getSize()); }
juce::String sectionLabel (const juce::String& name)
{
    if (name.startsWith ("scene")) return "steps";
    if (name == "pads" || name == "samples") return "sound";
    if (name == "global") return "tempo and swing";
    if (name == "padfx") return "inserts";
    if (name == "windows") return "windows";
    if (name == "keymap") return "key map";
    if (name == "matrix") return "Matrixxxer";
    if (name == "mod") return "modulation";
    if (name == "midi") return "MIDI";
    if (name == "song") return "chain";
    return "Layermax";
}
}

NeoBeatProcessor::Sections NeoBeatProcessor::captureSections()
{
    Sections s;
    for (int sc = 0; sc < nb::kNumScenes; ++sc)
    {
        juce::MemoryOutputStream o;
        o.writeInt (pattern.lengthOf (sc));
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
            for (int st = 0; st < nb::Pattern::kMaxSteps; ++st)
            {
                const uint32_t v = pattern.cells[PatternStore::at (sc, t, st)].load (std::memory_order_relaxed);
                if ((v & 0xff) != 0) { o.writeShort ((short) t); o.writeShort ((short) st); o.writeInt ((int) v); }
            }
        s["scene" + juce::String (sc)] = o.getMemoryBlock();
    }
    {
        juce::String t;
        for (int p = 0; p < kPads; ++p)
            for (const char* n : { "sound", "tune", "decay", "tone", "drive", "hollow", "evolve", "pan", "level", "choke", "mute", "width" })
                t << paramId (p, n) << "=" << juce::String (getParam (paramId (p, n)), 4) << "\n";
        s["pads"] = blockOf (t);
        juce::String sm;
        for (int p = 0; p < kPads; ++p) sm << (useSample[(size_t) p].load() && samples[(size_t) p] ? juce::String (samples[(size_t) p]->path) : juce::String()) << "\n";
        s["samples"] = blockOf (sm);
        s["global"] = blockOf ("swing=" + juce::String (swingParam->load(), 4) + "\ntempo=" + juce::String (tempoParam->load(), 2) + "\nmaster=" + juce::String (masterParam->load(), 2) + "\n");
    }
    auto child = [&] (const char* name, const std::function<void (juce::XmlElement&)>& write)
    {
        juce::XmlElement root ("PROJECT");
        write (root);
        s[name] = blockOf (root.toString (juce::XmlElement::TextFormat().singleLine()));
    };
    child ("padfx", [&] (juce::XmlElement& r) { io::writePadFx (r, project); });
    child ("windows", [&] (juce::XmlElement& r) { io::writeWindows (r, project); });
    child ("keymap", [&] (juce::XmlElement& r) { io::writeKeyMap (*r.createNewChildElement ("KEYMAP"), project.keymap); });
    child ("matrix", [&] (juce::XmlElement& r) { io::writeMatrix (r, project); });
    child ("mod", [&] (juce::XmlElement& r) { r.setAttribute ("glue", (double) project.glue); io::writeMod (*r.createNewChildElement ("MOD"), project.mod); });
    child ("midi", [&] (juce::XmlElement& r) { io::writeMidi (*r.createNewChildElement ("MIDI"), project.midi); });
    child ("song", [&] (juce::XmlElement& r) { io::writeSong (*r.createNewChildElement ("SONG"), project.song); });
    child ("layermax", [&] (juce::XmlElement& r) { io::writeGrid (*r.createNewChildElement ("GRID"), project.grid, false); });
    for (int i = 0; i < (int) project.grid.patterns.size(); ++i)
    {
        const auto& g = project.grid.patterns[(size_t) i];
        juce::MemoryOutputStream o;
        o.writeInt (g.length); o.writeInt (g.rate); o.writeFloat (g.swing);
        // mostly empty: runs of equal cells
        for (size_t k = 0; k < g.cells.size();)
        {
            size_t run = 1; while (k + run < g.cells.size() && g.cells[k + run] == g.cells[k] && run < 60000) ++run;
            o.writeInt ((int) g.cells[k]); o.writeShort ((short) (unsigned short) run); k += run;
        }
        s["layermax" + juce::String (i)] = o.getMemoryBlock();
    }
    return s;
}

void NeoBeatProcessor::applySections (const Sections& secs)
{
    for (const auto& kv : secs)
    {
        const auto& name = kv.first; const auto& mb = kv.second;
        if (name.startsWith ("scene"))
        {
            const int sc = name.substring (5).getIntValue();
            if (sc < 0 || sc >= nb::kNumScenes) continue;
            auto p = std::make_unique<nb::Pattern>();
            juce::MemoryInputStream in (mb, false);
            p->length = juce::jlimit (1, nb::Pattern::kMaxSteps, in.readInt());
            while (in.getNumBytesRemaining() >= 8)
            {
                const int t = in.readShort(), st = in.readShort(); const uint32_t v = (uint32_t) in.readInt();
                if (t >= 0 && t < nb::Pattern::kTracks && st >= 0 && st < nb::Pattern::kMaxSteps) p->steps[(size_t) t][(size_t) st] = PatternStore::unpack (v);
            }
            pattern.loadScene (sc, *p);
        }
        else if (name == "pads" || name == "global")
        {
            for (const auto& line : juce::StringArray::fromLines (textOf (mb)))
            {
                const auto id = line.upToFirstOccurrenceOf ("=", false, false);
                if (id.isEmpty()) continue;
                const float v = line.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
                if (std::fabs (getParam (id) - v) > 1.0e-4f) setParam (id, v);
            }
        }
        else if (name == "samples")
        {
            const auto lines = juce::StringArray::fromLines (textOf (mb));
            for (int p = 0; p < kPads; ++p)
            {
                const auto key = p < lines.size() ? lines[p] : juce::String();
                const auto have = useSample[(size_t) p].load() && samples[(size_t) p] ? juce::String (samples[(size_t) p]->path) : juce::String();
                if (key == have) continue;
                if (auto ptr = resolveSample (key)) setPadSample (p, ptr); else clearSample (p);
            }
        }
        else if (name == "layermax")
        {
            if (auto xml = juce::XmlDocument::parse (textOf (mb)))
                if (auto* g = xml->getChildByName ("GRID"))
                {
                    nb::GridState tmp; io::readGrid (*g, tmp);
                    tmp.patterns = std::move (project.grid.patterns);
                    project.grid = std::move (tmp);
                }
        }
        else if (name.startsWith ("layermax"))
        {
            const int i = name.substring (8).getIntValue();
            if (i < 0 || i >= (int) project.grid.patterns.size()) continue;
            auto& g = project.grid.patterns[(size_t) i];
            juce::MemoryInputStream in (mb, false);
            g.length = juce::jlimit (1, nb::GridPattern::kSize, in.readInt()); g.rate = juce::jlimit (0, 3, in.readInt()); g.swing = in.readFloat();
            size_t k = 0;
            while (in.getNumBytesRemaining() >= 6 && k < g.cells.size())
            {
                const uint32_t v = (uint32_t) in.readInt(); size_t run = (size_t) (unsigned short) in.readShort();
                while (run-- > 0 && k < g.cells.size()) g.cells[k++] = v;
            }
        }
        else if (auto xml = juce::XmlDocument::parse (textOf (mb)))
            io::readInto (*xml, project);
    }
    reloadProjectFiles();
    projectChanged();
}

void NeoBeatProcessor::undoCommit (int room)
{
    Sections cur = captureSections();
    if (! baselineValid) { baseline = std::move (cur); baselineValid = true; return; }
    Entry e; juce::StringArray what;
    for (const auto& kv : cur)
    {
        const auto it = baseline.find (kv.first);
        if (it != baseline.end() && it->second == kv.second) continue;
        e.after[kv.first] = kv.second;
        e.before[kv.first] = it != baseline.end() ? it->second : juce::MemoryBlock();
        what.addIfNotAlreadyThere (sectionLabel (kv.first));
    }
    if (e.after.empty()) return;
    e.label = what.joinIntoString (", ");
    room = juce::jlimit (0, kRooms - 1, room);
    auto& h = history[(size_t) room];
    h.undo.push_back (std::move (e));
    if (h.undo.size() > 100) h.undo.erase (h.undo.begin());
    h.redo.clear();
    baseline = std::move (cur);
}

void NeoBeatProcessor::undo (int room)
{
    if (room < 0 || room >= kRooms) return;
    undoCommit (room);                      // anything not yet recorded becomes a step first
    auto& h = history[(size_t) room];
    if (h.undo.empty()) return;
    Entry e = std::move (h.undo.back()); h.undo.pop_back();
    applySections (e.before);
    h.redo.push_back (std::move (e));
    baseline = captureSections(); baselineValid = true;
}

void NeoBeatProcessor::redo (int room)
{
    if (room < 0 || room >= kRooms) return;
    auto& h = history[(size_t) room];
    if (h.redo.empty()) return;
    Entry e = std::move (h.redo.back()); h.redo.pop_back();
    applySections (e.after);
    h.undo.push_back (std::move (e));
    baseline = captureSections(); baselineValid = true;
}

// ------------------------------------------------------------------ .NJQ

juce::Array<juce::File> NeoBeatProcessor::njqFiles() const
{
    auto found = shared::Settings::load().presets().findChildFiles (juce::File::findFiles, true, "*.njq;*.NJQ");
    found.sort();
    return found;
}

bool NeoBeatProcessor::saveNjq (const juce::File& file, const juce::String& name, juce::String& error)
{
    auto kit = std::make_unique<njq::Kit>();
    kit->name = name.isNotEmpty() ? name : file.getFileNameWithoutExtension();
    kit->madeWith = "NeoBeat 3.0";
    kit->bpm = hostSynced.load() ? displayBpm.load() : (double) tempoParam->load();
    kit->swing = swingParam->load();
    kit->patternIndex = currentPreset.load();
    kit->song = project.song;
    for (int s = 0; s < nb::kNumScenes; ++s) pattern.snapshotScene (s, kit->scenes[(size_t) s]);
    const auto& lib = ws->kit().lib();
    for (int p = 0; p < kPads; ++p)
    {
        auto& ln = kit->lanes[(size_t) p];
        ln.set = true; ln.role = p; ln.name = padName (p);
        ln.levelDb = getParam (paramId (p, "level")); ln.tune = getParam (paramId (p, "tune")); ln.decay = getParam (paramId (p, "decay"));
        ln.pan = getParam (paramId (p, "pan")); ln.width = getParam (paramId (p, "width")); ln.tone = getParam (paramId (p, "tone"));
        ln.drive = getParam (paramId (p, "drive")); ln.hollow = getParam (paramId (p, "hollow")); ln.evolve = getParam (paramId (p, "evolve"));
        ln.choke = (int) getParam (paramId (p, "choke"));
        if (useSample[(size_t) p].load() && samples[(size_t) p])
        {
            ln.hasSample = true; ln.l = samples[(size_t) p]->left; ln.r = samples[(size_t) p]->right; ln.sampleRate = samples[(size_t) p]->sampleRate;
        }
        else ln.stock = lib.sounds[(size_t) juce::jlimit (0, (int) lib.sounds.size() - 1, (int) getParam (paramId (p, "sound")))].id;
    }
    if (! njq::save (file, *kit, error)) return false;
    currentNjq = file.getFullPathName();
    return true;
}

bool NeoBeatProcessor::loadNjq (const juce::File& file, juce::String& error)
{
    auto kit = std::make_unique<njq::Kit>();
    if (! njq::load (file, *kit, error)) return false;
    const auto& lib = ws->kit().lib();
    for (int p = 0; p < kPads; ++p)
    {
        auto& ln = kit->lanes[(size_t) p];
        if (! ln.set) continue;
        setParam (paramId (p, "level"), ln.levelDb); setParam (paramId (p, "tune"), ln.tune); setParam (paramId (p, "decay"), ln.decay);
        setParam (paramId (p, "pan"), ln.pan); setParam (paramId (p, "width"), ln.width); setParam (paramId (p, "tone"), ln.tone);
        setParam (paramId (p, "drive"), ln.drive); setParam (paramId (p, "hollow"), ln.hollow); setParam (paramId (p, "evolve"), ln.evolve);
        setParam (paramId (p, "choke"), (float) ln.choke);
        if (ln.hasSample && ! ln.l.empty())
        {
            auto d = std::make_shared<nb::SampleData>();
            d->sampleRate = (float) ln.sampleRate; d->left = std::move (ln.l); d->right = std::move (ln.r);
            d->path = ("njq:" + file.getFullPathName() + "#" + juce::String (p)).toStdString();
            std::shared_ptr<const nb::SampleData> ptr = d;
            files[d->path] = ptr; keepAlive.push_back (ptr);
            setPadSample (p, ptr);
        }
        else
        {
            clearSample (p);
            const int si = ln.stock.isNotEmpty() ? lib.find (ln.stock.toStdString()) : -1;
            if (si >= 0) setParam (paramId (p, "sound"), (float) si);
        }
    }
    for (int s = 0; s < nb::kNumScenes; ++s) pattern.loadScene (s, kit->scenes[(size_t) s]);
    project.song = kit->song;
    setParam ("swing", kit->swing);
    setParam ("tempo", (float) juce::jlimit (60.0, 200.0, kit->bpm));       // the standalone's tempo; a host keeps its own
    suggestedBpm.store (kit->bpm);
    if (kit->patternIndex >= 0 && kit->patternIndex < nb::numPatterns()) currentPreset.store (kit->patternIndex);
    currentNjq = file.getFullPathName();
    projectChanged();
    return true;
}

bool NeoBeatProcessor::makeDefault (const juce::File& njqFile, bool startWithIt, juce::String& error)
{
    if (! njqFile.existsAsFile()) { error = "The file is not there any more."; return false; }
    auto st = shared::Settings::load();
    const auto folder = st.presets();
    juce::File target = njqFile;
    if (! njqFile.isAChildOf (folder))
    {
        target = folder.getChildFile (njqFile.getFileName());
        if (target.existsAsFile() && ! target.hasIdenticalContentTo (njqFile)) target = folder.getNonexistentChildFile (njqFile.getFileNameWithoutExtension(), njqFile.getFileExtension(), false);
        if (! target.existsAsFile() && ! njqFile.copyFileTo (target)) { error = "Could not copy it into " + folder.getFullPathName(); return false; }
    }
    if (startWithIt) st.startupNjq = target.getFullPathName();
    st.save();
    currentNjq = target.getFullPathName();
    return true;
}

// ------------------------------------------------------------------ the learner (one for the process, one file for both apps)

nbk::Learner& NeoBeatProcessor::learner() { return nbkstore::learner(); }
void NeoBeatProcessor::saveLearner() { nbkstore::save(); }

namespace
{
nbk::Features featuresOf (const nb::SampleData& s)
{
    const int n = (int) std::min<size_t> (s.left.size(), (size_t) (s.sampleRate * 10.0f));
    return nbk::analyse (s.left.data(), s.right.size() == s.left.size() ? s.right.data() : s.left.data(), n, s.sampleRate);
}
}

void NeoBeatProcessor::rateLane (int pad, float stars)
{
    if (pad < 0 || pad >= kPads || ! samples[(size_t) pad]) return;
    const auto f = featuresOf (*samples[(size_t) pad]);
    if (! f.valid) return;
    learner().rate (nbk::vectorOf (f), pad, stars, padName (pad).toStdString());
    saveLearner();
}
void NeoBeatProcessor::teachLane (int pad)
{
    if (pad < 0 || pad >= kPads || ! samples[(size_t) pad]) return;
    const auto f = featuresOf (*samples[(size_t) pad]);
    if (! f.valid) return;
    learner().teachRole (nbk::vectorOf (f), pad, padName (pad).toStdString());
    saveLearner();
}
float NeoBeatProcessor::laneRating (int pad)
{
    if (pad < 0 || pad >= kPads || ! samples[(size_t) pad]) return 0.0f;
    const auto f = featuresOf (*samples[(size_t) pad]);
    return f.valid ? learner().ratingOf (nbk::vectorOf (f)) : 0.0f;
}

void NeoBeatProcessor::matchLevel (int pad)
{
    if (pad < 0 || pad >= kPads || ! samples[(size_t) pad]) return;
    const auto& s = *samples[(size_t) pad];
    const auto lv = nbk::measureLevel (s.left, s.right, s.sampleRate);
    setParam (paramId (pad, "level"), nbk::matchGainDb (lv));
}

// ------------------------------------------------------------------ a folder of samples into the lanes

NeoBeatProcessor::ImportReport NeoBeatProcessor::importFolder (const juce::File& folder, bool pickMostMusical)
{
    ImportReport rep;
    if (! folder.isDirectory()) return rep;
    auto found = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    found.sort();
    rep.found = found.size();
    struct Item { juce::File file; std::shared_ptr<const nb::SampleData> data; };
    std::vector<Item> items; std::vector<nbk::Candidate> pool;
    auto& L = learner();
    for (const auto& f : found)
    {
        if (items.size() >= 256) break;                       // plenty to choose from
        auto d = loadFile (f);
        if (! d) continue;
        nbk::Candidate c; c.f = featuresOf (*d);
        if (! c.f.valid) continue;
        c.x = nbk::vectorOf (c.f); c.roles = L.roles (c.f); c.quality = nbk::musicalScore (c.f);
        if (pickMostMusical && c.quality < 0.2f) continue;    // clipped, drowned in noise, or not a one-shot
        items.push_back ({ f, d }); pool.push_back (c);
    }
    if (pool.empty()) return rep;
    const auto pick = nbk::chooseKit (L, pool, true);
    for (int lane = 0; lane < kPads; ++lane)
    {
        const int i = pick[(size_t) lane];
        if (i < 0) continue;
        setPadSample (lane, items[(size_t) i].data);
        const auto lv = nbk::measureLevel (items[(size_t) i].data->left, items[(size_t) i].data->right, items[(size_t) i].data->sampleRate);
        const float gain = nbk::matchGainDb (lv);
        setParam (paramId (lane, "level"), gain);
        setParam (paramId (lane, "tune"), 0.0f); setParam (paramId (lane, "decay"), 1.0f); setParam (paramId (lane, "tone"), 0.0f);
        setParam (paramId (lane, "drive"), 0.0f); setParam (paramId (lane, "hollow"), 0.0f); setParam (paramId (lane, "width"), 1.0f);
        setParam (paramId (lane, "pan"), 0.0f);
        ++rep.lanes;
        rep.lines.add (juce::String (nbk::roleName (lane)).paddedRight (' ', 11) + items[(size_t) i].file.getFileName()
                       + "   (fit " + juce::String (juce::roundToInt (juce::jmin (1.0f, pool[(size_t) i].roles[(size_t) lane] * 3.0f) * 100.0f)) + " %, "
                       + (gain >= 0 ? "+" : "") + juce::String (gain, 1) + " dB)");
    }
    if (! pickMostMusical)
    {   // all of it: everything (up to 64) also becomes Layermax's rows
        std::vector<std::string> paths;
        for (const auto& it : items) { if ((int) paths.size() >= nb::GridPattern::kSize) break; paths.push_back (it.file.getFullPathName().toStdString()); }
        project.grid.folder = folder.getFullPathName().toStdString();
        project.grid.files = paths; project.grid.useKit = false;
        rep.rows = (int) paths.size();
    }
    auto st = shared::Settings::load(); st.importFolder = folder.getFullPathName(); st.save();
    projectChanged();
    return rep;
}

// ------------------------------------------------------------------ export

struct NeoBeatProcessor::ExportThread final : juce::Thread
{
    ExportThread (NeoBeatProcessor& p, ExportOptions o, std::unique_ptr<nb::RenderInputs> in, float rate, float masterDb, juce::StringArray names)
        : juce::Thread ("NeoBeat export"), proc (p), opt (std::move (o)), inputs (std::move (in)), sr (rate), master (masterDb), laneNames (std::move (names)) {}
    ~ExportThread() override { stopThread (20000); }

    bool writeWav (const juce::File& f, const nb::Rendered& r)
    {
        if (r.size() <= 0) return false;
        const juce::File tmp = f.getSiblingFile (f.getFileName() + ".part");
        tmp.deleteFile();
        {
            auto* os = new juce::FileOutputStream (tmp);
            if (! os->openedOk()) { delete os; return false; }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, sr, 2, 24, {}, 0));
            if (w == nullptr) { delete os; return false; }
            const float* ch[2] = { r.l.data(), r.r.data() };
            if (! w->writeFromFloatArrays (ch, 2, r.size())) return false;
        }
        f.deleteFile();
        return tmp.moveFileTo (f);
    }

    void run() override
    {
        juce::StringArray written; juce::String message;
        opt.folder.createDirectory();
        const auto base = juce::File::createLegalFileName (opt.name.isEmpty() ? "NeoBeat" : opt.name);
        int jobs = (opt.wav ? 1 : 0), doneJobs = 0;
        std::vector<int> stemLanes;
        if (opt.stems)
        {
            double start = 0.0; nb::exportBeats (*inputs, opt.range, start);
            for (int lane = 0; lane < kPads; ++lane)
            {
                bool any = false;
                for (const auto& sc : inputs->scenes) for (int s = 0; s < sc.length && ! any; ++s) any = sc.steps[(size_t) lane][(size_t) s].vel > 0;
                if (any) stemLanes.push_back (lane);
            }
            jobs += (int) stemLanes.size();
        }
        auto progress = [&] (float f) { proc.exportDone.store ((doneJobs + juce::jlimit (0.0f, 1.0f, f)) / (float) std::max (1, jobs)); return ! threadShouldExit(); };
        if (opt.wav && ! threadShouldExit())
        {
            const auto f = opt.folder.getChildFile (base + ".wav");
            if (writeWav (f, nb::renderSong (*inputs, sr, opt.range, master, -1, progress))) written.add (f.getFullPathName());
            else if (! threadShouldExit()) message = "Could not write " + f.getFullPathName();
            ++doneJobs;
        }
        for (int lane : stemLanes)
        {
            if (threadShouldExit()) break;
            const auto f = opt.folder.getChildFile (base + " - " + juce::String (lane + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (laneNames[lane]) + ".wav");
            if (writeWav (f, nb::renderSong (*inputs, sr, opt.range, master, lane, progress))) written.add (f.getFullPathName());
            ++doneJobs;
        }
        if (opt.midi && ! threadShouldExit())
        {
            double start = 0.0; const double beats = nb::exportBeats (*inputs, opt.range, start);
            const auto notes = nb::songNotes (*inputs, opt.range, opt.straightMidi ? -1.0f : inputs->swing);
            std::vector<std::string> names; for (const auto& n : laneNames) names.push_back (n.toStdString());
            const auto bytes = nb::writeMidiFile (notes, inputs->bpm, beats, names);
            const auto f = opt.folder.getChildFile (base + ".mid");
            if (f.replaceWithData (bytes.data(), bytes.size())) written.add (f.getFullPathName());
            else message = "Could not write " + f.getFullPathName();
        }
        if (message.isEmpty()) message = threadShouldExit() ? "Stopped." : (written.isEmpty() ? "Nothing was written." : juce::String (written.size()) + (written.size() == 1 ? " file" : " files") + " in " + opt.folder.getFullPathName());
        { const juce::ScopedLock l (proc.exportLock); proc.exportMessage = message; proc.exportFiles = written; }
        proc.exportDone.store (1.0f);
        proc.exportBusy.store (false);
    }

    NeoBeatProcessor& proc; ExportOptions opt; std::unique_ptr<nb::RenderInputs> inputs; float sr, master; juce::StringArray laneNames;
};

bool NeoBeatProcessor::startExport (const ExportOptions& o)
{
    if (exportBusy.load()) return false;
    exportThread.reset();
    juce::StringArray names; for (int p = 0; p < kPads; ++p) names.add (padName (p));
    auto in = renderInputs();
    if (! hostSynced.load()) in->bpm = tempoParam->load();
    { const juce::ScopedLock l (exportLock); exportMessage.clear(); exportFiles.clear(); }
    exportDone.store (0.0f); exportBusy.store (true);
    auto st = shared::Settings::load(); st.exportFolder = o.folder.getFullPathName(); st.save();
    exportThread = std::make_unique<ExportThread> (*this, o, std::move (in), (float) (prepared ? sampleRate : 48000.0), masterParam->load(), names);
    exportThread->startThread();
    return true;
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
        auto p = std::make_unique<nb::Pattern>(); pattern.snapshot (*p);
        auto* pe = e->createNewChildElement ("PATTERN"); io::writePattern (*pe, *p);
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
            auto p = std::make_unique<nb::Pattern>(); io::readPattern (*c, *p); pattern.load (*p);
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
                if (auto ptr = resolveSample (path)) setPadSample (p, ptr);
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
    auto* scenes = xml->createNewChildElement ("SCENES");
    scenes->setAttribute ("edit", pattern.scene());
    auto p = std::make_unique<nb::Pattern>();
    for (int s = 0; s < nb::kNumScenes; ++s)
    {
        pattern.snapshotScene (s, *p);
        auto* se = scenes->createNewChildElement ("SCENE"); se->setAttribute ("index", s);
        io::writePattern (*se, *p);
    }
    auto* meta = xml->createNewChildElement ("META");
    meta->setAttribute ("preset", currentPreset.load());
    meta->setAttribute ("kit", currentKit.load());
    meta->setAttribute ("version", "3.0.0");
    meta->setAttribute ("njq", currentNjq);
    auto* smp = xml->createNewChildElement ("SAMPLES");
    for (int pad = 0; pad < kPads; ++pad)
        if (useSample[(size_t) pad].load() && samples[(size_t) pad]) smp->setAttribute ("p" + juce::String (pad), juce::String (samples[(size_t) pad]->path));
    xml->addChildElement (io::toXml (project).release());
    {
        // IDM Jack's panel and the include list it shares with ROLL. Whether it is held is the host
        // parameter "jack", saved with the others - and let go again on load (setStateInformation).
        auto* j = xml->createNewChildElement ("JACK");
        j->setAttribute ("density", jack.density);     j->setAttribute ("extremity", jack.extremity);
        j->setAttribute ("swing", jack.swing);         j->setAttribute ("repeat", jack.repeatProb);
        j->setAttribute ("window", jack.windowSteps);  j->setAttribute ("recall", jack.recallProb);
        j->setAttribute ("memory", jack.memory);       j->setAttribute ("division", jack.division);
        j->setAttribute ("retrig", jack.maxRetriggers); j->setAttribute ("timingMax", jack.timingMaxMs);
        j->setAttribute ("replace", jack.replace);     j->setAttribute ("recovery", jack.recoveryBars);
        j->setAttribute ("self", jack.selfRandomise);  j->setAttribute ("seed", juce::String ((juce::int64) jack.seed));
        j->setAttribute ("lock", jackLockSeed);
        int axes = 0; for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i) axes |= jack.axis[i] ? (1 << i) : 0;
        j->setAttribute ("axes", axes);
        j->setAttribute ("lanes", (int) jackMask());
    }
    copyXmlToBinary (*xml, destData);
}

void NeoBeatProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    stateRestored.store (true);
    auto p = std::make_unique<nb::Pattern>();
    if (auto* scenes = xml->getChildByName ("SCENES"))
    {
        for (int s = 0; s < nb::kNumScenes; ++s) { *p = nb::Pattern(); pattern.loadScene (s, *p); }
        for (auto* se : scenes->getChildWithTagNameIterator ("SCENE"))
        {
            const int s = se->getIntAttribute ("index", -1);
            if (s >= 0 && s < nb::kNumScenes) { io::readPattern (*se, *p); pattern.loadScene (s, *p); }
        }
        pattern.edit.store (juce::jlimit (0, nb::kNumScenes - 1, scenes->getIntAttribute ("edit", 0)));
    }
    else if (auto* pat = xml->getChildByName ("PATTERN"))
    {   // a 2.0 project: its one pattern is scene A
        io::readPattern (*pat, *p); pattern.loadScene (0, *p);
        nb::Pattern empty; empty.length = p->length;
        for (int s = 1; s < nb::kNumScenes; ++s) pattern.loadScene (s, empty);
        pattern.edit.store (0);
    }
    if (auto* meta = xml->getChildByName ("META"))
    {
        currentPreset.store (juce::jlimit (0, nb::numPatterns() - 1, meta->getIntAttribute ("preset", 0)));
        currentKit.store (juce::jlimit (0, (int) nb::kits().size() - 1, meta->getIntAttribute ("kit", 0)));
        currentNjq = meta->getStringAttribute ("njq");
    }
    if (auto* pr = xml->getChildByName ("PROJECT")) io::fromXml (*pr, project);
    else project = nb::Project();
    std::unique_ptr<juce::XmlElement> smpCopy;
    if (auto* smp = xml->getChildByName ("SAMPLES")) smpCopy = std::make_unique<juce::XmlElement> (*smp);
    if (auto* j = xml->getChildByName ("JACK"))
    {
        nsr::JackSettings d;
        jack.density = (float) j->getDoubleAttribute ("density", d.density);
        jack.extremity = (float) j->getDoubleAttribute ("extremity", d.extremity);
        jack.swing = (float) j->getDoubleAttribute ("swing", d.swing);
        jack.repeatProb = (float) j->getDoubleAttribute ("repeat", d.repeatProb);
        jack.windowSteps = j->getIntAttribute ("window", d.windowSteps);
        jack.recallProb = (float) j->getDoubleAttribute ("recall", d.recallProb);
        jack.memory = j->getIntAttribute ("memory", d.memory);
        jack.division = j->getIntAttribute ("division", d.division);
        jack.maxRetriggers = j->getIntAttribute ("retrig", d.maxRetriggers);
        jack.timingMaxMs = (float) j->getDoubleAttribute ("timingMax", d.timingMaxMs);
        jack.replace = j->getBoolAttribute ("replace", d.replace);
        jack.recoveryBars = (float) j->getDoubleAttribute ("recovery", d.recoveryBars);
        jack.selfRandomise = (float) j->getDoubleAttribute ("self", d.selfRandomise);
        jack.seed = (uint64_t) j->getStringAttribute ("seed", "1").getLargeIntValue();
        jackLockSeed = j->getBoolAttribute ("lock", false);
        const int axes = j->getIntAttribute ("axes", 0x1f);
        for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i) jack.axis[i] = (axes >> i) & 1;
        const uint32_t lanes = (uint32_t) j->getIntAttribute ("lanes", 0xffff);
        auto& list = chaos.allTargets();
        for (int p = 0; p < kPads && p < (int) list.size(); ++p) list[(size_t) p].on = (lanes >> p) & 1u;
        jackApply();
    }
    for (auto* name : { "PATTERN", "SCENES", "META", "SAMPLES", "PROJECT", "JACK" })
        while (auto* c = xml->getChildByName (name)) xml->removeChildElement (c, true);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    // IDM Jack is held through a host parameter, and host parameters are saved with the session, so
    // a session saved mid-seizure would open seizing. It opens let go; automation, if there is any,
    // takes it from there.
    if (auto* jp = apvts.getParameter ("jack")) jp->setValueNotifyingHost (0.0f);
    for (int pad = 0; pad < kPads; ++pad)
    {
        std::shared_ptr<const nb::SampleData> ptr;
        if (smpCopy != nullptr && smpCopy->hasAttribute ("p" + juce::String (pad))) ptr = resolveSample (smpCopy->getStringAttribute ("p" + juce::String (pad)));
        if (ptr) setPadSample (pad, ptr); else clearSample (pad);
    }
    reloadProjectFiles();
    projectChanged();
    // a restored project starts with a clean history
    baselineValid = false;
    for (auto& h : history) { h.undo.clear(); h.redo.clear(); }
}

juce::AudioProcessorEditor* NeoBeatProcessor::createEditor() { return new NeoBeatEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeoBeatProcessor(); }
