// The Neosystem look: one drawing file for Batchcut, NeoBeat and MidiJaQ, and the small widgets they share
// (knobs, pills, segmented switches, gems). Kept byte-identical in all three source trees.
//
// Every colour the look draws with is a named token in Skin. A skin is one filled-in Skin, so a new skin is a
// table of colours and nothing else — no paint code is written twice. The tokens are named for what they are
// for (ink, plate, well, lcd) rather than what they look like, so a dark skin fills the same names sensibly.
//
// skins::chromePearl() holds exactly the values the three plugins drew with before this file existed, so the
// default skin is pixel-identical to the look they have always had.
//
// The active skin is read while painting, which happens on the message thread only, so a plain global is
// enough. setSkin() is for the skin menu: call it, then repaint the top-level window.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

namespace nsui
{
// ------------------------------------------------------------------ the tokens
struct Skin
{
    juce::String name { "Chrome Pearl" };

    // text
    juce::uint32 ink = 0xff1f2925;          // body text
    juce::uint32 inkSoft = 0xff2b3631;      // engraved labels
    juce::uint32 inkDim = 0xff4b5852;       // text that is not the point
    juce::uint32 labelShadow = 0x99ffffff;  // the light line under an engraved label

    // the one accent, and what glows with it
    juce::uint32 accent = 0xff1f9e8c;
    juce::uint32 accentGlow = 0xff5fd8c4;
    juce::uint32 warn = 0xfff5b25a;
    juce::uint32 danger = 0xffd9534a;

    // the chassis: the surface everything sits on
    juce::uint32 chassisTop = 0xffe9efeb, chassisBottom = 0xffc8d1cc;
    juce::uint32 chassisGroove = 0x0dffffff;   // the fine vertical ruling
    juce::uint32 chassisBand = 0x00000000;     // a shade across the middle (transparent: none)

    // a plate: a raised panel
    juce::uint32 plateTop = 0xffe2e8e4, plateBottom = 0xffbcc6c0;
    juce::uint32 plateEdge = 0x40000000, plateHilite = 0xe6ffffff, plateShadow = 0x40223028;
    int plateShadowBlur = 10, plateShadowDrop = 4;
    float plateRadius = 12.0f;                 // the corner a plate takes when none is asked for

    // a well: a sunken trough (a segmented switch sits in one)
    juce::uint32 wellTop = 0xffa7b2ab, wellBottom = 0xffc3ccc6;
    juce::uint32 wellEdge = 0x5a14201a, wellHilite = 0x99ffffff;
    float wellRadius = 12.0f;
    float segmentedRadiusCap = 14.0f;      // how round the well behind a segmented switch gets

    // a pearl: the lit nacre panel a window header wears
    juce::uint32 pearl0 = 0xfff4f1e6, pearl1 = 0xffdceee6, pearl2 = 0xffece1f2, pearl3 = 0xffd9e9f4;
    juce::uint32 pearl4 = 0xfff3ebd6, pearl5 = 0xffdff0e7, pearl6 = 0xffeee4f1;
    juce::uint32 pearlEdge = 0x50586e64;

    // a tile: a small coloured block with a word on it
    juce::uint32 tileEdge = 0x5a1f2925;

    // an LCD: a lit readout
    juce::uint32 lcdTop = 0xff0a1611, lcdBottom = 0xff13261e;
    juce::uint32 lcdEdge = 0xb3000000, lcdHilite = 0xb3ffffff;
    juce::uint32 lcdInk = 0xffaaf6d0, lcdInkDim = 0xff5fae8a;

    // a gem: the little lamp on a toggle
    juce::uint32 gemOff = 0xff9aa6a0, gemHalo = 0xccbef0ff, gemEdge = 0x8c3c4642, gemShine = 0xf2ffffff;
    float gemHaloGrow = 2.0f;
    juce::uint32 gem0 = 0xffbdf4ff, gem1 = 0xffd6ffd0, gem2 = 0xfffff1b3, gem3 = 0xffffd1f3;

