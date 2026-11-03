#include "PluginEditor.h"

using namespace ui;

namespace
{
const char* const kKnobNames[] = { "TUNE", "DECAY", "TONE", "DRIVE", "HOLLOW", "EVOLVE", "PAN", "LEVEL" };
const char* const kKnobParams[] = { "tune", "decay", "tone", "drive", "hollow", "evolve", "pan", "level" };
const bool kKnobBipolar[] = { true, true, true, false, false, false, true, false };

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

// ============================================================== Look

Look::Look()
{
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
#endif
    setColour (juce::ResizableWindow::backgroundColourId, Palette::bg());
    setColour (juce::ComboBox::backgroundColourId, Palette::panelHi());
    setColour (juce::ComboBox::textColourId, Palette::text());
    setColour (juce::ComboBox::outlineColourId, Palette::line());
    setColour (juce::ComboBox::arrowColourId, Palette::dim());
    setColour (juce::PopupMenu::backgroundColourId, Palette::panel());
    setColour (juce::PopupMenu::textColourId, Palette::text());
    setColour (juce::PopupMenu::headerTextColourId, Palette::accent());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent().withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, Palette::panelHi());
    setColour (juce::TextButton::buttonOnColourId, Palette::accent());
    setColour (juce::TextButton::textColourOffId, Palette::text());
    setColour (juce::TextButton::textColourOnId, Palette::bg());
    setColour (juce::Slider::textBoxTextColourId, Palette::text());
    setColour (juce::Slider::textBoxBackgroundColourId, Palette::panelHi());
    setColour (juce::Slider::textBoxOutlineColourId, Palette::line());
    setColour (juce::Slider::trackColourId, Palette::accent().withAlpha (0.35f));
    setColour (juce::Label::textColourId, Palette::text());
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const auto colour = juce::Colour::fromString (s.getProperties().getWithDefault ("colour", Palette::accent().toString()).toString());
    const bool compact = (bool) s.getProperties().getWithDefault ("compact", false);
    const float textH = compact ? 0.0f : 15.0f;
    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h - textH);
    const float d = std::min (area.getWidth(), area.getHeight()) - 6.0f;
    auto c = area.withSizeKeepingCentre (d, d);
    const float r = d * 0.5f, cx = c.getCentreX(), cy = c.getCentreY();
    const float angle = start + pos * (end - start);
    // body
    g.setColour (Palette::panelHi());
    g.fillEllipse (c.reduced (r * 0.28f));
    g.setColour (Palette::line());
    g.drawEllipse (c.reduced (r * 0.28f), 1.0f);
    // track and value arcs
    auto arc = [&] (float a0, float a1, juce::Colour col, float thick)
    {
        juce::Path p;
        p.addCentredArc (cx, cy, r - 3.0f, r - 3.0f, 0.0f, a0, a1, true);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (start, end, Palette::line(), 3.0f);
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::fabs (angle - from) > 0.001f) arc (std::min (from, angle), std::max (from, angle), colour, 3.0f);
    // pointer
    const float pr0 = r * 0.30f, pr1 = r * 0.66f;
    g.setColour (Palette::text());
    g.drawLine (cx + pr0 * std::sin (angle), cy - pr0 * std::cos (angle), cx + pr1 * std::sin (angle), cy - pr1 * std::cos (angle), 2.0f);
    if (! compact)
    {
        g.setColour (s.isMouseOverOrDragging() ? Palette::text() : Palette::dim());
        g.setFont (font (11.5f));
        g.drawText (s.getTextFromValue (s.getValue()), x, y + h - (int) textH, w, (int) textH, juce::Justification::centred);
    }
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (Palette::panelHi());
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Palette::accent().withAlpha (0.6f) : Palette::line());
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Palette::dim());
    g.fillPath (arrow);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? Palette::accent() : Palette::panelHi();
    if (over && ! on) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.15f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Palette::accent().brighter (0.2f) : Palette::line());
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

// ============================================================== PadGrid

juce::Rectangle<float> PadGrid::padBounds (int pad) const
{
    const float gap = 6.0f;
    const float w = ((float) getWidth() - 3.0f * gap) / 4.0f, h = ((float) getHeight() - 3.0f * gap) / 4.0f;
    // pad 1 bottom-left, like hardware drum machines
    const int col = pad % 4, row = 3 - pad / 4;
    return { (float) col * (w + gap), (float) row * (h + gap), w, h };
}

