// Shared by NeoBeat and Batchcut — the .NJQ file: a kit and its sequence in one file.
//
// A zip container (so it stays one file you can move, mail or back up):
//   manifest.xml       name, tempo, swing, the 16 lanes (what each plays and how it is set), scenes A-D, the chain
//   samples/laneNN.flac   each lane's own sound, lossless (a lane that plays one of NeoBeat's stock sounds has none)
// Batchcut's Kit room writes them; NeoBeat reads and writes them.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "PatternXml.h"

namespace njq
{
struct Lane
{
    juce::String name;
    int role = 0;
    juce::String stock;            // the id of a NeoBeat stock sound (empty: the lane has its own sample, or nothing)
    bool hasSample = false;
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    float levelDb = 0.0f, tune = 0.0f, decay = 1.0f, pan = 0.0f, width = 1.0f, tone = 0.0f, drive = 0.0f, hollow = 0.0f, evolve = 0.5f;
    int choke = 0;
    bool set = false;              // false: leave the lane as it is when loading
};

struct Kit
{
    juce::String name = "Untitled", author, madeWith;
    double bpm = 120.0;
    float swing = 0.62f;
    std::array<Lane, 16> lanes;
    std::vector<nb::Pattern> scenes = std::vector<nb::Pattern> ((size_t) nb::kNumScenes);
    nb::SongSettings song;
    int patternIndex = -1;         // which of the 128 patterns it was auditioned with (-1: its own)
};

inline bool save (const juce::File& file, const Kit& kit, juce::String& error)
{
    juce::XmlElement root ("NJQ");
    root.setAttribute ("version", 1);
    root.setAttribute ("name", kit.name); root.setAttribute ("author", kit.author); root.setAttribute ("madeWith", kit.madeWith);
    root.setAttribute ("bpm", kit.bpm); root.setAttribute ("swing", (double) kit.swing); root.setAttribute ("patternIndex", kit.patternIndex);
    juce::ZipFile::Builder zip;
    juce::OwnedArray<juce::MemoryBlock> blocks;
    for (int i = 0; i < 16; ++i)
    {
        const Lane& ln = kit.lanes[(size_t) i];
        if (! ln.set) continue;
        auto* e = root.createNewChildElement ("LANE");
        e->setAttribute ("index", i); e->setAttribute ("name", ln.name); e->setAttribute ("role", ln.role);
        e->setAttribute ("level", (double) ln.levelDb); e->setAttribute ("tune", (double) ln.tune); e->setAttribute ("decay", (double) ln.decay);
        e->setAttribute ("pan", (double) ln.pan); e->setAttribute ("width", (double) ln.width); e->setAttribute ("tone", (double) ln.tone);
        e->setAttribute ("drive", (double) ln.drive); e->setAttribute ("hollow", (double) ln.hollow); e->setAttribute ("evolve", (double) ln.evolve);
        e->setAttribute ("choke", ln.choke);
        if (ln.stock.isNotEmpty()) e->setAttribute ("stock", ln.stock);
        if (ln.hasSample && ! ln.l.empty())
        {
            const juce::String path = "samples/lane" + juce::String (i + 1).paddedLeft ('0', 2) + ".flac";
            auto* mb = blocks.add (new juce::MemoryBlock());
            {
                juce::FlacAudioFormat flac;
                auto* os = new juce::MemoryOutputStream (*mb, false);
                std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, ln.sampleRate, 2, 24, {}, 5));
                if (w == nullptr) { delete os; error = "Could not write the sound of lane " + juce::String (i + 1); return false; }
                const int n = (int) ln.l.size();
                juce::AudioBuffer<float> buf (2, n);
                buf.copyFrom (0, 0, ln.l.data(), n);
                buf.copyFrom (1, 0, ln.r.size() == ln.l.size() ? ln.r.data() : ln.l.data(), n);
                // FLAC holds -1..1: a hotter sound is turned down, and its lane turned up by as much
                const float pk = buf.getMagnitude (0, n);
                if (pk > 0.999f) { buf.applyGain (0.999f / pk); e->setAttribute ("level", (double) ln.levelDb + 20.0 * std::log10 (pk / 0.999f)); }
                w->writeFromAudioSampleBuffer (buf, 0, n);
            }
            e->setAttribute ("sample", path);
            zip.addEntry (new juce::MemoryInputStream (*mb, false), 0, path, juce::Time::getCurrentTime());   // FLAC is already compressed
        }
    }
    for (int s = 0; s < nb::kNumScenes && s < (int) kit.scenes.size(); ++s)
    {
        auto* e = root.createNewChildElement ("SCENE"); e->setAttribute ("index", s);
        io::writePattern (*e, kit.scenes[(size_t) s]);
    }
    io::writeSong (*root.createNewChildElement ("SONG"), kit.song);
    const juce::String xml = root.toString();
    zip.addEntry (new juce::MemoryInputStream (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true), 9, "manifest.xml", juce::Time::getCurrentTime());
    // write beside the target, then move it into place: a failed save never leaves half a file
    const juce::File tmp = file.getSiblingFile (file.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr)) { tmp.deleteFile(); error = "Could not write " + file.getFullPathName(); return false; }
        out.flush();
    }
    if (! tmp.moveFileTo (file)) { tmp.deleteFile(); error = "Could not write " + file.getFullPathName(); return false; }
    return true;
}

