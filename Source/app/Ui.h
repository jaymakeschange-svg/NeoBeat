// MidiJaQ - the look (the chrome-pearl skin NeoBeat and Batchcut wear) and the small widgets: knobs,
// pills, segmented switches. Kept the same in all three source trees.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace mjqui
{
namespace C
{
    inline juce::Colour text()    { return juce::Colour (0xff1f2925); }
    inline juce::Colour label()   { return juce::Colour (0xff2b3631); }
    inline juce::Colour dim()     { return juce::Colour (0xff4b5852); }
    inline juce::Colour teal()    { return juce::Colour (0xff1f9e8c); }
    inline juce::Colour glow()    { return juce::Colour (0xff5fd8c4); }
    inline juce::Colour lcdText() { return juce::Colour (0xffaaf6d0); }
    inline juce::Colour lcdDim()  { return juce::Colour (0xff5fae8a); }
    inline juce::Colour amber()   { return juce::Colour (0xfff5b25a); }
    inline juce::Colour red()     { return juce::Colour (0xffd9534a); }
    // groups of events, on the dark display
    inline juce::Colour group (int g)
    {
        static const juce::uint32 c[] = { 0xff7fe0d0, 0xfff5b25a, 0xffe58ad8, 0xff8fb8ff, 0xffc6f27a, 0xffff8f7a, 0xfff2ee7a, 0xffb59cff };
        return g < 0 ? juce::Colour (0xffaab4af) : juce::Colour (c[(size_t) (g % 8)]);
    }
    // layers: background, tone, transient, noise, then your own
    inline juce::Colour layer (int l)
    {
        static const juce::uint32 c[] = { 0xff52695e, 0xff4fd9e0, 0xffffc35a, 0xffe07ad6, 0xff7fa8ff, 0xffb6f05a, 0xffff8a6a, 0xfff4f4f4 };
        return juce::Colour (c[(size_t) juce::jlimit (0, 7, l)]);
    }
}

inline juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
inline juce::Font caps (float size) { auto f = juce::Font (juce::FontOptions (size, juce::Font::bold)); f.setExtraKerningFactor (0.14f); return f; }
inline float textW (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }

inline void paintChassis (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffe9efeb), r.getY(), juce::Colour (0xffc8d1cc), r.getBottom()));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (float x = r.getX(); x < r.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
}
inline void paintPlate (juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.0f)
{
    juce::DropShadow (juce::Colour (0x40223028), 10, { 0, 4 }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffe2e8e4), r.getY(), juce::Colour (0xffbcc6c0), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine (r.getX() + radius, r.getY() + 0.8f, r.getRight() - radius, r.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colour (0x40000000));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}
inline void paintWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffa7b2ab), r.getY(), juce::Colour (0xffc3ccc6), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colour (0x5a14201a));
    g.drawRoundedRectangle (r.reduced (0.5f).translated (0, 0.8f), radius, 1.6f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (r.getX() + radius, r.getBottom() - 0.5f, r.getRight() - radius, r.getBottom() - 0.5f, 1.0f);
}
inline void paintLcd (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff0a1611), r.getY(), juce::Colour (0xff13261e), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawLine (r.getX() + radius, r.getBottom() + 0.5f, r.getRight() - radius, r.getBottom() + 0.5f, 1.0f);
}
inline void paintGem (juce::Graphics& g, juce::Rectangle<float> r, bool on = true, juce::Colour tint = {})
{
    if (on)
    {
        g.setColour (juce::Colour (0xccbef0ff)); g.fillEllipse (r.expanded (2.0f));
        if (! tint.isTransparent()) g.setColour (tint);
        else
        {
            juce::ColourGradient cg (juce::Colour (0xffbdf4ff), r.getX(), r.getY(), juce::Colour (0xffffd1f3), r.getRight(), r.getBottom(), false);
            cg.addColour (0.4, juce::Colour (0xffd6ffd0)); cg.addColour (0.7, juce::Colour (0xfffff1b3));
            g.setGradientFill (cg);
        }
    }
    else g.setColour (juce::Colour (0xff9aa6a0));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.fillEllipse (r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.18f, r.getWidth() * 0.28f, r.getHeight() * 0.28f);
    g.setColour (juce::Colour (0x8c3c4642));
    g.drawEllipse (r, 1.0f);
}
inline void paintLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft, float size = 9.0f, juce::Colour c = C::label())
{
    g.setFont (caps (size));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (t.toUpperCase(), r.translated (0, 1), j, true);
    g.setColour (c);
    g.drawText (t.toUpperCase(), r, j, true);
}

