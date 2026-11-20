#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../neobeat/shared/rooms/RoomView.h"

// CUT: recordings come in here and are cut into sounds, which every other room then draws on.
//
// Batchcut's version of this room has a spectrogram you can draw on, which belongs to Batchcut -
// it is what that plugin is for, and it is wired into its audio callback. MidiJaQ wants the other
// half: bring files in, let the detector find the sounds, and get them into the pool so LOOPS and
// KEYS have something to work with. For drawing on a spectrogram, Batchcut is one window away.

class CutRoom final : public juce::Component,
                      private juce::Timer,
                      private juce::FileDragAndDropTarget
{
public:
    explicit CutRoom (bc::RoomView& v);
    ~CutRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool key (const juce::KeyPress&);

private:
    void timerCallback() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    struct Parts;
    bc::RoomView& view;
    std::unique_ptr<Parts> parts;
    int seenFiles = -1, seenPool = -1;
    bool wasBusy = false;
};
