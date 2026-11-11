// The stand-alone effects' window. The look (Chrome Pearl plates, LCD wells, knurled knobs) and the two
// displays are NeoBeat's own, adapted to stand on their own.
#include "FxEditor.h"
#include <juce_dsp/juce_dsp.h>

namespace fxui
{
using namespace nbfx;

// ------------------------------------------------------------------ the look
namespace C
{
    inline juce::Colour text()  { return juce::Colour (0xff1f2925); }
    inline juce::Colour label() { return juce::Colour (0xff2b3631); }
    inline juce::Colour dim()   { return juce::Colour (0xff4b5852); }
    inline juce::Colour teal()  { return juce::Colour (0xff1f9e8c); }
    inline juce::Colour glow()  { return juce::Colour (0xff5fd8c4); }
    inline juce::Colour lcdText() { return juce::Colour (0xffaaf6d0); }
    inline juce::Colour lcdDim()  { return juce::Colour (0xff5fae8a); }
    inline juce::Colour amber() { return juce::Colour (0xfff5b25a); }
}
static juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
static juce::Font caps (float size) { auto f = juce::Font (juce::FontOptions (size, juce::Font::bold)); f.setExtraKerningFactor (0.14f); return f; }
static float textW (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }

static void paintChassis (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffe9efeb), r.getY(), juce::Colour (0xffc8d1cc), r.getBottom()));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (float x = r.getX(); x < r.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
}
static void paintWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xffa7b2ab), r.getY(), juce::Colour (0xffc3ccc6), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colour (0x5a14201a));
    g.drawRoundedRectangle (r.reduced (0.5f).translated (0, 0.8f), radius, 1.6f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (r.getX() + radius, r.getBottom() - 0.5f, r.getRight() - radius, r.getBottom() - 0.5f, 1.0f);
}
static void paintLcd (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
{
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff0a1611), r.getY(), juce::Colour (0xff13261e), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawLine (r.getX() + radius, r.getBottom() + 0.5f, r.getRight() - radius, r.getBottom() + 0.5f, 1.0f);
}
static void paintLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft, float size = 9.0f, juce::Colour c = C::label())
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
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = b.getToggleState();
        if (on)
        {
            g.setColour (C::glow().withAlpha (0.5f)); g.fillRoundedRectangle (r.expanded (1.5f), rad + 1.5f);
            juce::ColourGradient cg (juce::Colours::white, r.getX() + r.getWidth() * 0.3f, r.getY(), C::teal(), r.getX() + r.getWidth() * 0.6f, r.getBottom(), true);
            cg.addColour (0.3, juce::Colour (0xffc9fff1)); cg.addColour (0.75, C::glow());
            g.setGradientFill (cg);
        }
        else g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white, r.getY(), juce::Colour (0xffa9b4ae), r.getBottom()));
        g.fillRoundedRectangle (r, rad);
        if (over && ! on) { g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillRoundedRectangle (r, rad); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.1f)); g.fillRoundedRectangle (r, rad); }
        g.setColour (juce::Colour (0x66000000)); g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        if (! b.isEnabled()) { g.setColour (juce::Colour (0x60c6d0ca)); g.fillRoundedRectangle (r, rad); }
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return caps (std::min (9.5f, h * 0.36f)); }
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
        juce::Path tri; const float cx = (float) w - 11.0f, cy = h * 0.5f;
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
};

// ------------------------------------------------------------------ widgets
// A knob: on a host parameter, or free (a value and a callback).
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::String l, float lo, float hi, float def, bool logScale = false) : label (std::move (l)), minV (lo), maxV (hi), defV (def), value (def), isLog (logScale) {}
    std::function<void (float)> onChange;
    std::function<juce::String (float)> toText;
    bool main = false;

    void attach (juce::RangedAudioParameter* p) { param = p; sync(); }
    void setValue (float v) { value = juce::jlimit (minV, maxV, v); repaint(); }
    bool sync()
    {
        if (param == nullptr) return false;
        const float v = param->convertFrom0to1 (param->getValue());
        if (v == value) return false;
        value = v; repaint(); return true;
    }

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

    void mouseDown (const juce::MouseEvent&) override { dragStart = toNorm (value); if (param != nullptr) { param->beginChangeGesture(); gesture = true; } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float sens = e.mods.isShiftDown() ? 0.0012f : 0.005f;
        set (fromNorm (dragStart - (float) e.getDistanceFromDragStartY() * sens + (float) e.getDistanceFromDragStartX() * sens * 0.5f));
    }
    void mouseUp (const juce::MouseEvent&) override { if (param != nullptr && gesture) { param->endChangeGesture(); gesture = false; } }
    void mouseDoubleClick (const juce::MouseEvent&) override { edit ([this] { set (param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : defV); }); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float stepN = param != nullptr && param->getNormalisableRange().interval > 0.0f ? (w.deltaY > 0 ? 1.0f : -1.0f) / std::max (1.0f, maxV - minV) : w.deltaY * 0.06f;
        edit ([this, stepN] { set (fromNorm (toNorm (value) + stepN)); });
    }

