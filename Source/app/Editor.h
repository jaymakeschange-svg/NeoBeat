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
    void showSideTab (int tab);           // 0 the line, 1 the voices, 2 the Orb, 3 the room

    MjqProcessor& proc;
    int chosenStep = -1, chosenLane = 1;  // where the editor is, for the keyboard

private:
    void timerCallback() override;
    void paintVoices (juce::Graphics&);   // the multi: the chosen instrument, and all four key ranges
    void paintOrb (juce::Graphics&);      // the Orb: what the joystick is reading out, and the stack
    float currentValueOf (mjq::Cycled) const;   // where a control sits now, so a cycle knows where to land
    struct Parts;
    std::unique_ptr<Parts> parts;
    mjqui::Look look;
    int seenVersion = -1;
    juce::TooltipWindow tips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqEditor)
};
