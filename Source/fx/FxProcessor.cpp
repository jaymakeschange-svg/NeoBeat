#include "FxProcessor.h"
#include "FxEditor.h"

namespace nbfx
{
const std::vector<std::pair<const char*, double>>& divisions()
{
    static const std::vector<std::pair<const char*, double>> d { { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },
                                                                 { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/64", 0.0625 } };
    return d;
}
int divisionIndex (double beats)
{
    int best = 0; double err = 1e9;
    for (int i = 0; i < (int) divisions().size(); ++i) if (std::fabs (divisions()[(size_t) i].second - beats) < err) { err = std::fabs (divisions()[(size_t) i].second - beats); best = i; }
    return best;
}
static double divisionBeats (float index) { return divisions()[(size_t) juce::jlimit (0, (int) divisions().size() - 1, juce::roundToInt (index))].second; }

const std::vector<ParamDef>& paramDefs()
{
    using U = Unit; using W = When;
    static const std::vector<ParamDef> friction {
        { "rateDiv", "JUMP RATE", 0, 7, 4, false, 0, U::Division, W::Tempo },
        { "rateMs", "JUMP RATE", 10, 1000, 125, true, 0, U::Ms, W::Millis },
        { "swing", "SWING", 0.5f, 0.75f, 0.62f, false, 0, U::Percent },
        { "stretch", "STRETCH", 1, 64, 4, true, 0, U::Times, W::Always, true },
        { "creep", "CREEP", 0, 1, 0.15f, false, 0, U::Percent },
        { "slip", "YANK", 1, 200, 70, true, 0, U::Ms },
        { "grit", "GRIT", 0, 1, 0.25f, false, 0, U::Percent },
        { "holdFade", "HOLD FADE", 0, 1, 0, false, 0, U::Percent },
        { "snap", "SNAP", 0, 1, 0, false, 0, U::Percent },
        { "tape", "TAPE", 0, 1, 0, false, 0, U::Percent },
        { "attack", "ATTACK", 0, 200, 15, false, 0, U::Ms },
        { "peel", "PEEL", 0, 1, 1, false, 1, U::Percent },
        { "freeze", "FREEZE", 0, 1, 0.25f, false, 1, U::Percent },
        { "screech", "SCREECH", 100, 4000, 900, true, 1, U::Hz },
        { "adhesive", "ADHESIVE", 0, 1, 0.4f, false, 1, U::Percent },
        { "grain", "GRAIN", 1, 60, 8, true, 1, U::Ms },
        { "rasp", "RASP", 0, 1, 0.3f, false, 1, U::Percent },
        { "width", "WIDTH", 0, 1, 0.6f, false, 1, U::Percent },
        { "roll", "ROLL", 0, 1, 0.5f, false, 2, U::Percent },
        { "rollBody", "ROLL BODY", 0, 6, 5, false, 2, U::Body },
        { "rollSize", "ROLL SIZE", 0.5f, 3, 1, true, 2, U::Times },
        { "strip", "STRIP", 0, 1, 0.35f, false, 2, U::Percent },
        { "gateBeats", "GATE", 0, 32, 0, false, 3, U::GateBeats, W::Tempo },
        { "gateMs", "GATE", 0, 16000, 0, false, 3, U::GateMs, W::Millis },
        { "decay", "DECAY", 20, 4000, 400, true, 3, U::Ms },
        { "wet", "WET", 0, 1, 1, false, 3, U::Percent },
        { "seed", "SEED", 1, 99, 1, false, 3, U::Count },
        { "dry", "DRY", 0, 1.5f, 0, false, -1, U::Percent },
        { "thru", "INPUT", 0, 1.5f, 1, false, -1, U::Percent },
        { "level", "LEVEL", -24, 12, 0, false, -1, U::Db },
        { "threshold", "THRESHOLD", -48, 0, -24, false, -1, U::Db },
        { "capture", "LENGTH", 30, 2000, 250, true, -1, U::Ms },
        { "retrig", "RETRIGGER", 20, 500, 60, true, -1, U::Ms },
    };
    static const std::vector<ParamDef> bounce {
        { "rateDiv", "FIRST GAP", 0, 7, 1, false, 0, U::Division, W::Tempo },
        { "gridDiv", "GRID", 0, 7, 4, false, 0, U::Division, W::Tempo },
        { "rateMs", "FIRST GAP", 20, 2000, 400, true, 0, U::Ms, W::Millis },
        { "gridMs", "GRID", 5, 500, 60, true, 0, U::Ms, W::Millis },
        { "swing", "SWING", 0.5f, 0.75f, 0.62f, false, 0, U::Percent },
        { "bounciness", "BOUNCINESS", 0.3f, 0.95f, 0.78f, false, 0, U::Percent, W::Always, true },
        { "bounces", "BOUNCES", 1, 64, 32, false, 0, U::Count },
        { "speed", "SPEED", 0, 1, 0.3f, false, 0, U::Percent },
        { "tail", "TAIL", 0.05f, 4, 0.9f, true, 0, U::Seconds },
        { "near", "NEAR", 0.3f, 30, 1.5f, true, 1, U::Metres },
        { "far", "FAR", 0.3f, 40, 10, true, 1, U::Metres },
        { "panRandom", "PAN RANDOM", 0, 1, 0.5f, false, 1, U::Percent },
        { "panCentre", "PAN CENTRE", -1, 1, 0, false, 1, U::Pan },
        { "width", "WIDTH", 0, 1, 1, false, 1, U::Percent },
        { "room", "ROOM", 0, 1, 0.25f, false, 1, U::Percent },
        { "place", "ROOM TYPE", 0, 2, 0, false, 1, U::Place },
        { "wet", "WET", 0, 1, 1, false, 1, U::Percent },
        { "seed", "SEED", 1, 99, 1, false, 1, U::Count },
        { "dry", "DRY", 0, 1.5f, 1, false, -1, U::Percent },
        { "thru", "INPUT", 0, 1.5f, 1, false, -1, U::Percent },
        { "level", "LEVEL", -24, 12, 0, false, -1, U::Db },
        { "threshold", "THRESHOLD", -48, 0, -24, false, -1, U::Db },
        { "capture", "LENGTH", 30, 2000, 250, true, -1, U::Ms },
        { "retrig", "RETRIGGER", 20, 500, 60, true, -1, U::Ms },
    };
    return FxProcessor::isFriction() ? friction : bounce;
}

juce::StringArray pageNames()
{
    return FxProcessor::isFriction() ? juce::StringArray { "MOTION", "PEEL", "BODY", "SHAPE", "LINES" } : juce::StringArray { "MOTION", "SPACE" };
}

juce::String formatValue (Unit u, float v)
{
    static const char* bodies[] = { "Basketball", "Bucket", "Can", "Clank", "Pan", "PVC pipe", "Tube clank" };
    static const char* places[] = { "Small room", "Studio", "Large hall" };
    switch (u)
    {
        case Unit::Percent:   return juce::String (juce::roundToInt (v * 100)) + " %";
        case Unit::Ms:        return juce::String (juce::roundToInt (v)) + " ms";
        case Unit::Times:     return "x" + juce::String (v, v < 10.0f ? 2 : 1);
        case Unit::Hz:        return juce::String (juce::roundToInt (v)) + " Hz";
        case Unit::Metres:    return juce::String (v, 1) + " m";
        case Unit::Seconds:   return juce::String (v, 2) + " s";
        case Unit::Count:     return juce::String (juce::roundToInt (v));
        case Unit::Division:  return juce::String (divisions()[(size_t) juce::jlimit (0, 7, juce::roundToInt (v))].first);
        case Unit::Body:      return juce::String (bodies[juce::jlimit (0, 6, juce::roundToInt (v))]);
        case Unit::GateBeats: return v < 0.01f ? juce::String ("all") : juce::String (v, 2) + " beats";
        case Unit::GateMs:    return v < 1.0f ? juce::String ("all") : juce::String (juce::roundToInt (v)) + " ms";
        case Unit::Db:        return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
        case Unit::Pan:       return std::fabs (v) < 0.01f ? juce::String ("centre") : juce::String (juce::roundToInt (std::fabs (v) * 100)) + (v < 0 ? " L" : " R");
        case Unit::Place:     return juce::String (places[juce::jlimit (0, 2, juce::roundToInt (v))]);
        case Unit::Plain:     break;
    }
    return juce::String (v, 2);
}

static bool isStepped (Unit u) { return u == Unit::Count || u == Unit::Division || u == Unit::Body || u == Unit::Place; }

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
static const std::vector<Preset>& presets()
{
    static const std::vector<Preset> friction {
        { "Duct tape", {} },
        { "Slow peel", { { "stretch", 12 }, { "rateDiv", 2 }, { "slip", 140 }, { "creep", 0.4f }, { "peel", 1 }, { "adhesive", 0.7f }, { "screech", 500 } } },
        { "Machine rip", { { "stretch", 6 }, { "rateDiv", 6 }, { "slip", 12 }, { "grit", 0.6f }, { "snap", 0.6f }, { "screech", 2200 }, { "rasp", 0.1f } } },
        { "Frozen clank", { { "stretch", 24 }, { "peel", 0.15f }, { "freeze", 1 }, { "creep", 0.05f }, { "roll", 0.8f }, { "holdFade", 0.3f } } },
        { "Tape machine", { { "stretch", 3 }, { "tape", 1 }, { "peel", 0.2f }, { "freeze", 0.6f }, { "slip", 30 }, { "strip", 0.7f } } },
        { "Short accent", { { "stretch", 8 }, { "gateBeats", 1 }, { "decay", 180 }, { "snap", 0.4f } } },
    };
    static const std::vector<Preset> bounce {
        { "Ball drop", {} },
        { "Far away", { { "near", 2 }, { "far", 30 }, { "speed", 0.5f }, { "room", 0.5f }, { "place", 2 } } },
        { "Pinball", { { "rateDiv", 2 }, { "gridDiv", 6 }, { "bounciness", 0.9f }, { "bounces", 48 }, { "panRandom", 1 }, { "tail", 0.2f }, { "far", 4 } } },
        { "Coming closer", { { "near", 14 }, { "far", 0.6f }, { "speed", 0.6f }, { "bounciness", 0.82f } } },
        { "Dead floor", { { "bounciness", 0.45f }, { "bounces", 6 }, { "room", 0.1f }, { "far", 3 } } },
        { "Hall roll", { { "rateDiv", 1 }, { "gridDiv", 5 }, { "bounciness", 0.88f }, { "room", 0.8f }, { "place", 2 }, { "panRandom", 0.8f } } },
    };
    return FxProcessor::isFriction() ? friction : bounce;
}
} // namespace nbfx

using namespace nbfx;

juce::AudioProcessorValueTreeState::ParameterLayout FxProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& d : paramDefs())
    {
        juce::NormalisableRange<float> range (d.lo, d.hi);
        if (isStepped (d.unit)) range.interval = 1.0f;
        else if (d.log && d.lo > 0.0f) range.setSkewForCentre (std::sqrt (d.lo * d.hi));
        const Unit unit = d.unit;
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { d.id, 1 }, juce::String (d.label).toLowerCase() + (d.when == When::Millis ? " (ms)" : ""), range, d.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })));
    }
    // on/off switches as two-choice parameters: they snap to 0 or 1 whatever raw value a host sends
    // (AudioParameterBool keeps the raw value, so a restored state could read 0.67 for "on")
    const juce::StringArray offOn { "Off", "On" };
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rateTempo", 1 }, "tempo sync", offOn, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "keytrack", 1 }, "keytrack", offOn, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "choke", 1 }, "choke", offOn, isFriction() ? 1 : 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "mode", juce::StringArray { "Load a hit", "Catch hits" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "loop", 1 }, "loop", juce::StringArray { "Off", "1 bar", "2 bars", "4 bars" }, 0));
    return l;
}

