#include "MapRoom.h"
#include "Processor.h"
#include "../neobeat/shared/Ui.h"
#include "../neobeat/gen/Scale.h"

using namespace nsui;

namespace
{
constexpr int kRuler = 46;         // the scale down the left of the grid
constexpr int kLaneH = 150;        // the modulation lanes under it

const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

struct MapRoom::Parts
{
    juce::OwnedArray<Pill> tools, sources;
    int tool = 0, source = 0;
    Knob length { "LENGTH", 1.0f, 16.0f, 1.0f };
    Knob force { "FORCE", 0.0f, 1.0f, 0.8f };
    Knob steps { "STEPS", 4.0f, 64.0f, 16.0f };
    Pill clearAll { "CLEAR ALL" }, writeIt { "WRITE ME A PHRASE" };
    juce::Label scaleLabel { {}, "" };
    int painting = 0;                 // 1 adding, 2 erasing, while the mouse is down
    double playBeat = 0.0;
};

MapRoom::MapRoom (bc::RoomView& v) : view (v)
{
    parts = std::make_unique<Parts>();
    auto& q = *parts;

    const char* toolNames[] = { "NOTE", "CHOP", "GLIDE", "RATCHET", "ERASE" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = q.tools.add (new Pill (toolNames[i], true));
        b->onClick = [this, i] { parts->tool = i; for (int k = 0; k < parts->tools.size(); ++k) parts->tools[k]->setToggleState (k == i, juce::dontSendNotification); };
        addAndMakeVisible (b);
    }
    q.tools[0]->setToggleState (true, juce::dontSendNotification);

    const char* srcNames[] = { "KEYS", "LOOP 1", "LOOP 2", "LOOP 3", "LOOP 4" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = q.sources.add (new Pill (srcNames[i], true));
        b->setTooltip (i == 0 ? "A new mark plays the instrument from KEYS, at the degree it sits on."
                              : "A new mark plays a chop from this loop layer, the row saying which chop.");
        b->onClick = [this, i] { parts->source = i; for (int k = 0; k < parts->sources.size(); ++k) parts->sources[k]->setToggleState (k == i, juce::dontSendNotification); };
        addAndMakeVisible (b);
    }
    q.sources[0]->setToggleState (true, juce::dontSendNotification);
    for (auto* k : { &q.length, &q.force, &q.steps }) addAndMakeVisible (k);
    addAndMakeVisible (q.clearAll);
    addAndMakeVisible (q.writeIt);
    addAndMakeVisible (q.scaleLabel);

    q.length.setTooltip ("How many steps a new mark lasts.");
    q.force.setTooltip ("How hard it is played.");
    q.steps.setTooltip ("How long the pattern is.");

    q.steps.onCommit = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.map.steps = juce::jlimit (4, mjq::kMapMaxSteps, (int) parts->steps.getValue());
        p.mapChanged();
        repaint();
    };

    q.clearAll.setTooltip ("Every mark off the grid. The sounds stay where they are.");
    q.clearAll.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.map.clear();
        p.mapChanged();
        repaint();
    };

    q.writeIt.setTooltip ("A phrase in the key, using whatever KEYS and LOOPS have loaded.");
    q.writeIt.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.mapWrite();
        refresh();
    };

    setWantsKeyboardFocus (true);
}

MapRoom::~MapRoom() = default;

juce::Rectangle<int> MapRoom::gridArea() const
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromLeft (150 + 10);        // the tools
    r.removeFromRight (274 + 10);       // the inspector
    r.removeFromTop (16 + 26 + 8);      // the labels and the bar of controls
    r.removeFromBottom (kLaneH + 8);    // the modulation lanes
    r.removeFromLeft (kRuler + 7);
    return r;
}

juce::Rectangle<int> MapRoom::cellAt (int step, int row) const
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    const auto g = gridArea();
    const int steps = std::max (1, p.map.steps);
    const float w = (float) g.getWidth() / (float) steps;
    const float h = (float) g.getHeight() / (float) mjq::kMapRows;
    return juce::Rectangle<float> (g.getX() + w * (float) step,
                                   g.getBottom() - h * (float) (row + 1),
                                   w, h).toNearestInt();
}

bool MapRoom::cellFor (juce::Point<int> pt, int& step, int& row) const
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    const auto g = gridArea();
    if (! g.contains (pt)) return false;
    const int steps = std::max (1, p.map.steps);
    step = juce::jlimit (0, steps - 1, (int) ((float) (pt.x - g.getX()) / (float) g.getWidth() * (float) steps));
    row  = juce::jlimit (0, mjq::kMapRows - 1,
                         (int) ((float) (g.getBottom() - pt.y) / (float) g.getHeight() * (float) mjq::kMapRows));
    return true;
}

