// BANDS - the spectral delay room.
//
// Sixteen bandpasses, each with its own delay, its own feedback, and its own place on the stage.
// What that buys over a broadband delay is that a sound comes apart as it repeats: the top of the
// spectrum can circle while the bottom stays locked in the middle, so the repeats arrive in
// different places rather than travelling as one object.
//
// The room takes a BandsState by reference and nothing else. The other rooms in the family take a
// RoomView, because they need the pool, the session and the selection - a KIT room is about
// content. BANDS is about an effect, and an effect room needs the parameters and a way to say they
// changed. Keeping the contract that narrow is what lets Batchcut, MidiJaQ, JaqRack and Bounce
// Delay all host the same room without any of the Rooms machinery.
//
// Every per-band number shown here comes from nsr::bandPlan, which is the arithmetic setParams
// itself runs. A display that worked the numbers out for itself would be free to disagree with
// what you hear - above all on feedback, which the engine trims when the neighbour feed is up by
// an amount no interface could arrive at on its own.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Bands.h"
#include "../Ui.h"

#include <functional>

namespace bc
{

// Read off the enum rather than typed out, so adding a path to nsr::Travel does not leave two
// pickers in this room quietly offering the old four.
inline juce::StringArray travelNames()
{
    juce::StringArray a;
    for (int i = 0; i < (int) nsr::Travel::Count; ++i)
        a.add (nsr::travelName ((nsr::Travel) i));
    return a;
}

class BandsRoom final : public juce::Component,
                        private juce::Timer
{
public:
    // changed: the parameters moved and the engine should be told.
    // beats: the transport, so a travelling band is drawn where it actually is. Optional - without
    //        it the stage draws each band's written position and its path, which is what you want
    //        when nothing is playing anyway.
    BandsRoom (BandsState& state,
               std::function<void()> changed,
               std::function<double()> beats = {});
    ~BandsRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // the band whose detail the right-hand panel is showing
    void select (int band);
    int selected() const noexcept { return sel; }

    // Call after anything changes the state from outside, so the controls and the displays catch
    // up. Cheap: it reads the state and sets widget values, it does not touch audio.
    void refresh();

    static constexpr int kBands = BandsState::kBands;

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // the three displays
    void paintStage (juce::Graphics&, juce::Rectangle<float>);
    void paintRings (juce::Graphics&, juce::Rectangle<float>);
    void paintDetail (juce::Graphics&, juce::Rectangle<float>);

    // geometry, so the mouse and the paint agree about where a band is
    juce::Rectangle<float> stageArea() const;
    juce::Rectangle<float> ringsArea() const;
    juce::Point<float> stagePoint (float x, float y) const;     // place coords -> pixels
    int bandAtStage (juce::Point<float>) const;                 // -1 for none
    int bandAtRings (juce::Point<float>) const;

    void pushParams();      // controls -> state.dsp, then changed()
    void pullParams();      // state.dsp -> controls

    BandsState& state;
    std::function<void()> changed;
    std::function<double()> beats;

    int sel = 8;            // a band in the middle, so the detail panel opens on something typical
    int dragging = -1;
    bool dragFromStage = false;

    // The pairs. Each of these is two knobs under one heading: the value at the bottom band and
    // the value at the top, with everything between interpolated. It is the whole reason the
    // engine has sixteen of something and only fourteen controls for it.
    struct Pair
    {
        juce::String name;
        std::unique_ptr<nsui::Knob> start, finish;
    };
    std::vector<Pair> pairs;

    // The master time control, and the fix for Chimecho's one real usability failure. Start and
    // Finish set the shape across the spectrum; this scales the whole bank, and opens at the short
    // end so the bubbly register is where you begin rather than where you have to fight back to.
    nsui::Knob kLength { {}, 0.0f, 1.0f, 0.0f };   // the heading is painted; the knob needs no second one

    // the interaural split and its drift: one idea, so they sit together
    nsui::Knob kInterStart { "Start", 0.0f, nsr::kInterMaxMs, 0.0f };
    nsui::Knob kInterFinish { "Finish", 0.0f, nsr::kInterMaxMs, 0.0f };
    nsui::Knob kInterLfoHz { "Drift", 0.0f, 4.0f, 0.23f, true };
    nsui::Knob kInterLfoAmt { "Amount", 0.0f, 1.0f, 0.5f };

    // cross-band and mix
    nsui::Knob kQ { "Q", 0.5f, 12.0f, 2.6f, true };
    nsui::Knob kSideOffset { "Offset", -15.0f, 15.0f, 0.0f };
    nsui::Knob kSideFeedback { "Side", 0.0f, 0.95f, 0.0f };
    nsui::Knob kDry { "Dry", 0.0f, 1.0f, 1.0f };
    nsui::Knob kWet { "Wet", 0.0f, 1.0f, 0.8f };

    // the selected band's place
    nsui::Knob kWidth { "Width", 0.0f, 1.0f, 0.0f };
    nsui::Knob kBars { "Bars", 0.25f, 32.0f, 4.0f, true };
    nsui::Knob kDepth { "Depth", 0.0f, 1.0f, 0.0f };
    nsui::Knob kPhase { "Phase", 0.0f, 1.0f, 0.0f };
    nsui::Segmented segTravel { travelNames() };
    nsui::Gem gemLocked { "Locked" };

    // the fan, which is the only way anyone will actually set sixteen places
    nsui::Knob kFan { "Spread", 0.0f, 1.0f, 1.0f };
    nsui::Pill pillFan { "FAN" };
    nsui::Pill pillAlternate { "ALTERNATE", true };
    nsui::Segmented segTravelAll { travelNames() };
    nsui::Gem gemPlaced { "Placed" };

    juce::Rectangle<int> leftPlate, mainPlate, rightPlate;

    // Where the right-hand headings and the readout go. Worked out once in resized() and read by
    // paint(), because the first version had the two adding up the same offsets separately and
    // they disagreed: the readout ended up underneath the travel row.
    int yHeadSplit = 0, yHeadCross = 0, yHeadMix = 0, yHeadBand = 0;
    juce::Rectangle<int> detailBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandsRoom)
};

} // namespace bc
