// BatchCut - the processor: the session (files, edits, export), the preview player, recording the input,
// and the state saved with the project. The same code runs as the standalone app and as the VST3.
#pragma once

#include <set>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Player.h"
#include "../core/Session.h"
#include "../core/Kit.h"
#include "../core/Loops.h"
#include "../core/Keys.h"
#include "../neobeat/shared/LearnerStore.h"
#include <deque>

// A pool sound in the project's state, and back (KitProc.cpp)
namespace bcxml
{
std::unique_ptr<juce::XmlElement> sound (const bc::PoolItem& it);
std::shared_ptr<bc::PoolItem> sound (const juce::XmlElement& e, double maxSeconds = 30.0);
}

// The input recorder: the audio thread writes into chunks the message thread allocates ahead of it.
class Recorder
{
public:
    static constexpr int kChunks = 180;          // 30 minutes in 10 s chunks
    void prepare (double sampleRate) { stop(); sr = sampleRate; }
    void start()
    {
        stop();
        chunkLen = std::max (1024, (int) (10.0 * sr));
        for (int i = 0; i < 2; ++i) ensure (i);
        written.store (0); overflow.store (false);
        recording.store (true);
    }
    // message thread: stops, and hands back what was recorded
    void stop (std::vector<float>* l = nullptr, std::vector<float>* r = nullptr)
    {
        recording.store (false);
        const int64_t n = written.load();
        if (l != nullptr && r != nullptr)
        {
            l->resize ((size_t) n); r->resize ((size_t) n);
            for (int64_t i = 0; i < n; ++i)
            {
                const auto* c = chunks[(size_t) (i / chunkLen)].get();
                const size_t o = (size_t) (i % chunkLen);
                (*l)[(size_t) i] = c != nullptr ? c->l[o] : 0.0f; (*r)[(size_t) i] = c != nullptr ? c->r[o] : 0.0f;
            }
        }
        written.store (0);
        // the audio thread may still be in its block: freed a moment later
        stoppedAt = juce::Time::getMillisecondCounter(); pendingFree = true;
    }
    // message thread, on a timer: chunks ahead of the write position, and freeing after a stop
    void service()
    {
        if (recording.load())
        {
            const int k = (int) (written.load() / std::max (1, chunkLen));
            for (int i = k; i < std::min (kChunks, k + 2); ++i) ensure (i);
        }
        else if (pendingFree && juce::Time::getMillisecondCounter() - stoppedAt > 500)
        {
            for (int i = 0; i < kChunks; ++i) { ptrs[(size_t) i].store (nullptr); chunks[(size_t) i].reset(); }
            pendingFree = false;
        }
    }
    // audio thread
    void push (const float* l, const float* r, int n) noexcept
    {
        if (! recording.load()) return;
        int64_t w = written.load();
        for (int i = 0; i < n; ++i, ++w)
        {
            const int k = (int) (w / chunkLen);
            if (k >= kChunks) { recording.store (false); overflow.store (true); break; }
            Chunk* c = ptrs[(size_t) k].load();
            if (c == nullptr) { overflow.store (true); break; }   // the timer fell behind: what follows is dropped
            const size_t o = (size_t) (w % chunkLen);
            c->l[o] = l[i]; c->r[o] = r != nullptr ? r[i] : l[i];
        }
        written.store (w);
    }
    bool isRecording() const noexcept { return recording.load(); }
    double seconds() const noexcept { return (double) written.load() / std::max (1.0, sr); }
    double rate() const noexcept { return sr; }
    bool overflowed() const noexcept { return overflow.load(); }

private:
    struct Chunk { std::vector<float> l, r; };
    void ensure (int i)
    {
        if (i < 0 || i >= kChunks || chunks[(size_t) i]) return;
        auto c = std::make_unique<Chunk>(); c->l.assign ((size_t) chunkLen, 0.0f); c->r.assign ((size_t) chunkLen, 0.0f);
        ptrs[(size_t) i].store (c.get());
        chunks[(size_t) i] = std::move (c);
    }
    double sr = 48000.0;
    int chunkLen = 480000;
    std::array<std::unique_ptr<Chunk>, kChunks> chunks;
    std::array<std::atomic<Chunk*>, kChunks> ptrs {};
    std::atomic<int64_t> written { 0 };
    std::atomic<bool> recording { false }, overflow { false };
    juce::uint32 stoppedAt = 0;
    bool pendingFree = false;
};

class BcProcessor final : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::AsyncUpdater, private juce::Timer
{
public:
    BcProcessor();
    ~BcProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Batchcut"; }
    bool acceptsMidi() const override { return true; }          // LOOPS and KEYS play from a keyboard
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    bc::Session session;
    bc::Preview preview;
    Recorder recorder;
    juce::AudioFormatManager formats;

