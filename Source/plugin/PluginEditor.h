// NeoBeat — the editor. Dark, quiet, Amaranth Cycle-inspired: a 4x4 pad grid,
// the selected pad's sound and eight macro knobs, a 3D spectrum mesh of the
// selected pad, and the 16-track step grid with swing, velocity and rolls.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace ui
{
struct Palette
{
    static juce::Colour bg()       { return juce::Colour (0xff0d1015); }
    static juce::Colour panel()    { return juce::Colour (0xff151a21); }
    static juce::Colour panelHi()  { return juce::Colour (0xff1c222b); }
    static juce::Colour line()     { return juce::Colour (0xff2a313c); }
    static juce::Colour text()     { return juce::Colour (0xffd6dde6); }
    static juce::Colour dim()      { return juce::Colour (0xff6f7a88); }
    static juce::Colour accent()   { return juce::Colour (0xff3fd0c9); }
    static juce::Colour forGroup (const juce::String& g)
    {
        if (g.startsWith ("Round 5")) return juce::Colour (0xff3fd0c9);
        if (g.startsWith ("City"))    return juce::Colour (0xfff2a541);
        if (g.startsWith ("Round 4")) return juce::Colour (0xffd45db0);
        if (g.startsWith ("IronHead"))return juce::Colour (0xff9bd25b);
        return juce::Colour (0xffc9d1d9);
    }
};

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.5f)); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (12.5f, juce::Font::bold)); }
};

class PadGrid final : public juce::Component
{
public:
    explicit PadGrid (NeoBeatProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int padAt (juce::Point<int> pos) const;
    void tick();
    std::function<void (int)> onSelect;
    std::function<void (int)> onMenu;

private:
    juce::Rectangle<float> padBounds (int pad) const;
    NeoBeatProcessor& proc;
    std::array<uint32_t, 16> lastHits {};
    std::array<float, 16> flash {};
};

class StepGrid final : public juce::Component
{
public:
    explicit StepGrid (NeoBeatProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick() { const int s = proc.playStep.load(); if (s != lastStep) { lastStep = s; repaint(); } }
    std::function<void (int)> onSelectPad;
    static constexpr int kLabelWidth = 118;

private:
    bool cellAt (juce::Point<int> pos, int& track, int& step) const;
    void stepMenu (int track, int step);
    NeoBeatProcessor& proc;
    int lastStep = -1;
    int dragMode = 0;   // 1 = painting hits, -1 = erasing
    uint8_t dragVel = 100;
};

class Spectrum3D final : public juce::Component
{
public:
    explicit Spectrum3D (NeoBeatProcessor& p);
    void paint (juce::Graphics&) override;
    void tick();

private:
    static constexpr int kOrder = 11, kSize = 1 << kOrder, kBins = 72, kFrames = 44;
    NeoBeatProcessor& proc;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> ring = std::vector<float> (kSize, 0.0f), work = std::vector<float> (2 * kSize, 0.0f), window = std::vector<float> (kSize, 0.0f);
    int ringPos = 0;
    std::array<std::array<float, kBins>, kFrames> history {};
    int newest = 0;
};
} // namespace ui

class NeoBeatEditor final : public juce::AudioProcessorEditor, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    explicit NeoBeatEditor (NeoBeatProcessor&);
    ~NeoBeatEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void selectPad (int pad);
    void padMenu (int pad);
    void chooseSample (int pad);
    void refreshSoundBox();

    NeoBeatProcessor& proc;
    ui::Look look;
    ui::PadGrid pads;
    ui::StepGrid steps;
    ui::Spectrum3D spectrum;

    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, playButton { "PLAY" };
    juce::ComboBox presetBox, kitBox, modeBox, soundBox, lengthBox;
    juce::Slider swing, master, tempo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, masterAtt, tempoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAtt;

    static constexpr int kKnobs = 8;
    std::array<juce::Slider, kKnobs> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kKnobs> knobAtt;
    juce::ComboBox chokeBox;
    juce::TextButton muteButton { "MUTE" }, sampleButton { "LOAD SAMPLE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chokeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    int selected = 0, shownSound = -1, shownPreset = -1, shownKit = -1;
    uint32_t lastVersion = 0;
    bool shownSample = false;
    juce::Rectangle<int> headerArea, padPanel, editPanel, meshPanel, gridPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeoBeatEditor)
};
