// Shared by Batchcut (KEYS writes them) and MidiJaQ (plays them) - a .NJQ instrument: a version 2 .NJQ file
// of kind "instrument". Its manifest holds the INSTRUMENT part (how it plays) with one ZONE per sound (which
// keys and velocities it covers, its pitch, its loop, its hold point); each zone's sound is a lossless FLAC
// entry of the zip (instrument/zoneNNN.flac).
//
// Instruments live in their own folder (Documents/Marbelite/Instruments), not among NeoBeat's sequences:
// NeoBeat lists every .NJQ in its preset folder, and an instrument is not a kit.
#pragma once

#include "Njq.h"

namespace njq
{
struct Zone
{
    juce::String name, source;
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    float root = 60.0f, gainDb = 0.0f;
    int lowKey = 0, highKey = 127, lowVel = 1, highVel = 127, rr = 0, rrCount = 1;
    int loopStart = -1, loopEnd = -1, holdAt = 0;
    bool tuned = false;                 // the sound has a pitch of its own (so Hold can stay in tune)
};

struct InstrumentFile
{
    juce::String name = "Untitled", author, madeWith;
    int mode = 0;               // 0 tuned, 1 resonator, 2 chops
    int sustain = 0;            // 0 as recorded, 1 loop, 2 hold
    float attackMs = 2.0f, releaseMs = 300.0f, glideMs = 0.0f, resonance = 0.7f, brightness = 0.5f;
    bool mono = false;
    std::vector<Zone> zones;
};

namespace detail
{
inline std::shared_ptr<juce::MemoryBlock> flac (const std::vector<float>& l, const std::vector<float>& r, double sr, float& gain)
{
    gain = 1.0f;
    auto mb = std::make_shared<juce::MemoryBlock>();
    juce::FlacAudioFormat fmt;
    auto* os = new juce::MemoryOutputStream (*mb, false);
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os, sr, 2, 24, {}, 5));
    if (w == nullptr) { delete os; return nullptr; }
    const int n = (int) l.size();
    juce::AudioBuffer<float> buf (2, n);
    buf.copyFrom (0, 0, l.data(), n); buf.copyFrom (1, 0, r.size() == l.size() ? r.data() : l.data(), n);
    const float pk = buf.getMagnitude (0, n);
    if (pk > 0.999f) { buf.applyGain (0.999f / pk); gain = pk / 0.999f; }
    w->writeFromAudioSampleBuffer (buf, 0, n);
    w.reset();
    return mb;
}
inline bool unflac (const juce::MemoryBlock& mb, std::vector<float>& l, std::vector<float>& r, double& sr, float gain)
{
    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> rd (fmt.createReaderFor (new juce::MemoryInputStream (mb, false), true));
    if (rd == nullptr || rd->lengthInSamples < 2) return false;
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 60.0));
    juce::AudioBuffer<float> buf (2, n);
    rd->read (&buf, 0, n, 0, true, true);
    buf.applyGain (gain);
    sr = rd->sampleRate;
    l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n); r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + n);
    for (auto* ch : { &l, &r }) for (auto& x : *ch) if (! std::isfinite (x)) x = 0.0f;
    return true;
}
}

inline bool saveInstrument (const juce::File& file, const InstrumentFile& ins, juce::String& error)
{
    if (ins.zones.empty()) { error = "The instrument has no sounds."; return false; }
    Kit k;
    k.name = ins.name; k.author = ins.author; k.madeWith = ins.madeWith; k.kind = "instrument";
    auto e = std::make_shared<juce::XmlElement> ("INSTRUMENT");
    e->setAttribute ("mode", ins.mode); e->setAttribute ("sustain", ins.sustain); e->setAttribute ("mono", ins.mono ? 1 : 0);
    e->setAttribute ("attack", (double) ins.attackMs); e->setAttribute ("release", (double) ins.releaseMs); e->setAttribute ("glide", (double) ins.glideMs);
    e->setAttribute ("resonance", (double) ins.resonance); e->setAttribute ("brightness", (double) ins.brightness);
    for (int i = 0; i < (int) ins.zones.size(); ++i)
    {
        const auto& z = ins.zones[(size_t) i];
        if (z.l.size() < 2) continue;
        float gain = 1.0f;
        auto mb = detail::flac (z.l, z.r, z.sampleRate, gain);
        if (mb == nullptr) { error = "Could not write the sound of " + z.name; return false; }
        const juce::String path = "instrument/zone" + juce::String (i + 1).paddedLeft ('0', 3) + ".flac";
        k.files.push_back ({ path, mb });
        auto* ze = e->createNewChildElement ("ZONE");
        ze->setAttribute ("index", i); ze->setAttribute ("sample", path); ze->setAttribute ("name", z.name); ze->setAttribute ("source", z.source);
        ze->setAttribute ("gainFile", (double) gain);
        ze->setAttribute ("root", (double) z.root); ze->setAttribute ("gain", (double) z.gainDb);
        ze->setAttribute ("lowKey", z.lowKey); ze->setAttribute ("highKey", z.highKey); ze->setAttribute ("lowVel", z.lowVel); ze->setAttribute ("highVel", z.highVel);
        ze->setAttribute ("rr", z.rr); ze->setAttribute ("rrCount", z.rrCount);
        ze->setAttribute ("loopStart", z.loopStart); ze->setAttribute ("loopEnd", z.loopEnd); ze->setAttribute ("holdAt", z.holdAt);
        ze->setAttribute ("tuned", z.tuned ? 1 : 0);
    }
    k.extras.push_back (e);
    // (no scenes: an instrument plays no pattern of its own)
    for (auto& sc : k.scenes) sc.length = 16;
    return save (file, k, error);
}

