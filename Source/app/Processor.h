#pragma once
// MidiJaQ's plugin: it holds the line, hands the audio thread a snapshot of it, and turns what the
// player sends into MIDI. The editor only ever touches the line and calls lineChanged().
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#include "../core/Inst.h"
#include "../core/Player.h"

class MjqProcessor final : public juce::AudioProcessor
{
public:
    MjqProcessor();
    ~MjqProcessor() override = default;

    // ---- the line, and the snapshot the audio thread reads
    mjq::Line line;                                  // the message thread owns this
    mjq::Player player;
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
    std::shared_ptr<const mjq::Pass> shown;          // swapped under the audio thread's feet, never edited
    std::shared_ptr<const mjq::Instrument> loaded;
    std::vector<std::shared_ptr<const mjq::Instrument>> keep;   // the one just replaced, until the audio thread is past it
    double keepSince = 0.0;
    std::atomic<int> lastStep { -1 };
    std::vector<mjq::Line> undoStack, redoStack;
    mjq::Line marked;
    bool hasMark = false;
    juce::Random seeds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqProcessor)
};