FxProcessor::FxProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    core = std::make_unique<Core> (kKind);
    raw = { apvts.getRawParameterValue ("mode"), apvts.getRawParameterValue ("dry"), apvts.getRawParameterValue ("thru"), apvts.getRawParameterValue ("level"),
            apvts.getRawParameterValue ("threshold"), apvts.getRawParameterValue ("capture"), apvts.getRawParameterValue ("retrig"),
            apvts.getRawParameterValue ("keytrack"), apvts.getRawParameterValue ("choke"), apvts.getRawParameterValue ("loop") };
    midiIn.reserve (256);
    hit = defaultHit();
    startTimerHz (30);
}

FxProcessor::~FxProcessor() { stopTimer(); core->release(); }

void FxProcessor::setValue (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

bool FxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

void FxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    core->prepare ((float) sampleRate, std::max (samplesPerBlock, 4096));
    submittedHash = 0;   // render again for this rate
}

Settings FxProcessor::settingsNow() const
{
    Settings s;
    const bool tempo = value ("rateTempo") > 0.5f;
    if (isFriction())
    {
        auto& p = s.fs;
        p.rate.tempo = tempo; p.rate.beats = divisionBeats (value ("rateDiv")); p.rate.ms = value ("rateMs"); p.rate.swing = value ("swing");
        p.stretch = value ("stretch"); p.creep = value ("creep"); p.slipMs = value ("slip"); p.grit = value ("grit"); p.holdFade = value ("holdFade");
        p.snap = value ("snap"); p.tape = value ("tape"); p.attackMs = value ("attack");
        p.peel = value ("peel"); p.freeze = value ("freeze"); p.screechHz = value ("screech"); p.adhesive = value ("adhesive"); p.grainMs = value ("grain");
        p.rasp = value ("rasp"); p.width = value ("width");
        p.roll = value ("roll"); p.rollBody = juce::roundToInt (value ("rollBody")); p.rollSize = value ("rollSize"); p.strip = value ("strip");
        p.gateBeats = value ("gateBeats"); p.gateMs = value ("gateMs"); p.decayMs = value ("decay"); p.wet = value ("wet");
        p.seed = (uint32_t) juce::roundToInt (value ("seed"));
        p.lines = lines;
    }
    else
    {
        auto& p = s.bd;
        p.rate.tempo = tempo; p.rate.beats = divisionBeats (value ("rateDiv")); p.rate.ms = value ("rateMs"); p.rate.swing = value ("swing");
        p.gridBeats = divisionBeats (value ("gridDiv")); p.gridMs = value ("gridMs");
        p.bounciness = value ("bounciness"); p.maxBounces = juce::roundToInt (value ("bounces")); p.speed = value ("speed"); p.tail = value ("tail");
        p.distStart = value ("near"); p.distEnd = value ("far"); p.panRandom = value ("panRandom"); p.panCenter = value ("panCentre");
        p.width = value ("width"); p.room = value ("room"); p.wet = value ("wet"); p.seed = (uint32_t) juce::roundToInt (value ("seed"));
        s.place = juce::roundToInt (value ("place"));
    }
    return s;
}

