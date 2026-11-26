#include "JackRoom.h"

using namespace nsui;

namespace
{
const int kDivisions[] = { 4, 6, 8, 12 };

int divisionIndex (int div)
{
    for (int i = 0; i < 4; ++i) if (kDivisions[i] == div) return i;
    return 1;                                   // sixteenth triplets, the default
}
} // namespace

bc::JackRoom::JackRoom (nsr::Chaos& e, std::function<double()> bt)
    : chaos (e), beats (std::move (bt))
{
    const auto& p = chaos.getParams();

    kDensity.setValue (p.density);
    kExtremity.setValue (p.extremity);
    kRepeat.setValue (p.repeatProb);
    kSwing.setValue (p.swing);
    kSelf.setValue (p.selfRandomise);
    kRecovery.setValue (p.recoveryBars);
    kWindow.setValue ((float) p.windowSteps);
    kWindow.step = 1.0f;

    kDensity.setTooltip ("How often the lattice fires at all.");
    kExtremity.setTooltip ("How far each seizure goes when it does.");
    kRepeat.setTooltip ("Whether the next window re-rolls or repeats. High is what makes a figure "
                        "persist long enough to hear as an idea rather than as noise.");
    kSwing.setTooltip ("The lattice shuffle. 0.5 is straight.");
    kSelf.setTooltip ("Roll its own settings when it is switched on, at this depth. It never "
                      "reaches this control itself.");
    kRecovery.setTooltip ("How it lands back on release: zero is a hard cut, above that it glides "
                          "home over this many bars.");
    kWindow.setTooltip ("Lattice steps per window. A window is what shares a seed.");
    kWindow.toText = [] (float v) { return juce::String ((int) std::lround (v)) + " steps"; };
    kRecovery.toText = [] (float v)
    {
        return v < 0.01f ? juce::String ("hard cut") : juce::String (v, 2) + " bars";
    };

    for (auto* k : { &kDensity, &kExtremity, &kRepeat, &kSwing, &kSelf, &kRecovery, &kWindow })
    {
        k->onChange = [this] (float) { push(); };
        k->onCommit = [this] { push(); };
        addAndMakeVisible (*k);
    }

    segDivision.setSelected (divisionIndex (p.division));
    segDivision.setTooltip ("The lattice. Events can only land on it; randomness decides whether "
                            "and how much, never where. 1/6 is sixteenth triplets.");
    segDivision.onSelect = [this] (int i) { push(); juce::ignoreUnused (i); };
    addAndMakeVisible (segDivision);

    for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i)
    {
        auto g = std::make_unique<Gem> (nsr::chaosAxisName ((nsr::ChaosAxis) i));
        g->setToggleState (p.axis[i], juce::dontSendNotification);
        g->onClick = [this] { push(); };
        addAndMakeVisible (*g);
        axisGems[(size_t) i] = std::move (g);
    }

    pillOn.setTooltip ("Held: chaos while it is on, the original pattern back the moment it is "
                       "off. The written pattern is never altered, so there is nothing to undo.");
    pillOn.onClick = [this]
    {
        const double b = beats ? beats() : 0.0;
        if (pillOn.getToggleState()) chaos.engage (b);
        else                         chaos.release (b);
        repaint();
    };
    addAndMakeVisible (pillOn);

    pillRoll.setTooltip ("One press, every parameter once. For auditioning: press, listen, press "
                         "again. Nothing is restored — this one is a destination, not a visit.");
    pillRoll.onClick = [this] { chaos.rollOnce(); repaint(); };
    addAndMakeVisible (pillRoll);

    pillPanic.setTooltip ("Put everything back now, whatever state it is in.");
    pillPanic.onClick = [this]
    {
        chaos.restore();
        pillOn.setToggleState (false, juce::dontSendNotification);
        repaint();
    };
    addAndMakeVisible (pillPanic);

    startTimerHz (12);
}

bc::JackRoom::~JackRoom() { stopTimer(); }

void bc::JackRoom::refresh() { repaint(); }

void bc::JackRoom::timerCallback()
{
    // only redraw while it is doing something
    if (chaos.engaged() || chaos.isRecovering()) repaint();
}

void bc::JackRoom::push()
{
    auto p = chaos.getParams();
    p.density = kDensity.getValue();
    p.extremity = kExtremity.getValue();
    p.repeatProb = kRepeat.getValue();
    p.swing = kSwing.getValue();
    p.selfRandomise = kSelf.getValue();
    p.recoveryBars = kRecovery.getValue();
    p.windowSteps = (int) std::lround (kWindow.getValue());
    p.division = kDivisions[juce::jlimit (0, 3, segDivision.getSelected())];
    for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i)
        if (axisGems[(size_t) i] != nullptr) p.axis[i] = axisGems[(size_t) i]->getToggleState();
    chaos.setParams (p);
    repaint();
}

