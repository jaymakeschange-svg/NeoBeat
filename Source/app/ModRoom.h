#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../neobeat/shared/rooms/RoomView.h"

// MOD: the Orb, the shapes that move things, and the list of where they go.
//
// The Orb lives here rather than in KEYS, because it is modulation - a position that reads out of
// eight corners at once. The randomiser is in this list twice over: as a source, and as a set of
// destinations, so a slow shape can open and close how wild it gets. It cannot route into itself.

class ModRoom final : public juce::Component
{
public:
    explicit ModRoom (bc::RoomView& v);
    ~ModRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    struct Parts;
    bc::RoomView& view;
    std::unique_ptr<Parts> parts;

    juce::Rectangle<int> orbArea() const;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
};