void FxProcessor::timerCallback()
{
    // submit a render when the settings, the hit or the tempo have changed and held still a moment
    const Settings s = settingsNow();
    const double bpm = std::round (displayBpm.load() * 100.0) / 100.0;
    nb::Hasher h;
    if (isFriction()) hashStretch (h, s.fs); else { hashBounce (h, s.bd); h.i (s.place); }
    h.d (bpm).p (hit.get()).u (lineVersion);
    stepBeats.store (isFriction() ? (s.fs.rate.tempo ? s.fs.rate.beats : 0.0) : (s.bd.rate.tempo ? s.bd.gridBeats : 0.0));
    if (h.h != pendingHash) { pendingHash = h.h; stableTicks = 0; return; }
    if (++stableTicks < 4 || pendingHash == submittedHash) return;
    submittedHash = pendingHash;
    core->renderer.set (s, hit, bpm);
}

void FxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) { buffer.clear(); return; }
    const int ins = getTotalNumInputChannels();
    Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tr.playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) tr.bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition()) tr.ppq = *q; else tr.playing = false;
            if (auto sig = pos->getTimeSignature()) tr.beatsPerBar = juce::jlimit (1.0, 16.0, sig->numerator * 4.0 / std::max (1, sig->denominator));
        }
    displayBpm.store (tr.bpm); hostPlaying.store (tr.playing);
    midiIn.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && midiIn.size() < midiIn.capacity()) midiIn.push_back ({ juce::jlimit (0, n - 1, meta.samplePosition), true, m.getNoteNumber(), m.getFloatVelocity() });
    }
    midi.clear();
    Live live;
    live.mode = raw.mode->load() > 0.5f ? Mode::Capture : Mode::Load;
    live.dry = raw.dry->load(); live.thru = raw.thru->load();
    live.level = juce::Decibels::decibelsToGain (raw.level->load());
    live.threshold = juce::Decibels::decibelsToGain (raw.threshold->load());
    live.captureMs = raw.capture->load(); live.retrigMs = raw.retrig->load();
    live.keytrack = raw.keytrack->load() > 0.5f; live.choke = raw.choke->load() > 0.5f;
    static const int bars[] = { 0, 1, 2, 4 };
    live.loopBars = bars[(size_t) juce::jlimit (0, 3, juce::roundToInt (raw.loop->load()))];
    live.stepBeats = stepBeats.load();
    float* l = buffer.getWritePointer (0); float* r = buffer.getWritePointer (1);
    core->process (ins >= 1 ? l : nullptr, ins >= 2 ? r : (ins == 1 ? l : nullptr), l, r, n, tr, midiIn.data(), (int) midiIn.size(), live);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// ---------------------------------------------------------------- the hit
