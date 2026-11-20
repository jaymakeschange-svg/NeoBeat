#include "ModRoom.h"
#include "Processor.h"
#include "../neobeat/shared/Ui.h"

using namespace nsui;

namespace
{
const juce::Colour kBlue { 0xff7ca0ff };

const char* kCornerNames[] = { "WALKING", "SPARSE", "DENSE", "ACID", "HIGH", "LOW", "STAB", "ROLL" };
}

struct ModRoom::Parts
{
    Knob grip { "GRIP", 0.5f, 6.0f, 3.0f };
    Knob stack { "STACK", 1.0f, 4.0f, 4.0f };
    Knob drift { "DRIFT", 0.0f, 1.0f, 0.18f };
    Pill fill { "FILL WITH VARIATIONS" };
    Pill clearAll { "CLEAR ALL" };
    Pill addRoute { "ADD A ROUTE" };
    juce::Label note { {}, "" };
    int dragging = -1;
};

ModRoom::ModRoom (bc::RoomView& v) : view (v)
{
    parts = std::make_unique<Parts>();
    auto& q = *parts;

    for (auto* k : { &q.grip, &q.stack, &q.drift }) addAndMakeVisible (k);
    for (auto* b : { &q.fill, &q.clearAll, &q.addRoute }) addAndMakeVisible (b);
    addAndMakeVisible (q.note);

    q.grip.setTooltip ("How tightly the Orb holds the corner it is nearest. Higher is a cleaner switch between them.");
    q.stack.setTooltip ("How many of the eight corners' effects are stacked at once.");
    q.drift.setTooltip ("The Orb wandering on its own.");

    q.fill.setTooltip ("Writes eight related lines into the corners rather than eight copies of one. "
                       "Putting the same line in corner after corner gives eight identical corners, "
                       "which is why dragging the Orb used to do nothing.");
    q.fill.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.scatterOrb();
        refresh();
    };

    q.clearAll.setTooltip ("Every corner emptied.");
    q.clearAll.onClick = [this]
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.clearOrb();
        refresh();
    };

    q.grip.onChange = [this] (float v)
    {
        auto& p = static_cast<MjqProcessor&> (view.rooms);
        p.setOrbGrip (v);
    };

    q.note.setJustificationType (juce::Justification::topLeft);
    q.note.setText ("The randomiser is a source and a destination, so a slow shape can open and close "
                    "how wild it gets. It cannot route into itself.", juce::dontSendNotification);
}

ModRoom::~ModRoom() = default;

juce::Rectangle<int> ModRoom::orbArea() const
{
    auto r = getLocalBounds().reduced (10).removeFromLeft (336).reduced (12);
    r.removeFromTop (22);
    const int side = std::min (r.getWidth(), r.getHeight() - 150);
    return r.removeFromTop (std::max (120, side)).withSizeKeepingCentre (std::max (120, side), std::max (120, side));
}

void ModRoom::mouseDown (const juce::MouseEvent& e)
{
    if (orbArea().contains (e.getPosition())) { parts->dragging = 1; mouseDrag (e); }
}

void ModRoom::mouseDrag (const juce::MouseEvent& e)
{
    if (parts->dragging != 1) return;
    const auto a = orbArea().toFloat();
    const float x = juce::jlimit (-1.0f, 1.0f, (e.position.x - a.getCentreX()) / (a.getWidth() * 0.42f));
    const float y = juce::jlimit (-1.0f, 1.0f, (e.position.y - a.getCentreY()) / (a.getHeight() * 0.42f));
    static_cast<MjqProcessor&> (view.rooms).moveOrb (x, y);
    repaint();
}

void ModRoom::refresh()
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    auto& q = *parts;
    const auto& orb = p.orbFor (p.chosenTrack());
    q.grip.setValue (orb.grip);
    repaint();
}

