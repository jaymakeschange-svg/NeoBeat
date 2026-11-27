#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Processor.h"
#include "Ui.h"
#include "../neobeat/shared/rooms/RoomView.h"

// MidiJaQ's window: seven rooms along the top, one of them showing.
//
// CUT, LOOPS and KEYS are the same code Batchcut runs, not a copy of it - a raw file becomes
// something playable in the same few moves in either plugin. MAP, MOD, JACK and the Stereoroom
// are MidiJaQ's own, and are what makes it a melodic sampling sequencer rather than a slicer.
//
// The interface this replaces had one window with a sequencer in it and everything else folded
// into side tabs. It is gone.

class MjqEditor final : public juce::AudioProcessorEditor,
                        public bc::RoomView,
                        // public, for the same reason as CutRoom: JUCE finds a drop target by
                        // dynamic_cast, and a private base is invisible to it
                        public juce::FileDragAndDropTarget,
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

    // Files land anywhere in the window, not only on the one list that happens to be showing.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void paintOverChildren (juce::Graphics&) override;

    // 0 CUT, 1 LOOPS, 2 KEYS, 3 MAP, 4 MOD, 5 JACK, 6 STEREOROOM
    void showRoom (int which);
    int currentRoom() const noexcept { return room; }

    MjqProcessor& proc;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Parts;
    std::unique_ptr<Parts> parts;
    mjqui::Look look;
    bool dropping = false;
    juce::TooltipWindow tips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqEditor)
};
