// The stereo room, on screen: a picture you drag a voice around in, and a lane you draw its movement into.
//
// The picture is seen from above, with you at the bottom: left and right across, close and far up the page.
// All four voices are drawn, so a multi can be arranged by eye; the chosen one is solid and the others are
// faint. The lane under it is one bar (or several) of drawn movement, drawn with the mouse.
#pragma once

#include "../core/Room.h"
#include "Ui.h"

namespace mjq
{
// The picture, from above.
class RoomPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (float, float)> onMove;     // the chosen voice was dragged somewhere

    void setVoices (const std::array<Place, 4>& p, int chosenVoice, double beat, int perBar)
    {
        places = p; chosen = juce::jlimit (0, 3, chosenVoice); beats = beat; steps = perBar;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace nsui;
        auto r = getLocalBounds().toFloat();
        paintLcd (g, r, 8.0f);
        auto inner = r.reduced (10.0f);

        // the room: the listener at the bottom, the back wall at the top
        g.setColour (C::lcdDim().withAlpha (0.3f));
        for (float f : { 0.25f, 0.5f, 0.75f })
        {
            const float y = inner.getBottom() - inner.getHeight() * f;
            g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
        }
        g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());

        // you, at the front
        const juce::Point<float> ear (inner.getCentreX(), inner.getBottom() + 2.0f);
        g.setColour (C::lcdDim().withAlpha (0.7f));
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (ear), 1.2f);

        for (int v = 0; v < 4; ++v)
        {
            const auto& p = places[(size_t) v];
            float x = 0.0f, y = 0.0f;
            roomPlaceAt (p, beats, steps, x, y);

            const auto at = pointFor (inner, x, y);
            const bool isChosen = v == chosen;
            const float size = 7.0f + 5.0f * (1.0f - y);          // nearer reads bigger

            // where it was put, if a curve has carried it elsewhere
            if (p.moves())
            {
                const auto home = pointFor (inner, p.x, p.y);
                g.setColour (C::lcdDim().withAlpha (isChosen ? 0.5f : 0.2f));
                g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (home), 1.0f);
                g.drawLine (home.x, home.y, at.x, at.y, 1.0f);
            }

            auto dot = juce::Rectangle<float> (size, size).withCentre (at);
            if (! p.on)
            {
                g.setColour (C::lcdDim().withAlpha (isChosen ? 0.55f : 0.22f));
                g.drawEllipse (dot, isChosen ? 1.6f : 1.0f);
            }
            else
            {
                g.setColour ((isChosen ? C::glow() : C::lcdDim()).withAlpha (isChosen ? 0.9f : 0.4f));
                g.fillEllipse (dot);
                if (isChosen) { g.setColour (C::lcdText()); g.drawEllipse (dot, 1.4f); }
            }
            g.setFont (font (8.5f, true));
            g.setColour (isChosen ? C::lcdText() : C::lcdDim().withAlpha (0.7f));
            g.drawText (juce::String (v + 1), dot.expanded (4.0f).toNearestInt(), juce::Justification::centred, false);
        }

        g.setFont (font (8.0f));
        g.setColour (C::lcdDim().withAlpha (0.8f));
        g.drawText ("far", inner.removeFromTop (12).toNearestInt(), juce::Justification::topLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onMove) return;
        auto inner = getLocalBounds().toFloat().reduced (10.0f);
        if (inner.getWidth() < 2.0f || inner.getHeight() < 2.0f) return;
        const float x = juce::jlimit (-1.0f, 1.0f, (e.position.x - inner.getCentreX()) / (inner.getWidth() * 0.5f));
        const float y = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / inner.getHeight());
        onMove (x, y);
    }

private:
    static juce::Point<float> pointFor (juce::Rectangle<float> inner, float x, float y)
    {
        return { inner.getCentreX() + x * inner.getWidth() * 0.5f, inner.getBottom() - y * inner.getHeight() };
    }

    std::array<Place, 4> places {};
    int chosen = 0;
    double beats = 0.0;
    int steps = 16;
};

// The lane the movement is drawn into.
class RoomCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int, float)> onDraw;       // a point of the curve was drawn
    std::function<void()> onDone;

    void setCurve (const Place& p, double beat) { place = p; beats = beat; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace nsui;
        auto r = getLocalBounds().toFloat();
        paintLcd (g, r, 6.0f);
        auto inner = r.reduced (4.0f);
        const float mid = inner.getCentreY();

        g.setColour (C::lcdDim().withAlpha (0.35f));
        g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());
        for (int b = 1; b < std::max (1, place.bars); ++b)
        {
            const float x = inner.getX() + inner.getWidth() * (float) b / (float) std::max (1, place.bars);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        }

        // the drawn shape
        juce::Path path;
        for (int i = 0; i < kRoomCurve; ++i)
        {
            const float x = inner.getX() + inner.getWidth() * ((float) i + 0.5f) / (float) kRoomCurve;
            const float y = mid - place.curve[(size_t) i] * inner.getHeight() * 0.5f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (place.moves() ? C::glow() : C::lcdDim());
        g.strokePath (path, juce::PathStrokeType (1.8f));

        // where in the bar it is now
        if (place.moves())
        {
            const double span = (double) std::max (1, place.bars) * 4.0;
            double phase = std::fmod (beats, span);
            if (phase < 0.0) phase += span;
            const float x = inner.getX() + inner.getWidth() * (float) (phase / span);
            g.setColour (C::lcdText().withAlpha (0.8f));
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        }

        if (place.move == RoomMove::None)
        {
            g.setFont (font (9.5f));
            g.setColour (C::lcdDim());
            g.drawText ("choose what it moves", getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onDraw) return;
        auto inner = getLocalBounds().toFloat().reduced (4.0f);
        if (inner.getWidth() < 2.0f) return;
        const int i = juce::jlimit (0, kRoomCurve - 1, (int) ((e.position.x - inner.getX()) / inner.getWidth() * (float) kRoomCurve));
        const float v = juce::jlimit (-1.0f, 1.0f, (inner.getCentreY() - e.position.y) / (inner.getHeight() * 0.5f));
        onDraw (i, v);
    }
    void mouseUp (const juce::MouseEvent&) override { if (onDone) onDone(); }

private:
    Place place;
    double beats = 0.0;
};
} // namespace mjq