int PadGrid::padAt (juce::Point<int> pos) const
{
    for (int p = 0; p < 16; ++p)
        if (padBounds (p).contains (pos.toFloat())) return p;
    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const int sel = proc.selectedPad.load();
    for (int p = 0; p < 16; ++p)
    {
        auto r = padBounds (p);
        const auto colour = Palette::forGroup (proc.padGroup (p));
        const bool muted = proc.apvts.getRawParameterValue (proc.paramId (p, "mute"))->load() > 0.5f;
        g.setColour (Palette::panelHi());
        g.fillRoundedRectangle (r, 6.0f);
        if (flash[(size_t) p] > 0.01f)
        {
            g.setColour (colour.withAlpha (0.55f * flash[(size_t) p]));
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (colour.withAlpha (muted ? 0.25f : 0.9f));
        g.fillRoundedRectangle (r.withWidth (4.0f).reduced (0.0f, 8.0f), 2.0f);
        g.setColour (p == sel ? colour : Palette::line());
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, p == sel ? 1.6f : 1.0f);
        g.setColour (Palette::dim());
        g.setFont (font (10.5f, true));
        g.drawText (juce::String (p + 1), r.reduced (10.0f, 6.0f).toNearestInt(), juce::Justification::topLeft);
        if (proc.padUsesSample (p))
        {
            g.setColour (colour);
            g.drawText ("SMP", r.reduced (8.0f, 6.0f).toNearestInt(), juce::Justification::topRight);
        }
        else if (muted)
        {
            g.drawText ("M", r.reduced (8.0f, 6.0f).toNearestInt(), juce::Justification::topRight);
        }
        g.setColour (muted ? Palette::dim() : Palette::text());
        g.setFont (font (12.0f));
        g.drawFittedText (proc.padName (p), r.reduced (10.0f, 6.0f).withTrimmedTop (16.0f).toNearestInt(), juce::Justification::bottomLeft, 2, 0.9f);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int p = padAt (e.getPosition());
    if (p < 0) return;
    if (e.mods.isPopupMenu()) { if (onMenu) onMenu (p); return; }
    // velocity from where the pad is hit: higher is harder
    const auto r = padBounds (p);
    const float vel = juce::jlimit (0.3f, 1.0f, 1.0f - 0.7f * (((float) e.y - r.getY()) / r.getHeight()) + 0.15f);
    proc.audition (p, vel);
    if (onSelect) onSelect (p);
}

void PadGrid::tick()
{
    bool dirty = false;
    for (int p = 0; p < 16; ++p)
    {
        const auto h = proc.hitCount (p);
        if (h != lastHits[(size_t) p]) { lastHits[(size_t) p] = h; flash[(size_t) p] = 1.0f; dirty = true; }
        else if (flash[(size_t) p] > 0.01f) { flash[(size_t) p] *= 0.82f; dirty = true; }
    }
    if (dirty) repaint();
}

// ============================================================== StepGrid

bool StepGrid::cellAt (juce::Point<int> pos, int& track, int& step) const
{
    const int len = proc.pattern.length.load();
    const float top = 20.0f, rowH = ((float) getHeight() - top) / 16.0f;
    const float cw = ((float) getWidth() - (float) kLabelWidth) / (float) len;
    if (pos.y < top) return false;
    track = juce::jlimit (0, 15, (int) (((float) pos.y - top) / rowH));
    if (pos.x < kLabelWidth) { step = -1; return true; }
    step = juce::jlimit (0, len - 1, (int) (((float) pos.x - (float) kLabelWidth) / cw));
    return true;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int len = proc.pattern.length.load();
    const float top = 20.0f, rowH = ((float) getHeight() - top) / 16.0f;
    const float cw = ((float) getWidth() - (float) kLabelWidth) / (float) len;
    const int play = proc.playStep.load();
    const int sel = proc.selectedPad.load();

    // step numbers and beat shading
    g.setFont (font (10.0f));
    for (int s = 0; s < len; ++s)
    {
        const float x = (float) kLabelWidth + (float) s * cw;
        if ((s / 4) % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRect (x, top, cw, (float) getHeight() - top);
        }
        if (s % 4 == 0)
        {
            g.setColour (s % 16 == 0 ? Palette::text() : Palette::dim());
            g.drawText (juce::String (s / 4 + 1), (int) x + 2, 2, 30, 14, juce::Justification::left);
        }
    }
    if (play >= 0 && play < len)
    {
        g.setColour (Palette::accent().withAlpha (0.12f));
        g.fillRect ((float) kLabelWidth + (float) play * cw, top, cw, (float) getHeight() - top);
    }
    for (int t = 0; t < 16; ++t)
    {
        const float y = top + (float) t * rowH;
        const auto colour = Palette::forGroup (proc.padGroup (t));
        const bool muted = proc.apvts.getRawParameterValue (proc.paramId (t, "mute"))->load() > 0.5f;
        if (t == sel)
        {
            g.setColour (colour.withAlpha (0.08f));
            g.fillRect (0.0f, y, (float) getWidth(), rowH);
            g.setColour (colour);
            g.fillRect (0.0f, y + 3.0f, 2.0f, rowH - 6.0f);
        }
        g.setColour (muted ? Palette::dim().withAlpha (0.6f) : (t == sel ? Palette::text() : Palette::dim()));
        g.setFont (font (11.0f));
        g.drawText (juce::String (t + 1).paddedLeft ('0', 2) + "  " + proc.padName (t), 8, (int) y, kLabelWidth - 12, (int) rowH,
                    juce::Justification::centredLeft, true);
        for (int s = 0; s < len; ++s)
        {
            const auto st = proc.pattern.get (t, s);
            auto cell = juce::Rectangle<float> ((float) kLabelWidth + (float) s * cw, y, cw, rowH).reduced (1.5f, 2.5f);
            if (st.vel == 0)
            {
                g.setColour (Palette::panelHi().withAlpha (0.8f));
                g.fillRoundedRectangle (cell, 2.0f);
                continue;
            }
            const float a = 0.25f + 0.75f * (float) st.vel / 127.0f;
            g.setColour (colour.withAlpha (muted ? a * 0.35f : a));
            auto shifted = cell.translated (cell.getWidth() * 0.6f * (float) st.nudge / 100.0f, 0.0f);
            g.fillRoundedRectangle (shifted, 2.0f);
            if (st.roll >= 2 && st.roll <= 4)
            {
                g.setColour (Palette::bg().withAlpha (0.8f));
                const float dw = shifted.getWidth() / (float) (st.roll + 1);
                for (int k = 0; k < st.roll; ++k)
                    g.fillEllipse (shifted.getX() + dw * (float) (k + 1) - 1.5f, shifted.getBottom() - 5.0f, 3.0f, 3.0f);
            }
            else if (st.roll == 5)
            {
                g.setColour (Palette::bg().withAlpha (0.85f));
                g.setFont (font (9.5f, true));
                g.drawText ("3", shifted.toNearestInt(), juce::Justification::centred);
                g.setColour (colour.withAlpha (0.6f));
                g.drawLine (shifted.getX(), shifted.getBottom() + 1.5f, shifted.getRight() + cw, shifted.getBottom() + 1.5f, 1.0f);
            }
        }
    }
    // bar lines
    g.setColour (Palette::line());
    for (int s = 16; s < len; s += 16)
        g.drawVerticalLine (kLabelWidth + (int) ((float) s * cw), top, (float) getHeight());
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int t, s;
    dragMode = 0;
    if (! cellAt (e.getPosition(), t, s)) return;
    if (s < 0) { if (onSelectPad) onSelectPad (t); return; }
    if (e.mods.isPopupMenu()) { stepMenu (t, s); return; }
    auto st = proc.pattern.get (t, s);
    if (st.vel == 0)
    {
        dragVel = e.mods.isShiftDown() ? 124 : e.mods.isAltDown() ? 64 : (e.mods.isCtrlDown() || e.mods.isCommandDown()) ? 42 : 100;
        st.vel = dragVel; st.roll = 0; st.nudge = 0;
        dragMode = 1;
    }
    else
    {
        st = nb::Step();
        dragMode = -1;
    }
    proc.pattern.set (t, s, st);
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    int t, s;
    if (dragMode == 0 || ! cellAt (e.getPosition(), t, s) || s < 0) return;
    auto st = proc.pattern.get (t, s);
    if (dragMode == 1 && st.vel == 0) { st.vel = dragVel; proc.pattern.set (t, s, st); repaint(); }
    else if (dragMode == -1 && st.vel != 0) { proc.pattern.set (t, s, nb::Step()); repaint(); }
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int t, s;
    if (! cellAt (e.getPosition(), t, s) || s < 0) return;
    auto st = proc.pattern.get (t, s);
    if (st.vel == 0) return;
    st.vel = (uint8_t) juce::jlimit (1, 127, (int) st.vel + (w.deltaY > 0 ? 8 : -8));
    proc.pattern.set (t, s, st);
    repaint();
}

void StepGrid::stepMenu (int track, int step)
{
    const auto st = proc.pattern.get (track, step);
    juce::PopupMenu m;
    m.addSectionHeader ("Velocity");
    const int vels[] = { 124, 100, 64, 42 };
    const char* velNames[] = { "Accent", "Normal", "Ghost", "Soft ghost" };
    for (int i = 0; i < 4; ++i) m.addItem (1 + i, velNames[i], true, st.vel == vels[i]);
    m.addItem (5, "Off", true, st.vel == 0);
    m.addSectionHeader ("Roll");
    const char* rollNames[] = { "Single hit", "Ratchet x2", "Ratchet x3", "Ratchet x4", "Triplet roll (over two steps)" };
    const int rolls[] = { 0, 2, 3, 4, 5 };
    for (int i = 0; i < 5; ++i) m.addItem (10 + i, rollNames[i], true, st.vel > 0 && st.roll == rolls[i]);
    m.addSectionHeader ("Nudge");
    const int nudges[] = { -25, -10, 0, 10, 25 };
    const char* nudgeNames[] = { "Push 25% early", "Push 10% early", "On the grid", "Drag 10% late", "Drag 25% late" };
    for (int i = 0; i < 5; ++i) m.addItem (20 + i, nudgeNames[i], true, st.vel > 0 && st.nudge == nudges[i]);
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, track, step] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto s = safe->proc.pattern.get (track, step);
        const int velsL[] = { 124, 100, 64, 42 };
        const int rollsL[] = { 0, 2, 3, 4, 5 };
        const int nudgesL[] = { -25, -10, 0, 10, 25 };
        if (r >= 1 && r <= 4) s.vel = (uint8_t) velsL[r - 1];
        else if (r == 5) s = nb::Step();
        else if (r >= 10 && r <= 14) { if (s.vel == 0) s.vel = 100; s.roll = (uint8_t) rollsL[r - 10]; }
        else if (r >= 20 && r <= 24) { if (s.vel == 0) s.vel = 100; s.nudge = (int8_t) nudgesL[r - 20]; }
        safe->proc.pattern.set (track, step, s);
        safe->repaint();
    });
}

