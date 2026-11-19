// Shared by NeoBeat and Batchcut — the .NJQ file: a kit and its sequence in one file.
//
// A zip container (so it stays one file you can move, mail or back up):
//   manifest.xml       name, tempo, swing, the 16 lanes (what each plays and how it is set), scenes A-D, the chain
//   samples/laneNN.flac   each lane's own sound, lossless (a lane that plays one of NeoBeat's stock sounds has none)
// Batchcut's Kit room writes them; NeoBeat reads and writes them.
//
// Version 2 (Batchcut 3.0) adds, without taking anything away (NeoBeat 3.0 opens a version 2 kit as it is):
//   kind               "kit" (the default), "loop" (a LOOPS loop: its chops as lanes, the loop as scene A, and
//                      a LOOP part describing the layers), "instrument" (KEYS: a playable instrument for
//                      MidiJaQ; no lanes), and later "pattern" and "rack"
//   extra parts        any other element in the manifest is kept as it is and handed back on loading
//   files              extra entries in the zip (an instrument's zones) travel with the file
//   category           free text ("Texture", "Clubby", ...), written as it is given, so a preset recalls under
//                      the category it was saved with. Empty means uncategorised. A reader that does not know
//                      the attribute ignores it, so a categorised file still opens in Batchcut 2.x.
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
    juce::String kind = "kit";                                       // "kit", "loop", "instrument" ... (version 2)
    juce::String category;                                           // free text, empty: uncategorised (version 2)
    std::vector<std::shared_ptr<juce::XmlElement>> extras;          // other parts of the manifest (version 2)
    std::vector<std::pair<juce::String, std::shared_ptr<juce::MemoryBlock>>> files;   // other entries of the zip (version 2)
    const juce::XmlElement* extra (const juce::String& tag) const { for (const auto& e : extras) if (e != nullptr && e->hasTagName (tag)) return e.get(); return nullptr; }
    double bpm = 120.0;
    float swing = 0.62f;
    std::array<Lane, 16> lanes;
    std::vector<nb::Pattern> scenes = std::vector<nb::Pattern> ((size_t) nb::kNumScenes);
    nb::SongSettings song;
    int patternIndex = -1;         // which of the 128 patterns it was auditioned with (-1: its own)
};

// The categories a picker offers. Free text is allowed everywhere: whatever is typed is written as it is given,
// and a browser groups by what it finds, so this is a starting point rather than a closed set.
inline juce::StringArray suggestedCategories()
{
    return { "Texture", "Clubby", "Experimental", "Acoustic", "Metallic", "Liquid", "Rubber",
             "Percussive", "Tonal", "Bass", "Keys", "Strings", "Found sound", "Broken" };
}

// Tidy a typed category: control characters become spaces, runs of space collapse, the ends are trimmed. The
// capitalisation is left as typed, because how someone writes their own category is theirs to keep.
inline juce::String tidyCategory (const juce::String& s)
{
    juce::String out;
    for (const auto c : s) out += (juce::juce_wchar) (c < ' ' ? ' ' : c);
    while (out.contains ("  ")) out = out.replace ("  ", " ");
    return out.trim();
}

// What a browser groups on, so "clubby" and "Clubby" fall in one group and the first spelling seen is shown.
inline juce::String categoryKey (const juce::String& s) { return tidyCategory (s).toLowerCase(); }

inline bool save (const juce::File& file, const Kit& kit, juce::String& error)
{
    juce::XmlElement root ("NJQ");
    root.setAttribute ("version", 2);
    if (kit.kind.isNotEmpty() && kit.kind != "kit") root.setAttribute ("kind", kit.kind);
    if (const auto cat = tidyCategory (kit.category); cat.isNotEmpty()) root.setAttribute ("category", cat);
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
    for (const auto& e : kit.extras) if (e != nullptr) root.addChildElement (new juce::XmlElement (*e));
    for (const auto& f : kit.files) if (f.second != nullptr) zip.addEntry (new juce::MemoryInputStream (*f.second, false), 0, f.first, juce::Time::getCurrentTime());
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
    kit.kind = xml->getStringAttribute ("kind", "kit");
    kit.category = tidyCategory (xml->getStringAttribute ("category"));
    for (auto* e : xml->getChildIterator())
        if (! e->hasTagName ("LANE") && ! e->hasTagName ("SCENE") && ! e->hasTagName ("SONG")) kit.extras.push_back (std::make_shared<juce::XmlElement> (*e));
    if (samples)
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* ze = zip.getEntry (i);
            if (ze == nullptr || ze->filename == "manifest.xml" || ze->filename.startsWith ("samples/")) continue;
            auto mb = std::make_shared<juce::MemoryBlock>();
            std::unique_ptr<juce::InputStream> zs (zip.createStreamForEntry (i));
            if (zs != nullptr) { zs->readIntoMemoryBlock (*mb); kit.files.push_back ({ ze->filename, mb }); }
        }
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

