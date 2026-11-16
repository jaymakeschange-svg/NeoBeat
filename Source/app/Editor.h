#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Processor.h"
#include "Ui.h"

class MjqEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MjqEditor (MjqProcessor&);
    ~MjqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh();

    MjqProcessor& proc;
    int chosenStep = -1, chosenLane = 1;  // where the editor is, for the keyboard

private:
    void timerCallback() override;
    struct Parts;
    std::unique_ptr<Parts> parts;
    mjqui::Look look;
    int seenVersion = -1;
    juce::TooltipWindow tips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqEditor)
};