// ============================================================== Spectrum3D

Spectrum3D::Spectrum3D (NeoBeatProcessor& p) : proc (p)
{
    for (int i = 0; i < kSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kSize - 1));
    for (auto& f : history) f.fill (0.0f);
}

void Spectrum3D::tick()
{
    auto& fifo = proc.tapFifo;
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = fifo.read (ready);
        auto copy = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i) { ring[(size_t) ringPos] = proc.tapBuffer[(size_t) (start + i)]; ringPos = (ringPos + 1) % kSize; }
        };
        copy (scope.startIndex1, scope.blockSize1);
        copy (scope.startIndex2, scope.blockSize2);
    }
    for (int i = 0; i < kSize; ++i) work[(size_t) i] = ring[(size_t) ((ringPos + i) % kSize)] * window[(size_t) i];
    std::fill (work.begin() + kSize, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data());
    newest = (newest + 1) % kFrames;
    auto& frame = history[(size_t) newest];
    const float sr = 48000.0f, fLo = 30.0f, fHi = 16000.0f;
    for (int b = 0; b < kBins; ++b)
    {
        const float f0 = fLo * std::pow (fHi / fLo, (float) b / kBins), f1 = fLo * std::pow (fHi / fLo, (float) (b + 1) / kBins);
        const int i0 = juce::jlimit (1, kSize / 2 - 1, (int) (f0 / sr * kSize)), i1 = juce::jlimit (i0 + 1, kSize / 2, (int) (f1 / sr * kSize) + 1);
        float m = 0.0f;
        for (int i = i0; i < i1; ++i) m = std::max (m, work[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (m / (0.25f * kSize), -120.0f);
        frame[(size_t) b] = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 75.0f);
    }
    repaint();
}