private:
    template <class F> void edit (F f) { if (param != nullptr && ! gesture) param->beginChangeGesture(); f(); if (param != nullptr && ! gesture) param->endChangeGesture(); }
    void set (float v)
    {
        v = juce::jlimit (minV, maxV, v);
        if (param != nullptr) { v = param->getNormalisableRange().snapToLegalValue (v); if (v != value) param->setValueNotifyingHost (param->convertTo0to1 (v)); }
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
    bool isLog = false, gesture = false;
    float dragStart = 0.0f;
    juce::RangedAudioParameter* param = nullptr;
};

class Pill final : public juce::TextButton
{
public:
    explicit Pill (const juce::String& t = {}, bool toggle = false) : juce::TextButton (t) { setClickingTogglesState (toggle); }
};

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
    void paint (juce::Graphics& g) override { paintWell (g, getLocalBounds().toFloat(), getHeight() * 0.5f); }
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

// ------------------------------------------------------------------ the panel (1180 x 652, scaled by the editor)
class Panel final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit Panel (FxProcessor& p)
        : proc (p), fs (FxProcessor::isFriction()), mode ({ "LOAD A HIT", "CATCH HITS" }), loop ({ "LOOP OFF", "1 BAR", "2 BARS", "4 BARS" }), pages (pageNames())
    {
        addAndMakeVisible (mode);
        mode.onSelect = [this] (int i) { proc.setValue ("mode", (float) i); layoutMode(); };
        addAndMakeVisible (presets);
        for (int i = 0; i < proc.getNumPrograms(); ++i) presets.addItem (proc.getProgramName (i), i + 1);
        presets.onChange = [this] { if (presets.getSelectedId() > 0) { proc.setCurrentProgram (presets.getSelectedId() - 1); line = -1; triggerAsyncUpdate(); } };
        presets.setTooltip ("Starting points. Picking one resets the effect's knobs (not the levels or the catcher).");

        for (auto* b : { &load, &builtIn, &play, &keytrack, &choke, &keep, &tempo, &addLine, &delLine, &saveWav }) addAndMakeVisible (b);
        load.onClick = [this] { chooseFile(); };
        load.setTooltip ("Load a hit (WAV, AIFF, FLAC, MP3), up to 4 seconds. You can also drop a file anywhere on the window.");
        builtIn.onClick = [this] { proc.useDefaultHit(); repaint(); };
        builtIn.setTooltip ("Back to the built-in clank.");
        play.onClick = [this] { proc.engine().trigger (1.0f); };
        play.setTooltip ("Play the hit through the effect. MIDI notes do the same (route a MIDI track to this plugin).");
        addAndMakeVisible (loop);
        loop.onSelect = [this] (int i) { proc.setValue ("loop", (float) i); };
        loop.setTooltip ("Play the hit on every bar, every 2 or every 4 while the host runs.");
        keytrack.setClickingTogglesState (true);
        keytrack.onClick = [this] { proc.setValue ("keytrack", keytrack.getToggleState() ? 1.0f : 0.0f); };
        keytrack.setTooltip ("MIDI keys transpose the result: C3 plays it as it is.");
        choke.setClickingTogglesState (true);
        choke.onClick = [this] { proc.setValue ("choke", choke.getToggleState() ? 1.0f : 0.0f); };
        choke.setTooltip ("A new hit cuts the one before it (an accent that never piles up).");
        keep.onClick = [this] { if (proc.keepLastCapture()) { mode.setSelected (0); layoutMode(); } };
        keep.setTooltip ("Make the last caught hit the loaded hit, and switch to LOAD A HIT to work on it.");
        tempo.setClickingTogglesState (true);
        tempo.onClick = [this] { proc.setValue ("rateTempo", tempo.getToggleState() ? 1.0f : 0.0f); triggerAsyncUpdate(); };
        tempo.setTooltip ("TEMPO: the grid follows the host's tempo. MS: free time in milliseconds.");
        addLine.onClick = [this] { if (proc.lines.size() < 6) { nb::StretchLine l; l.fLo = 200.0f * (float) (1 + proc.lines.size()); l.fHi = l.fLo * 3.0f; l.offset = 0.02f * (float) proc.lines.size(); proc.lines.push_back (l); proc.linesChanged(); line = (int) proc.lines.size() - 1; page = 4; triggerAsyncUpdate(); } };
        delLine.onClick = [this] { if (line >= 0 && line < (int) proc.lines.size()) { proc.lines.erase (proc.lines.begin() + line); proc.linesChanged(); line = -1; triggerAsyncUpdate(); } };
        saveWav.onClick = [this] { chooseSave(); };
        saveWav.setTooltip ("Save what the display shows as a 24-bit WAV.");
        addAndMakeVisible (pages);
        pages.onSelect = [this] (int i) { page = i; triggerAsyncUpdate(); };

        auto side = [this] (const char* id) { auto* k = makeKnob (*def (id)); addAndMakeVisible (k); sideKnobs.add (k); return k; };
        kDry = side ("dry"); kThru = side ("thru"); kThreshold = side ("threshold"); kCapture = side ("capture"); kLevel = side ("level");
        kDry->setTooltip ("The loaded hit itself, beside the effect.");
        kThru->setTooltip ("The input, passed through.");
        kThreshold->setTooltip ("A hit is caught when the input rises above this (the line on the meter).");
        kCapture->setTooltip ("How much of each hit is taken. A new hit ends the one before early.");
        rebuild();
        layoutMode();
        setSize (FxEditor::kW, FxEditor::kH);
        startTimerHz (30);
    }
    ~Panel() override { stopTimer(); cancelPendingUpdate(); }

    // ---------------------------------------------------------------- knobs
    const ParamDef* def (const char* id) const { for (const auto& d : paramDefs()) if (juce::String (d.id) == id) return &d; return nullptr; }
    Knob* makeKnob (const ParamDef& d)
    {
        auto* k = new Knob (d.label, d.lo, d.hi, d.def, d.log);
        k->main = d.main;
        const Unit u = d.unit;
        k->toText = [u] (float v) { return formatValue (u, v); };
        k->attach (proc.apvts.getParameter (d.id));
        return k;
    }

    void rebuild()
    {
        bank.clear();
        const bool isTempo = proc.value ("rateTempo") > 0.5f;
        tempo.setToggleState (isTempo, juce::dontSendNotification);
        tempo.setButtonText (isTempo ? "TEMPO" : "MS");
        pages.setSelected (page);
        if (fs && page == 4)
        {
            if (line >= (int) proc.lines.size()) line = (int) proc.lines.size() - 1;
            if (line >= 0)
            {
                auto add = [this] (const char* name, float lo, float hi, float nb::StretchLine::* field, std::function<juce::String (float)> fmt = {})
                {
                    auto* k = bank.add (new Knob (name, lo, hi, proc.lines[(size_t) line].*field));
                    k->setValue (proc.lines[(size_t) line].*field);
                    k->toText = std::move (fmt);
                    const int li = line;
                    k->onChange = [this, li, field] (float v) { if (li < (int) proc.lines.size()) { proc.lines[(size_t) li].*field = v; proc.linesChanged(); repaint(); } };
                    addAndMakeVisible (k);
                };
                auto ms = [] (float v) { return juce::String (juce::roundToInt (v * 1000)) + " ms"; };
                add ("OFFSET", -0.5f, 0.5f, &nb::StretchLine::offset, ms);
                add ("TILT", -0.5f, 0.5f, &nb::StretchLine::tilt, ms);
                add ("SPRAY", 0, 1, &nb::StretchLine::spray);
                add ("EDGE", 0, 2, &nb::StretchLine::edge, [] (float v) { return juce::String (v, 2) + " oct"; });
                add ("LEVEL", 0, 2, &nb::StretchLine::level);
                add ("PAN", -1, 1, &nb::StretchLine::pan);
            }
        }
        else
            for (const auto& d : paramDefs())
            {
                if (d.page != page) continue;
                if ((d.when == When::Tempo && ! isTempo) || (d.when == When::Millis && isTempo)) continue;
                addAndMakeVisible (bank.add (makeKnob (d)));
            }
        layoutBank();
        repaint();
    }
    void handleAsyncUpdate() override { rebuild(); resized(); }

    void layoutBank()
    {
        int x = 22, y = 536;
        for (auto* k : bank) { if (x + 92 > getWidth() - 20) { x = 22; y += 102; } k->setBounds (x, y, 88, 96); x += 92; }
    }

    void layoutMode()
    {
        const bool cap = proc.value ("mode") > 0.5f;
        mode.setSelected (cap ? 1 : 0);
        for (auto* c : { (juce::Component*) &load, (juce::Component*) &builtIn, (juce::Component*) &play, (juce::Component*) &loop, (juce::Component*) &keytrack }) c->setVisible (! cap);
        keep.setVisible (cap);
        kDry->setVisible (! cap); kThru->setVisible (cap); kThreshold->setVisible (cap); kCapture->setVisible (cap);
        resized(); repaint();
    }

    void resized() override
    {
        const int W = getWidth();
        mode.setBounds (300, 14, 300, 32);
        presets.setBounds (614, 16, 200, 28);
        const bool cap = proc.value ("mode") > 0.5f;
        // the strip
        load.setBounds (22, 58, 104, 32); builtIn.setBounds (130, 58, 96, 32);
        play.setBounds (536, 58, 86, 32); loop.setBounds (630, 58, 300, 32); keytrack.setBounds (938, 58, 104, 32);
        keep.setBounds (536, 58, 150, 32);
        choke.setBounds (cap ? 694 : 1050, 58, 106, 32);
        // the display and the side column
        display = { 22, 100, W - 160, 380 };
        const int sx = W - 116;
        if (cap) { kThru->setBounds (sx, 100, 88, 94); kThreshold->setBounds (sx, 196, 88, 94); kCapture->setBounds (sx, 292, 88, 94); kLevel->setBounds (sx, 388, 88, 94); }
        else { kDry->setBounds (sx, 100, 88, 94); kLevel->setBounds (sx, 196, 88, 94); }
        // pages
        pages.setBounds (22, 492, fs ? 480 : 260, 32);
        tempo.setBounds (fs ? 512 : 292, 492, 90, 32);
        tempo.setVisible (page == 0 || (fs && page == 3));
        addLine.setBounds (612, 492, 100, 32); delLine.setBounds (718, 492, 120, 32);
        addLine.setVisible (fs); delLine.setVisible (fs && page == 4 && line >= 0);
        saveWav.setBounds (W - 22 - 110, 492, 110, 32);
        layoutBank();
    }

    // ---------------------------------------------------------------- painting
    void paint (juce::Graphics& g) override
    {
        paintChassis (g, getLocalBounds().toFloat());
        paintLabel (g, fs ? "FRICTION STRETCH" : "BOUNCE DELAY", { 22, 14, 270, 18 }, juce::Justification::centredLeft, 12.5f, C::text());
        paintLabel (g, "A NEOBEAT EFFECT | MARBELITE", { 22, 33, 270, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        // status
        auto st = juce::Rectangle<int> (828, 16, getWidth() - 850, 28);
        paintLcd (g, st.toFloat(), 6.0f);
        g.setFont (font (12.0f, true)); g.setColour (proc.engine().renderer.busy() ? C::amber() : C::lcdText());
        g.drawText (status, st.reduced (10, 0), juce::Justification::centredLeft, true);

        const bool cap = proc.value ("mode") > 0.5f;
        if (! cap)
        {
            auto name = juce::Rectangle<int> (232, 58, 296, 32);
            paintLcd (g, name.toFloat(), 6.0f);
            g.setColour (C::lcdText()); g.setFont (font (12.5f, true));
            juce::String label = proc.hitName();
            if (auto h = proc.currentHit()) label += "  |  " + juce::String ((double) h->left.size() / h->sampleRate, 2) + " s";
            g.drawText (label, name.reduced (10, 0), juce::Justification::centredLeft, true);
        }
        else
        {
            // the input meter with the threshold on it (-48 .. 0 dB)
            auto m = juce::Rectangle<int> (22, 58, 506, 32);
            paintLcd (g, m.toFloat(), 6.0f);
            auto bar = m.reduced (10, 9).toFloat();
            auto xOf = [&] (float db) { return bar.getX() + juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * bar.getWidth(); };
            const float db = juce::Decibels::gainToDecibels (meterShown, -60.0f);
            g.setColour (C::lcdDim().withAlpha (0.35f)); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (lamp > 0.05f ? C::amber() : C::lcdText()); g.fillRoundedRectangle (bar.withRight (std::max (bar.getX() + 2.0f, xOf (db))), 3.0f);
            const float tx = xOf (proc.value ("threshold"));
            g.setColour (juce::Colours::white); g.fillRect (tx - 1.0f, (float) m.getY() + 4.0f, 2.0f, (float) m.getHeight() - 8.0f);
            g.setColour (C::dim()); g.setFont (font (11.0f));
            g.drawText ("Every hit over the white line is caught.", juce::Rectangle<int> (808, 58, getWidth() - 830, 32), juce::Justification::centredLeft, true);
        }
        paintLcd (g, display.toFloat(), 10.0f);
        if (fs) paintFriction (g); else paintBounce (g);
    }

    float yOf (float f) const { return display.getBottom() - 6 - std::log (juce::jlimit (30.0f, 16000.0f, f) / 30.0f) / std::log (16000.0f / 30.0f) * (display.getHeight() - 12); }
    float fOf (float y) const { return 30.0f * std::pow (16000.0f / 30.0f, juce::jlimit (0.0f, 1.0f, (display.getBottom() - 6 - y) / (float) (display.getHeight() - 12))); }

    void paintFriction (juce::Graphics& g)
    {
        auto r = proc.engine().renderer.shown();
        if (! r || r->size() < 2048)
        {
            g.setColour (C::lcdDim()); g.setFont (font (15.0f));
            g.drawText (proc.value ("mode") > 0.5f ? "WAITING FOR A HIT ON THE INPUT" : "RENDERING...", display, juce::Justification::centred);
        }
        else
        {
            if (r.get() != imageSrc || image.getWidth() != display.getWidth())
            {
                imageSrc = r.get();
                image = juce::Image (juce::Image::ARGB, display.getWidth(), display.getHeight(), true);
                constexpr int N = 2048;
                juce::dsp::FFT fft (11);
                std::vector<float> buf (2 * N);
                const int W = image.getWidth(), H = image.getHeight(), len = r->size();
                for (int x = 0; x < W; ++x)
                {
                    const int start = (int) ((double) x / W * std::max (1, len - N));
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    for (int i = 0; i < N && start + i < len; ++i) buf[(size_t) i] = 0.5f * (r->l[(size_t) (start + i)] + r->r[(size_t) (start + i)]) * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / N));
                    fft.performFrequencyOnlyForwardTransform (buf.data());
                    for (int y = 4; y < H - 4; ++y)
                    {
                        const float f = fOf ((float) (display.getY() + y));
                        const int bin = juce::jlimit (1, N / 2 - 1, (int) (f / r->sampleRate * N));
                        const float v = juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (buf[(size_t) bin] + 1.0e-6f) + 55.0f) / 60.0f);
                        image.setPixelAt (x, y, juce::Colour::fromFloatRGBA (0.35f * v * v + 0.1f * v, 0.95f * v, 0.7f * v, v));
                    }
                }
            }
            g.drawImageAt (image, display.getX(), display.getY());
            auto len = display.reduced (8, 6).removeFromTop (18).removeFromRight (56);
            g.setColour (juce::Colour (0xd00a1611)); g.fillRoundedRectangle (len.toFloat(), 5.0f);
            g.setColour (C::lcdText()); g.setFont (font (11.0f));
            g.drawText (juce::String (r->size() / r->sampleRate, 1) + " s", len, juce::Justification::centred);
        }
        lineRects.clear();
        for (int i = 0; i < (int) proc.lines.size(); ++i)
        {
            const auto& l = proc.lines[(size_t) i];
            const float y0 = yOf (std::max (l.fLo, l.fHi)), y1 = yOf (std::min (l.fLo, l.fHi));
            const float xOff = (float) display.getX() + 30 + l.offset * 400.0f;
            auto band = juce::Rectangle<float> ((float) display.getX() + 4, y0, (float) display.getWidth() - 8, std::max (4.0f, y1 - y0));
            lineRects.add (band);
            const bool sel = i == line;
            g.setColour (juce::Colour (0xffffd1f3).withAlpha (sel ? 0.25f : 0.1f)); g.fillRect (band);
            g.setColour (juce::Colour (0xffffd1f3).withAlpha (l.on ? 0.9f : 0.3f));
            g.drawLine (band.getX(), band.getCentreY(), band.getRight(), band.getCentreY() - l.tilt * 200.0f, sel ? 2.0f : 1.2f);
            g.fillEllipse (xOff - 9, band.getCentreY() - 9, 18, 18);
            g.setColour (juce::Colour (0xff0a1611)); g.setFont (font (11.0f, true));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (xOff - 9, band.getCentreY() - 9, 18, 18).toNearestInt(), juce::Justification::centred);
        }
        auto hint = display.reduced (6, 5).removeFromBottom (20);
        g.setColour (juce::Colour (0xd00a1611)); g.fillRoundedRectangle (hint.toFloat(), 5.0f);   // readable over the sonogram
        g.setColour (C::lcdText()); g.setFont (font (11.0f));
        g.drawText (proc.lines.empty() ? "THE STRETCHED HIT (TIME ACROSS, PITCH UP) | + LINE STRETCHES A BAND OF IT ON ITS OWN" : "DRAG A LINE UP OR DOWN TO MOVE ITS BAND | CLICK IT TO EDIT IT ON THE LINES PAGE",
                    hint.reduced (8, 0), juce::Justification::centredLeft);
    }

    // perspective from just behind the listener: x = pan (-1..1), z = distance (m), y = height (m)
    juce::Point<float> project (float x, float y, float z) const
    {
        const float f = 4.0f, s = f / (std::max (0.0f, z) + 0.5f + f);
        const float horizon = (float) display.getY() + (float) display.getHeight() * 0.16f;
        const float k = (float) display.getHeight() * 0.80f / (1.6f * (f / (0.5f + f)));
        return { (float) display.getCentreX() + x * (float) display.getWidth() * 0.55f * s, horizon + (1.6f - y) * k * s };
    }

    void paintBounce (juce::Graphics& g)
    {
        const auto p = proc.settingsNow().bd;
        nb::BounceDelay bd;
        const auto plan = bd.plan (p, std::max (20.0, proc.displayBpm.load()), 0.0, 0.0);
        const float far = std::max (12.0f, std::max (p.distStart, p.distEnd) * 1.2f);
        g.saveState(); g.reduceClipRegion (display);
        g.setColour (C::lcdText().withAlpha (0.12f));
        for (int i = 0; i <= 10; ++i) g.drawLine (juce::Line<float> (project (-1.2f, 0, far * (float) i / 10.0f), project (1.2f, 0, far * (float) i / 10.0f)), 1.0f);
        for (int i = -6; i <= 6; ++i) g.drawLine (juce::Line<float> (project ((float) i / 5.0f, 0, 0), project ((float) i / 5.0f, 0, far)), 1.0f);
        g.setColour (C::lcdText().withAlpha (0.25f)); g.drawLine (juce::Line<float> (project (-1.2f, 3.0f, far), project (1.2f, 3.0f, far)), 1.0f);
        // the listener's head, seen from behind
        const juce::Point<float> head ((float) display.getCentreX(), (float) display.getBottom() - 48.0f);
        g.setColour (juce::Colour (0xff2a4a3c)); g.fillEllipse (head.x - 28, head.y - 34, 56, 64);
        g.setColour (C::lcdText().withAlpha (0.6f)); g.drawEllipse (head.x - 28, head.y - 34, 56, 64, 1.5f);
        g.fillEllipse (head.x - 34, head.y - 8, 8, 16); g.fillEllipse (head.x + 26, head.y - 8, 8, 16);
        juce::Path arc;
        const double total = plan.empty() ? 1.0 : std::max (0.1, plan.back().t);
        int shown = 0;
        for (size_t i = 0; i < plan.size(); ++i)
        {
            const auto& b = plan[i];
            const auto pt = project (b.pan, 0.0f, b.dist);
            const float sz = 8.0f + 26.0f * std::sqrt (std::max (0.0f, b.gain)) * 6.0f / (b.dist + 6.0f);
            if (i == 0) arc.startNewSubPath (pt);
            else arc.quadraticTo (project (0.5f * (b.pan + plan[i - 1].pan), 1.4f * std::max (0.2f, (float) (b.t - plan[i - 1].t) * 4.0f), 0.5f * (b.dist + plan[i - 1].dist)), pt);
            const bool lit = animT >= 0.0 && animT >= b.t && (i + 1 >= plan.size() || animT < plan[i + 1].t);
            g.setColour (juce::Colour (0xff7fe0d0).withAlpha (lit ? 1.0f : 0.35f + 0.6f * std::min (1.0f, b.gain * 1.5f)));
            g.fillEllipse (pt.x - sz * 0.5f, pt.y - sz * 0.5f, sz, sz);
            if (lit) { g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawEllipse (pt.x - sz * 0.5f - 3, pt.y - sz * 0.5f - 3, sz + 6, sz + 6, 2.0f); }
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillEllipse (pt.x - sz * 0.2f, pt.y - sz * 0.3f, sz * 0.22f, sz * 0.22f);
            if (sz > 14 && shown < 24) { g.setColour (juce::Colour (0xff0a1611)); g.setFont (font (10.0f, true)); g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (pt.x - 10, pt.y - 7, 20, 14).toNearestInt(), juce::Justification::centred); ++shown; }
        }
        g.setColour (C::lcdText().withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved), juce::AffineTransform());
        g.restoreState();
        g.setColour (C::lcdDim()); g.setFont (font (11.0f));
        g.drawText (juce::String (plan.size()) + " BOUNCES OVER " + juce::String (total, 2) + " S | NEAR " + juce::String (p.distStart, 1) + " M > FAR " + juce::String (p.distEnd, 1) + " M | SPHERE SIZE = LOUDNESS",
                    display.reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
    }

    // ---------------------------------------------------------------- mouse and files
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! fs) return;
        for (int i = 0; i < lineRects.size(); ++i)
            if (lineRects[i].contains (e.position)) { line = i; page = 4; grabY = e.position.y; grabLine = proc.lines[(size_t) i]; dragging = true; triggerAsyncUpdate(); return; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || line < 0 || line >= (int) proc.lines.size()) return;
        auto& l = proc.lines[(size_t) line];
        const float ratio = fOf (yOf (grabLine.fLo) + (e.position.y - grabY)) / std::max (20.0f, grabLine.fLo);
        l.fLo = juce::jlimit (20.0f, 18000.0f, grabLine.fLo * ratio); l.fHi = juce::jlimit (30.0f, 20000.0f, grabLine.fHi * ratio);
        proc.linesChanged(); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    static bool isAudio (const juce::String& f) { return f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".ogg"); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (const auto& f : files) if (isAudio (f)) return true; return false; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (const auto& f : files) if (isAudio (f) && proc.loadFile (juce::File (f))) { proc.setValue ("mode", 0.0f); layoutMode(); break; }
    }
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a hit", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<Panel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile() && ! safe->proc.loadFile (f)) safe->note = "COULD NOT READ " + f.getFileName().toUpperCase();
            safe->repaint();
        });
    }
    void chooseSave()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save the result", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (fs ? "Friction Stretch.wav" : "Bounce Delay.wav"), "*.wav");
        juce::Component::SafePointer<Panel> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
            safe->note = safe->proc.saveShown (f) ? "SAVED " + f.getFileName().toUpperCase() : juce::String ("NOTHING TO SAVE YET");
            safe->noteTicks = 90;
        });
    }

    // ---------------------------------------------------------------- the clock
    void timerCallback() override
    {
        bool tempoMoved = false;
        for (auto* k : bank) k->sync();
        for (auto* k : sideKnobs) k->sync();
        const bool isTempo = proc.value ("rateTempo") > 0.5f;
        if (isTempo != tempo.getToggleState()) tempoMoved = true;
        const bool cap = proc.value ("mode") > 0.5f;
        if ((mode.getSelected() == 1) != cap) layoutMode();
        loop.setSelected (juce::roundToInt (proc.value ("loop")));
        keytrack.setToggleState (proc.value ("keytrack") > 0.5f, juce::dontSendNotification);
        choke.setToggleState (proc.value ("choke") > 0.5f, juce::dontSendNotification);
        if (presets.getSelectedId() != proc.getCurrentProgram() + 1) presets.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        if (tempoMoved) triggerAsyncUpdate();

        auto& eng = proc.engine();
        const uint32_t h = eng.hits.load();
        if (h != lastHit) { lastHit = h; hitAt = juce::Time::getMillisecondCounterHiRes(); lamp = 1.0f; }
        lamp *= 0.85f;
        meterShown = std::max (eng.meter.load(), meterShown * 0.8f);
        juce::String s;
        if (noteTicks > 0) { --noteTicks; s = note; }
        else if (eng.renderer.busy()) s = "RENDERING...";
        else s = juce::String (proc.displayBpm.load(), 1) + " BPM | " + (proc.hostPlaying.load() ? "HOST RUNNING" : "HOST STOPPED") + (cap ? " | " + juce::String (eng.renderer.captures()) + " HITS CAUGHT" : juce::String());
        const void* shownNow = eng.renderer.shown().get();
        bool again = s != status || shownNow != lastShown || cap;
        status = s; lastShown = shownNow;
        if (! fs)
        {
            const double t = (juce::Time::getMillisecondCounterHiRes() - hitAt) / 1000.0;
            const double newT = t < 12.0 ? t : -1.0;
            if (newT >= 0.0 || animT >= 0.0) { animT = newT; again = true; }
        }
        const auto sig = proc.settingsNow();
        nb::Hasher hh; hashBounce (hh, sig.bd); hh.d (proc.displayBpm.load()).p (proc.currentHit().get());
        if (hh.h != paintHash) { paintHash = hh.h; again = true; }
        if (again) repaint();
    }

