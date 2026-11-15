// Batchcut 3.0 - KEYS on the processor's side: the instrument built from the pool's sounds, undo, the player,
// chords on the keys, the .NJQ instrument for MidiJaQ (saved and opened again), and KEYS in the project.
#include "Processor.h"
#include "../neobeat/shared/NjqInstrument.h"
#include "../neobeat/gen/Scale.h"
#include <set>
#ifndef BATCHCUT_VERSION
 #define BATCHCUT_VERSION "3.0.0"
#endif

namespace
{
struct Chord { const char* name; std::vector<int> steps; };
const std::vector<Chord>& chords()
{
    // -1 in steps: "in key" (three notes of the key, a third apart)
    static const std::vector<Chord> c {
        { "Single notes", { 0 } }, { "Major", { 0, 4, 7 } }, { "Minor", { 0, 3, 7 } }, { "7", { 0, 4, 7, 10 } }, { "Minor 7", { 0, 3, 7, 10 } },
        { "Major 7", { 0, 4, 7, 11 } }, { "Minor 9", { 0, 3, 7, 10, 14 } }, { "Sus 4", { 0, 5, 7 } }, { "Power", { 0, 7, 12 } }, { "In key", { -1 } },
    };
    return c;
}
}

int BcProcessor::numChords() { return (int) chords().size(); }
const char* BcProcessor::chordName (int i) { return chords()[(size_t) juce::jlimit (0, numChords() - 1, i)].name; }

std::vector<std::shared_ptr<const bc::PoolItem>> BcProcessor::keysSounds() const
{
    std::vector<std::shared_ptr<const bc::PoolItem>> out;
    for (const auto& it : kit.pool) if (it && keysSource.takes (*it) && keysLeftOut.count (it) == 0) out.push_back (it);
    return out;
}

void BcProcessor::keysBuild()
{
    if (kit.pool.empty()) { keysMessage = "The pool is empty: THIS FILE, ALL FILES or FOLDER brings sounds in"; ++keysVersion; return; }
    const auto sounds = keysSounds();
    bc::buildInstrument (keys, sounds);
    if (keys.zones.empty())
    {
        keysMessage = keys.mode == bc::KeysMode::Tuned ? "No sound from " + juce::String (keysSource.label()) + " has a clear pitch: try RESONATOR or CHOPS"
                                                       : "No sound from " + juce::String (keysSource.label());
    }
    else if (keys.mode == bc::KeysMode::Tuned)
    {
        float lo = 999.0f, hi = -999.0f; int layers = 0, robins = 0; std::set<int> roots;
        for (const auto& z : keys.zones) { lo = std::min (lo, z.root); hi = std::max (hi, z.root); roots.insert ((int) std::lround (z.root * 4.0f)); layers = std::max (layers, z.lowVel > 1 ? 2 : 1); robins = std::max (robins, z.rrCount); }
        keysMessage = juce::String ((int) keys.zones.size()) + " sounds from " + juce::String (keysSource.label()) + " tuned across the keys: " + gen::noteName ((int) std::lround (lo)) + " to " + gen::noteName ((int) std::lround (hi))
                    + (layers > 1 ? ", velocity layers" : "") + (robins > 1 ? ", round robins" : "");
    }
    else keysMessage = juce::String ((int) keys.zones.size()) + " sounds from " + juce::String (keysSource.label()) + (keys.mode == bc::KeysMode::Chops ? ", one a key from C2 up" : ", struck into a resonator tuned to each key");
    if (! keysLeftOut.empty() && ! keys.zones.empty()) keysMessage << " (" << (int) keysLeftOut.size() << " left out)";
    keysChanged(); keysMark();
}

void BcProcessor::keysChanged()
{
    auto ins = std::make_shared<const bc::Instrument> (keys);
    keysPlayer.setInstrument (ins);
    keysKeep.push_back ({ juce::Time::getMillisecondCounter(), std::move (ins) });
    ++keysVersion; stateDirty = true;
}

void BcProcessor::keysMark()
{
    if (keys == keysSaved) return;
    keysUndoStack.push_back (keysSaved); keysRedoStack.clear(); keysSaved = keys;
    if (keysUndoStack.size() > 60) keysUndoStack.erase (keysUndoStack.begin());
}
void BcProcessor::keysUndo()
{
    keysMark();
    if (keysUndoStack.empty()) return;
    keysRedoStack.push_back (keys); keys = keysSaved = keysUndoStack.back(); keysUndoStack.pop_back();
    keysChanged();
}
void BcProcessor::keysRedo()
{
    if (keysRedoStack.empty()) return;
    keysUndoStack.push_back (keys); keys = keysSaved = keysRedoStack.back(); keysRedoStack.pop_back();
    keysChanged();
}