void Spectrum3D::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto colour = Palette::forGroup (proc.padGroup (proc.selectedPad.load()));
    const float left = 18.0f, bottom = area.getBottom() - 22.0f;
    const float width = area.getWidth() - 160.0f, ampH = area.getHeight() * 0.42f;
    const float depthX = 120.0f, depthY = area.getHeight() * 0.42f;
    for (int k = 0; k < kFrames; ++k)
    {
        const int idx = (newest + 1 + k) % kFrames;   // oldest first
        const float d = 1.0f - (float) k / (float) (kFrames - 1);   // 1 = far back
        const float x0 = left + d * depthX, y0 = bottom - d * depthY;
        juce::Path p;
        p.startNewSubPath (x0, y0);
        for (int b = 0; b < kBins; ++b)
            p.lineTo (x0 + width * (float) b / (float) (kBins - 1), y0 - history[(size_t) idx][(size_t) b] * ampH);
        p.lineTo (x0 + width, y0);
        p.closeSubPath();
        g.setColour (Palette::panel());
        g.fillPath (p);
        g.setColour (colour.withAlpha (0.12f + 0.75f * (1.0f - d) * (1.0f - d)));
        g.strokePath (p, juce::PathStrokeType (k == kFrames - 1 ? 1.6f : 1.0f));
    }
    g.setColour (Palette::dim());
    g.setFont (font (10.0f));
    const float fLo = 30.0f, fHi = 16000.0f;
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = left + width * std::log (f / fLo) / std::log (fHi / fLo);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) x - 15, (int) bottom + 4, 30, 14, juce::Justification::centred);
    }
}