// The four assigned folders: one per kind of file the plugins write. Each is chosen once, in Settings, and is
// remembered from then on. Saving a single file somewhere else is not an assignment — a save dialog opens in the
// assigned folder, and wherever it ends up going, the folder itself does not move. Only assign() changes one.
// Importing is deliberately not in here: it stays loose, and remembers only the last place it was pointed at.
enum class Folder { Wav, Midi, Njq, Njqi };

struct Settings
{
    juce::String presetFolder;      // .NJQ: both apps save there and NeoBeat lists what is in it
    juce::String instrumentFolder;  // .NJQi: the instruments MidiJaQ plays
    juce::String midiFolder;        // .mid: the MIDI files Batchcut writes and NeoBeat exports
    juce::String exportFolder;      // .wav: one-shots, chops, stems
    juce::String startupNjq;        // the sequence a new NeoBeat opens with (empty: the stock one)
    bool askDefault = true;         // ask "make this a default sequence?" after saving or loading one
    juce::String importFolder;      // the last place something was imported from: loose, never an assignment

    static juce::File file() { return dir().getChildFile ("settings.xml"); }
    static Settings load()
    {
        Settings s;
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            s.presetFolder = xml->getStringAttribute ("presetFolder"); s.startupNjq = xml->getStringAttribute ("startupNjq");
            s.askDefault = xml->getIntAttribute ("askDefault", 1) != 0;
            s.exportFolder = xml->getStringAttribute ("exportFolder"); s.importFolder = xml->getStringAttribute ("importFolder");
            s.instrumentFolder = xml->getStringAttribute ("instrumentFolder"); s.midiFolder = xml->getStringAttribute ("midiFolder");
        }
        return s;
    }
    void save() const
    {
        juce::XmlElement xml ("MARBELITE_SHARED");
        xml.setAttribute ("presetFolder", presetFolder); xml.setAttribute ("startupNjq", startupNjq); xml.setAttribute ("askDefault", askDefault ? 1 : 0);
        xml.setAttribute ("exportFolder", exportFolder); xml.setAttribute ("importFolder", importFolder);
        xml.setAttribute ("instrumentFolder", instrumentFolder); xml.setAttribute ("midiFolder", midiFolder);
        xml.writeTo (file());
    }

    static const char* name (Folder k)
    {
        switch (k) { case Folder::Wav: return "WAV"; case Folder::Midi: return "MIDI";
                     case Folder::Njq: return ".NJQ"; case Folder::Njqi: default: return ".NJQi"; }
    }
    // where a kind goes until one is assigned
    static juce::File defaultFolder (Folder k)
    {
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Marbelite");
        switch (k)
        {
            case Folder::Wav:  return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Batchcut").getChildFile ("One-shots");
            case Folder::Midi: return docs.getChildFile ("MIDI");
            case Folder::Njq:  return docs.getChildFile ("NJQ");
            case Folder::Njqi: default: return docs.getChildFile ("Instruments");
        }
    }
    const juce::String& assigned (Folder k) const
    {
        switch (k) { case Folder::Wav: return exportFolder; case Folder::Midi: return midiFolder;
                     case Folder::Njq: return presetFolder; case Folder::Njqi: default: return instrumentFolder; }
    }
    juce::String& assigned (Folder k)
    {
        switch (k) { case Folder::Wav: return exportFolder; case Folder::Midi: return midiFolder;
                     case Folder::Njq: return presetFolder; case Folder::Njqi: default: return instrumentFolder; }
    }
    // the folder of a kind, made if it is not there yet. A folder that was assigned and has since been deleted or
    // unplugged falls back to the default rather than failing a save, and the assignment is left alone.
    juce::File folder (Folder k) const
    {
        const auto& a = assigned (k);
        juce::File f = a.isNotEmpty() ? juce::File (a) : defaultFolder (k);
        if (! f.createDirectory().wasOk()) { f = defaultFolder (k); f.createDirectory(); }
        return f;
    }
    // the only way an assignment changes. Saves never call this; the settings panel does.
    void assign (Folder k, const juce::File& f) { assigned (k) = f.getFullPathName(); }

    juce::File presets()     const { return folder (Folder::Njq); }
    juce::File instruments() const { return folder (Folder::Njqi); }
    juce::File wavs()        const { return folder (Folder::Wav); }
    juce::File midis()       const { return folder (Folder::Midi); }
};
} // namespace shared
