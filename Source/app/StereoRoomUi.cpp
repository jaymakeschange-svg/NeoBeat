#include "StereoRoomUi.h"
#include "Processor.h"
#include "../neobeat/shared/Ui.h"

using namespace nsui;

namespace
{
// the colour a node is drawn in, so the list and the stage agree
juce::Colour colourFor (int i)
{
    static const juce::uint32 wheel[] = { 0xff5fd8c4, 0xfff5b25a, 0xffd9534a, 0xff7ca0ff,
                                          0xffc77dff, 0xff1f9e8c, 0xffe0a3ff, 0xff9fb5ae };
    return juce::Colour (wheel[(size_t) (i % 8)]);
}

constexpr float kFloorTop = 0.16f;      // where the back of the stage sits, as a share of the height

// One set of numbers for the inspector, so paint and resized cannot disagree about where a label
// goes. They did, and the labels landed on top of the knobs.
constexpr int kHeadH = 24, kKnobH = 66, kSegH = 26, kLockH = 22, kLabelH = 16, kGap = 6;
}

struct StereoRoomUi::Parts
{
    std::vector<Node> nodes;
    int selected = -1;
    int dragging = -1;

    Segmented travel { { "STILL", "SWAY", "SWING", "CIRCLE", "DRIFT" } };
    Knob place { "PLACE", -1.0f, 1.0f, 0.0f }, depth { "DEPTH", 0.0f, 1.0f, 0.0f }, width { "WIDTH", 0.0f, 1.0f, 0.0f };
    Knob bars { "BARS", 0.25f, 8.0f, 4.0f }, reach { "REACH", 0.0f, 1.0f, 0.0f }, start { "START", 0.0f, 1.0f, 0.0f };
    Gem lock { "Lock to the middle, in mono" };
    Gem roomOn { "On" };
    Knob size { "SIZE", 0.0f, 1.0f, 0.5f }, decay { "DECAY", 0.0f, 1.0f, 0.5f }, damp { "DAMP", 0.0f, 1.0f, 0.5f };
    Knob level { "LEVEL", 0.0f, 1.0f, 0.35f }, monoBelow { "MONO BELOW", 0.0f, 250.0f, 120.0f };
    Pill spread { "SPREAD THEM OUT FOR ME" }, clearAll { "CLEAR ALL" };
    juce::Label hint { {}, "" };
};

