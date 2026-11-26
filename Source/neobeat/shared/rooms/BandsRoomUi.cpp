#include "BandsRoom.h"

using namespace nsui;

namespace
{

// The stage, drawn as a plan from above, listener below the display: x runs -1 to +1 across,
// y runs 0 at the FRONT (the bottom edge, nearest) to 1 at the BACK (the top edge, furthest).
//
// Set down here because BANDS is the first room in the family to draw one, and a second room that
// put the front at the top would make the two impossible to read side by side.
constexpr float kStageInset = 16.0f;

juce::String hzText (float hz)
{
    if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 0) + "k";
    if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 1) + "k";
    return juce::String ((int) (hz + 0.5f));
}

juce::String msText (float ms)
{
    if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
    if (ms >= 100.0f)  return juce::String ((int) (ms + 0.5f)) + " ms";
    return juce::String (ms, 1) + " ms";
}

// Low bands toward the accent, high bands toward the warning colour, so the spectrum reads as a
// ramp and a skin still gets to choose what the two ends look like.
juce::Colour bandColour (int b, int bands)
{
    const float t = bands > 1 ? (float) b / (float) (bands - 1) : 0.0f;
    return C::teal().interpolatedWith (C::amber(), t);
}

// How many repeats are worth drawing, and how long the band therefore rings for. Stopping at -34 dB
// rather than at silence: below that it is in the noise of anything else playing.
int audibleRepeats (float level, float feedback)
{
    const float floorAmp = 0.02f;
    if (level <= floorAmp) return 0;
    const float fb = std::abs (feedback);
    if (fb <= 0.02f) return 1;
    const int n = 1 + (int) std::ceil (std::log (floorAmp / level) / std::log (fb));
    return juce::jlimit (1, 64, n);
}

} // namespace

// ----------------------------------------------------------------------------------------- setup