void bc::JackRoom::resized()
{
    plate = { 12, 8, 1256, 720 };

    const int x = plate.getX() + 16;
    int y = 46;

    pillOn.setBounds (x, y, 92, 34);
    pillRoll.setBounds (x + 100, y, 82, 34);
    pillPanic.setBounds (x + 190, y, 100, 34);
    segDivision.setBounds (x + 320, y + 4, 220, 26);

    y += 56;
    const int kw = 118;
    kDensity.setBounds (x, y, kw, 88);
    kExtremity.setBounds (x + kw, y, kw, 88);
    kRepeat.setBounds (x + kw * 2, y, kw, 88);
    kSwing.setBounds (x + kw * 3, y, kw, 88);
    kWindow.setBounds (x + kw * 4, y, kw, 88);
    kSelf.setBounds (x + kw * 5, y, kw, 88);
    kRecovery.setBounds (x + kw * 6, y, kw, 88);

    y += 104;
    yAxesLabel = y;
    y += 16;
    for (int i = 0; i < (int) nsr::ChaosAxis::Count; ++i)
        if (axisGems[(size_t) i] != nullptr)
            axisGems[(size_t) i]->setBounds (x + i * 96, y, 88, 26);

    y += 40;
    yTargetsLabel = y;
    y += 16;
    listBox = { x, y, plate.getWidth() - 32, plate.getBottom() - y - 16 };
}

void bc::JackRoom::paint (juce::Graphics& g)
{
    paintChassis (g, getLocalBounds().toFloat());
    paintPlate (g, plate.toFloat());

    g.setFont (caps (10.0f));
    g.setColour (C::text());
    g.drawText ("IDM JACK", plate.getX() + 16, 18, 240, 14, juce::Justification::centredLeft);

    // what it is doing, in words, because a panel this plain has nothing else to say it
    juce::String state = "idle";
    juce::Colour col = C::dim();
    if (chaos.engaged())          { state = "HELD, seizing"; col = C::amber(); }
    else if (chaos.isRecovering()) { state = "recovering"; col = C::teal(); }
    g.setFont (font (10.0f, true));
    g.setColour (col);
    g.drawText (state, plate.getRight() - 260, 18, 244, 14, juce::Justification::centredRight);

    paintLabel (g, "LATTICE", { plate.getX() + 336, 32, 200, 12 });
    paintLabel (g, "AXES - what it may seize", { plate.getX() + 16, yAxesLabel, 300, 12 });
    paintLabel (g, "TARGETS - click to include or exclude", { plate.getX() + 16, yTargetsLabel, 400, 12 });

    paintTargets (g, listBox.toFloat());
}

void bc::JackRoom::paintTargets (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintWell (g, r);
    const auto& list = chaos.allTargets();
    if (list.empty())
    {
        g.setFont (font (10.0f));
        g.setColour (C::dim());
        g.drawText ("Nothing registered. The host plugin registers its own parameters.",
                    r, juce::Justification::centred);
        return;
    }

    const auto in = r.reduced (10.0f, 8.0f);
    const float rowH = 22.0f;
    const int perCol = std::max (1, (int) (in.getHeight() / rowH));
    const float colW = in.getWidth() / (float) std::max (1, ((int) list.size() + perCol - 1) / perCol);

    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& t = list[i];
        const int col = (int) i / perCol, row = (int) i % perCol;
        const float cx = in.getX() + (float) col * colW;
        const float cy = in.getY() + (float) row * rowH;

        // a never-target is drawn struck through, because it is not a setting anyone can change
        const bool blocked = t.never;
        g.setColour (blocked ? C::dim().withAlpha (0.5f)
                             : (t.on ? C::teal() : C::dim()));
        g.fillRoundedRectangle (cx, cy + 7.0f, 7.0f, 7.0f, 1.5f);

        g.setFont (font (9.5f));
        g.setColour (blocked ? C::dim().withAlpha (0.6f) : (t.on ? C::text() : C::dim()));
        const juce::String label = juce::String (t.room) + " · " + juce::String (t.name)
                                 + (blocked ? juce::String ("   never")
                                            : "   " + juce::String ((int) (t.reach * 100.0f)) + "%");
        g.drawText (label, (int) (cx + 14.0f), (int) cy, (int) colW - 20, (int) rowH,
                    juce::Justification::centredLeft);

        if (blocked)
        {
            // struck through across the label's column rather than measured: JUCE 8 moved string
            // measurement off Font, and the exact width is not worth a GlyphArrangement here
            g.drawLine (cx + 14.0f, cy + rowH * 0.5f, cx + colW - 24.0f, cy + rowH * 0.5f, 0.8f);
        }
    }
}

int bc::JackRoom::targetAt (juce::Point<float> p) const
{
    if (! listBox.toFloat().contains (p)) return -1;
    const auto in = listBox.toFloat().reduced (10.0f, 8.0f);
    const auto& list = chaos.allTargets();
    if (list.empty()) return -1;
    const float rowH = 22.0f;
    const int perCol = std::max (1, (int) (in.getHeight() / rowH));
    const float colW = in.getWidth() / (float) std::max (1, ((int) list.size() + perCol - 1) / perCol);
    const int col = (int) ((p.x - in.getX()) / colW);
    const int row = (int) ((p.y - in.getY()) / rowH);
    const int idx = col * perCol + row;
    return idx >= 0 && idx < (int) list.size() ? idx : -1;
}

void bc::JackRoom::mouseDown (const juce::MouseEvent& e)
{
    const int i = targetAt (e.position);
    if (i < 0) return;
    auto& t = chaos.allTargets()[(size_t) i];
    if (t.never) return;                       // not a setting: a mono lock stays locked
    t.on = ! t.on;
    repaint();
}