class Look final : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xffe6ece8));
        setColour (juce::PopupMenu::textColourId, C::text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::glow().withAlpha (0.55f));
        setColour (juce::PopupMenu::highlightedTextColourId, C::text());
        setColour (juce::ComboBox::textColourId, C::lcdText());
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff13261e));
        setColour (juce::TooltipWindow::textColourId, C::lcdText());
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff8a9791));
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xffe6ece8));
        setColour (juce::AlertWindow::textColourId, C::text());
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float rad = std::min (r.getHeight() * 0.5f, 14.0f);
        const bool on = b.getToggleState();
        // a tint only where one was set on purpose (solo, mute, record); the theme's own default is not one
        const auto tint = b.isColourSpecified (juce::TextButton::buttonOnColourId) ? b.findColour (juce::TextButton::buttonOnColourId) : juce::Colours::transparentBlack;
        if (on)
        {
            // lit: a pale glow with the tint below it, so the label stays dark on light
            const auto base = tint.isOpaque() ? tint.interpolatedWith (juce::Colours::white, 0.35f) : C::glow();
            g.setColour (base.withAlpha (0.5f)); g.fillRoundedRectangle (r.expanded (1.5f), rad + 1.5f);
            juce::ColourGradient cg = juce::ColourGradient::vertical (juce::Colour (0xfff2fffb), r.getY(), base, r.getBottom());
            cg.addColour (0.45, base.interpolatedWith (juce::Colour (0xfff2fffb), 0.55f));
            g.setGradientFill (cg);
        }
        else g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white, r.getY(), juce::Colour (0xffa9b4ae), r.getBottom()));
        g.fillRoundedRectangle (r, rad);
        if (over && ! on) { g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillRoundedRectangle (r, rad); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.1f)); g.fillRoundedRectangle (r, rad); }
        g.setColour (juce::Colour (0x66000000)); g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        if (! b.isEnabled()) { g.setColour (juce::Colour (0x70c6d0ca)); g.fillRoundedRectangle (r, rad); }
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return caps (std::min (9.5f, (float) h * 0.36f)); }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? juce::Colour (0xff08352f) : C::text());
        if (! b.isEnabled()) g.setColour (C::dim().withAlpha (0.6f));
        g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (6, 1), juce::Justification::centred, 1, 0.8f);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        paintLcd (g, juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 6.0f);
        juce::Path tri; const float cx = (float) w - 11.0f, cy = (float) h * 0.5f;
        tri.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (C::lcdDim()); g.fillPath (tri);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f, true); }
    void positionComboBoxText (juce::ComboBox& b, juce::Label& l) override
    {
        l.setBounds (8, 1, b.getWidth() - 26, b.getHeight() - 2);
        l.setFont (getComboBoxFont (b));
        l.setColour (juce::Label::textColourId, C::lcdText());
    }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xfff1f5f3), 0, juce::Colour (0xffd2dbd6), (float) h));
        g.fillRect (0, 0, w, h);
        g.setColour (juce::Colour (0x40000000)); g.drawRect (0, 0, w, h);
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        paintGem (g, juce::Rectangle<float> (r.getX() + 3, r.getCentreY() - 6, 12, 12), b.getToggleState());
        paintLabel (g, b.getButtonText(), b.getLocalBounds().withTrimmedLeft (20), juce::Justification::centredLeft, 8.0f);
    }
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        g.setColour (juce::Colour (0x22000000)); g.fillRoundedRectangle (r, 3.0f);
        auto t = vertical ? juce::Rectangle<float> (r.getX(), (float) thumbStart, r.getWidth(), (float) thumbSize) : juce::Rectangle<float> ((float) thumbStart, r.getY(), (float) thumbSize, r.getHeight());
        g.setColour (juce::Colour (0xff8a9791)); g.fillRoundedRectangle (t.reduced (1.0f), 3.0f);
    }
};

