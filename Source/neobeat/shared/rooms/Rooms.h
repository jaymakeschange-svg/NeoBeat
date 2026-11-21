#pragma once

// The three rooms that turn recordings into something playable: KIT, LOOPS and KEYS.
//
// This was Batchcut's, sitting on its processor. MidiJaQ wants the same three rooms - a raw file
// should become a playable instrument in the same few moves in either plugin - so rather than
// keep two copies that drift apart, the rooms live here and both processors inherit them.
//
// The cut turned out to be clean. Across the three rooms' thousand-odd lines, nothing reached
// into the plugin that owned them except an audio format manager and the audition preview, and
// both of those are plain objects that can live here too. So there are no hooks back into the
// host, no virtuals, and nothing for a host to remember to call. A processor inherits this and
// has the rooms.
//
// What the host still owns is the part that differs between the two plugins: the audio callback,
// the parameters, the project file, and in Batchcut's case the CUT room and its recorder.

#include "../LearnerStore.h"
#include "Engine.h"
#include "Keys.h"
#include "Kit.h"
#include "Loops.h"
#include "Player.h"
#include "Session.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <set>
#include <vector>

// A pool sound in the project's state, and back (KitRooms.cpp)
namespace bcxml
{
std::unique_ptr<juce::XmlElement> sound (const bc::PoolItem& it);
std::shared_ptr<bc::PoolItem> sound (const juce::XmlElement& e, double maxSeconds = 30.0);
}

namespace bc
{

struct Rooms
{
    Rooms() { formats.registerBasicFormats(); }
    virtual ~Rooms() = default;

    Rooms (const Rooms&) = delete;
    Rooms& operator= (const Rooms&) = delete;

    // ---- what the rooms are built on
    Session session;
    Preview preview;                                             // auditioning a sound
    juce::AudioFormatManager formats;

    bool stateDirty = true;                                      // something changed: the project wants saving
    void stateChanged() { stateDirty = true; }
    juce::String lastMessage;                                    // shown in the header (a file that failed, a recording saved)

    // view state kept with the project
    double viewStart = 0.0, viewEnd = 0.0;
    int openFile = -1, openPart = 0;

    // The two things a room cannot do for itself. A host overrides them: one tells whoever is
    // watching that something changed, the other stops whatever is sounding.
    virtual void roomsNotify() {}
    virtual void stopPlayback() { preview.stopPlaying(); }
    double hostRate = 48000.0;                                   // the host's sample rate, when it is playing
    std::atomic<double> hostBpm { 0.0 };


    // ---- the files the rooms draw on (CutRooms.cpp)
    static bool isAudioFile (const juce::File& f);
    int addPaths (const juce::StringArray& paths);              // files or folders (searched for audio files); returns how many were new
    void open (int file, int part = 0);
    void removeFiles (std::vector<int> rows);                    // off the list (the files themselves are not touched)
    void clearFiles();                                           // Clear all: every file off the list
    juce::File recordingsDir() const;
    juce::File exportDir() const;
    juce::File midiDir() const;

    // Gives the loader the four things it needs to touch audio: how to probe a file, how to read
    // part of one, how to write a wav, how to make a folder. The Session is deliberately
    // format-agnostic, so without these every read fails - which is exactly what MidiJaQ did,
    // because it never set them. Shared, so no plugin has to remember to.
    // The caller still sets session.onChange (it needs its own notifier) and calls session.start().
    void wireSession();

    static bool writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r,
                          double sr, int bits, std::string& err);

    // ---- the Kit room: a NeoBeat kit out of the one-shots (message thread)
    KitModel kit;
    KitPlayer kitPlayer;
    void kitGather (bool allFiles);                              // the open file's one-shots, or every file's, into the pool
    int kitAddFolder (const juce::File& folder);                 // a folder of one-shots into the pool; how many
    void kitClearPool();                                         // Clear all: the pool empties (sounds still in lanes stay)
    void kitClearLanes();                                        // every lane that is not locked loses its sounds
    void kitChoose();                                            // a sound on every lane that is not locked
    void kitShuffle();                                           // another kit out of the same pool
    void kitRandom (int lane = 0);                               // RANDOM: fresh sounds dealt with no repeats, no ranking
    bool kitAnyLane = false;                                     // RANDOM ignores the lanes' roles
    void kitAssign (int lane, int layer, int item, bool teach);  // teach: you put it there, so that is what it is
    void kitNext (int lane, int dir);                            // the next (or last) best sound for a lane
    void kitRate (int item, float stars);
    float kitRating (int item);
    void kitChanged (int lane = -1);                             // a lane changed (-1: all): the player and the project hear of it
    void kitSetPattern (int index, bool takeTempo = false);
    void kitMark();                                              // a change is complete: it can be undone from here
    void kitUndo(); void kitRedo();
    bool kitCanUndo() const noexcept { return ! kitUndoStack.empty(); }
    bool kitCanRedo() const noexcept { return ! kitRedoStack.empty(); }
    void kitPlayItem (int item);                                 // a pool sound as it is
    void kitPlayLane (int lane) { kitPlayer.hit (lane); }
    bool kitSaveNjq (const juce::File& file, juce::String& error);
    std::vector<int> kitCandidates (int lane) { return kit.candidates (lane, nbkstore::learner()); }
    std::shared_ptr<const KitSound> kitSound (int lane) const
        { return lane >= 0 && lane < kLanes ? laneSounds[(size_t) lane] : nullptr; }
    juce::String kitMessage;
    int kitVersion = 0;                                          // changes with the kit (the room repaints)

