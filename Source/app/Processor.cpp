#include "Processor.h"
#include "Editor.h"

MjqProcessor::MjqProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    mjq::generate (line, (uint32_t) juce::Random::getSystemRandom().nextInt (1000000) + 1);
    lineChanged();
    message = "JAQ writes a line; DIALECT moves its notes, TRAFFIC how many play.";
}

void MjqProcessor::lineChanged()
{
    auto p = std::make_shared<mjq::Pass> (mjq::readPass (line));
    std::atomic_store (&shown, std::shared_ptr<const mjq::Pass> (std::move (p)));
    version.fetch_add (1);
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
    undoStack.push_back (line);
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool MjqProcessor::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (line);
    line = undoStack.back();
    undoStack.pop_back();
    lineChanged();
    return true;
}

bool MjqProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (line);
    line = redoStack.back();
    redoStack.pop_back();
    lineChanged();
    return true;
}

void MjqProcessor::prepareToPlay (double sampleRate, int)
{
    player.prepare (sampleRate);
    player.allKeysUp();
}

void MjqProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();                                   // MidiJaQ makes MIDI; its instrument comes later

    // the keys held down, for Follow, Bass and the Gate clock
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn())            player.keyDown (msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())      player.keyUp (msg.getNoteNumber());
        else if (msg.isAllNotesOff())  player.allKeysUp();
    }
    midi.clear();                                    // what came in played the line; it is not passed on

    auto snapshot = std::atomic_load (&shown);
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

    mjq::MidiOut out[256];
    const int n = player.process (line, *snapshot, clockMode, host, audio.getNumSamples(), out, 256);
    for (int i = 0; i < n; ++i)
        midi.addEvent (out[i].on ? juce::MidiMessage::noteOn (1, out[i].note, (juce::uint8) out[i].velocity)
                                 : juce::MidiMessage::noteOff (1, out[i].note),
                       out[i].sample);

    lastStep.store (player.stepNow (*snapshot));
}

// ------------------------------------------------------------------ the line in the project
juce::String MjqProcessor::toXml (const mjq::Line& L, mjq::ClockMode clock)
{
    juce::XmlElement root ("MIDIJAQ");
    root.setAttribute ("version", 1);
    root.setAttribute ("seed", (int) L.seed);
    root.setAttribute ("steps", L.numSteps);
    root.setAttribute ("offset", L.offset);
    root.setAttribute ("direction", (int) L.direction);
    root.setAttribute ("mode", (int) L.mode);
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

bool MjqProcessor::fromXml (const juce::String& xml, mjq::Line& L, mjq::ClockMode& clock)
{
    auto root = juce::parseXML (xml);
    if (root == nullptr || ! root->hasTagName ("MIDIJAQ")) return false;

    L = mjq::Line();
    L.seed = (uint32_t) root->getIntAttribute ("seed", 1);
    L.numSteps = juce::jlimit (1, mjq::kMaxSteps, root->getIntAttribute ("steps", 16));
    L.offset = root->getIntAttribute ("offset", 0);
    L.direction = (mjq::Direction) juce::jlimit (0, 2, root->getIntAttribute ("direction", 0));
    L.mode = (mjq::Mode) juce::jlimit (0, 2, root->getIntAttribute ("mode", 0));
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

void MjqProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto xml = toXml (line, clockMode);
    dest.replaceAll (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
}

void MjqProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    mjq::Line L;
    auto clock = clockMode;
    if (! fromXml (juce::String::fromUTF8 ((const char*) data, size), L, clock)) return;
    line = L;
    clockMode = clock;
    lineChanged();
}

juce::AudioProcessorEditor* MjqProcessor::createEditor() { return new MjqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MjqProcessor(); }
