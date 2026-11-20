#include "RandomRoom.h"
#include "Processor.h"
#include "../neobeat/shared/Ui.h"

using namespace nsui;

struct RandomRoom::Parts
{
    Pill roll { "ROLL" }, hold { "HOLD" };
    Gem onKey { "On a keypress" }, onGrid { "On the grid" }, free { "Free running" }, onAsk { "Only when I ask" };
    Knob every { "EVERY", 1.0f, 16.0f, 4.0f };
    Knob amount { "AMOUNT", 0.0f, 1.0f, 0.45f };
    juce::Label hint { {}, "" };
    int flash = 0;
};

RandomRoom::RandomRoom (bc::RoomView& v) : view (v)
{
    parts = std::make_unique<Parts>();
    auto& q = *parts;

    for (auto* b : { &q.roll, &q.hold }) addAndMakeVisible (b);
    for (auto* gm : { &q.onKey, &q.onGrid, &q.free, &q.onAsk }) addAndMakeVisible (gm);
    addAndMakeVisible (q.every);
    addAndMakeVisible (q.amount);
    addAndMakeVisible (q.hint);

    q.roll.setTooltip ("Roll everything on the list, now.");
    q.roll.onClick = [this]
    {
        static_cast<MjqProcessor&> (view.rooms).rollRandom();
        parts->flash = 6;
        refresh();
    };

    q.hold.setTooltip ("Freezes what you are hearing, so the next keypress will not take it away. "
                       "Without this a good accident is gone before you can keep it.");
    q.hold.onClick = [this]
    {
        static_cast<MjqProcessor&> (view.rooms).holdRandom();
        refresh();
    };

    auto trigger = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        auto& q2 = *parts;
        p.randomOnKey  = q2.onKey.getToggleState();
        p.randomOnGrid = q2.onGrid.getToggleState();
        p.randomFree   = q2.free.getToggleState();
        p.randomEvery  = juce::jlimit (1, 16, (int) q2.every.getValue());
        p.randomAmount = q2.amount.getValue();
        p.stateChanged();
    };
    for (auto* gm : { &q.onKey, &q.onGrid, &q.free }) gm->onClick = trigger;
    q.onAsk.onClick = [this, trigger]
    {
        auto& q2 = *parts;
        if (q2.onAsk.getToggleState())
            for (auto* gm : { &q2.onKey, &q2.onGrid, &q2.free }) gm->setToggleState (false, juce::dontSendNotification);
        trigger();
        refresh();
    };
    q.every.onCommit = [trigger] { trigger(); };
    q.amount.onCommit = [trigger] { trigger(); };

    q.hint.setJustificationType (juce::Justification::topLeft);
    startTimerHz (12);
}

RandomRoom::~RandomRoom() { stopTimer(); }

void RandomRoom::timerCallback()
{
    if (parts->flash > 0) { --parts->flash; repaint(); }
}

void RandomRoom::refresh()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;
    q.onKey.setToggleState (p.randomOnKey, juce::dontSendNotification);
    q.onGrid.setToggleState (p.randomOnGrid, juce::dontSendNotification);
    q.free.setToggleState (p.randomFree, juce::dontSendNotification);
    q.onAsk.setToggleState (! (p.randomOnKey || p.randomOnGrid || p.randomFree), juce::dontSendNotification);
    q.every.setValue ((float) p.randomEvery);
    q.amount.setValue (p.randomAmount);
    q.hint.setText (p.heldRolls() > 0
                      ? juce::String (p.heldRolls()) + " rolls held, ready to drop into a preset."
                      : juce::String ("Nothing held yet. HOLD keeps what you are hearing."),
                    juce::dontSendNotification);
    repaint();
}

bool RandomRoom::key (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::spaceKey)) { parts->roll.triggerClick(); return true; }
    if (k.isKeyCode ('H')) { parts->hold.triggerClick(); return true; }
    return false;
}

void RandomRoom::mouseDown (const juce::MouseEvent& e)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto body = getLocalBounds().reduced (10);
    body.removeFromLeft (300 + 10);
    auto list = body.reduced (12);
    list.removeFromTop (18 + 8);

    const int row = (e.y - list.getY()) / 26;
    if (row < 0 || row >= (int) p.randomTargets().size()) return;
    p.toggleRandomTarget (row);
    refresh();
}