bc::BandsRoom::BandsRoom (BandsState& s, std::function<void()> ch, std::function<double()> bt)
    : state (s), changed (std::move (ch)), beats (std::move (bt))
{
    // The pairs. Two knobs per idea: what it is at the bottom of the spectrum and what it is at
    // the top, with the fourteen bands between interpolated. Sixteen of everything would be
    // unusable, and a single value for all of them would not be a spectral delay.
    struct Spec { const char* name; float lo, hi, startDef, finishDef; bool log; const char* tip; };
    const Spec specs[] = {
        { "Range",    20.0f, 18000.0f,   90.0f, 9000.0f, true,
          "Where the sixteen bands sit. Log spaced between these two, because that is how we hear." },
        { "Time",      1.0f,  2000.0f,   45.0f,  320.0f, true,
          "Delay per band. Short at the bottom and long at the top spreads a sound forward in time as it climbs." },
        { "Feedback",  0.0f,    0.95f,   0.30f,   0.58f, false,
          "How much each band feeds itself. Trimmed automatically when the neighbour feed is up, so the loop stays under one." },
        { "Level",     0.0f,     1.0f,   1.0f,    0.55f, false,
          "How loud each band's repeats are." },
        { "Sweep",     0.0f,     8.0f,   0.07f,    0.9f, true,
          "Rate of the delay-time wobble, per band." },
        { "Wobble",    0.0f,    30.0f,   0.0f,     5.0f, false,
          "Depth of that wobble, in milliseconds. A few milliseconds at the top is what stops the repeats sounding stapled in place." },
    };

    for (const auto& sp : specs)
    {
        Pair p;
        p.name = sp.name;
        p.start  = std::make_unique<Knob> (juce::String (), sp.lo, sp.hi, sp.startDef, sp.log);
        p.finish = std::make_unique<Knob> (juce::String (), sp.lo, sp.hi, sp.finishDef, sp.log);
        p.start->setTooltip (juce::String (sp.tip) + "  This is the value at the bottom band.");
        p.finish->setTooltip (juce::String (sp.tip) + "  This is the value at the top band.");
        pairs.push_back (std::move (p));
    }

    // Readouts, so a knob says what it is rather than a number between nought and one.
    pairs[0].start->toText = pairs[0].finish->toText = [] (float v) { return hzText (v) + " Hz"; };
    pairs[1].start->toText = pairs[1].finish->toText = [] (float v) { return msText (v); };
    pairs[2].start->toText = pairs[2].finish->toText = [] (float v) { return juce::String (v, 2); };
    pairs[3].start->toText = pairs[3].finish->toText = [] (float v) { return juce::String (v, 2); };
    pairs[4].start->toText = pairs[4].finish->toText = [] (float v) { return juce::String (v, 2) + " Hz"; };
    pairs[5].start->toText = pairs[5].finish->toText = [] (float v) { return juce::String (v, 1) + " ms"; };

    for (auto& p : pairs)
    {
        for (auto* k : { p.start.get(), p.finish.get() })
        {
            k->onChange = [this] (float) { pushParams(); };
            k->onCommit = [this] { pushParams(); };
            addAndMakeVisible (*k);
        }
    }

    // The interaural split and its drift. One idea, so they sit together.
    kInterStart.toText = kInterFinish.toText = [] (float v)
    {
        return v <= 0.0f ? juce::String ("off") : juce::String (v, 1) + " ms";
    };
    kInterStart.setTooltip ("Left/right delay offset at the bottom band. Between "
                            + juce::String (nsr::kInterMinMs, 1) + " and "
                            + juce::String (nsr::kInterMaxMs, 1)
                            + " ms a band sits inside the head rather than in a place; zero is off.");
    kInterFinish.setTooltip ("The same offset at the top band.");
    kInterLfoHz.setTooltip ("How fast that offset moves. Stops it settling into a fixed position.");
    kInterLfoAmt.setTooltip ("How much of the offset moves.");

    kQ.setTooltip ("Bandwidth, shared by all sixteen. Low Q leaks between neighbours on purpose.");
    kSideOffset.setTooltip ("Which neighbour each band takes from. +1 means band 1 feeds on band 2, "
                            "so energy climbs the spectrum as it repeats; -1 sends it down. "
                            "The bands are a line, not a ring: past either end, energy leaves.");
    kSideFeedback.setTooltip ("How much of the neighbour each band takes.");
    kDry.setTooltip ("The input, straight through.");
    kWet.setTooltip ("The bank.");
    kSideOffset.step = 1.0f;
    kSideOffset.toText = [] (float v) { const int n = (int) std::lround (v); return n == 0 ? juce::String ("none") : (n > 0 ? "+" + juce::String (n) : juce::String (n)); };
    kQ.toText = [] (float v) { return juce::String (v, 1); };
    kInterLfoHz.toText = [] (float v) { return juce::String (v, 2) + " Hz"; };

    for (auto* k : { &kQ, &kSideOffset, &kSideFeedback, &kInterStart, &kInterFinish,
                     &kInterLfoHz, &kInterLfoAmt, &kDry, &kWet })
    {
        k->onChange = [this] (float) { pushParams(); };
        k->onCommit = [this] { pushParams(); };
        addAndMakeVisible (*k);
    }

    // The selected band's place.
    kWidth.setTooltip ("How wide this band sits: a point, or the whole stage.");
    kBars.setTooltip ("Bars for one lap of its path.");
    kDepth.setTooltip ("How far it moves along that path.");
    kPhase.setTooltip ("Where in the lap it starts, so two bands need not move together.");
    kBars.toText = [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1); };

    for (auto* k : { &kWidth, &kBars, &kDepth, &kPhase })
    {
        k->onChange = [this] (float) { pushParams(); };
        k->onCommit = [this] { pushParams(); };
        addAndMakeVisible (*k);
    }

    segTravel.setTooltip ("The path this band takes.");
    segTravel.onSelect = [this] (int i)
    {
        state.place[(size_t) sel].travel = (nsr::Travel) juce::jlimit (0, (int) nsr::Travel::Count - 1, i);
        if (changed) changed();
        repaint();
    };
    addAndMakeVisible (segTravel);

    gemLocked.setTooltip ("Locked: this band sits in the middle, in mono, and no path moves it. "
                          "On for the bottom bands by default, because a wandering low end is what it costs you.");
    gemLocked.onClick = [this]
    {
        state.place[(size_t) sel].locked = gemLocked.getToggleState();
        if (changed) changed();
        repaint();
    };
    addAndMakeVisible (gemLocked);

    // The fan, which is the only way anyone is going to set sixteen places.
    kFan.setTooltip ("How far the fan opens. The spread is weighted toward the top, and the bottom "
                     "bands stay locked: low frequencies carry almost no direction and fold the "
                     "moment anything sums to mono. Press FAN to apply it.");
    addAndMakeVisible (kFan);

    pillFan.setTooltip ("Place all sixteen bands across the stage.");
    pillFan.onClick = [this]
    {
        state.fan (kFan.getValue(), pillAlternate.getToggleState());
        if (changed) changed();
        pullParams();
        repaint();
    };
    addAndMakeVisible (pillFan);

    pillAlternate.setToggleState (true, juce::dontSendNotification);
    pillAlternate.setTooltip ("Alternate: odd bands go left, even bands right, so the fan opens both ways.");
    addAndMakeVisible (pillAlternate);

    segTravelAll.setTooltip ("Give every band this path at once. Their periods stay staggered, so "
                             "they do not move as one block.");
    segTravelAll.onSelect = [this] (int i)
    {
        state.travelAll ((nsr::Travel) juce::jlimit (0, (int) nsr::Travel::Count - 1, i), 4);
        if (changed) changed();
        pullParams();
        repaint();
    };
    addAndMakeVisible (segTravelAll);

    gemPlaced.setTooltip ("Off: the bank goes straight out and nothing is placed. "
                          "Useful for hearing what the placement is actually doing.");
    gemPlaced.onClick = [this]
    {
        state.placed = gemPlaced.getToggleState();
        if (changed) changed();
        repaint();
    };
    addAndMakeVisible (gemPlaced);

    pullParams();
    startTimerHz (24);
}

