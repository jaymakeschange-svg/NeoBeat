// The stand-alone effects' window: NeoBeat's Friction Stretch sonogram / Bounce Delay room, with a
// strip for loading, playing and catching hits.
#pragma once

#include "FxProcessor.h"

namespace fxui { class Panel; }

class FxEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FxEditor (FxProcessor&);
    ~FxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int kW = 1180, kH = 652;

private:
    std::unique_ptr<juce::LookAndFeel> look;
    std::unique_ptr<fxui::Panel> panel;
    juce::TooltipWindow tips { this, 600 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxEditor)
};