// ---------------------------------------------------------------- the .NJQ instrument
bool BcProcessor::keysSaveNjq (const juce::File& file, juce::String& error)
{
    if (keys.zones.empty()) { error = "The instrument has no sounds yet: BUILD it first."; return false; }
    njq::InstrumentFile f;
    f.name = juce::String (keys.name); f.madeWith = "Batchcut " BATCHCUT_VERSION;
    f.mode = (int) keys.mode; f.sustain = (int) keys.sustain; f.mono = keys.mono;
    f.attackMs = keys.attackMs; f.releaseMs = keys.releaseMs; f.glideMs = keys.glideMs; f.resonance = keys.resonance; f.brightness = keys.brightness;
    for (const auto& z : keys.zones)
    {
        if (! z.sound) continue;
        njq::Zone o;
        o.name = juce::String (z.sound->name); o.source = juce::String (z.sound->source);
        o.l = z.sound->l; o.r = z.sound->r; o.sampleRate = z.sound->sr;
        o.root = z.root; o.gainDb = z.gainDb; o.lowKey = z.lowKey; o.highKey = z.highKey; o.lowVel = z.lowVel; o.highVel = z.highVel;
        o.rr = z.rr; o.rrCount = z.rrCount; o.loopStart = z.loopStart; o.loopEnd = z.loopEnd; o.holdAt = z.holdAt;
        f.zones.push_back (std::move (o));
    }
    if (! njq::saveInstrument (file, f, error)) return false;
    keysMessage = "Saved " + file.getFileName() + ": " + juce::String ((int) f.zones.size()) + (f.zones.size() == 1 ? " sound" : " sounds") + " for MidiJaQ, in " + file.getParentDirectory().getFileName();
    return true;
}

bool BcProcessor::keysOpenNjq (const juce::File& file, juce::String& error)
{
    njq::InstrumentFile f;
    if (! njq::loadInstrument (file, f, error)) return false;
    bc::Instrument ins;
    ins.name = f.name.toStdString(); ins.mode = (bc::KeysMode) f.mode; ins.sustain = (bc::KeysSustain) f.sustain; ins.mono = f.mono;
    ins.attackMs = f.attackMs; ins.releaseMs = f.releaseMs; ins.glideMs = f.glideMs; ins.resonance = f.resonance; ins.brightness = f.brightness;
    for (auto& z : f.zones)
    {
        auto it = std::make_shared<bc::PoolItem>();
        it->name = z.name.toStdString(); it->source = z.source.toStdString(); it->l = std::move (z.l); it->r = std::move (z.r); it->sr = z.sampleRate;
        bc::analyseItem (*it, nbkstore::learner());
        bc::KeyZone k; k.sound = it; k.root = z.root; k.gainDb = z.gainDb; k.lowKey = z.lowKey; k.highKey = z.highKey; k.lowVel = z.lowVel; k.highVel = z.highVel;
        k.rr = z.rr; k.rrCount = z.rrCount; k.loopStart = z.loopStart; k.loopEnd = z.loopEnd; k.holdAt = z.holdAt;
        ins.zones.push_back (k);
    }
    keys = ins;
    keysMessage = "Opened " + file.getFileName() + ": " + juce::String ((int) keys.zones.size()) + " sounds";
    keysChanged(); keysMark();
    return true;
}

// ---------------------------------------------------------------- KEYS in the project
std::unique_ptr<juce::XmlElement> BcProcessor::keysToXml()
{
    auto root = std::make_unique<juce::XmlElement> ("KEYS");
    root->setAttribute ("name", juce::String (keys.name)); root->setAttribute ("mode", (int) keys.mode); root->setAttribute ("sustain", (int) keys.sustain);
    root->setAttribute ("attack", (double) keys.attackMs); root->setAttribute ("release", (double) keys.releaseMs); root->setAttribute ("glide", (double) keys.glideMs);
    root->setAttribute ("mono", keys.mono ? 1 : 0); root->setAttribute ("resonance", (double) keys.resonance); root->setAttribute ("brightness", (double) keys.brightness);
    root->setAttribute ("sourceKind", (int) keysSource.kind); root->setAttribute ("sourceFrom", juce::String (keysSource.from)); root->setAttribute ("sourceFamily", keysSource.family);
    root->setAttribute ("chord", keysChord.load()); root->setAttribute ("midiTo", midiTarget.load());
    std::vector<const bc::PoolItem*> used;
    auto ref = [&] (const bc::PoolItem* s) { for (int i = 0; i < (int) used.size(); ++i) if (used[(size_t) i] == s) return i; used.push_back (s); return (int) used.size() - 1; };
    for (const auto& z : keys.zones)
    {
        if (! z.sound) continue;
        auto* ze = root->createNewChildElement ("ZONE");
        ze->setAttribute ("sound", ref (z.sound.get())); ze->setAttribute ("root", (double) z.root); ze->setAttribute ("gain", (double) z.gainDb);
        ze->setAttribute ("lowKey", z.lowKey); ze->setAttribute ("highKey", z.highKey); ze->setAttribute ("lowVel", z.lowVel); ze->setAttribute ("highVel", z.highVel);
        ze->setAttribute ("rr", z.rr); ze->setAttribute ("rrCount", z.rrCount); ze->setAttribute ("loopStart", z.loopStart); ze->setAttribute ("loopEnd", z.loopEnd); ze->setAttribute ("holdAt", z.holdAt);
    }
    for (const auto* s : used) root->addChildElement (bcxml::sound (*s).release());
    return root;
}