bc::BandsRoom::~BandsRoom() { stopTimer(); }

void bc::BandsRoom::timerCallback()
{
    // Only worth a repaint if something on the stage is actually moving.
    if (! beats) return;
    for (int b = 0; b < kBands; ++b)
        if (state.place[(size_t) b].moves()) { repaint (stageArea().getSmallestIntegerContainer()); return; }
}

void bc::BandsRoom::select (int band)
{
    const int b = juce::jlimit (0, kBands - 1, band);
    if (b == sel) return;
    sel = b;
    pullParams();
    repaint();
}

void bc::BandsRoom::refresh() { pullParams(); repaint(); }

// ------------------------------------------------------------------------------ state <-> controls

void bc::BandsRoom::pushParams()
{
    auto& d = state.dsp;
    d.startHz        = pairs[0].start->getValue();
    d.finishHz       = pairs[0].finish->getValue();
    d.startTimeMs    = pairs[1].start->getValue();
    d.finishTimeMs   = pairs[1].finish->getValue();
    d.startFeedback  = pairs[2].start->getValue();
    d.finishFeedback = pairs[2].finish->getValue();
    d.startLevel     = pairs[3].start->getValue();
    d.finishLevel    = pairs[3].finish->getValue();
    d.startLfoHz     = pairs[4].start->getValue();
    d.finishLfoHz    = pairs[4].finish->getValue();
    d.startLfoMs     = pairs[5].start->getValue();
    d.finishLfoMs    = pairs[5].finish->getValue();

    d.q            = kQ.getValue();
    d.sideOffset   = (int) std::lround (kSideOffset.getValue());
    d.sideFeedback = kSideFeedback.getValue();
    d.startInterMs = kInterStart.getValue();
    d.finishInterMs = kInterFinish.getValue();
    d.interLfoHz   = kInterLfoHz.getValue();
    d.interLfoAmt  = kInterLfoAmt.getValue();
    d.dry          = kDry.getValue();
    d.wet          = kWet.getValue();

    auto& pl = state.place[(size_t) sel];
    pl.width = kWidth.getValue();
    pl.bars  = kBars.getValue();
    pl.depth = kDepth.getValue();
    pl.phase = kPhase.getValue();

    if (changed) changed();
    repaint();
}

