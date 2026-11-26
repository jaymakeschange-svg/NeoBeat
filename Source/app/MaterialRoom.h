#pragma once

// MATERIAL: the learner's objects, played.
//
// Deliberately plain. Three things you can do, in the order you would do them:
//
//   1. open a material    the learner's .json for a clank, a pan, a PVC pipe
//   2. strike it          hear the object, hit at a different strength each time
//   3. send sound through it   any pool sound, heard in that material
//
// and one button that puts what you made into the pool, where the rest of Batchcut can reach it.

#include "Ui.h"
#include "Processor.h"
#include <memory>

class MaterialRoom final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    MaterialRoom (BcProcessor& p, std::function<void()> changed);
    ~MaterialRoom() override;

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }

private:
    void timerCallback() override;
    void openFolder (const juce::File& dir);
    void loadRow (int row);
    void say (const juce::String& s);

    BcProcessor& proc;
    std::function<void()> onChanged;

    // the materials found in the folder
    juce::Array<juce::File> files;
    int current = -1;
    juce::File folder;

    bcui::Pill folderBtn { "Folder..." }, strike { "Strike" }, render { "Render 8 hits to pool" };
    bcui::Pill through { "Send through" }, prev { "<" }, next { ">" };
    juce::ListBox list { "materials", nullptr };
    juce::ComboBox poolBox;
    juce::Slider mixKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label message;

    struct Rows;
    std::unique_ptr<Rows> rows;
    bool dropping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialRoom)
};