    // a button
    juce::uint32 buttonTop = 0xffffffff, buttonBottom = 0xffa9b4ae;
    juce::uint32 buttonLitHilite = 0xfff2fffb, buttonOnInk = 0xff08352f;
    juce::uint32 buttonEdge = 0x66000000, buttonHilite = 0xe6ffffff;
    juce::uint32 hoverVeil = 0x40ffffff, pressVeil = 0x1a000000, disabledVeil = 0x70c6d0ca;
    float buttonRadiusCap = 14.0f;         // how round a tall button is allowed to get
    // How a lit button is filled. 0: a vertical wash that takes a tint, so a button can be given a colour
    // of its own (solo, mute, record). 1: a radial glow in the accent colour, the same on every button.
    // These are two real looks rather than two spellings of one, so each skin says which it wants.
    int litStyle = 0;
    juce::uint32 litRadialMid = 0xffc9fff1;

    // a knob
    juce::uint32 knobRingBack = 0x40586560, knobRingIdle = 0xa01f2925;
    juce::uint32 knobRidgeDark = 0xff7e8b85, knobRidgeLight = 0xfff6f9f7;
    juce::uint32 knobCapHi = 0xffffffff, knobCapMid = 0xffe2e8e4, knobCapLow = 0xff9ca9a2, knobCapLo = 0xff56625c;
    juce::uint32 knobCapEdge = 0x66000000, knobPointer = 0xff1b2420;

    // a slider's track and thumb
    juce::uint32 sliderThumbTop = 0xffffffff, sliderThumbBottom = 0xff5d6a64, sliderThumbEdge = 0x88000000;
    float toggleLabelSize = 8.0f;          // the word beside a gem
    // Does a text field read as a lit readout, or as an ordinary field? NeoBeat styles its editors as LCDs;
    // Batchcut leaves them plain. That is a look, not a detail, so the skin says which.
    bool lcdEditors = false;
    // Does a slider wear the chassis treatment - a sunken track and a chrome thumb - or the stock one?
    bool styledSliders = false;

    // menus, tooltips, scrollbars
    juce::uint32 menuBack = 0xffe6ece8, menuTop = 0xfff1f5f3, menuBottom = 0xffd2dbd6, menuEdge = 0x40000000;
    juce::uint32 tooltipBack = 0xff13261e;
    juce::uint32 scrollTrack = 0x22000000, scrollThumb = 0xff8a9791;