// samples = false: the manifest only (the browser lists kits without reading their sound)
inline bool load (const juce::File& file, Kit& kit, juce::String& error, bool samples = true)
{
    kit = Kit();
    if (! file.existsAsFile()) { error = "The file is not there: " + file.getFullPathName(); return false; }
    juce::ZipFile zip (file);
    const auto* me = zip.getEntry ("manifest.xml");
    if (me == nullptr) { error = "Not a .NJQ file: " + file.getFileName(); return false; }
    std::unique_ptr<juce::InputStream> ms (zip.createStreamForEntry (*me));
    if (ms == nullptr) { error = "Could not read " + file.getFileName(); return false; }
    auto xml = juce::XmlDocument::parse (ms->readEntireStreamAsString());
    if (xml == nullptr || ! xml->hasTagName ("NJQ")) { error = "Not a .NJQ file: " + file.getFileName(); return false; }
    kit.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    kit.author = xml->getStringAttribute ("author"); kit.madeWith = xml->getStringAttribute ("madeWith");
    kit.bpm = juce::jlimit (30.0, 300.0, xml->getDoubleAttribute ("bpm", 120.0));
    kit.swing = (float) juce::jlimit (0.5, 0.75, xml->getDoubleAttribute ("swing", 0.62));
    kit.patternIndex = xml->getIntAttribute ("patternIndex", -1);
    juce::FlacAudioFormat flac;
    for (auto* e : xml->getChildWithTagNameIterator ("LANE"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (i < 0 || i >= 16) continue;
        Lane& ln = kit.lanes[(size_t) i];
        ln.set = true;
        ln.name = e->getStringAttribute ("name"); ln.role = juce::jlimit (0, 15, e->getIntAttribute ("role", i)); ln.stock = e->getStringAttribute ("stock");
        ln.levelDb = (float) juce::jlimit (-30.0, 24.0, e->getDoubleAttribute ("level")); ln.tune = (float) juce::jlimit (-24.0, 24.0, e->getDoubleAttribute ("tune"));
        ln.decay = (float) juce::jlimit (0.25, 4.0, e->getDoubleAttribute ("decay", 1.0)); ln.pan = (float) juce::jlimit (-1.0, 1.0, e->getDoubleAttribute ("pan"));
        ln.width = (float) juce::jlimit (0.0, 2.0, e->getDoubleAttribute ("width", 1.0)); ln.tone = (float) juce::jlimit (-1.0, 1.0, e->getDoubleAttribute ("tone"));
        ln.drive = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("drive")); ln.hollow = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("hollow"));
        ln.evolve = (float) juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("evolve", 0.5)); ln.choke = juce::jlimit (0, 4, e->getIntAttribute ("choke"));
        const auto path = e->getStringAttribute ("sample");
        if (path.isEmpty()) continue;
        ln.hasSample = true;
        if (! samples) continue;
        const auto* se = zip.getEntry (path);
        if (se == nullptr) { ln.hasSample = false; continue; }
        // into memory first: the decoder wants to seek
        juce::MemoryBlock mb;
        { std::unique_ptr<juce::InputStream> ss (zip.createStreamForEntry (*se)); if (ss == nullptr) { ln.hasSample = false; continue; } ss->readIntoMemoryBlock (mb); }
        std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
        if (r == nullptr || r->lengthInSamples <= 0) { ln.hasSample = false; continue; }
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 30.0));
        juce::AudioBuffer<float> buf ((int) std::max (1u, r->numChannels), n);
        r->read (&buf, 0, n, 0, true, true);
        ln.sampleRate = r->sampleRate;
        ln.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
        const float* rp = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : buf.getReadPointer (0);
        ln.r.assign (rp, rp + n);
        for (auto* ch : { &ln.l, &ln.r }) for (auto& x : *ch) if (! std::isfinite (x)) x = 0.0f;
    }
    for (auto* e : xml->getChildWithTagNameIterator ("SCENE"))
    {
        const int s = e->getIntAttribute ("index", -1);
        if (s >= 0 && s < nb::kNumScenes) io::readPattern (*e, kit.scenes[(size_t) s]);
    }
    if (auto* sg = xml->getChildByName ("SONG")) io::readSong (*sg, kit.song);
    return true;
}
} // namespace njq