StereoRoomUi::StereoRoomUi (bc::RoomView& v) : view (v)
{
    parts = std::make_unique<Parts>();
    auto& q = *parts;

    addAndMakeVisible (q.travel);
    for (auto* k : { &q.place, &q.depth, &q.width, &q.bars, &q.reach, &q.start,
                     &q.size, &q.decay, &q.damp, &q.level, &q.monoBelow })
        addAndMakeVisible (k);
    for (auto* g : { &q.lock, &q.roomOn }) addAndMakeVisible (g);
    addAndMakeVisible (q.spread);
    addAndMakeVisible (q.clearAll);
    addAndMakeVisible (q.hint);

    auto& proc = static_cast<MjqProcessor&> (view.rooms);

    q.place.setTooltip ("Where it stands, left to right. Moving it does not change how loud it is.");
    q.depth.setTooltip ("How far back. Further away is duller and sends more to the room, which is what reads as distance.");
    q.width.setTooltip ("How wide. Mid/side, so a mono listener hears the same thing whatever this is set to.");
    q.bars.setTooltip ("How long one lap takes. A quarter beat is ear to ear rapidly; eight bars is a slow drift.");
    q.reach.setTooltip ("How far it travels from where you put it.");
    q.start.setTooltip ("Where in the lap it begins, so two things need not move together.");

    q.monoBelow.setTooltip ("Everything under this is summed to the middle, whatever each thing's own place says. "
                            "A locked element is already mono; this catches the low end inside everything else.");

    q.lock.setTooltip ("Pinned to the middle, in mono, and no path moves it. On by default for anything holding the bottom of the mix.");
    q.roomOn.setTooltip ("Switching the room on changes nothing until you move something.");

    q.spread.setTooltip ("Fans everything across the stage by where it falls in the bar. A sixteen-chop loop is sixty-four "
                         "things to place by hand, so this is the sane starting point.");
    q.spread.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        rebuildNodes();
        const int n = (int) parts->nodes.size();
        for (int i = 0; i < n && i < (int) p.places.size(); ++i)
        {
            auto& pl = p.places[(size_t) i];
            if (pl.locked) continue;
            const float t = n > 1 ? (float) i / (float) (n - 1) : 0.5f;
            pl.on = true;
            pl.x = -0.85f + 1.7f * t;
            pl.y = 0.12f + 0.5f * std::fabs (t - 0.5f) * 2.0f;
            pl.width = 0.35f + 0.3f * t;
            pl.phase = t;
        }
        refresh();
    };

    q.clearAll.setTooltip ("Everything back to the middle of the stage, at the front, as it was.");
    q.clearAll.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        for (auto& pl : p.places) { const bool keep = pl.locked; pl = {}; pl.locked = keep; }
        refresh();
    };

    auto edit = [this] (auto&& fn)
    {
        return [this, fn] (float v)
        {
            auto& p = static_cast<MjqProcessor&> (view.rooms);
            const int i = parts->selected;
            if (i < 0 || i >= (int) p.places.size()) return;
            fn (p.places[(size_t) i], v);
            p.stereoChanged();
            repaint();
        };
    };

    q.place.onChange = edit ([] (nsr::Place& pl, float v) { pl.x = v; });
    q.depth.onChange = edit ([] (nsr::Place& pl, float v) { pl.y = v; });
    q.width.onChange = edit ([] (nsr::Place& pl, float v) { pl.width = v; });
    q.bars.onChange = edit ([] (nsr::Place& pl, float v) { pl.bars = v; });
    q.reach.onChange = edit ([] (nsr::Place& pl, float v) { pl.depth = v; });
    q.start.onChange = edit ([] (nsr::Place& pl, float v) { pl.phase = v; });
    q.travel.onSelect = [this] (int t)
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        const int i = parts->selected;
        if (i < 0 || i >= (int) p.places.size()) return;
        p.places[(size_t) i].travel = (nsr::Travel) juce::jlimit (0, (int) nsr::Travel::Count - 1, t);
        p.places[(size_t) i].on = true;
        p.stereoChanged();
        repaint();
    };
    q.lock.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        const int i = parts->selected;
        if (i < 0 || i >= (int) p.places.size()) return;
        p.places[(size_t) i].locked = parts->lock.getToggleState();
        p.places[(size_t) i].on = true;
        p.stereoChanged();
        refresh();
    };

    q.roomOn.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.stage.on = parts->roomOn.getToggleState();
        p.stereoChanged();
        refresh();
    };
    auto stageEdit = [this] (auto&& fn)
    {
        return [this, fn] (float v)
        {
            auto& p = static_cast<MjqProcessor&> (view.rooms);
            fn (p.stage, v);
            p.stereoChanged();
        };
    };
    q.size.onChange = stageEdit ([] (nsr::Stage& st, float v) { st.size = v; });
    q.decay.onChange = stageEdit ([] (nsr::Stage& st, float v) { st.decay = v; });
    q.damp.onChange = stageEdit ([] (nsr::Stage& st, float v) { st.damp = v; });
    q.level.onChange = stageEdit ([] (nsr::Stage& st, float v) { st.level = v; });
    q.monoBelow.onChange = stageEdit ([] (nsr::Stage& st, float v) { st.monoBelowHz = v; });

    juce::ignoreUnused (proc);
    q.hint.setJustificationType (juce::Justification::centredLeft);
    rebuildNodes();
}

StereoRoomUi::~StereoRoomUi() = default;