// ============================================================== Editor

NeoBeatEditor::NeoBeatEditor (NeoBeatProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pads (p), steps (p), spectrum (p)
{
    setLookAndFeel (&look);
    setSize (1180, 740);

    addAndMakeVisible (pads);
    addAndMakeVisible (steps);
    addAndMakeVisible (spectrum);
    pads.onSelect = [this] (int pad) { selectPad (pad); };
    pads.onMenu = [this] (int pad) { padMenu (pad); };
    steps.onSelectPad = [this] (int pad) { selectPad (pad); };

    // presets, grouped by family
    juce::String family;
    for (int i = 0; i < nb::kNumPresets; ++i)
    {
        const auto& pr = nb::presets()[(size_t) i];
        if (pr.family != family.toStdString()) { family = pr.family; presetBox.addSectionHeading (family); }
        presetBox.addItem (juce::String (i + 1).paddedLeft ('0', 2) + "  " + pr.name + "   " + juce::String ((int) pr.tempo) + " bpm", i + 1);
    }
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0 && id - 1 != proc.currentPreset.load()) { proc.loadPreset (id - 1); shownPreset = id - 1; } };
    prevPreset.onClick = [this] { proc.loadPreset ((proc.currentPreset.load() + nb::kNumPresets - 1) % nb::kNumPresets); };
    nextPreset.onClick = [this] { proc.loadPreset ((proc.currentPreset.load() + 1) % nb::kNumPresets); };
    for (auto* c : { &presetBox, &kitBox, &modeBox, &soundBox, &lengthBox, &chokeBox }) addAndMakeVisible (*c);
    for (auto* b : { &prevPreset, &nextPreset, &playButton, &muteButton, &sampleButton }) addAndMakeVisible (*b);

    for (int i = 0; i < (int) nb::kits().size(); ++i) kitBox.addItem (nb::kits()[(size_t) i].name, i + 1);
    kitBox.onChange = [this] { const int id = kitBox.getSelectedId(); if (id > 0 && id - 1 != proc.currentKit.load()) { proc.loadKit (id - 1); shownKit = id - 1; } };

    modeBox.addItemList ({ "Host", "Key", "Pads" }, 1);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    modeBox.setTooltip ("Host: plays with your DAW's transport. Key: hold C3 or above to play the pattern transposed. Pads: no sequencer.");

    playButton.setClickingTogglesState (true);
    playAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "run", playButton);

    tempo.setSliderStyle (juce::Slider::LinearBar);
    tempo.setTextValueSuffix (" bpm");
    tempo.setColour (juce::Slider::trackColourId, Palette::accent().withAlpha (0.18f));
    addAndMakeVisible (tempo);
    tempoAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "tempo", tempo);

    for (auto* s : { &swing, &master })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->getProperties().set ("compact", true);
        s->setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (*s);
    }
    swingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "swing", swing);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "master", master);
    master.getProperties().set ("bipolar", false);

    lengthBox.addItem ("16 steps", 16);
    lengthBox.addItem ("32 steps", 32);
    lengthBox.addItem ("64 steps", 64);
    lengthBox.setSelectedId (proc.pattern.length.load(), juce::dontSendNotification);
    lengthBox.onChange = [this]
    {
        const int len = lengthBox.getSelectedId();
        if (len <= 0 || len == proc.pattern.length.load()) return;
        // growing copies the existing bars forward, so the groove keeps going
        const int old = proc.pattern.length.load();
        for (int t = 0; t < 16; ++t)
            for (int s = old; s < len; ++s) proc.pattern.set (t, s, proc.pattern.get (t, s % old));
        proc.pattern.length.store (len);
        proc.pattern.version.fetch_add (1);
        steps.repaint();
    };

    // sound picker, grouped
    juce::String group;
    const auto& lib = proc.library();
    for (size_t i = 0; i < lib.sounds.size(); ++i)
    {
        if (lib.sounds[i].group != group.toStdString()) { group = lib.sounds[i].group; soundBox.addSectionHeading (group); }
        soundBox.addItem (lib.sounds[i].name, (int) i + 1);
    }
    soundBox.onChange = [this]
    {
        const int id = soundBox.getSelectedId();
        if (id <= 0) return;
        if (proc.padUsesSample (selected)) proc.clearSample (selected);
        if (auto* prm = proc.apvts.getParameter (proc.paramId (selected, "sound"))) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (id - 1)));
        shownSound = id - 1;
    };

    for (int i = 0; i < kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.getProperties().set ("bipolar", kKnobBipolar[i]);
        k.setMouseDragSensitivity (160);
        addAndMakeVisible (k);
    }
    chokeBox.addItem ("No choke", 1);
    for (int c = 1; c <= 4; ++c) chokeBox.addItem ("Choke " + juce::String (c), c + 1);
    muteButton.setClickingTogglesState (true);
    sampleButton.onClick = [this] { chooseSample (selected); };

    selectPad (0);
    startTimerHz (30);
}