// ------------------------------------------------------------------ what the two apps share on this computer
namespace shared
{
inline juce::File dir()
{
    // (MARBELITE_SHARED_DIR: the tests keep away from the real one)
    const auto env = juce::SystemStats::getEnvironmentVariable ("MARBELITE_SHARED_DIR", {});
    auto d = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Marbelite").getChildFile ("Shared");
    d.createDirectory();
    return d;
}
inline juce::File learnerFile() { return dir().getChildFile ("learner.txt"); }

struct Settings
{
    juce::String presetFolder;      // where .NJQ files live: both apps save there and NeoBeat lists what is in it
    juce::String startupNjq;        // the sequence a new NeoBeat opens with (empty: the stock one)
    bool askDefault = true;         // ask "make this a default sequence?" after saving or loading one
    juce::String exportFolder, importFolder;

    static juce::File file() { return dir().getChildFile ("settings.xml"); }
    static Settings load()
    {
        Settings s;
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            s.presetFolder = xml->getStringAttribute ("presetFolder"); s.startupNjq = xml->getStringAttribute ("startupNjq");
            s.askDefault = xml->getIntAttribute ("askDefault", 1) != 0;
            s.exportFolder = xml->getStringAttribute ("exportFolder"); s.importFolder = xml->getStringAttribute ("importFolder");
        }
        return s;
    }
    void save() const
    {
        juce::XmlElement xml ("MARBELITE_SHARED");
        xml.setAttribute ("presetFolder", presetFolder); xml.setAttribute ("startupNjq", startupNjq); xml.setAttribute ("askDefault", askDefault ? 1 : 0);
        xml.setAttribute ("exportFolder", exportFolder); xml.setAttribute ("importFolder", importFolder);
        xml.writeTo (file());
    }
    // the preset folder (until one is chosen: Documents/Marbelite/NJQ)
    juce::File presets() const
    {
        juce::File f = presetFolder.isNotEmpty() ? juce::File (presetFolder) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Marbelite").getChildFile ("NJQ");
        f.createDirectory();
        return f;
    }
};
} // namespace shared
