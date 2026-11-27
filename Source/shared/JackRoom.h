// IDM Jack — a plain panel, built to be played rather than looked at.
//
// Spec: docs/DECISIONS.md, "C — IDM Jack". Deliberately not a designed room: switch, roll, the
// seven settings, the five axes, and a list of what it is allowed to seize. Enough to test with.
// The room it eventually becomes can be drawn once it is known what the thing actually wants.
//
// Takes an nsr::Chaos by reference and nothing else, the same contract as BandsRoom, so any plugin
// can host it by registering its own parameters and handing the engine over.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Chaos.h"
#include "Ui.h"

#include <functional>

namespace bc
{

class JackRoom final : public juce::Component,
                       private juce::Timer
{
public:
    // beats: the transport. Jack needs it — the lattice is in musical time, not seconds — so
    // without it the panel can still be set up but ON does nothing.
    JackRoom (nsr::Chaos& engine, std::function<double()> beats = {});
    ~JackRoom() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Call when the registered target list changes, so the list redraws.
    void refresh();

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void push();
    void paintTargets (juce::Graphics&, juce::Rectangle<float>);
    int targetAt (juce::Point<float>) const;

    nsr::Chaos& chaos;
    std::function<double()> beats;

    nsui::Pill pillOn { "ON", true };          // held: chaos while on, original back on release
    nsui::Pill pillRoll { "ROLL" };            // the global randomiser: one press, everything once
    nsui::Pill pillPanic { "RESTORE" };

    nsui::Knob kDensity { "Density", 0.0f, 1.0f, 0.5f };
    nsui::Knob kExtremity { "Extremity", 0.0f, 1.0f, 0.6f };
    nsui::Knob kRepeat { "Repeat", 0.0f, 1.0f, 0.45f };
    nsui::Knob kSwing { "Swing", 0.5f, 0.75f, 0.58f };
    nsui::Knob kSelf { "Self", 0.0f, 1.0f, 0.0f };
    nsui::Knob kRecovery { "Recovery", 0.0f, 4.0f, 0.0f };
    nsui::Knob kWindow { "Window", 2.0f, 32.0f, 8.0f };

    nsui::Segmented segDivision { { "1/4", "1/6", "1/8", "1/12" } };
    std::array<std::unique_ptr<nsui::Gem>, (size_t) nsr::ChaosAxis::Count> axisGems;

    juce::Rectangle<int> plate, listBox;
    // Worked out in resized() and read by paint(). Hardcoding them in paint() put the AXES label
    // on top of the gems and the TARGETS label on top of that — the same fault BandsRoom had.
    int yAxesLabel = 0, yTargetsLabel = 0;
    int lastFired = 0;
    double lastBeat = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JackRoom)
};

} // namespace bc