NeoBeatEditor::~NeoBeatEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NeoBeatEditor::selectPad (int pad)
{
    selected = juce::jlimit (0, 15, pad);
    proc.selectedPad.store (selected);
    const auto colour = Palette::forGroup (proc.padGroup (selected));
    for (int i = 0; i < kKnobs; ++i)
    {
        knobAtt[(size_t) i].reset();
        knobAtt[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, proc.paramId (selected, kKnobParams[i]), knobs[(size_t) i]);
        if (auto* prm = proc.apvts.getParameter (proc.paramId (selected, kKnobParams[i])))
            knobs[(size_t) i].setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        knobs[(size_t) i].getProperties().set ("colour", colour.toString());
        knobs[(size_t) i].repaint();
    }
    chokeAtt.reset();
    chokeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, proc.paramId (selected, "choke"), chokeBox);
    muteAtt.reset();
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, proc.paramId (selected, "mute"), muteButton);
    shownSound = -1;
    refreshSoundBox();
    pads.repaint();
    steps.repaint();
    repaint (editPanel);
}

void NeoBeatEditor::refreshSoundBox()
{
    const bool smp = proc.padUsesSample (selected);
    const int si = (int) proc.apvts.getRawParameterValue (proc.paramId (selected, "sound"))->load();
    if (smp == shownSample && si == shownSound) return;
    shownSample = smp; shownSound = si;
    if (smp) soundBox.setText ("Sample: " + proc.padName (selected), juce::dontSendNotification);
    else soundBox.setSelectedId (si + 1, juce::dontSendNotification);
    const auto colour = Palette::forGroup (proc.padGroup (selected));
    for (auto& k : knobs) { k.getProperties().set ("colour", colour.toString()); k.repaint(); }
    pads.repaint();
    steps.repaint();
    repaint (editPanel);
}