bool FxProcessor::loadFile (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 16 || reader->sampleRate < 4000.0) return false;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (Renderer::kMaxHitSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buf (2, n);
    reader->read (&buf, 0, n, 0, true, true);
    auto s = std::make_shared<nb::SampleData>();
    s->sampleRate = (float) reader->sampleRate; s->path = f.getFileNameWithoutExtension().toStdString();
    s->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
    s->right.assign (buf.getReadPointer (reader->numChannels > 1 ? 1 : 0), buf.getReadPointer (reader->numChannels > 1 ? 1 : 0) + n);
    hit = s; hitIsBuiltIn = false;
    return true;
}

void FxProcessor::useDefaultHit() { hit = defaultHit(); hitIsBuiltIn = true; }

bool FxProcessor::keepLastCapture()
{
    auto c = core->renderer.lastCapture();
    if (! c || c->left.size() < 64) return false;
    hit = c; hitIsBuiltIn = false;
    setValue ("mode", 0.0f);
    return true;
}

static bool writeWav (juce::OutputStream* out, const float* l, const float* r, int n, double sr, int bits)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out);
    auto options = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (bits);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr) return false;
    const float* chans[] = { l, r };
    return writer->writeFromFloatArrays (chans, 2, n);
}

bool FxProcessor::saveShown (const juce::File& f)
{
    auto r = core->renderer.shown();
    if (! r || r->size() < 16) return false;
    f.deleteFile();
    auto out = f.createOutputStream();
    if (out == nullptr) return false;
    return writeWav (out.release(), r->l.data(), r->r.data(), r->size(), r->sampleRate, 24);
}

