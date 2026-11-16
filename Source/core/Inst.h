#pragma once
// The instrument MidiJaQ plays: a .NJQ instrument made in Batchcut's KEYS room, read in and handed to the
// voices. The voices themselves are shared code (neobeat/shared/InstrumentPlay.h), so a sound plays here
// exactly as it does in Batchcut.
#include <juce_core/juce_core.h>
#include <memory>
#include <string>
#include <vector>

#include "../neobeat/gen/Scale.h"
#include "../neobeat/shared/InstrumentPlay.h"
#include "../neobeat/shared/NjqInstrument.h"

namespace mjq
{
// one sound of an instrument: what the voices need of it, and its name for the window
struct Sound
{
    std::vector<float> l, r;
    double sr = 48000.0;
    std::string name;
};

using Zone = njq::ZoneT<Sound>;
using Instrument = njq::InstrumentT<Sound>;
using InstrumentPlayer = njq::PlayerT<Sound>;
using PlayNote = njq::PlayNote;

// Reads a .NJQ instrument. A kit or a loop is refused by name, so the message says what the file is.
inline bool loadInstrument (const juce::File& file, Instrument& out, juce::String& error)
{
    njq::InstrumentFile f;
    if (! njq::loadInstrument (file, f, error)) return false;

    Instrument ins;
    ins.name = f.name.toStdString();
    ins.mode = (njq::PlayMode) juce::jlimit (0, 2, f.mode);
    ins.sustain = (njq::Sustain) juce::jlimit (0, 2, f.sustain);
    ins.attackMs = f.attackMs; ins.releaseMs = f.releaseMs; ins.glideMs = f.glideMs;
    ins.mono = f.mono; ins.resonance = f.resonance; ins.brightness = f.brightness;

    for (auto& z : f.zones)
    {
        auto s = std::make_shared<Sound>();
        s->l = std::move (z.l);
        s->r = std::move (z.r);
        s->sr = z.sampleRate > 0.0 ? z.sampleRate : 48000.0;
        s->name = z.name.toStdString();

        Zone zone;
        zone.sound = s;
        zone.root = z.root;
        zone.lowKey = z.lowKey; zone.highKey = z.highKey;
        zone.lowVel = z.lowVel; zone.highVel = z.highVel;
        zone.rr = z.rr; zone.rrCount = z.rrCount;
        zone.loopStart = z.loopStart; zone.loopEnd = z.loopEnd;
        zone.holdAt = z.holdAt;
        zone.gainDb = z.gainDb;
        zone.tunedSound = z.tuned;
        ins.zones.push_back (std::move (zone));
    }
    if (ins.zones.empty()) { error = "No sound in " + file.getFileName() + " could be read."; return false; }
    out = std::move (ins);
    return true;
}

// What to say about an instrument once it is in: its sounds, how it plays, how wide it reaches.
inline juce::String describe (const Instrument& ins)
{
    if (ins.zones.empty()) return "No instrument.";
    int lowKey = 127, highKey = 0, rr = 1, layers = 0;
    for (const auto& z : ins.zones)
    {
        lowKey = std::min (lowKey, z.lowKey); highKey = std::max (highKey, z.highKey);
        rr = std::max (rr, z.rrCount);
        layers += z.lowVel > 1 ? 1 : 0;
    }
    juce::String s;
    s << juce::String (ins.name) << ": " << (int) ins.zones.size() << " sounds, "
      << njq::playModeName (ins.mode) << ", " << njq::sustainName (ins.sustain);
    if (rr > 1) s << ", " << rr << " round robins";
    if (layers > 0) s << ", velocity layers";
    s << ", " << gen::noteName (lowKey) << " to " << gen::noteName (highKey);
    return s;
}
} // namespace mjq