// A knob with its label and value. Values are plain (not normalised); toText formats them.
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::String l, float lo, float hi, float def, bool logScale = false) : label (std::move (l)), minV (lo), maxV (hi), defV (def), value (def), isLog (logScale) {}
    std::function<void (float)> onChange;      // while dragging
    std::function<void()> onCommit;            // when a drag (or a click, wheel, reset) is over
    std::function<juce::String (float)> toText;
    bool main = false;
    float step = 0.0f;                         // > 0: values snap to steps of this

    void setValue (float v) { v = juce::jlimit (std::min (minV, maxV), std::max (minV, maxV), v); if (v != value) { value = v; repaint(); } }
    float getValue() const noexcept { return value; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        paintLabel (g, label, b.removeFromTop (12), juce::Justification::centred, 7.5f, main ? C::text() : C::label());
        auto valueArea = b.removeFromBottom (15);
        const float size = (float) std::min (b.getWidth(), b.getHeight()) - 2.0f;
        auto area = juce::Rectangle<float> (size, size).withCentre (b.toFloat().getCentre());
        const float cx = area.getCentreX(), cy = area.getCentreY(), rKnob = size * 0.5f - 7.0f;
        const float a0 = -2.356f, a1 = 2.356f;
        const float ang = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, toNorm (value));
        auto arc = [&] (float r, float s, float e, juce::Colour col, float w)
        {
            juce::Path p; p.addCentredArc (cx, cy, r, r, 0.0f, std::min (s, e), std::max (s, e), true);
            g.setColour (col); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arc (rKnob + 4.0f, a0, a1, juce::Colour (0x40586560), 2.5f);
        arc (rKnob + 4.0f, a0, ang, main ? C::teal() : juce::Colour (0xa01f2925), 2.5f);
        for (int i = 0; i < 36; ++i)
        {
            const float t = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.setColour (i % 2 ? juce::Colour (0xff7e8b85) : juce::Colour (0xfff6f9f7));
            g.drawLine (cx + (rKnob - 1.0f) * std::sin (t), cy - (rKnob - 1.0f) * std::cos (t), cx + rKnob * std::sin (t), cy - rKnob * std::cos (t), 2.0f);
        }
        auto cap = juce::Rectangle<float> (rKnob * 2.0f - 6.0f, rKnob * 2.0f - 6.0f).withCentre ({ cx, cy });
        if (main) { g.setColour (C::glow().withAlpha (0.55f)); g.fillEllipse (cap.expanded (5.0f)); }
        juce::ColourGradient cg (juce::Colours::white, cap.getX() + cap.getWidth() * 0.35f, cap.getY() + cap.getHeight() * 0.28f, juce::Colour (0xff56625c), cap.getRight(), cap.getBottom(), true);
        cg.addColour (0.22, juce::Colour (0xffe2e8e4)); cg.addColour (0.58, juce::Colour (0xff9ca9a2));
        g.setGradientFill (cg); g.fillEllipse (cap);
        g.setColour (juce::Colour (0x66000000)); g.drawEllipse (cap, 1.0f);
        const float pr0 = cap.getWidth() * 0.18f, pr1 = cap.getWidth() * 0.46f;
        g.setColour (juce::Colour (0xff1b2420));
        g.drawLine (cx + pr0 * std::sin (ang), cy - pr0 * std::cos (ang), cx + pr1 * std::sin (ang), cy - pr1 * std::cos (ang), 2.6f);
        const juce::String txt = toText ? toText (value) : juce::String (value, 2);
        g.setFont (font (10.5f));
        const int tw = std::min (valueArea.getWidth(), (int) textW (g.getCurrentFont(), txt) + 12);
        auto vr = valueArea.withSizeKeepingCentre (tw, 14).toFloat();
        paintLcd (g, vr, 4.0f);
        g.setColour (C::lcdText());
        g.drawText (txt, vr.toNearestInt(), juce::Justification::centred, true);
    }

    void enablementChanged() override { setAlpha (isEnabled() ? 1.0f : 0.4f); }
    void mouseDown (const juce::MouseEvent&) override { dragStart = toNorm (value); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float sens = e.mods.isShiftDown() ? 0.0012f : 0.005f;
        set (fromNorm (dragStart - (float) e.getDistanceFromDragStartY() * sens + (float) e.getDistanceFromDragStartX() * sens * 0.5f));
    }
    void mouseUp (const juce::MouseEvent&) override { if (onCommit) onCommit(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (defV); if (onCommit) onCommit(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float d = step > 0.0f ? (w.deltaY > 0 ? 1.0f : -1.0f) * step / std::max (1.0e-6f, maxV - minV) : w.deltaY * 0.06f;
        set (fromNorm (toNorm (value) + d));
        if (onCommit) onCommit();
    }

private:
    void set (float v)
    {
        v = juce::jlimit (std::min (minV, maxV), std::max (minV, maxV), v);
        if (step > 0.0f) v = minV + step * std::round ((v - minV) / step);
        if (v == value) return;
        value = v;
        if (onChange) onChange (v);
        repaint();
    }
    float toNorm (float v) const
    {
        if (isLog && minV > 0.0f) return std::log (std::max (minV, v) / minV) / std::log (maxV / minV);
        return (v - minV) / (maxV - minV);
    }
    float fromNorm (float n) const
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        if (isLog && minV > 0.0f) return minV * std::pow (maxV / minV, n);
        return minV + n * (maxV - minV);
    }
    juce::String label;
    float minV, maxV, defV, value;
    bool isLog = false;
    float dragStart = 0.0f;
};