// ---------------------------------------------------------------- programs and state
int FxProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String FxProcessor::getProgramName (int i) { return i >= 0 && i < (int) presets().size() ? juce::String (presets()[(size_t) i].name) : juce::String(); }
void FxProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= (int) presets().size()) return;
    program = i;
    for (const auto& d : paramDefs()) if (d.page >= 0) setValue (d.id, d.def);   // the side column (levels, catcher) is left alone
    setValue ("rateTempo", 1.0f);
    for (const auto& v : presets()[(size_t) i].values) setValue (v.first, v.second);
    lines.clear(); linesChanged();
}

void FxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root (isFriction() ? "NEOBEAT_FRICTION" : "NEOBEAT_BOUNCE");
    root.setAttribute ("version", 1);
    root.setAttribute ("program", program);
    if (auto params = apvts.copyState().createXml()) root.addChildElement (params.release());
    auto* ls = root.createNewChildElement ("LINES");
    for (const auto& l : lines)
    {
        auto* e = ls->createNewChildElement ("LINE");
        e->setAttribute ("on", l.on ? 1 : 0); e->setAttribute ("lo", l.fLo); e->setAttribute ("hi", l.fHi); e->setAttribute ("offset", l.offset);
        e->setAttribute ("tilt", l.tilt); e->setAttribute ("spray", l.spray); e->setAttribute ("edge", l.edge); e->setAttribute ("level", l.level); e->setAttribute ("pan", l.pan);
    }
    if (hit && ! hitIsBuiltIn && hit->left.size() > 16)
    {
        // the hit travels with the project (16-bit WAV, at most 4 s)
        juce::MemoryBlock block;
        const auto& R = hit->right.size() == hit->left.size() ? hit->right : hit->left;
        if (writeWav (new juce::MemoryOutputStream (block, false), hit->left.data(), R.data(), (int) hit->left.size(), hit->sampleRate, 16))
        {
            auto* h = root.createNewChildElement ("HIT");
            h->setAttribute ("name", juce::String (hit->path));
            h->addTextElement (block.toBase64Encoding());
        }
    }
    copyXmlToBinary (root, dest);
}