void bc::BandsRoom::pullParams()
{
    const auto& d = state.dsp;
    pairs[0].start->setValue (d.startHz);        pairs[0].finish->setValue (d.finishHz);
    pairs[1].start->setValue (d.startTimeMs);    pairs[1].finish->setValue (d.finishTimeMs);
    pairs[2].start->setValue (d.startFeedback);  pairs[2].finish->setValue (d.finishFeedback);
    pairs[3].start->setValue (d.startLevel);     pairs[3].finish->setValue (d.finishLevel);
    pairs[4].start->setValue (d.startLfoHz);     pairs[4].finish->setValue (d.finishLfoHz);
    pairs[5].start->setValue (d.startLfoMs);     pairs[5].finish->setValue (d.finishLfoMs);

    kQ.setValue (d.q);
    kSideOffset.setValue ((float) d.sideOffset);
    kSideFeedback.setValue (d.sideFeedback);
    kInterStart.setValue (d.startInterMs);
    kInterFinish.setValue (d.finishInterMs);
    kInterLfoHz.setValue (d.interLfoHz);
    kInterLfoAmt.setValue (d.interLfoAmt);
    kDry.setValue (d.dry);
    kWet.setValue (d.wet);

    const auto& pl = state.place[(size_t) sel];
    kWidth.setValue (pl.width);
    kBars.setValue (pl.bars);
    kDepth.setValue (pl.depth);
    kPhase.setValue (pl.phase);
    segTravel.setSelected ((int) pl.travel);
    gemLocked.setToggleState (pl.locked, juce::dontSendNotification);
    gemPlaced.setToggleState (state.placed, juce::dontSendNotification);

    // A locked band has nowhere to go, so the path controls say so rather than pretending.
    const bool canMove = ! pl.locked;
    segTravel.setEnabled (canMove);
    kBars.setEnabled (canMove && pl.travel != nsr::Travel::Still);
    kDepth.setEnabled (canMove && pl.travel != nsr::Travel::Still);
    kPhase.setEnabled (canMove && pl.travel != nsr::Travel::Still);
}

// ----------------------------------------------------------------------------------------- layout

juce::Rectangle<float> bc::BandsRoom::stageArea() const
{
    return juce::Rectangle<int> (mainPlate.getX() + 12, 40, mainPlate.getWidth() - 24, 300).toFloat();
}

juce::Rectangle<float> bc::BandsRoom::ringsArea() const
{
    return juce::Rectangle<int> (mainPlate.getX() + 12, 392, mainPlate.getWidth() - 24, 320).toFloat();
}

void bc::BandsRoom::resized()
{
    leftPlate  = { 12, 8, 300, 720 };
    mainPlate  = { 322, 8, 652, 720 };
    rightPlate = { 984, 8, 284, 720 };

    // left: six pairs, name above, two knobs below
    {
        const int x = leftPlate.getX() + 10;
        const int w = (leftPlate.getWidth() - 20) / 2;
        int y = 36;
        for (auto& p : pairs)
        {
            p.start->setBounds (x, y + 14, w, 84);
            p.finish->setBounds (x + w, y + 14, w, 84);
            y += 100;
        }
    }

    // main: the stage, the fan row, the rings
    {
        const auto st = stageArea().getSmallestIntegerContainer();
        int y = st.getBottom() + 8;
        const int x = mainPlate.getX() + 12;
        kFan.setBounds (x, y - 10, 72, 60);
        pillFan.setBounds (x + 78, y + 6, 54, 26);
        pillAlternate.setBounds (x + 136, y + 6, 92, 26);
        segTravelAll.setBounds (x + 236, y + 6, 290, 26);
        gemPlaced.setBounds (x + 534, y + 7, 90, 24);
    }

    // right: interiority, cross-band, mix, then the selected band
    {
        const int x = rightPlate.getX() + 10;
        const int w = rightPlate.getWidth() - 20;
        const int half = w / 2, third = w / 3;

        int y = 36;
        yHeadSplit = y;
        kInterStart.setBounds (x, y + 14, half, 80);
        kInterFinish.setBounds (x + half, y + 14, half, 80);
        y += 94;
        kInterLfoHz.setBounds (x, y, half, 80);
        kInterLfoAmt.setBounds (x + half, y, half, 80);
        y += 86;

        yHeadCross = y;
        kSideOffset.setBounds (x, y + 14, third, 80);
        kSideFeedback.setBounds (x + third, y + 14, third, 80);
        kQ.setBounds (x + 2 * third, y + 14, third, 80);
        y += 96;

        yHeadMix = y;
        kDry.setBounds (x, y + 14, half, 80);
        kWet.setBounds (x + half, y + 14, half, 80);
        y += 96;

        // the band detail: heading, readout, then its path
        yHeadBand = y;
        y += 14;
        detailBox = { x, y, w, 70 };
        y += 76;
        segTravel.setBounds (x, y, w, 26);
        y += 30;
        gemLocked.setBounds (x, y, 90, 22);
        y += 26;
        const int q = w / 4;
        kWidth.setBounds (x, y, q, 76);
        kBars.setBounds (x + q, y, q, 76);
        kDepth.setBounds (x + 2 * q, y, q, 76);
        kPhase.setBounds (x + 3 * q, y, q, 76);
    }
}