void RandomRoom::paint (juce::Graphics& g)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;
    paintPlate (g, getLocalBounds().toFloat());

    auto r = getLocalBounds().reduced (10);

    // ---- what sets it off
    auto left = r.removeFromLeft (300);
    paintWell (g, left.toFloat());
    {
        auto body = left.reduced (12);
        paintLabel (g, "WHAT SETS IT OFF", body.removeFromTop (18), juce::Justification::centredLeft, 11.0f, C::amber());
        body.removeFromTop (4 + 22 * 4 + 8 + 64 + 8);

        auto keep = body.removeFromTop (110);
        g.setColour (juce::Colour (0xff141c19));
        g.fillRoundedRectangle (keep.toFloat(), 6.0f);
        g.setColour (C::teal().withAlpha (0.55f));
        g.drawRoundedRectangle (keep.toFloat(), 6.0f, 1.0f);
        auto kb = keep.reduced (10);
        paintLabel (g, "KEEPING A ROLL", kb.removeFromTop (16), juce::Justification::centredLeft, 10.0f, C::teal());
        g.setColour (C::label());
        g.setFont (font (10.0f));
        g.drawFittedText ("HOLD freezes what you are hearing. The next keypress will not take it away.",
                          kb.removeFromTop (30), juce::Justification::topLeft, 2);
        kb.removeFromTop (4);
        auto slots = kb.removeFromTop (24);
        const int held = p.heldRolls();
        for (int i = 0; i < 8; ++i)
        {
            auto cell = slots.removeFromLeft (slots.getWidth() / (8 - i)).reduced (2, 0);
            const bool full = i < held;
            paintTile (g, cell.toFloat(), full ? C::teal().withAlpha (0.8f) : C::dim().withAlpha (0.12f),
                       juce::String (i + 1), false);
        }
    }

    // ---- what it may touch
    r.removeFromLeft (10);
    paintWell (g, r.toFloat());
    {
        auto body = r.reduced (12);
        auto head = body.removeFromTop (18);
        paintLabel (g, "WHAT IT MAY TOUCH", head.removeFromLeft (180), juce::Justification::centredLeft, 11.0f, C::amber());
        paintLabel (g, "reach says how far it may move each one", head, juce::Justification::centredRight, 10.0f, C::dim());
        body.removeFromTop (8);

        for (const auto& t : p.randomTargets())
        {
            if (body.getHeight() < 90) break;
            auto row = body.removeFromTop (26);
            const bool never = t.never;
            g.setColour (juce::Colour (0xff141c19));
            g.fillRoundedRectangle (row.toFloat().reduced (0, 2), 4.0f);
            if (never)
            {
                g.setColour (C::red().withAlpha (0.5f));
                g.drawRoundedRectangle (row.toFloat().reduced (0, 2), 4.0f, 1.0f);
            }
            auto in = row.reduced (9, 0);
            g.setColour (C::dim());
            g.setFont (caps (9.0f));
            g.drawText (t.room, in.removeFromLeft (84), juce::Justification::centredLeft, false);
            g.setColour (C::text());
            g.setFont (font (11.0f));
            g.drawText (t.name, in.removeFromLeft (150), juce::Justification::centredLeft, true);

            auto tag = in.removeFromRight (54);
            g.setColour (never ? C::red() : (t.on ? C::amber() : C::dim()));
            g.setFont (caps (8.0f));
            g.drawText (never ? "NEVER" : (t.on ? "ON" : "OFF"), tag, juce::Justification::centredRight, false);

            auto bar = in.removeFromRight (in.getWidth() - 10).reduced (0, 10);
            g.setColour (C::dim().withAlpha (0.25f));
            g.fillRoundedRectangle (bar.toFloat(), 2.5f);
            if (! never)
            {
                auto fill = bar.withWidth ((int) ((float) bar.getWidth() * t.reach));
                g.setColour (t.on ? C::amber() : C::dim().withAlpha (0.5f));
                g.fillRoundedRectangle (fill.toFloat(), 2.5f);
            }
        }

        auto warn = body.removeFromBottom (64);
        g.setColour (juce::Colour (0xff141c19));
        g.fillRoundedRectangle (warn.toFloat(), 6.0f);
        g.setColour (C::red().withAlpha (0.55f));
        g.drawRoundedRectangle (warn.toFloat(), 6.0f, 1.0f);
        auto wb = warn.reduced (10);
        auto dot = wb.removeFromLeft (18);
        g.setColour (C::red());
        g.fillEllipse (dot.removeFromTop (18).withSizeKeepingCentre (11, 11).toFloat());
        g.setColour (C::label());
        g.setFont (font (10.0f));
        g.drawFittedText ("Two things it will not touch, however the list is set: a mono lock, or anything holding "
                          "the bottom of the mix. It cannot target itself either.",
                          wb, juce::Justification::topLeft, 3);
    }

    if (q.flash > 0)
    {
        g.setColour (C::amber().withAlpha (0.05f * (float) q.flash));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    }
}

void RandomRoom::resized()
{
    auto& q = *parts;
    auto r = getLocalBounds().reduced (10);

    auto head = r.removeFromTop (0);
    juce::ignoreUnused (head);

    auto left = r.removeFromLeft (300).reduced (12);
    left.removeFromTop (18 + 4);
    for (auto* gm : { &q.onKey, &q.onGrid, &q.free, &q.onAsk })
    {
        gm->setBounds (left.removeFromTop (22));
    }
    left.removeFromTop (8);
    auto row = left.removeFromTop (64);
    q.every.setBounds (row.removeFromLeft (row.getWidth() / 2));
    q.amount.setBounds (row);

    left.removeFromTop (8 + 110 + 8);
    q.hint.setBounds (left.removeFromTop (34));

    // the two big buttons sit at the bottom of the left plate
    auto bottom = getLocalBounds().reduced (10).removeFromLeft (300).reduced (12).removeFromBottom (30);
    q.roll.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
    bottom.removeFromLeft (6);
    q.hold.setBounds (bottom);
}