void FxProcessor::setStateInformation (const void* data, int size)
{
    auto root = getXmlFromBinary (data, size);
    if (root == nullptr || ! root->hasTagName (isFriction() ? "NEOBEAT_FRICTION" : "NEOBEAT_BOUNCE")) return;
    program = juce::jlimit (0, std::max (0, getNumPrograms() - 1), root->getIntAttribute ("program", 0));
    if (auto* params = root->getChildByName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*params));
    lines.clear();
    if (auto* ls = root->getChildByName ("LINES"))
        for (auto* e : ls->getChildIterator())
        {
            if (lines.size() >= 6) break;
            nb::StretchLine l;
            l.on = e->getIntAttribute ("on", 1) != 0;
            l.fLo = juce::jlimit (20.0f, 18000.0f, (float) e->getDoubleAttribute ("lo", l.fLo)); l.fHi = juce::jlimit (30.0f, 20000.0f, (float) e->getDoubleAttribute ("hi", l.fHi));
            l.offset = juce::jlimit (-0.5f, 0.5f, (float) e->getDoubleAttribute ("offset", 0.0)); l.tilt = juce::jlimit (-0.5f, 0.5f, (float) e->getDoubleAttribute ("tilt", 0.0));
            l.spray = juce::jlimit (0.0f, 1.0f, (float) e->getDoubleAttribute ("spray", l.spray)); l.edge = juce::jlimit (0.0f, 2.0f, (float) e->getDoubleAttribute ("edge", l.edge));
            l.level = juce::jlimit (0.0f, 2.0f, (float) e->getDoubleAttribute ("level", l.level)); l.pan = juce::jlimit (-1.0f, 1.0f, (float) e->getDoubleAttribute ("pan", 0.0));
            lines.push_back (l);
        }
    linesChanged();
    bool gotHit = false;
    if (auto* h = root->getChildByName ("HIT"))
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (h->getAllSubText()) && block.getSize() > 44)
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::MemoryInputStream (block, false), true));
            if (reader != nullptr && reader->lengthInSamples > 16 && reader->sampleRate >= 4000.0)
            {
                const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (Renderer::kMaxHitSeconds * reader->sampleRate));
                juce::AudioBuffer<float> buf (2, n);
                reader->read (&buf, 0, n, 0, true, true);
                auto s = std::make_shared<nb::SampleData>();
                s->sampleRate = (float) reader->sampleRate; s->path = h->getStringAttribute ("name", "Saved hit").toStdString();
                s->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n); s->right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + n);
                hit = s; hitIsBuiltIn = false; gotHit = true;
            }
        }
    }
    if (! gotHit) useDefaultHit();
}

juce::AudioProcessorEditor* FxProcessor::createEditor() { return new FxEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FxProcessor(); }
