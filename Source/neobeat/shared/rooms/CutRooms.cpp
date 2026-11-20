// The files side of the rooms: bringing recordings in, opening one, taking them out again, and
// the folders everything is written to. This was Batchcut's CUT room talking to its processor;
// MidiJaQ needs the same, because a room with no sounds in it is not much of a room.

#include "Rooms.h"

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

