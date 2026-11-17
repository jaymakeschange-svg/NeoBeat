#pragma once
// MidiJaQ's plugin: it holds the line, hands the audio thread a snapshot of it, and turns what the
// player sends into MIDI. The editor only ever touches the line and calls lineChanged().
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#include "../core/Inst.h"
#include "../core/Player.h"
#include "../core/Session.h"

class MjqProcessor final : public juce::AudioProcessor
{
public:
    MjqProcessor();
    ~MjqProcessor() override = default;

    // ---- the line, and the snapshot the audio thread reads
    mjq::Line line;                                  // the selected line, on the message thread
    mjq::Session session;                             // 128 slots, four lines, chord track
    mjq::Player player;                               // line 1, kept for the original public API
    std::array<mjq::Player, mjq::kSessionTracks - 1> otherPlayers;
    std::array<mjq::InstrumentPlayer, mjq::kSessionTracks - 1> otherVoices;
    void setInstrumentLevel (float level);
    int chosenSlot() const noexcept { return session.selected; }
    int chosenTrack() const noexcept { return session.selectedTrack; }
    void chooseSlot (int index);
    void chooseTrack (int index);
    void changeWriter (mjq::Generator);
    void changeChord (int bar, const mjq::Chord&);
    void changeChain (int length);
    void changeTrackEnabled (bool enabled);
    bool savePatterns (const juce::File&, juce::String& error);
    bool loadPatterns (const juce::File&, juce::String& error);
    void rebuildSession (bool full = false);
    mjq::ClockMode clockMode = mjq::ClockMode::Pattern;
    std::atomic<int> version { 0 };                  // the editor redraws when this moves
    juce::String message;                            // one line under the window

    // ---- the instrument it plays, when one is loaded (a .NJQ from Batchcut's KEYS room)
    mjq::InstrumentPlayer voices;
    juce::String instrumentFile;                     // where it came from, so the project can open it again
    bool openInstrument (const juce::File&, juce::String& error);
    void closeInstrument();
    std::shared_ptr<const mjq::Instrument> instrument() const { return std::atomic_load (&loaded); }

    void lineChanged();                              // rebuild the snapshot and bump the version
    void roll();                                     // JAQ: a new family from a new seed
    void rollLane (mjq::Lane lane);                  // one lane again, the rest as they are
    void mark();                                     // remember the line, for undo
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    std::shared_ptr<const mjq::Pass> pass() const { return std::atomic_load (&shown); }
    int stepNow() const noexcept { return lastStep.load(); }

    // ---- the plugin
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MidiJaQ"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::String toXml (const mjq::Line&, mjq::ClockMode, const juce::String& instrumentFile);
    static bool fromXml (const juce::String&, mjq::Line&, mjq::ClockMode&, juce::String& instrumentFile);

private:
    struct Prepared
    {
        mjq::Session state;
        std::array<std::array<mjq::Pass, mjq::kSessionTracks>, mjq::kSessionSlots> passes {};
        std::array<std::array<std::shared_ptr<const std::vector<mjq::Pass>>, mjq::kSessionTracks>, mjq::kSessionSlots> evolving {};
    };
    std::shared_ptr<const Prepared> prepared;
    std::shared_ptr<const mjq::Pass> shown;          // editor's preview of the chosen line
    std::shared_ptr<const mjq::Instrument> loaded;
    std::vector<std::shared_ptr<const mjq::Instrument>> keep;   // the one just replaced, until the audio thread is past it
    double keepSince = 0.0;
    std::atomic<int> lastStep { -1 };
    double hostSampleRate = 48000.0;
    int playingSlot = -1;
    struct History { int slot, track; mjq::Line line; };
    std::vector<History> undoStack, redoStack;
    mjq::Line marked;
    bool hasMark = false;
    juce::Random seeds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqProcessor)
};