    static bool isAudioFile (const juce::File& f);
    int addPaths (const juce::StringArray& paths);              // files or folders (searched for audio files); returns how many were new
    void open (int file, int part = 0);
    void removeFiles (std::vector<int> rows);                    // off the list (the files themselves are not touched)
    void clearFiles();                                           // Clear all: every file off the list

    // playback of the open part. What plays and from where, for the playhead.
    void playEvent (const bc::Event& e);                         // as it will be exported (fades, level)
    void playRange (int64_t from, int64_t to, bool loop);        // the part with its layers and edits
    void stopPlayback() { preview.stopPlaying(); playDoc = nullptr; }
    bool isPlaying() const noexcept { return preview.playing(); }
    // the playhead in samples of the open part (-1: not playing it)
    int64_t playhead() const;
    bool playingEvent = false;

    bool startRecording();
    juce::File stopRecording();                                  // the recording's file (added and opened), or none
    juce::File renderToTemp (const bc::Event& e);                // one event as a WAV, for dragging out to a DAW
    juce::File exportDir() const;
    juce::File midiDir() const;
    juce::File recordingsDir() const;
    juce::String lastMessage;                                    // shown in the header (a recording saved, a file that failed)

    // view state kept with the project
    double viewStart = 0.0, viewEnd = 0.0;
    int openFile = -1, openPart = 0;
    void stateChanged() { stateDirty = true; }

    static bool writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, double sr, int bits, std::string& err);

    // ---- the Kit room: a NeoBeat kit out of the one-shots (message thread)
    bc::KitModel kit;
    bc::KitPlayer kitPlayer;
    void kitGather (bool allFiles);                              // the open file's one-shots, or every file's, into the pool
    int kitAddFolder (const juce::File& folder);                 // a folder of one-shots into the pool; how many
    void kitClearPool();                                         // Clear all: the pool empties (sounds still in lanes stay)
    void kitClearLanes();                                        // every lane that is not locked loses its sounds
    void kitChoose();                                            // a sound on every lane that is not locked
    void kitShuffle();                                           // another kit out of the same pool
    void kitRandom (int lane = 0);                               // RANDOM: fresh sounds dealt with no repeats, no ranking (lane: the one the message counts)
    bool kitAnyLane = false;                                     // RANDOM ignores the lanes' roles
    void kitAssign (int lane, int layer, int item, bool teach);  // teach: you put it there, so that is what it is
    void kitNext (int lane, int dir);                            // the next (or last) best sound for a lane
    void kitRate (int item, float stars);
    float kitRating (int item);
    void kitChanged (int lane = -1);                             // a lane changed (-1: all of them): the player and the project hear of it
    void kitSetPattern (int index, bool takeTempo = false);       // takeTempo: the kit takes the pattern's own tempo and swing
    void kitMark();                                              // a change is complete: it can be undone from here
    void kitUndo(); void kitRedo();
    bool kitCanUndo() const noexcept { return ! kitUndoStack.empty(); }
    bool kitCanRedo() const noexcept { return ! kitRedoStack.empty(); }
    void kitPlayItem (int item);                                 // a pool sound as it is
    void kitPlayLane (int lane) { kitPlayer.hit (lane); }
    bool kitSaveNjq (const juce::File& file, juce::String& error);
    std::vector<int> kitCandidates (int lane) { return kit.candidates (lane, nbkstore::learner()); }
    std::shared_ptr<const bc::KitSound> kitSound (int lane) const { return lane >= 0 && lane < bc::kLanes ? laneSounds[(size_t) lane] : nullptr; }
    juce::String kitMessage;
    int kitVersion = 0;                                          // changes with the kit (the room repaints)

    // ---- LOOPS: the light sample sequencer (message thread). The pool is the Kit room's.
    bc::LoopSet loops;
    bc::LoopPlayer loopPlayer;
    juce::String loopsMessage;
    int loopsVersion = 0;
    int loopsAddLayer();                                         // a new layer (from the whole pool), rolled; its index, or -1 (8 already)
    void loopsRemoveLayer (int layer);
    void loopsRoll (int layer, bool newSeed = true);             // layer -1: every layer (ROLL ALL); newSeed false: the same roll, new settings
    bool loopsRerollHit (int layer, int hit);
    void loopsChanged();                                         // anything changed: the player and the project hear of it
    void loopsMark();                                            // a change is complete: undoable from here
    void loopsUndo(); void loopsRedo();
    bool loopsCanUndo() const noexcept { return ! loopsUndoStack.empty(); }
    bool loopsCanRedo() const noexcept { return ! loopsRedoStack.empty(); }
    void loopsPlayHit (int layer, int hit);                      // one chop, as it plays in the loop
    std::vector<std::string> loopsSources() const;               // the recordings and folders the pool holds sounds from
    bool loopsExportAudio (const juce::File& dir, bool stems, juce::String& error);   // WAV loops of the mix (and each layer)
    bool loopsExportMidi (const juce::File& file, juce::String& error);
    bool loopsSaveNjq (const juce::File& file, juce::String& error);                  // for NeoBeat: the chops as lanes, the loop as a pattern
    bool loopsBounce (juce::String& error);                      // the loop as one new sound in the pool
    double loopsRate() const noexcept { return hostRate > 0 ? hostRate : 48000.0; }

    // ---- KEYS: sounds made into an instrument for MidiJaQ (message thread)
    bc::Instrument keys;
    bc::KeysPlayer keysPlayer;
    bc::LoopSource keysSource;                                   // where its sounds come from (in the pool)
    std::set<std::shared_ptr<const bc::PoolItem>> keysLeftOut;   // sounds LEAVE OUT took out (until the source or the pool changes)
    juce::MidiKeyboardState keyboard;                            // the on-screen keys
    std::atomic<int> keysChord { 0 };                            // the on-screen and MIDI keys play chords (0: single notes)
    std::atomic<int> midiTarget { 2 };                           // where the keys go: 2 LOOPS, 3 KEYS (the last of the two shown)
    juce::String keysMessage;
    int keysVersion = 0;
    std::vector<std::shared_ptr<const bc::PoolItem>> keysSounds() const;
    void keysBuild();                                            // the instrument from its source's sounds, in its mode
    void keysChanged();
    void keysMark();
    void keysUndo(); void keysRedo();
    bool keysCanUndo() const noexcept { return ! keysUndoStack.empty(); }
    bool keysCanRedo() const noexcept { return ! keysRedoStack.empty(); }
    bool keysSaveNjq (const juce::File& file, juce::String& error);
    bool keysOpenNjq (const juce::File& file, juce::String& error);
    static int numChords();
    static const char* chordName (int i);

