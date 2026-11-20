#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../neobeat/shared/rooms/RoomView.h"
#include "../neobeat/shared/StereoRoom.h"

// The Stereoroom: one stage, with everything in the preset standing somewhere on it.
//
// What stands on it is not one object per loop but one per chop, because that is where the
// interesting part is - a loop can play while its third chop travels ear to ear and the rest holds
// still. The engine behind this is nsr::, which was written and measured first: equal-power
// placement that does not change loudness as it moves, mid/side width that survives a mono fold,
// and a mono lock that pins an element to the middle whatever its path says.

class StereoRoomUi final : public juce::Component
{
public:
    explicit StereoRoomUi (bc::RoomView& v);
    ~StereoRoomUi() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void tick();                              // the stage moves while the song plays
    bool key (const juce::KeyPress&);

    // one thing standing on the stage
    struct Node
    {
        juce::String name;
        int owner = -1;                       // the loop layer it belongs to, or -1 for a voice
        int chop = -1;                        // which chop of it, or -1
        bool lowEnd = false;                  // most of its energy is low: locked by default
        juce::Colour colour;
    };

private:
    struct Parts;
    bc::RoomView& view;
    std::unique_ptr<Parts> parts;

    void rebuildNodes();                      // from the preset, with nothing to set up
    void selectAt (juce::Point<int> p);
    int nodeAt (juce::Point<int> p) const;
    juce::Point<float> placeOf (int node) const;
    juce::Rectangle<int> stageArea() const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};
