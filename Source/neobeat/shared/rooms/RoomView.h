#pragma once

// What the three rooms' interfaces need from the editor around them, and nothing else.
//
// Batchcut's View grew to hold everything on screen: the spectrogram's visible stretch, the
// selection, the drawing tools, the undo history of an edit. The KIT, LOOPS and KEYS rooms use
// almost none of that. Between them they touch the rooms themselves, the open part, and six
// numbers saying what is selected where.
//
// So that much lives here and both plugins' editors inherit it. Batchcut's View keeps the
// spectrogram half, which is its own; MidiJaQ's editor adds whatever its new rooms need.

#include "Rooms.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace bc
{

struct RoomView
{
    explicit RoomView (Rooms& r) : rooms (r) {}
    virtual ~RoomView() = default;

    Rooms& rooms;
    std::shared_ptr<const Doc> doc;       // the open part

    int room = 0;                         // which room is showing
    int kitLane = 0, kitLayer = 0, kitItem = -1;   // what is selected in the kit room
    int loopLayer = 0, loopHit = -1;      // what is selected in LOOPS
    int keysZone = -1;                    // what is selected in KEYS

    std::function<void()> repaintAll;     // something on screen changed
    std::function<void()> refreshPanels;  // the panels show something that changed
    std::function<void()> roomChanged;    // a different room is showing
};

// ---------------------------------------------------------------- KIT (KitRoom.cpp)
// A NeoBeat kit out of the one-shots: the pool on the left, the 16 lanes in the middle (auditioned inside
// one of NeoBeat's 128 patterns), the selected lane's layers on the right.
class KitRoom final : public juce::Component, private juce::Timer
{
public:
    explicit KitRoom (RoomView& v);
    ~KitRoom() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();
    bool key (const juce::KeyPress&);
private:
    void timerCallback() override;
    struct Parts;
    RoomView& view;
    std::unique_ptr<Parts> parts;
    int seenVersion = -1, seenStep = -2;
    bool wasBusy = false;
};

// ---------------------------------------------------------------- LOOPS (LoopsRoom.cpp)
// The light sample sequencer: where the sounds come from on the left, the layers of chops in the middle,
// the selected layer and chop on the right.
class LoopsRoom final : public juce::Component, private juce::Timer
{
public:
    explicit LoopsRoom (RoomView& v);
    ~LoopsRoom() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();
    bool key (const juce::KeyPress&);
    juce::Component& grid();                // for the tests
private:
    void timerCallback() override;
    struct Parts;
    RoomView& view;
    std::unique_ptr<Parts> parts;
    int seenVersion = -1, seenPool = -1;
    double seenBeat = -2.0;
};

// ---------------------------------------------------------------- KEYS (KeysRoom.cpp)
// Sounds made into an instrument: where they come from on the left, the keyboard map and the keys to play in
// the middle, how it plays on the right.
class KeysRoom final : public juce::Component, private juce::Timer
{
public:
    explicit KeysRoom (RoomView& v);
    ~KeysRoom() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool key (const juce::KeyPress&);
    bool keyState (bool isKeyDown);                 // the computer keyboard as keys (A to K: C3 to C4)
    void letGo();                                   // every key up
    juce::Component& map();                 // for the tests
private:
    void timerCallback() override;
    struct Parts;
    RoomView& view;
    std::unique_ptr<Parts> parts;
    int seenVersion = -1, seenPool = -1;
};

} // namespace bc