void ModRoom::paint (juce::Graphics& g)
{
    auto& p = static_cast<MjqProcessor&> (view.rooms);
    paintPlate (g, getLocalBounds().toFloat());

    auto r = getLocalBounds().reduced (10);

    // ---- the Orb
    auto left = r.removeFromLeft (336);
    paintWell (g, left.toFloat());
    {
        auto head = left.reduced (12).removeFromTop (20);
        paintLabel (g, "THE ORB", head, juce::Justification::centredLeft, 11.0f, C::amber());
    }
    const auto a = orbArea();
    {
        g.setColour (juce::Colour (0xff141c19));
        g.fillRoundedRectangle (a.toFloat(), 6.0f);

        const auto f = a.toFloat();
        for (float ring : { 0.20f, 0.30f, 0.38f })
        {
            g.setColour (C::teal().withAlpha (0.08f));
            g.drawEllipse (f.getCentreX() - f.getWidth() * ring, f.getCentreY() - f.getHeight() * ring,
                           f.getWidth() * ring * 2.0f, f.getHeight() * ring * 2.0f, 1.0f);
        }

        const auto& orb = p.orbFor (p.chosenTrack());
        for (int i = 0; i < mjq::kOrbCorners; ++i)
        {
            const float ang = juce::MathConstants<float>::twoPi * (float) i / (float) mjq::kOrbCorners
                            - juce::MathConstants<float>::halfPi;
            const juce::Point<float> at (f.getCentreX() + std::cos (ang) * f.getWidth() * 0.38f,
                                         f.getCentreY() + std::sin (ang) * f.getHeight() * 0.38f);
            auto box = juce::Rectangle<float> (0, 0, 76, 24).withCentre (at);
            paintTile (g, box, C::teal().withAlpha (0.10f), kCornerNames[i], false);
            juce::ignoreUnused (orb);
        }

        // where it is now
        const auto& o = p.orbFor (p.chosenTrack());
        const juce::Point<float> at (f.getCentreX() + o.x * f.getWidth() * 0.42f,
                                     f.getCentreY() + o.y * f.getHeight() * 0.42f);
        juce::ColourGradient grad (C::glow(), at.x - 6.0f, at.y - 6.0f, C::teal().withAlpha (0.0f), at.x, at.y + 18.0f, true);
        g.setGradientFill (grad);
        g.fillEllipse (at.x - 17.0f, at.y - 17.0f, 34.0f, 34.0f);
        g.setColour (C::teal().withAlpha (0.4f));
        g.drawEllipse (at.x - 28.0f, at.y - 28.0f, 56.0f, 56.0f, 1.0f);
    }

    // ---- the sources
    r.removeFromLeft (10);
    auto mid = r;
    mid.removeFromRight (300 + 10);
    paintWell (g, mid.toFloat());
    {
        auto body = mid.reduced (12);
        paintLabel (g, "SOURCES", body.removeFromTop (18), juce::Justification::centredLeft, 11.0f, kBlue);

        struct Src { const char* name; const char* detail; juce::Colour colour; int shape; };
        const Src sources[] = {
            { "LFO 1",  "8 bars · sine",     kBlue,     0 },
            { "LFO 2",  "1/2 beat · drawn",  kBlue,     1 },
            { "ENV 1",  "on each note",      C::teal(), 2 },
            { "RANDOM", "cycles · 2 bars",   C::amber(),3 }
        };
        for (const auto& s : sources)
        {
            if (body.getHeight() < 70) break;
            auto card = body.removeFromTop (72);
            g.setColour (juce::Colour (0xff141c19));
            g.fillRoundedRectangle (card.toFloat().reduced (0, 3), 5.0f);
            auto in = card.reduced (9, 9);
            auto head = in.removeFromTop (18);
            auto dot = head.removeFromLeft (16);
            g.setColour (s.colour);
            g.fillEllipse (dot.withSizeKeepingCentre (10, 10).toFloat());
            g.setColour (C::text());
            g.setFont (font (11.0f));
            g.drawText (s.name, head.removeFromLeft (80), juce::Justification::centredLeft, false);
            g.setColour (C::label());
            g.setFont (font (10.0f));
            g.drawText (s.detail, head, juce::Justification::centredRight, false);

            auto box = in.reduced (0, 2);
            g.setColour (juce::Colour (0xff1a2320));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            juce::Path path;
            for (int k = 0; k <= 100; ++k)
            {
                const float t = (float) k / 100.0f;
                float y = 0.0f;
                switch (s.shape)
                {
                    case 0: y = std::sin (t * juce::MathConstants<float>::twoPi); break;
                    case 1: y = (std::fmod (t * 8.0f, 1.0f) < 0.5f) ? 0.7f : -0.5f; break;
                    case 2: y = std::exp (-std::fmod (t * 4.0f, 1.0f) * 6.0f) * 0.95f; break;
                    default: y = std::sin (t * 37.1f) * 0.45f + std::sin (t * 5.3f) * 0.45f; break;
                }
                const juce::Point<float> at ((float) box.getX() + (float) box.getWidth() * t,
                                             (float) box.getCentreY() - y * (float) box.getHeight() * 0.4f);
                if (k == 0) path.startNewSubPath (at); else path.lineTo (at);
            }
            g.setColour (s.colour);
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }
    }

    // ---- the routes
    auto right = getLocalBounds().reduced (10).removeFromRight (300);
    paintWell (g, right.toFloat());
    {
        auto body = right.reduced (12);
        paintLabel (g, "ROUTES", body.removeFromTop (18), juce::Justification::centredLeft, 11.0f, kBlue);
        paintLabel (g, "Right-click anything in any room to send a source to it.",
                    body.removeFromTop (26), juce::Justification::topLeft, 10.0f, C::dim());

        struct Route { const char* src; const char* dest; const char* depth; juce::Colour colour; };
        const Route routes[] = {
            { "LFO 1",  "Orb · x",            "55%",  kBlue },
            { "LFO 1",  "Random · reach",     "60%",  kBlue },
            { "LFO 2",  "Bright",             "42%",  kBlue },
            { "LFO 2",  "Chop 3 · place",     "80%",  kBlue },
            { "ENV 1",  "Level",              "88%",  C::teal() },
            { "ENV 1",  "Random · rate",      "25%",  C::teal() },
            { "RANDOM", "Stereoroom · width", "18%",  C::amber() },
            { "RANDOM", "Traffic",            "35%",  C::amber() },
            { "ORB",    "Effect stack",       "100%", C::amber() }
        };
        for (const auto& rt : routes)
        {
            if (body.getHeight() < 60) break;
            auto row = body.removeFromTop (28);
            g.setColour (juce::Colour (0xff141c19));
            g.fillRoundedRectangle (row.toFloat().reduced (0, 2), 4.0f);
            auto in = row.reduced (8, 0);
            g.setColour (rt.colour);
            g.setFont (font (10.0f));
            g.drawText (rt.src, in.removeFromLeft (52), juce::Justification::centredLeft, false);
            g.setColour (C::dim());
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), in.removeFromLeft (14), juce::Justification::centred, false);
            g.setColour (C::text());
            g.drawText (rt.dest, in.removeFromLeft (in.getWidth() - 40), juce::Justification::centredLeft, true);
            g.setColour (C::label());
            g.drawText (rt.depth, in, juce::Justification::centredRight, false);
        }
    }
}

void ModRoom::resized()
{
    auto& q = *parts;
    auto left = getLocalBounds().reduced (10).removeFromLeft (336).reduced (12);
    left.removeFromTop (22);
    const auto orb = orbArea();
    left.removeFromTop (orb.getHeight());
    left.removeFromTop (8);

    auto row = left.removeFromTop (64);
    const int kw = row.getWidth() / 3;
    q.grip.setBounds (row.removeFromLeft (kw));
    q.stack.setBounds (row.removeFromLeft (kw));
    q.drift.setBounds (row);
    left.removeFromTop (6);
    q.fill.setBounds (left.removeFromTop (26));
    left.removeFromTop (4);
    q.clearAll.setBounds (left.removeFromTop (24));

    auto right = getLocalBounds().reduced (10).removeFromRight (300).reduced (12);
    q.addRoute.setBounds (right.removeFromBottom (28));
    right.removeFromBottom (6);
    q.note.setBounds (right.removeFromBottom (54));
}
