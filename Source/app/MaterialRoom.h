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
//
// And a way in for any sound: drop an audio file here, or press Fit with a pool sound chosen, and
// Batchcut learns the object that made it - the learner's own fit, four times side by side. Every
// fit becomes a material in its own file and a sound in the pool, closest first, and the room opens
// them when they are done.
//
// And breeding (7b): Wander makes eight variants of the loaded material and the fitted sound - the
// material moved and crossed, the sound stretched, smeared, granulated and sent through it; Grow climbs
// toward the sound from what is loaded or kept, generation after generation; Scratch makes eight
// materials from nothing, one of each shape (bar, plate, drum skin, tube, closed tube, bell, metal
// junk, rubber), REACH setting how wild. They appear in a strip
// along the bottom of the picture: click one to hear it, Keep the ones to breed from next. Only a kept
// child goes into the pool.

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
    bool isAudio (const juce::File& f) const;
    void fitFinished();

    BcProcessor& proc;
    std::function<void()> onChanged;

    // the materials found in the folder
    juce::Array<juce::File> files;
    int current = -1;
    juce::File folder;

    bcui::Pill folderBtn { "Folder..." }, strike { "Strike" }, render { "Render 8 hits to pool" };
    bcui::Pill through { "Send through" }, prev { "<" }, next { ">" };
    bcui::Pill fit { "Fit" }, cancel { "Cancel" };
    int seenGeneration = 0;

    // breeding
    bcui::Pill wander { "Wander" }, grow { "Grow" }, scratch { "Scratch" };
    juce::Slider reachKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label reachLabel;
    int seenBreed = 0;
    void breedFinished();
public:
    // the generation's eight tiles, for the tests: tile i spans an eighth of the width (inset 3, 2), and
    // its Keep button is the 44 x 18 at the bottom right of the tile inset by 5 more
    juce::Component& getStrip() noexcept;
private:
    struct Strip;
    std::unique_ptr<Strip> strip;
    juce::ListBox list { "materials", nullptr };
    juce::ComboBox poolBox;
    juce::Slider mixKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label message;

    struct Rows;
    std::unique_ptr<Rows> rows;
    bool dropping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialRoom)
};