// Everything in the preset, found on its own: every chop of every loop layer is its own node, and
// each voice of the instrument is one too. Nothing to set up.
void StereoRoomUi::rebuildNodes()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& nodes = parts->nodes;
    nodes.clear();

    int colour = 0;
    for (int layer = 0; layer < p.loops.numLayers; ++layer)
    {
        const auto& L = p.loops.layers[(size_t) layer];
        for (int h = 0; h < (int) L.hits.size(); ++h)
        {
            const auto& hit = L.hits[(size_t) h];
            if (! hit.sound) continue;
            Node n;
            n.name = "L" + juce::String (layer + 1) + " chop " + juce::String (h + 1);
            if (! hit.sound->name.empty()) n.name += ": " + juce::String (hit.sound->name).substring (0, 10);
            n.owner = layer;
            n.chop = h;
            n.lowEnd = hit.sound->f.low > 0.5f;      // most of its energy under 150 Hz
            n.colour = colourFor (colour++);
            nodes.push_back (std::move (n));
        }
    }

    if (! p.keys.zones.empty())
    {
        Node n;
        n.name = ! p.keys.name.empty() ? juce::String (p.keys.name) : juce::String ("The instrument");
        n.colour = colourFor (colour++);
        nodes.push_back (std::move (n));
    }

    // the places follow the nodes, and anything holding the low end starts locked
    if (p.places.size() != nodes.size())
    {
        const auto old = p.places;
        p.places.assign (nodes.size(), {});
        for (size_t i = 0; i < p.places.size(); ++i)
        {
            if (i < old.size()) p.places[i] = old[i];
            else if (nodes[i].lowEnd) p.places[i].locked = true;
        }
    }
    if (parts->selected >= (int) nodes.size()) parts->selected = nodes.empty() ? -1 : 0;
}

juce::Rectangle<int> StereoRoomUi::stageArea() const
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromLeft (258 + 10);
    r.removeFromRight (274 + 10);
    return r;
}

// where a node is drawn: back of the stage is narrow and high, the front is wide and low
juce::Point<float> StereoRoomUi::placeOf (int node) const
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    if (node < 0 || node >= (int) p.places.size()) return {};
    float x = 0.0f, y = 0.0f;
    nsr::placeAt (p.places[(size_t) node], p.stageBeats(), x, y);

    const auto area = stageArea().toFloat();
    const float depth = juce::jlimit (0.0f, 1.0f, y);
    const float near_ = 1.0f - depth;
    const float halfWidth = area.getWidth() * (0.10f + 0.38f * near_);
    const float cx = area.getCentreX() + x * halfWidth;
    const float cy = area.getY() + area.getHeight() * (kFloorTop + (0.80f - kFloorTop) * near_);
    return { cx, cy };
}

