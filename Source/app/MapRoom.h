#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../neobeat/shared/rooms/RoomView.h"
#include "../core/Map.h"

// MAP: one grid, two kinds of mark.
//
// Up the side are degrees of the scale, not drum lanes, so a keys mark dragged upward moves
// through the key and never out of it. A loop mark on the same row means a chop instead, because
// each mark carries its own source. Underneath are the modulation lanes, so a phrase can be drawn
// and shaped without leaving the room.

class MapRoom final : public juce::Component
{
public:
    explicit MapRoom (bc::RoomView& v);
    ~MapRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void tick();                       // the playhead moves
    bool key (const juce::KeyPress&);

private:
    struct Parts;
    bc::RoomView& view;
    std::unique_ptr<Parts> parts;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> cellAt (int step, int row) const;
    bool cellFor (juce::Point<int> p, int& step, int& row) const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};