// ------------------------------------------------------------------------------------------- paint

void bc::BandsRoom::paint (juce::Graphics& g)
{
    paintChassis (g, getLocalBounds().toFloat());
    paintPlate (g, leftPlate.toFloat());
    paintPlate (g, mainPlate.toFloat());
    paintPlate (g, rightPlate.toFloat());

    g.setFont (caps (10.0f));
    g.setColour (C::text());
    g.drawText ("SPECTRUM", leftPlate.getX() + 12, 16, 200, 14, juce::Justification::centredLeft);
    g.drawText ("BANDS", mainPlate.getX() + 12, 16, 200, 14, juce::Justification::centredLeft);
    g.drawText ("INTERIORITY", rightPlate.getX() + 10, 16, 200, 14, juce::Justification::centredLeft);

    // pair names down the left
    {
        int y = 36;
        for (auto& p : pairs)
        {
            paintLabel (g, p.name.toUpperCase(), { leftPlate.getX() + 12, y, 200, 12 });
            ++y;
            paintLabel (g, "start", { leftPlate.getX() + 12, y + 84, 60, 12 });
            paintLabel (g, "finish", { leftPlate.getX() + 12 + (leftPlate.getWidth() - 20) / 2, y + 84, 60, 12 });
            y += 99;
        }
    }

    // right-hand headings, at the positions resized() worked out
    {
        const int x = rightPlate.getX() + 12;
        paintLabel (g, "SPLIT", { x, yHeadSplit, 200, 12 });
        paintLabel (g, "CROSS-BAND", { x, yHeadCross, 200, 12 });
        paintLabel (g, "MIX", { x, yHeadMix, 200, 12 });
        paintLabel (g, "BAND " + juce::String (sel + 1), { x, yHeadBand, 200, 12 },
                    juce::Justification::centredLeft, 9.0f, C::teal());
        paintDetail (g, detailBox.toFloat());
    }

    paintStage (g, stageArea());
    paintRings (g, ringsArea());

    g.setFont (font (10.0f));
    g.setColour (C::dim());
    g.drawFittedText ("Drag a band on the stage to move it, or click its row below to select it. "
                      "FAN places all sixteen at once.",
                      { mainPlate.getX() + 12, ringsArea().getSmallestIntegerContainer().getBottom() + 2,
                        mainPlate.getWidth() - 24, 16 },
                      juce::Justification::topLeft, 1);
}

juce::Point<float> bc::BandsRoom::stagePoint (float x, float y) const
{
    const auto r = stageArea().reduced (kStageInset);
    return { r.getCentreX() + x * r.getWidth() * 0.5f,
             r.getBottom() - y * r.getHeight() };     // y = 0 at the front, which is the bottom edge
}