int StereoRoomUi::nodeAt (juce::Point<int> pt) const
{
    int best = -1; float bestD = 1e9f;
    for (int i = 0; i < (int) parts->nodes.size(); ++i)
    {
        const auto c = placeOf (i);
        const float d = c.getDistanceFrom (pt.toFloat());
        if (d < 34.0f && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void StereoRoomUi::selectAt (juce::Point<int> pt)
{
    const int i = nodeAt (pt);
    if (i >= 0) { parts->selected = i; refresh(); }
}

void StereoRoomUi::mouseDown (const juce::MouseEvent& e)
{
    if (! stageArea().contains (e.getPosition())) return;
    selectAt (e.getPosition());
    parts->dragging = nodeAt (e.getPosition());
}

void StereoRoomUi::mouseDrag (const juce::MouseEvent& e)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    const int i = parts->dragging;
    if (i < 0 || i >= (int) p.places.size()) return;

    auto& pl = p.places[(size_t) i];
    if (pl.locked) { parts->hint.setText ("That one is locked to the middle. Unlock it to move it.", juce::dontSendNotification); return; }

    const auto area = stageArea().toFloat();
    const float ny = juce::jlimit (0.0f, 1.0f,
        1.0f - (((float) e.position.y - area.getY()) / area.getHeight() - kFloorTop) / (0.80f - kFloorTop));
    const float near_ = 1.0f - ny;
    const float halfWidth = area.getWidth() * (0.10f + 0.38f * near_);
    const float nx = juce::jlimit (-1.0f, 1.0f, ((float) e.position.x - area.getCentreX()) / std::max (1.0f, halfWidth));

    pl.on = true;
    pl.x = nx;
    pl.y = ny;
    p.stereoChanged();
    refresh();
}

void StereoRoomUi::mouseUp (const juce::MouseEvent&) { parts->dragging = -1; }

void StereoRoomUi::tick()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    for (const auto& pl : p.places)
        if (pl.moves()) { repaint (stageArea()); return; }
}

void StereoRoomUi::refresh()
{
    rebuildNodes();
    auto& q = *parts;
    auto& p = static_cast<MjqProcessor&> (view.rooms);

    q.roomOn.setToggleState (p.stage.on, juce::dontSendNotification);
    q.size.setValue (p.stage.size);
    q.decay.setValue (p.stage.decay);
    q.damp.setValue (p.stage.damp);
    q.level.setValue (p.stage.level);
    q.monoBelow.setValue (p.stage.monoBelowHz);

    const int i = q.selected;
    const bool has = i >= 0 && i < (int) p.places.size();
    for (auto* c : std::initializer_list<juce::Component*> { &q.place, &q.depth, &q.width, &q.bars, &q.reach, &q.start, &q.travel, &q.lock })
        c->setEnabled (has);

    if (has)
    {
        const auto& pl = p.places[(size_t) i];
        q.place.setValue (pl.x);
        q.depth.setValue (pl.y);
        q.width.setValue (pl.width);
        q.bars.setValue (pl.bars);
        q.reach.setValue (pl.depth);
        q.start.setValue (pl.phase);
        q.travel.setSelected ((int) pl.travel);
        q.lock.setToggleState (pl.locked, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &q.place, &q.width, &q.bars, &q.reach, &q.start, &q.travel })
            c->setEnabled (! pl.locked);
    }

    q.hint.setText (parts->nodes.empty()
                      ? "Nothing to place yet. Make a loop or an instrument first."
                      : juce::String (parts->nodes.size()) + " things on the stage",
                    juce::dontSendNotification);
    repaint();
}

bool StereoRoomUi::key (const juce::KeyPress& k)
{
    if (parts->nodes.empty()) return false;
    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::upKey))
    { parts->selected = std::max (0, parts->selected - 1); refresh(); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::downKey))
    { parts->selected = std::min ((int) parts->nodes.size() - 1, parts->selected + 1); refresh(); return true; }
    if (k.isKeyCode ('M')) { parts->lock.setToggleState (! parts->lock.getToggleState(), juce::sendNotification); return true; }
    return false;
}

