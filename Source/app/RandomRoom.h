#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../neobeat/shared/rooms/RoomView.h"

// RANDOM: one roll that reaches every room, and fires while you play.
//
// Not the same thing as the Random gem in KEYS and LOOPS, which is a one-shot you press while
// building. This is a running system: it has a list of what it may touch and how far it may move
// each one, and something that sets it off - a keypress, a number of steps, a rate of its own.
//
// It needs HOLD or it is useless. A randomiser that fires on every keypress will sooner or later
// produce something worth keeping and then destroy it on the next note, so HOLD freezes the roll
// you are hearing and drops it into a slot.
//
// Two things it will not touch however the list is set: a mono lock, or anything holding the
// bottom of the mix. Randomising those turns a mix inside out and has to be undone by hand. It
// cannot target itself either, or its own rate randomises its own rate.

class RandomRoom final : public juce::Component, private juce::Timer
{
public:
    explicit RandomRoom (bc::RoomView& v);
    ~RandomRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool key (const juce::KeyPress&);

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;

    struct Parts;
    bc::RoomView& view;
    std::unique_ptr<Parts> parts;
};