void NeoBeatEditor::padMenu (int pad)
{
    selectPad (pad);
    juce::PopupMenu m;
    m.addItem (1, "Load sample...");
    m.addItem (2, "Back to the library sound", proc.padUsesSample (pad));
    const bool muted = proc.apvts.getRawParameterValue (proc.paramId (pad, "mute"))->load() > 0.5f;
    m.addItem (3, muted ? "Unmute" : "Mute");
    m.addItem (4, "Clear this pad's steps");
    juce::Component::SafePointer<NeoBeatEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, pad, muted] (int r)
    {
        if (safe == nullptr) return;
        auto& pr = safe->proc;
        if (r == 1) safe->chooseSample (pad);
        if (r == 2) pr.clearSample (pad);
        if (r == 3) if (auto* prm = pr.apvts.getParameter (pr.paramId (pad, "mute"))) prm->setValueNotifyingHost (muted ? 0.0f : 1.0f);
        if (r == 4) for (int s = 0; s < nb::Pattern::kMaxSteps; ++s) pr.pattern.set (pad, s, nb::Step());
        safe->shownSound = -1;
        safe->refreshSoundBox();
    });
}

void NeoBeatEditor::chooseSample (int pad)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample onto pad " + juce::String (pad + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<NeoBeatEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, pad] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f.existsAsFile() && safe->proc.loadSample (pad, f)) { safe->selectPad (pad); safe->proc.audition (pad, 0.85f); }
    });
}

void NeoBeatEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const int pad = pads.padAt (pads.getLocalPoint (this, juce::Point<int> (x, y)));
    const int target = pad >= 0 ? pad : selected;
    if (proc.loadSample (target, juce::File (files[0]))) { selectPad (target); proc.audition (target, 0.85f); }
}

void NeoBeatEditor::timerCallback()
{
    if (proc.selectedPad.load() != selected) selectPad (proc.selectedPad.load());
    pads.tick();
    steps.tick();
    spectrum.tick();
    refreshSoundBox();
    const int pr = proc.currentPreset.load();
    if (pr != shownPreset) { shownPreset = pr; presetBox.setSelectedId (pr + 1, juce::dontSendNotification); steps.repaint(); }
    const int kit = proc.currentKit.load();
    if (kit != shownKit) { shownKit = kit; kitBox.setSelectedId (kit + 1, juce::dontSendNotification); pads.repaint(); steps.repaint(); }
    if (lengthBox.getSelectedId() != proc.pattern.length.load()) { lengthBox.setSelectedId (proc.pattern.length.load(), juce::dontSendNotification); steps.repaint(); }
    const uint32_t v = proc.pattern.version.load();
    if (v != lastVersion) { lastVersion = v; steps.repaint(); }
    repaint (headerArea.withTrimmedLeft (820).withWidth (130));
}

void NeoBeatEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg());
    // header
    g.setColour (Palette::panel());
    g.fillRect (headerArea);
    g.setColour (Palette::line());
    g.drawHorizontalLine (headerArea.getBottom(), 0.0f, (float) getWidth());
    g.setColour (Palette::text());
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)).withExtraKerningFactor (0.22f));
    g.drawText ("NEOBEAT", 20, 0, 160, headerArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (Palette::accent());
    g.fillRect (20, headerArea.getHeight() - 16, 28, 2);
    g.setColour (Palette::dim());
    g.setFont (font (9.5f, true));
    g.drawText ("SWING", swing.getX() - 10, swing.getBottom() - 2, swing.getWidth() + 20, 12, juce::Justification::centred);
    g.drawText ("MASTER", master.getX() - 10, master.getBottom() - 2, master.getWidth() + 20, 12, juce::Justification::centred);
    g.drawText (proc.hostSynced.load() ? "HOST " + juce::String (proc.displayBpm.load(), 1) : "STANDALONE TEMPO",
                tempo.getX(), tempo.getBottom() + 1, tempo.getWidth(), 12, juce::Justification::centred);

    // panels
    for (auto r : { padPanel, editPanel, meshPanel, gridPanel })
    {
        g.setColour (Palette::panel());
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (Palette::line());
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
    g.setFont (font (10.5f, true));
    g.setColour (Palette::dim());
    g.drawText ("PADS", padPanel.getX() + 14, padPanel.getY() + 8, 100, 14, juce::Justification::left);
    g.drawText ("SPECTRUM  /  PAD " + juce::String (selected + 1), meshPanel.getX() + 14, meshPanel.getY() + 8, 300, 14, juce::Justification::left);
    g.drawText ("SEQUENCER", gridPanel.getX() + 14, gridPanel.getY() + 8, 200, 14, juce::Justification::left);
    g.drawText ("click: hit   shift: accent   alt: ghost   right-click: rolls & nudge   wheel: velocity",
                gridPanel.getX() + 110, gridPanel.getY() + 8, 520, 14, juce::Justification::left);

    // pad editor header and knob labels
    const auto colour = Palette::forGroup (proc.padGroup (selected));
    g.setColour (colour);
    g.fillRoundedRectangle ((float) editPanel.getX() + 14.0f, (float) editPanel.getY() + 12.0f, 4.0f, 14.0f, 2.0f);
    g.setColour (Palette::text());
    g.setFont (font (13.0f, true));
    g.drawText ("PAD " + juce::String (selected + 1), editPanel.getX() + 24, editPanel.getY() + 10, 80, 18, juce::Justification::left);
    g.setColour (Palette::dim());
    g.setFont (font (11.0f));
    g.drawText (proc.padGroup (selected), editPanel.getX() + 84, editPanel.getY() + 10, 200, 18, juce::Justification::left);
    g.setFont (font (10.0f, true));
    for (int i = 0; i < kKnobs; ++i)
    {
        const auto b = knobs[(size_t) i].getBounds();
        g.drawText (kKnobNames[i], b.getX(), b.getY() - 14, b.getWidth(), 12, juce::Justification::centred);
    }
}

void NeoBeatEditor::resized()
{
    headerArea = { 0, 0, getWidth(), 60 };
    padPanel = { 16, 76, 360, 316 };
    editPanel = { 16, 404, 360, 320 };
    meshPanel = { 392, 76, 772, 196 };
    gridPanel = { 392, 284, 772, 440 };

    prevPreset.setBounds (176, 16, 26, 28);
    presetBox.setBounds (204, 16, 270, 28);
    nextPreset.setBounds (476, 16, 26, 28);
    kitBox.setBounds (518, 16, 150, 28);
    modeBox.setBounds (680, 16, 86, 28);
    playButton.setBounds (776, 16, 58, 28);
    tempo.setBounds (844, 14, 104, 26);
    swing.setBounds (964, 6, 42, 42);
    master.setBounds (1024, 6, 42, 42);

    pads.setBounds (padPanel.reduced (14).withTrimmedTop (16));
    spectrum.setBounds (meshPanel.reduced (8).withTrimmedTop (18));
    steps.setBounds (gridPanel.reduced (10).withTrimmedTop (20));
    lengthBox.setBounds (gridPanel.getRight() - 104, gridPanel.getY() + 4, 92, 22);

    auto e = editPanel.reduced (14);
    e.removeFromTop (22);
    soundBox.setBounds (e.removeFromTop (28));
    e.removeFromTop (20);
    const int kw = e.getWidth() / 4;
    auto row1 = e.removeFromTop (80);
    e.removeFromTop (18);
    auto row2 = e.removeFromTop (80);
    for (int i = 0; i < 4; ++i) knobs[(size_t) i].setBounds (row1.getX() + i * kw, row1.getY(), kw, row1.getHeight());
    for (int i = 0; i < 4; ++i) knobs[(size_t) i + 4].setBounds (row2.getX() + i * kw, row2.getY(), kw, row2.getHeight());
    e.removeFromTop (12);
    auto bottom = e.removeFromTop (28);
    chokeBox.setBounds (bottom.removeFromLeft (112));
    bottom.removeFromLeft (8);
    muteButton.setBounds (bottom.removeFromLeft (70));
    bottom.removeFromLeft (8);
    sampleButton.setBounds (bottom);
}