    // groups of events on a dark display, and the layers of a sound
    juce::uint32 groupNone = 0xffaab4af;
    std::array<juce::uint32, 8> groups { 0xff7fe0d0, 0xfff5b25a, 0xffe58ad8, 0xff8fb8ff, 0xffc6f27a, 0xffff8f7a, 0xfff2ee7a, 0xffb59cff };
    std::array<juce::uint32, 8> layers { 0xff52695e, 0xff4fd9e0, 0xffffc35a, 0xffe07ad6, 0xff7fa8ff, 0xffb6f05a, 0xffff8a6a, 0xfff4f4f4 };
};

namespace skins
{
// the look Batchcut and MidiJaQ have always had
inline Skin chromePearl() { return {}; }

// the same look as NeoBeat has always drawn it. The two copies of this code drifted apart over time: NeoBeat's
// plates sit on a heavier shadow, take a wider corner and a cooler grey, its chassis carries a shade across the
// middle, and its gems glow a little wider. Nothing here is a design decision — it is what NeoBeat already did,
// written down, so merging the drawing code changes nothing on screen. Which of the two the Neosystem settles
// on is a question for the skin stage, not something to decide by merging files.
inline Skin chromePearlDeep()
{
    Skin s;
    s.name = "Chrome Pearl Deep";
    s.chassisBand = 0x18303a35;
    s.plateBottom = 0xffb4bfb8;
    s.plateShadow = 0x55223028;
    s.plateShadowBlur = 14; s.plateShadowDrop = 6;
    s.plateRadius = 14.0f;
    s.gemHaloGrow = 2.5f;
    s.toggleLabelSize = 8.5f;
    s.litStyle = 1;                        // NeoBeat lights a button with a radial glow
    s.disabledVeil = 0x60c6d0ca;
    s.buttonRadiusCap = 1.0e6f;            // and lets a tall button go fully round
    s.segmentedRadiusCap = 1.0e6f;
    s.lcdEditors = true;
    s.styledSliders = true;
    return s;
}
}

inline Skin& skinRef() { static Skin s = skins::chromePearl(); return s; }
inline const Skin& skin() { return skinRef(); }
inline void setSkin (const Skin& s) { skinRef() = s; }
inline juce::Colour col (juce::uint32 argb) { return juce::Colour (argb); }

// ------------------------------------------------------------------ the colours, by role
namespace C
{
    inline juce::Colour text()    { return col (skin().ink); }
    inline juce::Colour label()   { return col (skin().inkSoft); }
    inline juce::Colour dim()     { return col (skin().inkDim); }
    inline juce::Colour teal()    { return col (skin().accent); }
    inline juce::Colour glow()    { return col (skin().accentGlow); }
    inline juce::Colour lcdText() { return col (skin().lcdInk); }
    inline juce::Colour lcdDim()  { return col (skin().lcdInkDim); }
    inline juce::Colour amber()   { return col (skin().warn); }
    inline juce::Colour red()     { return col (skin().danger); }
    // groups of events, on the dark display
    inline juce::Colour group (int g) { return g < 0 ? col (skin().groupNone) : col (skin().groups[(size_t) (g % 8)]); }
    // layers: background, tone, transient, noise, then your own
    inline juce::Colour layer (int l) { return col (skin().layers[(size_t) juce::jlimit (0, 7, l)]); }
}

inline juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
inline juce::Font caps (float size) { auto f = juce::Font (juce::FontOptions (size, juce::Font::bold)); f.setExtraKerningFactor (0.14f); return f; }
inline float textW (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }

inline void paintChassis (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& s = skin();
    g.setGradientFill (juce::ColourGradient::vertical (col (s.chassisTop), r.getY(), col (s.chassisBottom), r.getBottom()));
    g.fillRect (r);
    if (! col (s.chassisBand).isTransparent())
    {
        juce::ColourGradient mid (juce::Colour (0x00000000), 0, r.getY(), juce::Colour (0x00000000), 0, r.getBottom(), false);
        mid.addColour (0.36, juce::Colour (0x22000000).withAlpha (0.0f));
        mid.addColour (0.5, col (s.chassisBand));
        mid.addColour (0.64, juce::Colour (0x00000000));
        g.setGradientFill (mid); g.fillRect (r);
    }
    // brushed lines
    g.setColour (col (s.chassisGroove));
    for (float x = r.getX(); x < r.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
}
// radius < 0: the skin's own plate corner
inline void paintPlate (juce::Graphics& g, juce::Rectangle<float> r, float radius = -1.0f)
{
    const auto& s = skin();
    if (radius < 0.0f) radius = s.plateRadius;
    juce::DropShadow (col (s.plateShadow), s.plateShadowBlur, { 0, s.plateShadowDrop }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (juce::ColourGradient::vertical (col (s.plateTop), r.getY(), col (s.plateBottom), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (col (s.plateHilite));
    g.drawLine (r.getX() + radius, r.getY() + 0.8f, r.getRight() - radius, r.getY() + 0.8f, 1.0f);
    g.setColour (col (s.plateEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}
inline void paintWell (juce::Graphics& g, juce::Rectangle<float> r, float radius = -1.0f)
{
    const auto& s = skin();
    if (radius < 0.0f) radius = s.wellRadius;
    g.setGradientFill (juce::ColourGradient::vertical (col (s.wellTop), r.getY(), col (s.wellBottom), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (col (s.wellEdge));
    g.drawRoundedRectangle (r.reduced (0.5f).translated (0, 0.8f), radius, 1.6f);
    g.setColour (col (s.wellHilite));
    g.drawLine (r.getX() + radius, r.getBottom() - 0.5f, r.getRight() - radius, r.getBottom() - 0.5f, 1.0f);
}
inline void paintLcd (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
{
    const auto& s = skin();
    g.setGradientFill (juce::ColourGradient::vertical (col (s.lcdTop), r.getY(), col (s.lcdBottom), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (col (s.lcdEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.2f);
    g.setColour (col (s.lcdHilite));
    g.drawLine (r.getX() + radius, r.getBottom() + 0.5f, r.getRight() - radius, r.getBottom() + 0.5f, 1.0f);
}
inline void paintGem (juce::Graphics& g, juce::Rectangle<float> r, bool on = true, juce::Colour tint = {})
{
    const auto& s = skin();
    if (on)
    {
        g.setColour (col (s.gemHalo)); g.fillEllipse (r.expanded (s.gemHaloGrow));
        if (! tint.isTransparent()) g.setColour (tint);
        else
        {
            juce::ColourGradient cg (col (s.gem0), r.getX(), r.getY(), col (s.gem3), r.getRight(), r.getBottom(), false);
            cg.addColour (0.4, col (s.gem1)); cg.addColour (0.7, col (s.gem2));
            g.setGradientFill (cg);
        }
    }
    else g.setColour (col (s.gemOff));
    g.fillEllipse (r);
    g.setColour (col (s.gemShine));
    g.fillEllipse (r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.18f, r.getWidth() * 0.28f, r.getHeight() * 0.28f);
    g.setColour (col (s.gemEdge));
    g.drawEllipse (r, 1.0f);
}
// a pearl: nacre, lit from the top left. A window header wears one.
inline void paintPearl (juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f, bool glow = false)
{
    const auto& s = skin();
    if (glow)
    {
        g.setColour (C::glow().withAlpha (0.45f));
        g.drawRoundedRectangle (r.expanded (2.0f), radius + 2.0f, 4.0f);
    }
    juce::ColourGradient grad (col (s.pearl0), r.getX(), r.getY(), col (s.pearl6), r.getRight(), r.getBottom(), false);
    grad.addColour (0.18, col (s.pearl1));
    grad.addColour (0.36, col (s.pearl2));
    grad.addColour (0.54, col (s.pearl3));
    grad.addColour (0.71, col (s.pearl4));
    grad.addColour (0.86, col (s.pearl5));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.85f), r.getX() + r.getWidth() * 0.2f, r.getY(),
                                juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.45f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, radius);
    g.setColour (glow ? C::glow() : col (s.pearlEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, glow ? 2.0f : 1.0f);
}

inline void paintLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft, float size = 9.0f, juce::Colour c = C::label())
{
    g.setFont (caps (size));
    g.setColour (col (skin().labelShadow));
    g.drawText (t.toUpperCase(), r.translated (0, 1), j, true);
    g.setColour (c);
    g.drawText (t.toUpperCase(), r, j, true);
}

// a tile: a small coloured block with a word on it, selected or not, optionally ringed
inline void paintTile (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, const juce::String& text, bool selected, juce::Colour ring = {})
{
    if (selected) { g.setColour (C::glow().withAlpha (0.55f)); g.fillRoundedRectangle (r.expanded (3.0f), 8.0f); }
    if (! ring.isTransparent()) { g.setColour (ring.withAlpha (0.8f)); g.drawRoundedRectangle (r.expanded (1.5f), 7.0f, 2.0f); }
    g.setColour (fill); g.fillRoundedRectangle (r, 6.0f);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white.withAlpha (0.65f), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getCentreY()));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (selected ? C::teal() : col (skin().tileEdge));
    g.drawRoundedRectangle (r, 6.0f, selected ? 2.0f : 1.0f);
    if (text.isNotEmpty() && r.getWidth() > 14)
    {
        g.setColour (C::text());
        g.setFont (caps (std::min (8.0f, r.getHeight() * 0.42f)));
        g.drawText (text, r.reduced (4, 1).toNearestInt(), juce::Justification::centredLeft, true);
    }
}

class Look final : public juce::LookAndFeel_V4
{
public:
    Look() { apply(); }
    // after a skin change: re-read every colour this look holds
    void apply()
    {
        const auto& s = skin();
        setColour (juce::PopupMenu::backgroundColourId, col (s.menuBack));
        setColour (juce::PopupMenu::textColourId, C::text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::glow().withAlpha (0.55f));
        setColour (juce::PopupMenu::highlightedTextColourId, C::text());
        setColour (juce::ComboBox::textColourId, C::lcdText());
        if (s.lcdEditors)
        {
            setColour (juce::ComboBox::arrowColourId, C::lcdDim());
            setColour (juce::Label::textColourId, C::text());
            setColour (juce::TextEditor::backgroundColourId, col (s.lcdBottom));
            setColour (juce::TextEditor::textColourId, C::lcdText());
        }
        setColour (juce::TooltipWindow::backgroundColourId, col (s.tooltipBack));
        setColour (juce::TooltipWindow::textColourId, C::lcdText());
        setColour (juce::ScrollBar::thumbColourId, col (s.scrollThumb));
        setColour (juce::AlertWindow::backgroundColourId, col (s.menuBack));
        setColour (juce::AlertWindow::textColourId, C::text());
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto& s = skin();
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float rad = std::min (r.getHeight() * 0.5f, s.buttonRadiusCap);
        const bool on = b.getToggleState();
        // a tint only where one was set on purpose (solo, mute, record); the theme's own default is not one
        const auto tint = b.isColourSpecified (juce::TextButton::buttonOnColourId) ? b.findColour (juce::TextButton::buttonOnColourId) : juce::Colours::transparentBlack;
        if (on)
        {
            const auto base = tint.isOpaque() ? tint.interpolatedWith (juce::Colours::white, 0.35f) : C::glow();
            g.setColour (base.withAlpha (0.5f)); g.fillRoundedRectangle (r.expanded (1.5f), rad + 1.5f);
            if (s.litStyle == 1)
            {
                // a radial glow in the accent colour, lit from up and to the left
                juce::ColourGradient cg (col (s.buttonTop), r.getX() + r.getWidth() * 0.3f, r.getY(),
                                         C::teal(), r.getX() + r.getWidth() * 0.6f, r.getBottom(), true);
                cg.addColour (0.3, col (s.litRadialMid));
                cg.addColour (0.75, C::glow());
                g.setGradientFill (cg);
            }
            else
            {
                // a pale vertical wash with the tint below it, so the label stays dark on light
                juce::ColourGradient cg = juce::ColourGradient::vertical (col (s.buttonLitHilite), r.getY(), base, r.getBottom());
                cg.addColour (0.45, base.interpolatedWith (col (s.buttonLitHilite), 0.55f));
                g.setGradientFill (cg);
            }
        }
        else g.setGradientFill (juce::ColourGradient::vertical (col (s.buttonTop), r.getY(), col (s.buttonBottom), r.getBottom()));
        g.fillRoundedRectangle (r, rad);
        if (over && ! on) { g.setColour (col (s.hoverVeil)); g.fillRoundedRectangle (r, rad); }
        if (down) { g.setColour (col (s.pressVeil)); g.fillRoundedRectangle (r, rad); }
        g.setColour (col (s.buttonEdge)); g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (col (s.buttonHilite)); g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        if (! b.isEnabled()) { g.setColour (col (s.disabledVeil)); g.fillRoundedRectangle (r, rad); }
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return caps (std::min (9.5f, (float) h * 0.36f)); }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? col (skin().buttonOnInk) : C::text());
        if (! b.isEnabled()) g.setColour (C::dim().withAlpha (0.6f));
        // Button labels are set in capitals by the look. A name whose own capitals carry meaning - ".NJQi",
        // "MidiJaQ" - sets the keepCase property and is drawn as it was written.
        const bool keepCase = (bool) b.getProperties().getWithDefault ("keepCase", false);
        g.drawFittedText (keepCase ? b.getButtonText() : b.getButtonText().toUpperCase(),
                          b.getLocalBounds().reduced (6, 1), juce::Justification::centred, 1, 0.8f);
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
        const auto& s = skin();
        g.setGradientFill (juce::ColourGradient::vertical (col (s.menuTop), 0, col (s.menuBottom), (float) h));
        g.fillRect (0, 0, w, h);
        g.setColour (col (s.menuEdge)); g.drawRect (0, 0, w, h);
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        paintGem (g, juce::Rectangle<float> (r.getX() + 3, r.getCentreY() - 6, 12, 12), b.getToggleState());
        paintLabel (g, b.getButtonText(), b.getLocalBounds().withTrimmedLeft (20), juce::Justification::centredLeft, skin().toggleLabelSize);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const auto& sk = skin();
        if (! sk.styledSliders)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        const bool vert = style == juce::Slider::LinearVertical;
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = vert ? r.withSizeKeepingCentre (8.0f, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), 8.0f);
        paintWell (g, track, 4.0f);
        const auto fill = s.findColour (juce::Slider::trackColourId);
        g.setColour (fill.isTransparent() ? C::teal() : fill);
        if (vert) g.fillRoundedRectangle (track.withTop (pos).reduced (1.5f, 0), 3.0f);
        else g.fillRoundedRectangle (track.withRight (pos).reduced (0, 1.5f), 3.0f);
        auto thumb = vert ? juce::Rectangle<float> (track.getCentreX() - 9.0f, pos - 4.0f, 18.0f, 8.0f)
                          : juce::Rectangle<float> (pos - 4.0f, track.getCentreY() - 8.0f, 8.0f, 16.0f);
        g.setGradientFill (juce::ColourGradient::vertical (col (sk.sliderThumbTop), thumb.getY(), col (sk.sliderThumbBottom), thumb.getBottom()));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (col (sk.sliderThumbEdge));
        g.drawRoundedRectangle (thumb, 3.0f, 0.8f);
    }
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool) override
    {
        const auto& s = skin();
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        g.setColour (col (s.scrollTrack)); g.fillRoundedRectangle (r, 3.0f);
        auto t = vertical ? juce::Rectangle<float> (r.getX(), (float) thumbStart, r.getWidth(), (float) thumbSize) : juce::Rectangle<float> ((float) thumbStart, r.getY(), (float) thumbSize, r.getHeight());
        g.setColour (col (s.scrollThumb)); g.fillRoundedRectangle (t.reduced (1.0f), 3.0f);
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
    std::function<void()> onRightClick;        // the randomiser lives here
    bool main = false;
    bool cycling = false;                      // drawn with a ring, so a knob that moves on its own shows it
    float step = 0.0f;                         // > 0: values snap to steps of this

    void setValue (float v) { v = juce::jlimit (std::min (minV, maxV), std::max (minV, maxV), v); if (v != value) { value = v; repaint(); } }
    float getValue() const noexcept { return value; }

    void paint (juce::Graphics& g) override
    {
        const auto& s = skin();
        auto b = getLocalBounds();
        paintLabel (g, label, b.removeFromTop (12), juce::Justification::centred, 7.5f, main ? C::text() : C::label());
        auto valueArea = b.removeFromBottom (15);
        const float size = (float) std::min (b.getWidth(), b.getHeight()) - 2.0f;
        auto area = juce::Rectangle<float> (size, size).withCentre (b.toFloat().getCentre());
        const float cx = area.getCentreX(), cy = area.getCentreY(), rKnob = size * 0.5f - 7.0f;
        const float a0 = -2.356f, a1 = 2.356f;
        const float ang = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, toNorm (value));
        auto arc = [&] (float r, float st, float e, juce::Colour c, float w)
        {
            juce::Path p; p.addCentredArc (cx, cy, r, r, 0.0f, std::min (st, e), std::max (st, e), true);
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arc (rKnob + 4.0f, a0, a1, col (s.knobRingBack), 2.5f);
        arc (rKnob + 4.0f, a0, ang, main ? C::teal() : col (s.knobRingIdle), 2.5f);
        // a knob that is moving on its own wears a ring, so you can see which ones are not yours any more
        if (cycling) arc (rKnob + 7.5f, a0, a1, C::amber().withAlpha (0.85f), 1.6f);
        for (int i = 0; i < 36; ++i)
        {
            const float t = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.setColour (i % 2 ? col (s.knobRidgeDark) : col (s.knobRidgeLight));
            g.drawLine (cx + (rKnob - 1.0f) * std::sin (t), cy - (rKnob - 1.0f) * std::cos (t), cx + rKnob * std::sin (t), cy - rKnob * std::cos (t), 2.0f);
        }
        auto cap = juce::Rectangle<float> (rKnob * 2.0f - 6.0f, rKnob * 2.0f - 6.0f).withCentre ({ cx, cy });
        if (main) { g.setColour (C::glow().withAlpha (0.55f)); g.fillEllipse (cap.expanded (5.0f)); }
        juce::ColourGradient cg (col (s.knobCapHi), cap.getX() + cap.getWidth() * 0.35f, cap.getY() + cap.getHeight() * 0.28f, col (s.knobCapLo), cap.getRight(), cap.getBottom(), true);
        cg.addColour (0.22, col (s.knobCapMid)); cg.addColour (0.58, col (s.knobCapLow));
        g.setGradientFill (cg); g.fillEllipse (cap);
        g.setColour (col (s.knobCapEdge)); g.drawEllipse (cap, 1.0f);
        const float pr0 = cap.getWidth() * 0.18f, pr1 = cap.getWidth() * 0.46f;
        g.setColour (col (s.knobPointer));
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
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        dragStart = toNorm (value);
    }
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
    // the pointer lands on a button, not on the row behind it, so a tooltip for the row goes on every button
    void setTooltip (const juce::String& t) override
    {
        juce::SettableTooltipClient::setTooltip (t);
        for (auto* b : buttons) b->setTooltip (t);
    }
    void paint (juce::Graphics& g) override { paintWell (g, getLocalBounds().toFloat(), std::min (skin().segmentedRadiusCap, (float) getHeight() * 0.5f)); }
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
} // namespace nsui