private:
    std::unique_ptr<juce::XmlElement> loopsToXml();
    void loopsFromXml (const juce::XmlElement&);
    std::vector<bc::LoopSet> loopsUndoStack, loopsRedoStack;
    bc::LoopSet loopsSaved;
    std::deque<std::pair<juce::uint32, std::shared_ptr<const bc::LoopSnapshot>>> loopKeep;   // never freed on the audio thread
    std::atomic<double> hostBpm { 0.0 };
    std::unique_ptr<juce::XmlElement> loopsXmlCache; int loopsXmlVersion = -1;
    std::unique_ptr<juce::XmlElement> keysToXml();
    void keysFromXml (const juce::XmlElement&);
    std::vector<bc::Instrument> keysUndoStack, keysRedoStack;
    bc::Instrument keysSaved;
    std::deque<std::pair<juce::uint32, std::shared_ptr<const bc::Instrument>>> keysKeep;
    std::unique_ptr<juce::XmlElement> keysXmlCache; int keysXmlVersion = -1;
    std::atomic<int> chordRoot { 0 }, chordScale { 3 };         // "In key" chords follow LOOPS' key

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void refreshStateCache();
    void applyState (const juce::XmlElement& root);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void kitTakeGathered();
    std::unique_ptr<juce::XmlElement> kitToXml();
    void kitFromXml (const juce::XmlElement&);
    std::array<std::shared_ptr<const bc::KitSound>, bc::kLanes> laneSounds {};
    std::vector<std::shared_ptr<const bc::KitSound>> kitKeep;   // never freed on the audio thread
    std::unique_ptr<juce::XmlElement> kitXmlCache; int kitXmlVersion = -1;
    using KitLanes = std::array<bc::KitLane, bc::kLanes>;
    KitLanes kitSaved {};
    std::vector<KitLanes> kitUndoStack, kitRedoStack;

    std::atomic<float>* level = nullptr;
    std::atomic<float>* thru = nullptr;
    double hostRate = 48000.0;
    std::vector<float> scratch;                                  // a second channel for a mono output
    const bc::Doc* playDoc = nullptr;
    int64_t playOffset = 0;
    juce::CriticalSection stateLock;
    juce::MemoryBlock stateCache;
    std::unique_ptr<juce::XmlElement> pendingState;
    bool stateDirty = true;
    int ticks = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BcProcessor)
};