class Pill final : public juce::TextButton
{
public:
    explicit Pill (const juce::String& t = {}, bool toggle = false) : juce::TextButton (t) { setClickingTogglesState (toggle); }
};

// A row of mutually exclusive pills.
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Segmented (const juce::StringArray& items)
    {
        for (int i = 0; i < items.size(); ++i)
        {
            auto* b = buttons.add (new Pill (items[i]));
            b->onClick = [this, i] { setSelected (i); if (onSelect) onSelect (i); };
            addAndMakeVisible (b);
        }
        setSelected (0);
    }
    std::function<void (int)> onSelect;
    void setSelected (int i) { selected = i; for (int k = 0; k < buttons.size(); ++k) buttons[k]->setToggleState (k == i, juce::dontSendNotification); }
    int getSelected() const noexcept { return selected; }
    void enablementChanged() override { setAlpha (isEnabled() ? 1.0f : 0.45f); }
    void setItemTooltip (int i, const juce::String& t) { if (i >= 0 && i < buttons.size()) buttons[i]->setTooltip (t); }
    void paint (juce::Graphics& g) override { paintWell (g, getLocalBounds().toFloat(), std::min (14.0f, (float) getHeight() * 0.5f)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (3);
        const int n = std::max (1, buttons.size()), w = r.getWidth() / n;
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setBounds (r.getX() + i * w, r.getY(), i == n - 1 ? r.getRight() - (r.getX() + i * w) : w, r.getHeight());
    }
private:
    juce::OwnedArray<Pill> buttons;
    int selected = 0;
};

// A gem with a label, that toggles.
class Gem final : public juce::ToggleButton
{
public:
    explicit Gem (const juce::String& t) : juce::ToggleButton (t) {}
};
} // namespace mjqui