void bc::BandsRoom::paintStage (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintLcd (g, r);
    const auto in = r.reduced (kStageInset);
    const double bt = beats ? beats() : 0.0;

    // the floor: centre line, and arcs for distance
    g.setColour (C::lcdDim().withAlpha (0.35f));
    g.drawLine (in.getCentreX(), in.getY(), in.getCentreX(), in.getBottom(), 1.0f);
    for (int k = 1; k <= 3; ++k)
    {
        const float yy = in.getBottom() - in.getHeight() * (float) k / 4.0f;
        g.drawLine (in.getX(), yy, in.getRight(), yy, 0.6f);
    }
    g.setFont (font (8.0f));
    g.setColour (C::lcdDim());
    g.drawText ("L", (int) in.getX(), (int) in.getBottom() - 12, 20, 12, juce::Justification::centredLeft);
    g.drawText ("R", (int) in.getRight() - 20, (int) in.getBottom() - 12, 20, 12, juce::Justification::centredRight);
    g.drawText ("back", (int) in.getCentreX() - 30, (int) in.getY(), 60, 12, juce::Justification::centred);
    g.drawText ("front", (int) in.getCentreX() - 30, (int) in.getBottom() - 12, 60, 12, juce::Justification::centred);

    if (! state.placed)
    {
        g.setFont (caps (11.0f));
        g.setColour (C::amber().withAlpha (0.8f));
        g.drawText ("PLACEMENT OFF", in, juce::Justification::centred);
        return;
    }

    // Everything below is clipped to the stage. A band at the edge with its width up reaches well
    // past it otherwise - the first version drew a wide band as a streak straight across the plate
    // and out the other side, which read as a rendering fault rather than as a wide band.
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (r.toNearestInt());

    // each band: its path first, faint, then the band itself where it is now
    for (int b = 0; b < kBands; ++b)
    {
        const auto& pl = state.place[(size_t) b];
        if (! pl.on) continue;

        const auto plan = nsr::bandPlan (state.dsp, b, kBands);
        const auto col = bandColour (b, kBands);
        const bool isSel = b == sel;

        if (pl.moves())
        {
            juce::Path path;
            for (int k = 0; k <= 48; ++k)
            {
                float px = 0.0f, py = 0.0f;
                nsr::placeAt (pl, bt + (double) pl.bars * 4.0 * (double) k / 48.0, px, py);
                const auto pt = stagePoint (px, py);
                if (k == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (col.withAlpha (isSel ? 0.40f : 0.16f));
            g.strokePath (path, juce::PathStrokeType (isSel ? 1.4f : 1.0f));
        }

        float px = 0.0f, py = 0.0f;
        nsr::placeAt (pl, bt, px, py);
        const auto pt = stagePoint (px, py);

        // Level sets the size and the band's own ring time sets the halo, so a long band reads as
        // a long band without having to look at the rows below.
        const float rad = 4.0f + plan.level * 7.0f;
        const float ring = plan.timeMs * (float) audibleRepeats (plan.level, plan.feedback);

        if (pl.width > 0.001f)
        {
            // width drawn as a horizontal extent, which is what it is
            const float halfW = pl.width * stageArea().reduced (kStageInset).getWidth() * 0.25f;
            g.setColour (col.withAlpha (0.18f));
            g.fillRoundedRectangle (pt.x - halfW, pt.y - rad * 0.45f, halfW * 2.0f, rad * 0.9f, rad * 0.45f);
        }

        g.setColour (col.withAlpha (juce::jlimit (0.05f, 0.30f, ring / 3000.0f)));
        g.fillEllipse (pt.x - rad * 2.2f, pt.y - rad * 2.2f, rad * 4.4f, rad * 4.4f);

        g.setColour (pl.locked ? C::lcdDim() : col);
        g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f);

        if (isSel)
        {
            g.setColour (C::lcdText());
            g.drawEllipse (pt.x - rad - 3.0f, pt.y - rad - 3.0f, (rad + 3.0f) * 2.0f, (rad + 3.0f) * 2.0f, 1.4f);
        }

        // Only the selected band and the two ends are labelled. Sixteen labels on a stage this
        // size overlap into a smear in the middle, and the rows below already name every band.
        if (isSel || b == 0 || b == kBands - 1)
        {
            g.setFont (font (8.0f));
            g.setColour (isSel ? C::lcdText() : C::lcdDim());
            g.drawText (hzText (plan.hz), (int) pt.x - 22, (int) (pt.y + rad + 1.0f), 44, 10,
                        juce::Justification::centred);
        }
    }
}

void bc::BandsRoom::paintRings (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintLcd (g, r);
    const auto body = r.reduced (10.0f, 8.0f);
    const auto ruler = body.withHeight (11.0f);           // a time ruler along the top
    const auto in = body.withTrimmedTop (12.0f);
    const float rowH = in.getHeight() / (float) kBands;
    const float labelW = 44.0f;
    const float timeX = in.getX() + labelW;
    const float timeW = in.getRight() - timeX - 44.0f;    // room for the ring total on the right

    // One time axis for every row, so the rows can be compared. That comparison is the point:
    // it is how you see that the top of the spectrum rings on long after the bottom has gone.
    float axisMs = 1.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const auto pl = nsr::bandPlan (state.dsp, b, kBands);
        axisMs = std::max (axisMs, pl.timeMs * (float) audibleRepeats (pl.level, pl.feedback));
    }
    axisMs = std::min (axisMs, 8000.0f);

    // The grid follows the axis rather than staying on a fixed step: a bank of 20 ms delays and a
    // bank of 2 s delays want very different rulers, and a one-second grid on the first draws
    // nothing at all.
    const float gridMs = axisMs > 2400.0f ? 1000.0f
                       : axisMs > 600.0f  ? 250.0f
                       : axisMs > 120.0f  ? 50.0f
                                          : 10.0f;
    g.setFont (font (8.0f));
    for (float t = gridMs; t < axisMs; t += gridMs)
    {
        const float xx = timeX + timeW * t / axisMs;
        g.setColour (C::lcdDim().withAlpha (0.22f));
        g.drawLine (xx, in.getY(), xx, in.getBottom(), 0.6f);
        g.setColour (C::lcdDim().withAlpha (0.65f));
        g.drawText (msText (t), (int) xx - 30, (int) ruler.getY(), 60, 11, juce::Justification::centred);
    }
    g.setColour (C::lcdDim());
    g.drawText ("ring", (int) (in.getRight() - 42), (int) ruler.getY(), 42, 11,
                juce::Justification::centredRight);

    for (int b = 0; b < kBands; ++b)
    {
        // Band 0 at the bottom of the display, like a spectrum: the low end belongs at the bottom.
        const float y = in.getBottom() - rowH * (float) (b + 1);
        const auto row = juce::Rectangle<float> (in.getX(), y, in.getWidth(), rowH);
        const auto plan = nsr::bandPlan (state.dsp, b, kBands);
        const auto& pl = state.place[(size_t) b];
        const auto col = bandColour (b, kBands);
        const bool isSel = b == sel;

        if (isSel)
        {
            g.setColour (C::lcdText().withAlpha (0.10f));
            g.fillRect (row);
        }

        g.setFont (font (8.5f));
        g.setColour (isSel ? C::lcdText() : C::lcdDim());
        g.drawText (hzText (plan.hz), (int) in.getX(), (int) y, (int) labelW - 6, (int) rowH,
                    juce::Justification::centredRight);

        // the repeats: one blip per arrival, each down by the feedback
        const int n = audibleRepeats (plan.level, plan.feedback);
        const float cy = y + rowH * 0.5f;
        float amp = plan.level;
        for (int k = 1; k <= n; ++k)
        {
            const float t = plan.timeMs * (float) k;
            if (t > axisMs) break;
            const float xx = timeX + timeW * t / axisMs;
            const float h = juce::jlimit (1.5f, rowH * 0.42f, rowH * 0.42f * amp);
            g.setColour (col.withAlpha (juce::jlimit (0.12f, 0.95f, amp)));
            g.fillRect (xx - 1.0f, cy - h, 2.0f, h * 2.0f);

            // the first arrival carries the sweep, drawn as the window it actually moves in
            if (k == 1 && plan.lfoMs > 0.05f)
            {
                const float wobW = timeW * plan.lfoMs / axisMs;
                g.setColour (col.withAlpha (0.28f));
                g.fillRect (xx - wobW, cy - 1.0f, wobW * 2.0f, 2.0f);
            }
            amp *= std::abs (plan.feedback);
        }

        // what the row adds up to, on the right
        const float ringMs = plan.timeMs * (float) n;
        g.setFont (font (8.0f));
        g.setColour (isSel ? C::lcdText() : C::lcdDim().withAlpha (0.75f));
        g.drawText (msText (ringMs), (int) (in.getRight() - 42), (int) y, 42, (int) rowH,
                    juce::Justification::centredRight);

        // locked and travelling marked at the left edge, so the rows say where a band is too
        if (pl.locked)
        {
            g.setColour (C::lcdDim());
            g.fillRect (in.getX() - 6.0f, cy - 1.0f, 4.0f, 2.0f);
        }
        else if (pl.moves())
        {
            g.setColour (col.withAlpha (0.7f));
            g.fillEllipse (in.getX() - 7.0f, cy - 2.0f, 4.0f, 4.0f);
        }
    }

}

