// The files side of the rooms: bringing recordings in, opening one, taking them out again, and
// the folders everything is written to. This was Batchcut's CUT room talking to its processor;
// MidiJaQ needs the same, because a room with no sounds in it is not much of a room.

#include "Rooms.h"
#include <limits>

void bc::Rooms::wireSession()
{
    session.settings.outDir = exportDir().getFullPathName().toStdString();

    session.prober = [this] (const std::string& path, bc::FileInfo& info, std::string& err)
    {
        const juce::File f (juce::String::fromUTF8 (path.c_str()));
        if (! f.existsAsFile()) { err = "the file is missing"; return false; }
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) { err = "not an audio file this can read"; return false; }
        if (r->sampleRate < 1000.0 || r->lengthInSamples <= 0) { err = "the file is empty"; return false; }
        info.sr = r->sampleRate; info.length = r->lengthInSamples; info.channels = (int) r->numChannels;
        return true;
    };
    session.loader = [this] (const std::string& path, int64_t start, int64_t len, bc::Audio& out, std::string& err)
    {
        const juce::File f (juce::String::fromUTF8 (path.c_str()));
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) { err = f.existsAsFile() ? "could not read the file" : "the file is missing"; return false; }
        len = std::max<int64_t> (0, std::min<int64_t> (len, (int64_t) r->lengthInSamples - start));
        if (len <= 0 || len > (int64_t) std::numeric_limits<int>::max()) { err = "nothing to read"; return false; }
        const bool stereo = r->numChannels > 1;
        juce::AudioBuffer<float> buf (stereo ? 2 : 1, (int) len);
        if (! r->read (&buf, 0, (int) len, start, true, stereo)) { err = "could not read the file"; return false; }
        out.sr = r->sampleRate;
        out.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
        if (stereo) out.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len); else out.r.clear();
        for (auto* v : { &out.l, &out.r }) for (auto& x : *v) if (! std::isfinite (x)) x = 0.0f;
        return true;
    };
    session.writer = [] (const std::string& path, const std::vector<float>& l, const std::vector<float>& r,
                         double sr, int bits, std::string& err)
    {
        return writeWav (juce::File (juce::String::fromUTF8 (path.c_str())), l, r, sr, bits, err);
    };
    session.makeDir = [] (const std::string& dir)
    {
        return juce::File (juce::String::fromUTF8 (dir.c_str())).createDirectory().wasOk();
    };
}

bool bc::Rooms::isAudioFile (const juce::File& f)
{
    static const juce::StringArray ext { ".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac", ".ogg", ".mp3" };
    return f.existsAsFile() && ext.contains (f.getFileExtension().toLowerCase());
}

int bc::Rooms::addPaths (const juce::StringArray& paths)
{
    std::vector<std::string> add;
    for (const auto& p : paths)
    {
        const juce::File f (p);
        if (f.isDirectory())
        {
            auto found = f.findChildFiles (juce::File::findFiles, true, "*", juce::File::FollowSymlinks::noCycles);
            found.sort();
            for (const auto& c : found) if (isAudioFile (c) && add.size() < 2000) add.push_back (c.getFullPathName().toStdString());
        }
        else if (isAudioFile (f)) add.push_back (f.getFullPathName().toStdString());
    }
    const size_t before = session.files.size();
    session.addFiles (add);
    const int added = (int) (session.files.size() - before);
    if (added > 0 && ! session.doc()) open ((int) before, 0);
    stateChanged();
    roomsNotify();
    return added;
}

void bc::Rooms::open (int file, int part)
{
    if (file < 0 || file >= (int) session.files.size()) return;
    stopPlayback();
    openFile = file; openPart = std::max (0, part);
    viewStart = viewEnd = 0.0;   // the editor fits the new part
    session.open (file, openPart);
    stateChanged();
}

void bc::Rooms::removeFiles (std::vector<int> rows)
{
    std::sort (rows.begin(), rows.end());
    rows.erase (std::unique (rows.begin(), rows.end()), rows.end());
    rows.erase (std::remove_if (rows.begin(), rows.end(), [this] (int r) { return r < 0 || r >= (int) session.files.size(); }), rows.end());
    if (rows.empty()) return;
    const auto d = session.doc();
    const int openRow = d ? d->file : -1;
    const bool openGoes = openRow >= 0 && std::binary_search (rows.begin(), rows.end(), openRow);
    if (openGoes) stopPlayback();
    for (auto r = rows.rbegin(); r != rows.rend(); ++r)
    {
        session.removeFile (*r);
        if (openFile == *r) openFile = -1; else if (openFile > *r) --openFile;
    }
    const int n = (int) rows.size();
    lastMessage = "Took " + juce::String (n) + (n == 1 ? " file" : " files") + " off the list";
    stateChanged();
    roomsNotify();
    if (openGoes && ! session.files.empty())
    {
        // the first file left after the one that was open
        const int below = (int) (std::lower_bound (rows.begin(), rows.end(), openRow) - rows.begin());
        open (std::min (openRow - below, (int) session.files.size() - 1), 0);
    }
}

void bc::Rooms::clearFiles()
{
    const int n = (int) session.files.size();
    if (n == 0) return;
    stopPlayback();
    session.clearFiles();
    openFile = -1; openPart = 0; viewStart = viewEnd = 0.0;
    lastMessage = "Took all " + juce::String (n) + (n == 1 ? " file" : " files") + " off the list (the files themselves are untouched; the kit pool keeps its sounds)";
    stateChanged();
    roomsNotify();
}



juce::File bc::Rooms::recordingsDir() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Batchcut").getChildFile ("Recordings");
}

// the assigned WAV folder (Settings). Until one is assigned: Music/Batchcut/One-shots, as before.
juce::File bc::Rooms::exportDir() const
{
    return shared::Settings::load().wavs();
}

// the assigned MIDI folder (Settings). Until one is assigned: Documents/Marbelite/MIDI.
juce::File bc::Rooms::midiDir() const
{
    return shared::Settings::load().midis();
}

