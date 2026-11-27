#pragma once

// A preset bar: the name of what is loaded, an arrow either side, and a list that auditions as you
// move down it.
//
// The point is that trying presets should cost one keypress, not a trip through a file dialog. The
// arrows step to the next or previous preset in the folder and load it there and then, so you can
// hold one down and hear the whole folder go past. The list does the same on hover: moving over a
// row loads and plays it, and moving off without clicking puts back whatever was loaded before, so
// browsing is free - you cannot lose the thing you had by looking at something else.
//
// That last part is what makes hover audition safe rather than annoying, and it is the only part
// with any subtlety: the bar takes a snapshot of the room before the first hover of a visit, and
// restores it unless a click commits. The snapshot is the room's own serialiser, so there is no
// second definition of "what the room holds" to drift out of step.

#include "../Ui.h"
#include <functional>
#include <memory>

namespace bc
{

class PresetBar final : public juce::Component,
                        private juce::Timer
{
public:
    PresetBar();
    ~PresetBar() override;

    // Where the presets are and what they are called, e.g. ("*.njqi"). Rescans.
    void setFolder (const juce::File& dir, const juce::String& wildcard);
    void rescan();

    // Load one for real. Return false and the bar says so rather than pretending.
    std::function<bool (const juce::File&)> onLoad;

    // Or a list of named items that are not files: NeoBeat's built-in patterns, say. Loading one
    // calls onLoadItem with its index instead of onLoad. Everything else - the arrows, the hover
    // audition, the snapshot that makes browsing free - works the same either way.
    void setItems (const juce::StringArray& itemNames, int currentIndex);
    std::function<bool (int)> onLoadItem;

    // Which one is loaded, when something other than the bar loaded it. -1: none of them.
    void setCurrent (int index);

    // Remember / put back what is loaded now, so hovering cannot lose it. Both are required for
    // hover audition; without them the bar still steps and loads, it just does not preview.
    std::function<std::unique_ptr<juce::XmlElement>()> onSnapshot;
    std::function<void (const juce::XmlElement&)> onRestore;

    // How long an audition plays before it stops, in ms. Long enough to hear a loop or a chord by
    // default; a plugin whose presets are patterns sets about a bar.
    int auditionMs = 1200;

    // Make a sound, now that something is loaded (play the loop, strike a chord).
    std::function<void()> onAudition;
    std::function<void()> onAuditionStop;

    void step (int by);                       // -1 previous, +1 next
    juce::String currentName() const;
    int count() const noexcept { return names.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class List;

    void openList();
    void closeList (bool commit);
    void hoverRow (int row);                  // -1: nothing
    bool loadIndex (int i);
    void timerCallback() override;

    juce::File folder;
    juce::String pattern { "*" };
    juce::Array<juce::File> files;
    juce::StringArray names;                  // what the list shows: the files' names, or the items
    bool itemMode = false;
    int current = -1;
    juce::String message;                     // shown instead of the name when something went wrong

    nsui::Pill prev { "<" }, next { ">" };
    std::unique_ptr<List> list;
    std::unique_ptr<juce::XmlElement> before;  // the room as it was when browsing began
    int hovered = -1;
    bool auditioning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace bc