private:
    FxProcessor& proc;
    bool fs;
    Segmented mode, loop, pages;
    juce::ComboBox presets;
    Pill load { "LOAD FILE" }, builtIn { "BUILT-IN" }, play { "PLAY" }, keytrack { "KEYTRACK" }, choke { "CHOKE" }, keep { "KEEP LAST HIT" }, tempo { "TEMPO" },
         addLine { "+ LINE" }, delLine { "DELETE LINE" }, saveWav { "SAVE WAV" };
    juce::OwnedArray<Knob> bank, sideKnobs;
    Knob* kDry = nullptr; Knob* kThru = nullptr; Knob* kThreshold = nullptr; Knob* kCapture = nullptr; Knob* kLevel = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> display;
    juce::Image image;
    const void* imageSrc = nullptr;
    const void* lastShown = nullptr;
    juce::Array<juce::Rectangle<float>> lineRects;
    int page = 0, line = -1, noteTicks = 0;
    bool dragging = false;
    float grabY = 0.0f, lamp = 0.0f, meterShown = 0.0f;
    nb::StretchLine grabLine;
    juce::String status, note;
    uint32_t lastHit = 0;
    uint64_t paintHash = 0;
    double hitAt = -1.0e9, animT = -1.0;
};
} // namespace fxui

FxEditor::FxEditor (FxProcessor& p) : juce::AudioProcessorEditor (p)
{
    look = std::make_unique<fxui::Look>();
    setLookAndFeel (look.get());
    panel = std::make_unique<fxui::Panel> (p);
    addAndMakeVisible (*panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (kW / 2, kH / 2, kW * 2, kH * 2);
    double scale = 1.0;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = juce::jlimit (0.5, 1.0, std::min ((double) d->userBounds.getWidth() * 0.92 / kW, (double) d->userBounds.getHeight() * 0.88 / kH));
    setSize (juce::roundToInt (kW * scale), juce::roundToInt (kH * scale));
}

FxEditor::~FxEditor() { panel.reset(); setLookAndFeel (nullptr); }

void FxEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xffc8d1cc)); }

void FxEditor::resized()
{
    if (panel == nullptr) return;
    panel->setBounds (0, 0, kW, kH);
    panel->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW, (float) getHeight() / (float) kH));
}