void StereoRoomUi::paint (juce::Graphics& g)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;

    paintPlate (g, getLocalBounds().toFloat());

    // ---- the list of what is on the stage
    auto r = getLocalBounds().reduced (10);
    auto left = r.removeFromLeft (258);
    paintWell (g, left.toFloat());
    {
        auto body = left.reduced (8);
        paintLabel (g, "IN THE STEREOROOM", body.removeFromTop (16));
        paintLabel (g, "Taken from the preset. Nothing to set up.", body.removeFromTop (26),
                    juce::Justification::topLeft, 10.0f, C::dim());
        for (int i = 0; i < (int) q.nodes.size(); ++i)
        {
            if (body.getHeight() < 24) { paintLabel (g, "...", body, juce::Justification::centredLeft, 10.0f, C::dim()); break; }
            auto row = body.removeFromTop (22);
            if (i == q.selected)
            {
                g.setColour (C::teal().withAlpha (0.16f));
                g.fillRoundedRectangle (row.toFloat().reduced (1.0f), 3.0f);
            }
            auto dot = row.removeFromLeft (16);
            g.setColour (q.nodes[(size_t) i].colour);
            g.fillEllipse (dot.withSizeKeepingCentre (9, 9).toFloat());

            auto tag = row.removeFromRight (46);
            const bool locked = i < (int) p.places.size() && p.places[(size_t) i].locked;
            g.setColour (locked ? C::amber() : C::dim());
            g.setFont (caps (8.0f));
            g.drawText (locked ? "MONO" : "FREE", tag, juce::Justification::centredRight, false);

            g.setColour (i == q.selected ? C::text() : C::label());
            g.setFont (font (11.0f));
            g.drawText (q.nodes[(size_t) i].name, row.reduced (2, 0), juce::Justification::centredLeft, true);
        }
    }

    // ---- the stage
    r.removeFromLeft (10);
    r.removeFromRight (274 + 10);
    auto area = r;
    {
        g.setColour (juce::Colour (0xff0d1413));
        g.fillRoundedRectangle (area.toFloat(), 7.0f);

        const auto f = area.toFloat();
        // lines running away from you
        for (int i = 0; i <= 8; ++i)
        {
            const float t = (float) i / 8.0f;
            const float xb = f.getX() + f.getWidth() * t;
            const float xt = f.getCentreX() + (xb - f.getCentreX()) * 0.26f;
            g.setColour (C::teal().withAlpha (0.10f));
            g.drawLine (xb, f.getY() + f.getHeight() * 0.80f, xt, f.getY() + f.getHeight() * kFloorTop, 1.0f);
        }
        // and across
        for (int i = 1; i <= 5; ++i)
        {
            const float t = (float) i / 6.0f;
            const float y = f.getY() + f.getHeight() * (kFloorTop + (0.80f - kFloorTop) * (1.0f - t * t));
            const float inset = f.getWidth() * 0.37f * t;
            g.setColour (C::teal().withAlpha (0.04f + 0.06f * t));
            g.drawLine (f.getX() + inset, y, f.getRight() - inset, y, 1.0f);
        }

        paintLabel (g, "LEFT",  area.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft, 9.0f, C::dim());
        paintLabel (g, "RIGHT", area.reduced (10, 6).removeFromTop (14), juce::Justification::topRight, 9.0f, C::dim());
        paintLabel (g, "BACK OF THE ROOM", area.reduced (10, 6).removeFromTop (14), juce::Justification::centredTop, 9.0f, C::dim());

        // where the listener is
        {
            juce::Path tri;
            const float cx = f.getCentreX(), cy = f.getBottom() - 20.0f;
            tri.addTriangle (cx - 8.0f, cy + 7.0f, cx + 8.0f, cy + 7.0f, cx, cy - 6.0f);
            g.setColour (C::dim());
            g.fillPath (tri);
        }

        // the path the selected thing walks
        if (q.selected >= 0 && q.selected < (int) p.places.size() && p.places[(size_t) q.selected].moves())
        {
            juce::Path path;
            const auto& pl = p.places[(size_t) q.selected];
            for (int i = 0; i <= 64; ++i)
            {
                const double beat = (double) i / 64.0 * (double) pl.bars * 4.0;
                float px = 0.0f, py = 0.0f;
                nsr::placeAt (pl, beat, px, py);
                const float near_ = 1.0f - juce::jlimit (0.0f, 1.0f, py);
                const float hw = f.getWidth() * (0.10f + 0.38f * near_);
                const juce::Point<float> at (f.getCentreX() + px * hw,
                                             f.getY() + f.getHeight() * (kFloorTop + (0.80f - kFloorTop) * near_));
                if (i == 0) path.startNewSubPath (at); else path.lineTo (at);
            }
            g.setColour (C::amber().withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (1.4f));
        }

        // and the things themselves, far ones first so near ones sit over them
        std::vector<int> order;
        for (int i = 0; i < (int) q.nodes.size(); ++i) order.push_back (i);
        std::sort (order.begin(), order.end(), [this, &p] (int a, int b)
        {
            const float ya = a < (int) p.places.size() ? p.places[(size_t) a].y : 0.0f;
            const float yb = b < (int) p.places.size() ? p.places[(size_t) b].y : 0.0f;
            return ya > yb;
        });

        for (int i : order)
        {
            const auto c = placeOf (i);
            const float depth = i < (int) p.places.size() ? p.places[(size_t) i].y : 0.0f;
            const float near_ = 1.0f - juce::jlimit (0.0f, 1.0f, depth);
            const float size = 38.0f + 62.0f * near_;
            const auto colour = q.nodes[(size_t) i].colour;

            juce::ColourGradient grad (colour.withAlpha (0.78f * (0.45f + 0.55f * near_)),
                                       c.x - size * 0.14f, c.y - size * 0.18f,
                                       colour.withAlpha (0.0f), c.x, c.y + size * 0.5f, true);
            grad.addColour (0.55, colour.withAlpha (0.20f));
            g.setGradientFill (grad);
            g.fillEllipse (c.x - size * 0.5f, c.y - size * 0.42f, size, size * 0.84f);

            if (i == q.selected)
            {
                g.setColour (C::amber());
                g.drawEllipse (c.x - size * 0.5f - 3.0f, c.y - size * 0.42f - 3.0f, size + 6.0f, size * 0.84f + 6.0f, 1.5f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (font (10.0f).boldened());
            g.drawText (q.nodes[(size_t) i].name.upToFirstOccurrenceOf (":", false, false),
                        juce::Rectangle<float> (c.x - 50.0f, c.y - 7.0f, 100.0f, 14.0f).toNearestInt(),
                        juce::Justification::centred, false);
        }

        if (q.nodes.empty())
            paintLabel (g, "Nothing to place yet. Make a loop in LOOPS or an instrument in KEYS.",
                        area.reduced (20), juce::Justification::centred, 11.0f, C::dim());
    }

    // ---- the inspector
    auto right = getLocalBounds().reduced (10).removeFromRight (274);
    paintWell (g, right.toFloat());
    {
        auto body = right.reduced (8);
        auto head = body.removeFromTop (kHeadH);
        if (q.selected >= 0 && q.selected < (int) q.nodes.size())
        {
            auto dot = head.removeFromLeft (16);
            g.setColour (q.nodes[(size_t) q.selected].colour);
            g.fillEllipse (dot.withSizeKeepingCentre (10, 10).toFloat());
            g.setColour (C::text());
            g.setFont (font (12.0f));
            g.drawText (q.nodes[(size_t) q.selected].name, head, juce::Justification::centredLeft, true);
        }
        body.removeFromTop (kKnobH + kGap);
        paintLabel (g, "HOW IT TRAVELS", body.removeFromTop (kLabelH));
        body.removeFromTop (kSegH + kGap + kKnobH + kGap + kLockH + kGap);
        paintLabel (g, "THE ROOM", body.removeFromTop (kLabelH));
    }
}

void StereoRoomUi::resized()
{
    auto& q = *parts;
    auto right = getLocalBounds().reduced (10).removeFromRight (274).reduced (8);

    // the two buttons first, off the bottom, so the rest cannot run over them
    q.spread.setBounds (right.removeFromBottom (28));
    right.removeFromBottom (4);
    q.clearAll.setBounds (right.removeFromBottom (24));
    right.removeFromBottom (6);

    right.removeFromTop (kHeadH);

    auto row = right.removeFromTop (kKnobH);
    const int kw = row.getWidth() / 3;
    q.place.setBounds (row.removeFromLeft (kw));
    q.depth.setBounds (row.removeFromLeft (kw));
    q.width.setBounds (row);
    right.removeFromTop (kGap);

    right.removeFromTop (kLabelH);
    q.travel.setBounds (right.removeFromTop (kSegH));
    right.removeFromTop (kGap);

    row = right.removeFromTop (kKnobH);
    q.bars.setBounds (row.removeFromLeft (kw));
    q.reach.setBounds (row.removeFromLeft (kw));
    q.start.setBounds (row);
    right.removeFromTop (kGap);

    q.lock.setBounds (right.removeFromTop (kLockH));
    right.removeFromTop (kGap);

    right.removeFromTop (kLabelH);
    q.roomOn.setBounds (right.removeFromTop (kLockH));
    row = right.removeFromTop (kKnobH);
    const int sw = std::max (1, row.getWidth() / 4);
    q.size.setBounds (row.removeFromLeft (sw));
    q.decay.setBounds (row.removeFromLeft (sw));
    q.damp.setBounds (row.removeFromLeft (sw));
    q.level.setBounds (row);
    q.monoBelow.setBounds (right.removeFromTop (kKnobH));

    auto left = getLocalBounds().reduced (10).removeFromLeft (258).reduced (8);
    q.hint.setBounds (left.removeFromBottom (20));
}