void bc::BandsRoom::paintDetail (juce::Graphics& g, juce::Rectangle<float> r)
{
    paintWell (g, r);
    const auto plan = nsr::bandPlan (state.dsp, sel, kBands);
    const auto& pl = state.place[(size_t) sel];
    const auto in = r.reduced (8.0f, 6.0f);

    float px = 0.0f, py = 0.0f;
    nsr::placeAt (pl, beats ? beats() : 0.0, px, py);

    const int n = audibleRepeats (plan.level, plan.feedback);

    // Two columns of what this band is actually doing. The feedback here is the trimmed figure,
    // which is why it can read lower than the knob: with the neighbour feed up, it is.
    struct Line { juce::String k, v; };
    const Line left[] = {
        { "centre",   hzText (plan.hz) + " Hz" },
        { "delay",    msText (plan.timeMs) },
        { "feedback", juce::String (plan.feedback, 2) },
    };
    const Line right[] = {
        { "rings",    msText (plan.timeMs * (float) n) + "  (" + juce::String (n) + ")" },
        { "split",    plan.interMs <= 0.0f ? juce::String ("off") : juce::String (plan.interMs, 1) + " ms" },
        { "at",       pl.locked ? juce::String ("locked, mono")
                                : juce::String (px, 2) + " / " + juce::String (py, 2)
                                  + (pl.moves() ? " " + juce::String (nsr::travelName (pl.travel)).toLowerCase()
                                                : juce::String()) },
    };

    const float colW = in.getWidth() * 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        const float yy = in.getY() + (float) i * 22.0f;
        g.setFont (font (8.5f));
        g.setColour (C::label());
        g.drawText (left[i].k, (int) in.getX(), (int) yy, 52, 14, juce::Justification::centredLeft);
        g.drawText (right[i].k, (int) (in.getX() + colW), (int) yy, 42, 14, juce::Justification::centredLeft);
        g.setFont (font (10.0f, true));
        g.setColour (C::text());
        g.drawText (left[i].v, (int) (in.getX() + 52), (int) yy, (int) colW - 56, 14, juce::Justification::centredLeft);
        g.drawText (right[i].v, (int) (in.getX() + colW + 42), (int) yy, (int) colW - 46, 14, juce::Justification::centredLeft);
    }
}