void MapRoom::mouseDown (const juce::MouseEvent& e)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    int step = 0, row = 0;
    if (! cellFor (e.getPosition(), step, row)) return;

    const int tool = parts->tool;
    const bool erasing = tool == 4 || e.mods.isRightButtonDown() || p.map.at (step, row) != nullptr;

    if (erasing)
    {
        p.map.erase (step, row);
        parts->painting = 2;
    }
    else
    {
        mjq::Mark m;
        m.step = step;
        m.row = row;
        m.len = juce::jlimit (1, 16, (int) parts->length.getValue());
        m.velocity = parts->force.getValue();
        const int src = parts->source;
        m.source = src == 0 ? mjq::MarkSource::Keys : mjq::MarkSource::Loop;
        m.layer = std::max (0, src - 1);
        m.glide = tool == 2;
        m.ratchet = tool == 3 ? 3 : 1;
        p.map.put (m);
        parts->painting = 1;
    }
    p.mapChanged();
    repaint();
}

void MapRoom::mouseDrag (const juce::MouseEvent& e)
{
    if (parts->painting == 0) return;
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    int step = 0, row = 0;
    if (! cellFor (e.getPosition(), step, row)) return;

    if (parts->painting == 2) p.map.erase (step, row);
    else if (p.map.at (step, row) == nullptr)
    {
        mjq::Mark m;
        m.step = step; m.row = row;
        m.len = juce::jlimit (1, 16, (int) parts->length.getValue());
        m.velocity = parts->force.getValue();
        const int src = parts->source;
        m.source = src == 0 ? mjq::MarkSource::Keys : mjq::MarkSource::Loop;
        m.layer = std::max (0, src - 1);
        p.map.put (m);
    }
    p.mapChanged();
    repaint();
}

void MapRoom::mouseUp (const juce::MouseEvent&) { parts->painting = 0; }

void MapRoom::tick()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    const double b = p.stageBeats();
    if (std::fabs (b - parts->playBeat) > 0.01) { parts->playBeat = b; repaint (gridArea()); }
}

void MapRoom::refresh()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;
    q.steps.setValue ((float) p.map.steps);
    q.scaleLabel.setText (juce::String (kNoteNames[juce::jlimit (0, 11, p.map.root)]) + " "
                          + juce::String (gen::scale (p.map.scale).name), juce::dontSendNotification);
    repaint();
}

bool MapRoom::key (const juce::KeyPress& k)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    { p.map.clear(); p.mapChanged(); repaint(); return true; }
    return false;
}