inline bool loadInstrument (const juce::File& file, InstrumentFile& ins, juce::String& error)
{
    ins = InstrumentFile();
    Kit k;
    if (! load (file, k, error)) return false;
    const auto* e = k.extra ("INSTRUMENT");
    if (k.kind != "instrument" || e == nullptr) { error = file.getFileName() + " is a NeoBeat sequence, not an instrument."; return false; }
    ins.name = k.name; ins.author = k.author; ins.madeWith = k.madeWith;
    ins.mode = juce::jlimit (0, 2, e->getIntAttribute ("mode")); ins.sustain = juce::jlimit (0, 2, e->getIntAttribute ("sustain")); ins.mono = e->getIntAttribute ("mono") != 0;
    ins.attackMs = (float) juce::jlimit (0.0, 5000.0, e->getDoubleAttribute ("attack", 2.0)); ins.releaseMs = (float) juce::jlimit (1.0, 20000.0, e->getDoubleAttribute ("release", 300.0));
    ins.glideMs = (float) juce::jlimit (0.0, 5000.0, e->getDoubleAttribute ("glide")); ins.resonance = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("resonance", 0.7));
    ins.brightness = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("brightness", 0.5));
    for (auto* ze : e->getChildWithTagNameIterator ("ZONE"))
    {
        Zone z;
        const auto path = ze->getStringAttribute ("sample");
        const juce::MemoryBlock* mb = nullptr;
        for (const auto& f : k.files) if (f.first == path && f.second != nullptr) mb = f.second.get();
        if (mb == nullptr || ! detail::unflac (*mb, z.l, z.r, z.sampleRate, (float) ze->getDoubleAttribute ("gainFile", 1.0))) continue;
        z.name = ze->getStringAttribute ("name"); z.source = ze->getStringAttribute ("source");
        z.root = (float) juce::jlimit (-24.0, 151.0, ze->getDoubleAttribute ("root", 60.0)); z.gainDb = (float) juce::jlimit (-60.0, 40.0, ze->getDoubleAttribute ("gain"));
        z.lowKey = juce::jlimit (0, 127, ze->getIntAttribute ("lowKey")); z.highKey = juce::jlimit (z.lowKey, 127, ze->getIntAttribute ("highKey", 127));
        z.lowVel = juce::jlimit (1, 127, ze->getIntAttribute ("lowVel", 1)); z.highVel = juce::jlimit (z.lowVel, 127, ze->getIntAttribute ("highVel", 127));
        z.rrCount = juce::jlimit (1, 64, ze->getIntAttribute ("rrCount", 1)); z.rr = juce::jlimit (0, z.rrCount - 1, ze->getIntAttribute ("rr"));
        const int n = (int) z.l.size();
        z.loopStart = juce::jlimit (-1, n - 1, ze->getIntAttribute ("loopStart", -1)); z.loopEnd = juce::jlimit (-1, n - 1, ze->getIntAttribute ("loopEnd", -1));
        if (z.loopEnd <= z.loopStart) z.loopStart = z.loopEnd = -1;
        z.holdAt = juce::jlimit (0, std::max (0, n - 1), ze->getIntAttribute ("holdAt"));
        z.tuned = ze->getIntAttribute ("tuned", ins.mode == 0 ? 1 : 0) != 0;      // older files: a Tuned instrument's sounds are pitched
        ins.zones.push_back (std::move (z));
    }
    if (ins.zones.empty()) { error = "No sound in " + file.getFileName() + " could be read."; return false; }
    return true;
}
} // namespace njq

namespace shared
{
// where .NJQ instruments go: beside NeoBeat's preset folder, in a folder of their own
inline juce::File instrumentsFolder()
{
    auto f = Settings::load().presets().getParentDirectory().getChildFile ("Instruments");
    f.createDirectory();
    return f;
}
}
