// The Orb, on screen: a ring of eight corners with a joystick in the middle of them.
//
// Drag the knob and the line changes under your hand. A corner with something in it is drawn solid, an empty
// one hollow; the brightness of each shows how much say it currently has, so you can see the blend as well as
// hear it. Click a corner to travel to it, shift-click to put the line you are playing into it.
#pragma once

#include "../core/Orb.h"
#include "Ui.h"

namespace mjq
{
class OrbPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    OrbPad() { setWantsKeyboardFocus (false); }

    std::function<void (float, float)> onMove;      // the joystick, in a circle of radius 1
    std::function<void (int)> onTravel;             // go to a corner
    std::function<void (int)> onPut;                // put the line being played into a corner
    std::function<void (int)> onPick;               // a corner was chosen, to edit its stack

    void setOrb (const Orb& o, float jx, float jy, int picked)
    {
        orb = o; x = jx; y = jy; chosen = picked;
        blend = orbRead (orb, x, y);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace nsui;
        auto r = getLocalBounds().toFloat();
        paintLcd (g, r, 8.0f);

        const auto c = r.getCentre();
        const float radius = std::min (r.getWidth(), r.getHeight()) * 0.5f - kCornerR - 6.0f;

        // the rings, so the middle reads as a place rather than a gap
        g.setColour (C::lcdDim().withAlpha (0.28f));
        for (float f : { 0.33f, 0.66f, 1.0f }) g.drawEllipse (juce::Rectangle<float> (radius * 2.0f * f, radius * 2.0f * f).withCentre (c), 1.0f);
        for (int i = 0; i < kOrbCorners; ++i)
        {
            float cx = 0.0f, cy = 0.0f;
            orbCornerAt (i, cx, cy);
            g.drawLine (c.x, c.y, c.x + cx * radius, c.y + cy * radius, 1.0f);
        }

        // the corners
        for (int i = 0; i < kOrbCorners; ++i)
        {
            const auto p = placeOf (i, c, radius);
            const auto& corner = orb.corners[(size_t) i];
            const float say = blend.weight[(size_t) i];
            auto dot = juce::Rectangle<float> (kCornerR * 2.0f, kCornerR * 2.0f).withCentre (p);

            if (say > 0.01f)
            {
                g.setColour (C::glow().withAlpha (0.15f + 0.55f * say));
                g.fillEllipse (dot.expanded (3.0f + 5.0f * say));
            }
            if (corner.set)
            {
                g.setColour (C::glow().withAlpha (0.45f + 0.55f * say));
                g.fillEllipse (dot);
                g.setColour (C::lcdText());
                g.drawEllipse (dot, i == chosen ? 2.0f : 1.0f);
            }
            else
            {
                g.setColour (C::lcdDim().withAlpha (0.6f));
                g.drawEllipse (dot, i == chosen ? 2.0f : 1.0f);
            }

            g.setFont (font (9.0f, true));
            g.setColour (corner.set ? C::lcdText() : C::lcdDim());
            g.drawText (juce::String (i + 1), dot.toNearestInt(), juce::Justification::centred, false);
        }

        // the joystick, and the line from the middle so its distance reads
        const juce::Point<float> knob (c.x + x * radius, c.y + y * radius);
        g.setColour (C::lcdDim().withAlpha (0.5f));
        g.drawLine (c.x, c.y, knob.x, knob.y, 1.0f);

        auto k = juce::Rectangle<float> (kKnobR * 2.0f, kKnobR * 2.0f).withCentre (knob);
        g.setColour (C::glow().withAlpha (0.3f));
        g.fillEllipse (k.expanded (5.0f));
        g.setColour (C::lcdText());
        g.fillEllipse (k);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (k, 1.0f);

        if (! orb.anySet())
        {
            g.setFont (font (10.5f));
            g.setColour (C::lcdDim());
            g.drawText ("Put a line in a corner to begin", getLocalBounds().reduced (10), juce::Justification::centredBottom, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int corner = cornerUnder (e.position);
        if (corner >= 0)
        {
            if (onPick) onPick (corner);
            if (e.mods.isShiftDown()) { if (onPut) onPut (corner); }
            else if (onTravel) onTravel (corner);
            dragging = false;
            return;
        }
        dragging = true;
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || ! onMove) return;
        auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre();
        const float radius = std::min (r.getWidth(), r.getHeight()) * 0.5f - kCornerR - 6.0f;
        if (radius <= 1.0f) return;
        float nx = (e.position.x - c.x) / radius, ny = (e.position.y - c.y) / radius;
        const float len = std::sqrt (nx * nx + ny * ny);
        if (len > 1.0f) { nx /= len; ny /= len; }
        onMove (nx, ny);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (cornerUnder (e.position) < 0 && onMove) onMove (0.0f, 0.0f); }

private:
    static constexpr float kCornerR = 11.0f, kKnobR = 7.0f;

    juce::Point<float> placeOf (int i, juce::Point<float> c, float radius) const
    {
        float cx = 0.0f, cy = 0.0f;
        orbCornerAt (i, cx, cy);
        return { c.x + cx * radius, c.y + cy * radius };
    }

    int cornerUnder (juce::Point<float> p) const
    {
        auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre();
        const float radius = std::min (r.getWidth(), r.getHeight()) * 0.5f - kCornerR - 6.0f;
        for (int i = 0; i < kOrbCorners; ++i)
            if (placeOf (i, c, radius).getDistanceFrom (p) <= kCornerR + 2.0f) return i;
        return -1;
    }

    Orb orb;
    OrbBlend blend;
    float x = 0.0f, y = 0.0f;
    int chosen = 0;
    bool dragging = false;
};
} // namespace mjq
