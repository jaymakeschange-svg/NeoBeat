#include "PluginEditor.h"

NeoBeatEditor::NeoBeatEditor (NeoBeatProcessor& p) : juce::AudioProcessorEditor (p), proc (p), shell (p)
{
    addAndMakeVisible (shell);
    // start at a size that fits the screen (laptops get a smaller window)
    float scale = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userBounds;
        scale = juce::jlimit (0.55f, 1.0f, std::min ((area.getWidth() - 40) / 1440.0f, (area.getHeight() - 80) / 900.0f));
    }
    setResizable (true, true);
    setResizeLimits (720, 450, 2160, 1350);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1440.0 / 900.0);
    setSize (juce::roundToInt (1440 * scale), juce::roundToInt (900 * scale));
}

NeoBeatEditor::~NeoBeatEditor() = default;

void NeoBeatEditor::resized()
{
    const float s = (float) getWidth() / 1440.0f;
    shell.setTransform (juce::AffineTransform::scale (s));
    shell.setBounds (0, 0, 1440, 900);
}