// ------------------------------------------------------------------------------------------- mouse

int bc::BandsRoom::bandAtStage (juce::Point<float> p) const
{
    if (! state.placed || ! stageArea().contains (p)) return -1;
    const double bt = beats ? beats() : 0.0;
    int best = -1;
    float bestD = 18.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const auto& pl = state.place[(size_t) b];
        if (! pl.on) continue;
        float px = 0.0f, py = 0.0f;
        nsr::placeAt (pl, bt, px, py);
        const float d = stagePoint (px, py).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

int bc::BandsRoom::bandAtRings (juce::Point<float> p) const
{
    const auto r = ringsArea();
    if (! r.contains (p)) return -1;
    const auto in = r.reduced (10.0f, 8.0f);
    const float rowH = in.getHeight() / (float) kBands;
    const int fromBottom = (int) ((in.getBottom() - p.y) / rowH);
    return juce::jlimit (0, kBands - 1, fromBottom);
}

void bc::BandsRoom::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (const int b = bandAtStage (p); b >= 0)
    {
        select (b);
        dragging = b;
        dragFromStage = true;
        return;
    }
    if (const int b = bandAtRings (p); b >= 0)
    {
        select (b);
        return;
    }
    dragging = -1;
}

void bc::BandsRoom::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || ! dragFromStage) return;

    auto& pl = state.place[(size_t) dragging];
    if (pl.locked) return;        // a locked band does not move, and dragging it should not lie

    const auto in = stageArea().reduced (kStageInset);
    pl.x = juce::jlimit (-1.0f, 1.0f, (e.position.x - in.getCentreX()) / (in.getWidth() * 0.5f));
    pl.y = juce::jlimit (0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight());

    if (changed) changed();
    repaint();
}

void bc::BandsRoom::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
    dragFromStage = false;
}