void MapRoom::paint (juce::Graphics& g)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;
    paintPlate (g, getLocalBounds().toFloat());

    // ---- the grid's well
    auto well = gridArea().expanded (kRuler + 7, 0).withTrimmedLeft (0);
    well = gridArea().withLeft (gridArea().getX() - kRuler - 7).expanded (6, 6);
    paintWell (g, well.toFloat());

    const auto area = gridArea();
    const int steps = std::max (1, p.map.steps);
    const float cw = (float) area.getWidth() / (float) steps;
    const float ch = (float) area.getHeight() / (float) mjq::kMapRows;

    // the scale down the side: degrees, not drum lanes
    const auto& sc = gen::scale (p.map.scale);
    const int numDeg = mjq::degreesIn (sc.mask);
    for (int row = 0; row < mjq::kMapRows; ++row)
    {
        const int note = mjq::noteOfRow (row, sc.mask, p.map.root, p.map.octave);
        auto cell = juce::Rectangle<float> ((float) area.getX() - kRuler - 3.0f,
                                            (float) area.getBottom() - ch * (float) (row + 1),
                                            (float) kRuler, ch).toNearestInt();
        const bool octaveLine = numDeg > 0 && (row % numDeg) == 0;
        if (octaveLine)
        {
            g.setColour (C::text().withAlpha (0.07f));
            g.fillRoundedRectangle (cell.toFloat().reduced (1.0f), 2.0f);
        }
        g.setColour (octaveLine ? C::text() : C::dim());
        g.setFont (font (9.0f));
        g.drawText (juce::String (kNoteNames[((note % 12) + 12) % 12]), cell, juce::Justification::centred, false);
    }

    // the grid itself
    for (int s = 1; s < steps; ++s)
    {
        g.setColour (C::teal().withAlpha ((s % 4 == 0) ? 0.16f : 0.05f));
        const float x = (float) area.getX() + cw * (float) s;
        g.drawLine (x, (float) area.getY(), x, (float) area.getBottom(), 1.0f);
    }
    for (int row = 1; row < mjq::kMapRows; ++row)
    {
        g.setColour (C::teal().withAlpha ((numDeg > 0 && row % numDeg == 0) ? 0.16f : 0.04f));
        const float y = (float) area.getBottom() - ch * (float) row;
        g.drawLine ((float) area.getX(), y, (float) area.getRight(), y, 1.0f);
    }

    // the marks: teal for the instrument, amber for a chop, so the two sources read apart
    for (const auto& m : p.map.marks)
    {
        auto cell = juce::Rectangle<float> ((float) area.getX() + cw * (float) m.step,
                                            (float) area.getBottom() - ch * (float) (m.row + 1),
                                            cw * (float) std::max (1, m.len) - 1.5f, ch - 1.5f);
        const auto base = m.source == mjq::MarkSource::Keys ? C::teal() : C::amber();
        g.setColour (base.withAlpha (0.35f + 0.55f * juce::jlimit (0.0f, 1.0f, m.velocity)));
        g.fillRoundedRectangle (cell, 3.0f);
        if (m.ratchet > 1)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            for (int k = 1; k < m.ratchet; ++k)
            {
                const float x = cell.getX() + cell.getWidth() * (float) k / (float) m.ratchet;
                g.drawLine (x, cell.getY(), x, cell.getBottom(), 1.0f);
            }
        }
        if (m.glide)
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawLine (cell.getX() + 2.0f, cell.getBottom() - 2.0f, cell.getRight() - 2.0f, cell.getY() + 2.0f, 1.2f);
        }
    }

    // where it is now
    if (steps > 0)
    {
        const double beatsPerStep = 4.0 / (double) std::max (1, p.map.perBar) * 4.0;
        const double pos = std::fmod (std::max (0.0, q.playBeat) / std::max (0.001, beatsPerStep), (double) steps);
        const float x = (float) area.getX() + cw * (float) pos;
        g.setColour (C::amber().withAlpha (0.85f));
        g.drawLine (x, (float) area.getY(), x, (float) area.getBottom(), 2.0f);
    }

    // ---- labels
    auto r = getLocalBounds().reduced (10);
    auto tools = r.removeFromLeft (150);
    paintLabel (g, "MARKS", tools.removeFromTop (16));
    tools.removeFromTop (5 * 27 + 14 - 16);
    paintLabel (g, "WHAT IT PLAYS", tools.removeFromTop (16));
    auto right = r.removeFromRight (274);
    paintWell (g, right.toFloat());
    paintLabel (g, "THE MARK", right.reduced (8).removeFromTop (16));

    r.removeFromLeft (10);
    r.removeFromRight (10);
    paintLabel (g, "DEGREES OF THE SCALE: a mark can only land in the key", r.removeFromTop (16));

    // ---- the modulation lanes
    auto lanes = getLocalBounds().reduced (10);
    lanes.removeFromLeft (150 + 10);
    lanes.removeFromRight (274 + 10);
    lanes = lanes.removeFromBottom (kLaneH);
    paintWell (g, lanes.toFloat());
    auto lb = lanes.reduced (8);
    paintLabel (g, "MODULATION", lb.removeFromTop (16), juce::Justification::centredLeft, 9.0f, juce::Colour (0xff7ca0ff));
    const char* laneNames[] = { "LFO 2", "ENV 1", "RANDOM" };
    const juce::Colour laneCols[] = { juce::Colour (0xff7ca0ff), C::teal(), C::amber() };
    for (int i = 0; i < 3; ++i)
    {
        auto lane = lb.removeFromTop (lb.getHeight() / (3 - i));
        auto name = lane.removeFromLeft (70);
        paintLabel (g, laneNames[i], name, juce::Justification::centredRight, 9.0f, C::label());
        lane.removeFromLeft (7);
        auto box = lane.reduced (0, 2);
        g.setColour (juce::Colour (0xff1a2320));
        g.fillRoundedRectangle (box.toFloat(), 3.0f);
        juce::Path path;
        for (int k = 0; k <= 80; ++k)
        {
            const float t = (float) k / 80.0f;
            float y = 0.0f;
            if (i == 0) y = std::sin (t * juce::MathConstants<float>::twoPi * 4.0f);
            else if (i == 1) y = std::exp (-std::fmod (t * 4.0f, 1.0f) * 4.0f) * 0.9f;
            else y = std::sin (t * 31.7f) * 0.5f + std::sin (t * 7.3f) * 0.4f;
            const juce::Point<float> at ((float) box.getX() + (float) box.getWidth() * t,
                                         (float) box.getCentreY() - y * (float) box.getHeight() * 0.4f);
            if (k == 0) path.startNewSubPath (at); else path.lineTo (at);
        }
        g.setColour (laneCols[i]);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
}

void MapRoom::resized()
{
    auto& q = *parts;
    auto r = getLocalBounds().reduced (10);

    auto tools = r.removeFromLeft (150);
    tools.removeFromTop (16);
    for (auto* b : q.tools) { b->setBounds (tools.removeFromTop (24)); tools.removeFromTop (3); }
    tools.removeFromTop (14);
    for (auto* b : q.sources) { b->setBounds (tools.removeFromTop (24)); tools.removeFromTop (3); }
    tools.removeFromTop (8);
    q.writeIt.setBounds (tools.removeFromBottom (28));
    tools.removeFromBottom (4);
    q.clearAll.setBounds (tools.removeFromBottom (24));

    r.removeFromLeft (10);
    auto right = r.removeFromRight (274).reduced (8);
    right.removeFromTop (20);
    auto row = right.removeFromTop (66);
    const int kw = row.getWidth() / 3;
    q.length.setBounds (row.removeFromLeft (kw));
    q.force.setBounds (row.removeFromLeft (kw));
    q.steps.setBounds (row);
    right.removeFromTop (8);
    q.scaleLabel.setBounds (right.removeFromTop (22));
}