void BcProcessor::keysFromXml (const juce::XmlElement& root)
{
    keys = bc::Instrument(); keysLeftOut.clear();
    keys.name = root.getStringAttribute ("name", "My keys").toStdString();
    keys.mode = (bc::KeysMode) juce::jlimit (0, 2, root.getIntAttribute ("mode")); keys.sustain = (bc::KeysSustain) juce::jlimit (0, 2, root.getIntAttribute ("sustain"));
    keys.attackMs = (float) juce::jlimit (0.0, 5000.0, root.getDoubleAttribute ("attack", 2.0)); keys.releaseMs = (float) juce::jlimit (1.0, 20000.0, root.getDoubleAttribute ("release", 300.0));
    keys.glideMs = (float) juce::jlimit (0.0, 5000.0, root.getDoubleAttribute ("glide")); keys.mono = root.getIntAttribute ("mono") != 0;
    keys.resonance = (float) juce::jlimit (0.0, 1.0, root.getDoubleAttribute ("resonance", 0.7)); keys.brightness = (float) juce::jlimit (0.0, 1.0, root.getDoubleAttribute ("brightness", 0.5));
    keysSource.kind = (bc::LoopSource::Kind) juce::jlimit (0, 2, root.getIntAttribute ("sourceKind")); keysSource.from = root.getStringAttribute ("sourceFrom").toStdString(); keysSource.family = juce::jlimit (0, 11, root.getIntAttribute ("sourceFamily"));
    keysChord.store (juce::jlimit (0, numChords() - 1, root.getIntAttribute ("chord"))); midiTarget.store (root.getIntAttribute ("midiTo", 2) == 3 ? 3 : 2);
    std::vector<std::shared_ptr<const bc::PoolItem>> sounds;
    for (auto* se : root.getChildWithTagNameIterator ("SOUND")) sounds.push_back (bcxml::sound (*se, 60.0));
    for (auto* ze : root.getChildWithTagNameIterator ("ZONE"))
    {
        const int si = ze->getIntAttribute ("sound", -1);
        if (si < 0 || si >= (int) sounds.size() || sounds[(size_t) si] == nullptr) continue;
        bc::KeyZone z; z.sound = sounds[(size_t) si];
        const int n = (int) z.sound->l.size();
        z.root = (float) juce::jlimit (-24.0, 151.0, ze->getDoubleAttribute ("root", 60.0)); z.gainDb = (float) juce::jlimit (-60.0, 40.0, ze->getDoubleAttribute ("gain"));
        z.lowKey = juce::jlimit (0, 127, ze->getIntAttribute ("lowKey")); z.highKey = juce::jlimit (z.lowKey, 127, ze->getIntAttribute ("highKey", 127));
        z.lowVel = juce::jlimit (1, 127, ze->getIntAttribute ("lowVel", 1)); z.highVel = juce::jlimit (z.lowVel, 127, ze->getIntAttribute ("highVel", 127));
        z.rrCount = juce::jlimit (1, 64, ze->getIntAttribute ("rrCount", 1)); z.rr = juce::jlimit (0, z.rrCount - 1, ze->getIntAttribute ("rr"));
        z.loopStart = juce::jlimit (-1, n - 1, ze->getIntAttribute ("loopStart", -1)); z.loopEnd = juce::jlimit (-1, n - 1, ze->getIntAttribute ("loopEnd", -1));
        if (z.loopEnd <= z.loopStart) z.loopStart = z.loopEnd = -1;
        z.holdAt = juce::jlimit (0, std::max (0, n - 1), ze->getIntAttribute ("holdAt"));
        keys.zones.push_back (z);
    }
    keysUndoStack.clear(); keysRedoStack.clear(); keysSaved = keys;
    keysChanged();
}

// ---------------------------------------------------------------- the keys played (audio thread)
// Each key pressed (or let go) becomes the chord's notes. "In key": three notes of LOOPS' key, a third apart.
int bcExpandChord (int chord, int root, int scaleIndex, const bc::KeysNote& in, bc::KeysNote* out, int room)
{
    const auto& c = chords()[(size_t) juce::jlimit (0, (int) chords().size() - 1, chord)];
    int n = 0;
    if (c.steps.size() == 1 && c.steps[0] == -1)
    {
        const auto& sc = gen::scale (scaleIndex);
        const int base = gen::quantize (in.note, root, sc);
        for (int k = 0; k < 3 && n < room; ++k) { auto e = in; e.note = juce::jlimit (0, 127, gen::degreeNote (2 * k, base, root, sc)); out[n++] = e; }
        return n;
    }
    for (int st : c.steps) { if (n >= room) break; auto e = in; e.note = juce::jlimit (0, 127, in.note + st); out[n++] = e; }
    return n;
}
