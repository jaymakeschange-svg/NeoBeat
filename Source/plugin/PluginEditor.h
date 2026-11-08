// NeoBeat 2.0 — the editor: the shell (1440 x 900), scaled to the window.
#pragma once

#include "../ui/UI.h"

class NeoBeatEditor final : public juce::AudioProcessorEditor
{
public:
    explicit NeoBeatEditor (NeoBeatProcessor&);
    ~NeoBeatEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    NeoBeatProcessor& proc;
    ui::Shell shell;
    juce::TooltipWindow tooltips { this, 600 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeoBeatEditor)
};
