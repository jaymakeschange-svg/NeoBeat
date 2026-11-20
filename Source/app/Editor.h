#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Processor.h"
#include "Ui.h"
#include "../neobeat/shared/rooms/RoomView.h"

// MidiJaQ's window: seven rooms along the top, one of them showing.
//
// CUT, LOOPS and KEYS are the same code Batchcut runs, not a copy of it - a raw file becomes
// something playable in the same few moves in either plugin. MAP, MOD, RANDOM and the Stereoroom
// are MidiJaQ's own, and are what makes it a melodic sampling sequencer rather than a slicer.
//
// The interface this replaces had one window with a sequencer in it and everything else folded
// into side tabs. It is gone.

class MjqEditor final : public juce::AudioProcessorEditor,
                        public bc::RoomView,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit MjqEditor (MjqProcessor&);
    ~MjqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void refresh();

    // 0 CUT, 1 LOOPS, 2 KEYS, 3 MAP, 4 MOD, 5 RANDOM, 6 STEREOROOM
    void showRoom (int which);
    int currentRoom() const noexcept { return room; }

    MjqProcessor& proc;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Parts;
    std::unique_ptr<Parts> parts;
    mjqui::Look look;
    juce::TooltipWindow tips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqEditor)
};