    // ---- LOOPS: the light sample sequencer (message thread). The pool is the Kit room's.
    LoopSet loops;
    LoopPlayer loopPlayer;
    juce::String loopsMessage;
    int loopsVersion = 0;
    int loopsAddLayer();                                         // a new layer (from the whole pool), rolled; its index, or -1
    void loopsRemoveLayer (int layer);
    void loopsRoll (int layer, bool newSeed = true);             // layer -1: every layer; newSeed false: the same roll, new settings
    bool loopsRerollHit (int layer, int hit);
    void loopsChanged();                                         // anything changed: the player and the project hear of it
    void loopsClearAll();                                        // Clear all: every layer empties, back to one
    void loopsMark();                                            // a change is complete: undoable from here
    void loopsUndo(); void loopsRedo();
    bool loopsCanUndo() const noexcept { return ! loopsUndoStack.empty(); }
    bool loopsCanRedo() const noexcept { return ! loopsRedoStack.empty(); }
    void loopsPlayHit (int layer, int hit);                      // one chop, as it plays in the loop
    std::vector<std::string> loopsSources() const;               // the recordings and folders the pool holds sounds from
    bool loopsExportAudio (const juce::File& dir, bool stems, juce::String& error);
    bool loopsExportMidi (const juce::File& file, juce::String& error);
    bool loopsSaveNjq (const juce::File& file, juce::String& error);
    // a loop that comes back: the room's own XML in a file, samples embedded, so presets round-trip
    bool loopsSavePreset (const juce::File& file, juce::String& error);
    bool loopsOpenPreset (const juce::File& file, juce::String& error);
    bool loopsBounce (juce::String& error);                      // the loop as one new sound in the pool
    double loopsRate() const noexcept { return hostRate > 0 ? hostRate : 48000.0; }

    // ---- KEYS: sounds made into an instrument (message thread)
    Instrument keys;
    KeysPlayer keysPlayer;
    LoopSource keysSource;                                       // where its sounds come from (in the pool)
    std::set<std::shared_ptr<const PoolItem>> keysLeftOut;       // sounds LEAVE OUT took out
    juce::MidiKeyboardState keyboard;                            // the on-screen keys
    std::atomic<int> keysChord { 0 };                            // the keys play chords (0: single notes)
    std::atomic<int> midiTarget { 2 };                           // where the keys go: 2 LOOPS, 3 KEYS
    juce::String keysMessage;
    int keysVersion = 0;
    std::vector<std::shared_ptr<const PoolItem>> keysSounds() const;
    void keysBuild();                                            // the instrument from its source's sounds, in its mode
    void keysChanged();
    void keysClearAll();                                         // Clear all: every zone goes, the instrument empties
    void keysMark();
    void keysUndo(); void keysRedo();
    bool keysCanUndo() const noexcept { return ! keysUndoStack.empty(); }
    bool keysCanRedo() const noexcept { return ! keysRedoStack.empty(); }
    bool keysSaveNjq (const juce::File& file, juce::String& error);
    bool keysOpenNjq (const juce::File& file, juce::String& error);
    static int numChords();
    static const char* chordName (int i);

    // ---- the rooms in the project file
    std::unique_ptr<juce::XmlElement> kitToXml();
    void kitFromXml (const juce::XmlElement&);
    std::unique_ptr<juce::XmlElement> loopsToXml();
    void loopsFromXml (const juce::XmlElement&);
    std::unique_ptr<juce::XmlElement> keysToXml();
    void keysFromXml (const juce::XmlElement&);

protected:
    void kitTakeGathered();

    // kit
    std::array<std::shared_ptr<const KitSound>, kLanes> laneSounds {};
    std::vector<std::shared_ptr<const KitSound>> kitKeep;        // never freed on the audio thread
    std::unique_ptr<juce::XmlElement> kitXmlCache; int kitXmlVersion = -1;
    using KitLanes = std::array<KitLane, kLanes>;
    KitLanes kitSaved {};
    std::vector<KitLanes> kitUndoStack, kitRedoStack;

    // loops
    std::vector<LoopSet> loopsUndoStack, loopsRedoStack;
    LoopSet loopsSaved;
    std::deque<std::pair<juce::uint32, std::shared_ptr<const LoopSnapshot>>> loopKeep;
    std::unique_ptr<juce::XmlElement> loopsXmlCache; int loopsXmlVersion = -1;

    // keys
    std::vector<Instrument> keysUndoStack, keysRedoStack;
    Instrument keysSaved;
    std::deque<std::pair<juce::uint32, std::shared_ptr<const Instrument>>> keysKeep;
    std::unique_ptr<juce::XmlElement> keysXmlCache; int keysXmlVersion = -1;
    std::atomic<int> chordRoot { 0 }, chordScale { 3 };          // "In key" chords follow LOOPS' key
};

} // namespace bc
