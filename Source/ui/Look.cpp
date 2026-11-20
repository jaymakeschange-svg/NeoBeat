// NeoBeat 2.0 — the Chrome Pearl look: chassis, plates, wells, pearl, LCDs, gems, knobs.
#include "UI.h"

namespace ui
{
juce::Colour C::family (const juce::String& f)
{
    if (f == "SMOOTH") return juce::Colour (0xffc7d3ff);
    if (f == "SUSTAIN") return juce::Colour (0xffbdf4ff);
    if (f == "EXCITE") return juce::Colour (0xfffff1b3);
    if (f == "PITCH") return juce::Colour (0xffffd1f3);
    if (f == "FILTER") return juce::Colour (0xffd6ffd0);
    if (f == "TEXTURE") return juce::Colour (0xffffc9a8);
    if (f == "PARTIALS") return juce::Colour (0xffe6d6ff);
    if (f == "TIME") return juce::Colour (0xffffe2bd);
    if (f == "FRICTION") return juce::Colour (0xfff5b25a);
    if (f == "BOUNCE") return juce::Colour (0xff7fe0d0);
    return juce::Colour (0xffdfe6e2);
}

juce::Colour C::fxColour (const nb::CellFx& fx) { return family (fxFamily (fx)); }

juce::Colour C::mod (nb::ModSrc s)
{
    static const juce::uint32 c[] = { 0xff14907f, 0xffc63c86, 0xffc9620e, 0xff5d52c9, 0xff2f6fc4, 0xff8a5a00, 0xff5f7010, 0xff2f7a46, 0xffb8322a, 0xff9a3d3d };
    return juce::Colour (c[(size_t) juce::jlimit (0, 9, (int) s)]);
}


juce::String fxName (const nb::CellFx& fx)
{
    switch (fx.kind)
    {
        case nb::CellFx::Library: return nb::matrixFxInfo()[(size_t) fx.fx].name;
        case nb::CellFx::FrictionStretchFx: return "Friction Stretch";
        case nb::CellFx::BounceDelayFx: return "Bounce Delay";
        default: return "None";
    }
}
juce::String fxShort (const nb::CellFx& fx)
{
    if (fx.kind == nb::CellFx::FrictionStretchFx) return "FRICTION x" + juce::String (juce::roundToInt (fx.stretch.stretch));
    if (fx.kind == nb::CellFx::BounceDelayFx) return "BOUNCE";
    if (fx.kind == nb::CellFx::None) return {};
    return juce::String (nb::matrixFxInfo()[(size_t) fx.fx].name).replace ("Spectral ", "").toUpperCase();
}
juce::String fxFamily (const nb::CellFx& fx)
{
    if (fx.kind == nb::CellFx::FrictionStretchFx) return "FRICTION";
    if (fx.kind == nb::CellFx::BounceDelayFx) return "BOUNCE";
    if (fx.kind == nb::CellFx::None) return {};
    const juce::String cat = nb::matrixFxInfo()[(size_t) fx.fx].category;
    if (cat == "Smoothing") return "SMOOTH";
    if (cat == "Sustaining") return "SUSTAIN";
    if (cat == "Excitement") return "EXCITE";
    if (cat == "Pitch") return "PITCH";
    if (cat == "Filtering") return "FILTER";
    if (cat == "Texture") return "TEXTURE";
    if (cat == "Partials") return "PARTIALS";
    return "TIME";
}
nb::CellFx fxFromIndex (int i)
{
    if (i == 24) return nb::CellFx::friction (nb::StretchParams());
    if (i == 25) return nb::CellFx::bouncing (nb::BounceParams());
    return nb::CellFx::library ((nb::MatrixFx) juce::jlimit (0, 23, i));
}
int fxIndex (const nb::CellFx& fx)
{
    if (fx.kind == nb::CellFx::FrictionStretchFx) return 24;
    if (fx.kind == nb::CellFx::BounceDelayFx) return 25;
    if (fx.kind == nb::CellFx::None) return -1;
    return (int) fx.fx;
}
juce::StringArray fxMenuNames()
{
    juce::StringArray a;
    for (int i = 0; i < 24; ++i) a.add (nb::matrixFxInfo()[(size_t) i].name);
    a.add ("Friction Stretch"); a.add ("Bounce Delay");
    return a;
}
juce::String noteName (int note)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[(size_t) ((note % 12 + 12) % 12)]) + juce::String (note / 12 - 2);   // Ableton naming: C3 = 60
}

// The drawing itself, the look and feel, and Segmented now live in Source/shared/Ui.h, which all three
// plugins draw with. What is left here is NeoBeat's own: the colours of its effect families and mod
// sources, the names of its effects, its Knob - which is a mod destination as well as a control - and its
// LcdBox.

// ------------------------------------------------------------------ the knob
Knob::Knob (Ctx& c, juce::String l, float lo, float hi, float def, bool logScale)
    : ctx (c), label (std::move (l)), minV (lo), maxV (hi), defV (def), value (def), isLog (logScale) {}

float Knob::toNorm (float v) const
{
    if (isLog && minV > 0.0f) return std::log (std::max (minV, v) / minV) / std::log (maxV / minV);
    return (v - minV) / (maxV - minV);
}
float Knob::fromNorm (float n) const
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (isLog && minV > 0.0f) return minV * std::pow (maxV / minV, n);
    return minV + n * (maxV - minV);
}
void Knob::setValue (float v, bool notify)
{
    v = juce::jlimit (std::min (minV, maxV), std::max (minV, maxV), v);
    if (v == value) return;
    value = v;
    if (param != nullptr && notify) param->setValueNotifyingHost (param->convertTo0to1 (v));
    if (notify && onChange) onChange (v);
    repaint();
}
void Knob::attach (juce::RangedAudioParameter* p)
{
    param = p;
    if (p != nullptr) { const auto r = p->getNormalisableRange(); minV = r.start; maxV = r.end; defV = p->convertFrom0to1 (p->getDefaultValue()); syncFromParam(); }
}
void Knob::syncFromParam()
{
    if (param == nullptr) return;
    const float v = param->convertFrom0to1 (param->getValue());
    if (v != value) { value = v; repaint(); }
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    paintLabel (g, label, b.removeFromTop (12), juce::Justification::centred, 7.5f, main ? C::text() : C::label());
    auto valueArea = b.removeFromBottom (15);
    const float size = (float) std::min (b.getWidth(), b.getHeight()) - 2.0f;
    auto area = juce::Rectangle<float> (size, size).withCentre (b.toFloat().getCentre());
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float rKnob = size * 0.5f - 7.0f;
    const float a0 = -2.356f, a1 = 2.356f;
    float norm = param != nullptr ? param->getValue() : toNorm (value);
    if (param != nullptr && isLog) norm = toNorm (value);
    const float ang = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, norm);
    // track and value arc
    auto arc = [&] (float r, float s, float e, juce::Colour col, float w)
    {
        juce::Path p; p.addCentredArc (cx, cy, r, r, 0.0f, std::min (s, e), std::max (s, e), true);
        g.setColour (col); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (rKnob + 4.0f, a0, a1, juce::Colour (0x40586560), 2.5f);
    arc (rKnob + 4.0f, a0, ang, juce::Colour (0xa01f2925), 2.5f);
    // mod rings: the routes that reach this knob
    int ring = 0;
    if (dest.isNotEmpty())
        for (const auto& r : ctx.P().mod.routes)
        {
            const juce::String d (r.dest);
            bool hit = d == dest;
            if (! hit && dest.startsWith ("pad/")) { auto parts = juce::StringArray::fromTokens (dest, "/", ""); hit = parts.size() == 3 && d == "pad/*/" + parts[2]; }
            if (! hit) continue;
            const float rr = rKnob + 8.5f + 4.0f * ring++;
            const float e = juce::jlimit (a0, a1, ang + (a1 - a0) * r.depth);
            arc (rr, ang, e, C::mod (r.src), 3.0f);
            g.setColour (C::mod (r.src)); g.fillEllipse (cx + rr * std::sin (e) - 2.5f, cy - rr * std::cos (e) - 2.5f, 5.0f, 5.0f);
            if (ring >= 3) break;
        }
    if (dropHover) { g.setColour (C::mod (ctx.dockSrc).withAlpha (0.7f)); g.drawEllipse (area.reduced (1.0f), 2.0f); }
    // knurled edge
    const int ticks = 36;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = juce::MathConstants<float>::twoPi * i / ticks;
        g.setColour (i % 2 ? juce::Colour (0xff7e8b85) : juce::Colour (0xfff6f9f7));
        g.drawLine (cx + (rKnob - 1.0f) * std::sin (t), cy - (rKnob - 1.0f) * std::cos (t), cx + rKnob * std::sin (t), cy - rKnob * std::cos (t), 2.0f);
    }
    auto cap = juce::Rectangle<float> (rKnob * 2.0f - 6.0f, rKnob * 2.0f - 6.0f).withCentre ({ cx, cy });
    if (main) { g.setColour (C::glow().withAlpha (0.55f)); g.fillEllipse (cap.expanded (5.0f)); }
    juce::ColourGradient cg (juce::Colours::white, cap.getX() + cap.getWidth() * 0.35f, cap.getY() + cap.getHeight() * 0.28f,
                             juce::Colour (0xff56625c), cap.getRight(), cap.getBottom(), true);
    cg.addColour (0.22, juce::Colour (0xffe2e8e4)); cg.addColour (0.58, juce::Colour (0xff9ca9a2));
    g.setGradientFill (cg); g.fillEllipse (cap);
    g.setColour (juce::Colour (0x66000000)); g.drawEllipse (cap, 1.0f);
    const float pr0 = cap.getWidth() * 0.18f, pr1 = cap.getWidth() * 0.46f;
    g.setColour (juce::Colour (0xff1b2420));
    g.drawLine (cx + pr0 * std::sin (ang), cy - pr0 * std::cos (ang), cx + pr1 * std::sin (ang), cy - pr1 * std::cos (ang), 2.6f);
    // value
    const juce::String txt = toText ? toText (value) : juce::String (value, 2);
    g.setFont (font (10.5f));
    const int tw = std::min (valueArea.getWidth(), (int) textW (g.getCurrentFont(), txt) + 12);
    auto vr = valueArea.withSizeKeepingCentre (tw, 14).toFloat();
    paintLcd (g, vr, 4.0f);
    g.setColour (C::lcdText());
    g.drawText (txt, vr.toNearestInt(), juce::Justification::centred, true);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && dest.isNotEmpty())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Modulating " + destLabel (dest));
        auto& routes = ctx.P().mod.routes;
        int n = 0;
        for (size_t i = 0; i < routes.size(); ++i)
            if (juce::String (routes[i].dest) == dest)
            {
                m.addItem (1000 + (int) i, juce::String ("Remove ") + nb::modSrcName (routes[i].src) + "  (" + juce::String (juce::roundToInt (routes[i].depth * 100)) + " %)");
                ++n;
            }
        if (n == 0) m.addItem (-1, "Nothing yet: drag a source from the dock onto this knob", false, false);
        m.addSeparator();
        juce::PopupMenu add;
        for (int s = 0; s < (int) nb::ModSrc::Count; ++s) add.addItem (2000 + s, nb::modSrcName ((nb::ModSrc) s));
        m.addSubMenu ("Add a source", add);
        // the knob may be rebuilt away while the menu is open: only the copies below are used
        m.showMenuAsync (juce::PopupMenu::Options(), [&c = ctx, d = dest] (int r)
        {
            if (r <= 0) return;   // dismissed (also what an editor closing with the menu open sends)
            auto& rs = c.P().mod.routes;
            if (r >= 1000 && r < 2000 && r - 1000 < (int) rs.size()) { rs.erase (rs.begin() + (r - 1000)); c.changed(); }
            else if (r >= 2000) addRoute (c, (nb::ModSrc) (r - 2000), d);
        });
        return;
    }
    dragStartNorm = toNorm (value);
    if (param != nullptr && ! gesture) { param->beginChangeGesture(); gesture = true; }
}
void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const float sens = e.mods.isShiftDown() ? 0.0015f : 0.0055f;
    const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm - (float) e.getDistanceFromDragStartY() * sens + (float) e.getDistanceFromDragStartX() * sens * 0.5f);
    setValue (fromNorm (n), true);
}
void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (param != nullptr && gesture) { param->endChangeGesture(); gesture = false; }
    if (e.mouseWasDraggedSinceMouseDown() && onDragEnd) onDragEnd();
}
void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr && ! gesture) param->beginChangeGesture();
    setValue (defV, true);
    if (param != nullptr && ! gesture) param->endChangeGesture();
    if (onDragEnd) onDragEnd();
}
void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param != nullptr && ! gesture) param->beginChangeGesture();
    setValue (fromNorm (toNorm (value) + w.deltaY * 0.06f), true);
    if (param != nullptr && ! gesture) param->endChangeGesture();
    if (onDragEnd) onDragEnd();
}
bool Knob::isInterestedInDragSource (const SourceDetails& d) { return dest.isNotEmpty() && d.description.toString().startsWith ("mod:"); }
void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int s = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    addRoute (ctx, (nb::ModSrc) juce::jlimit (0, (int) nb::ModSrc::Count - 1, s), dest);
}

void LcdBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintLcd (g, r);
    auto t = getLocalBounds().reduced (10, 4);
    if (line2.isEmpty()) { g.setColour (C::lcdText()); g.setFont (font (std::min (15.0f, r.getHeight() * 0.5f), true)); g.drawText (line1, t, juce::Justification::centredLeft, true); return; }
    g.setColour (C::lcdText()); g.setFont (font (std::min (15.0f, r.getHeight() * 0.36f), true));
    g.drawText (line1, t.removeFromTop (t.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour (C::lcdDim()); g.setFont (font (std::min (11.5f, r.getHeight() * 0.27f)));
    g.drawText (line2, t, juce::Justification::topLeft, true);
}

// ------------------------------------------------------------------ shared helpers
void addRoute (Ctx& ctx, nb::ModSrc src, const juce::String& dest, float depth)
{
    auto& rs = ctx.P().mod.routes;
    for (auto& r : rs) if (r.src == src && juce::String (r.dest) == dest) { r.depth = depth; ctx.changed(); return; }
    if (rs.size() >= 128) return;
    rs.push_back ({ src, dest.toStdString(), depth });
    ctx.changed();
}

juce::String destLabel (const juce::String& dest)
{
    auto p = juce::StringArray::fromTokens (dest, "/", "");
    if (p.isEmpty()) return dest;
    if (p[0] == "pad" && p.size() == 3) return (p[1] == "*" ? juce::String ("All lanes") : "Lane " + juce::String (p[1].getIntValue() + 1)) + " " + p[2];
    if (p[0] == "cell" && p.size() == 3) return "Node " + juce::String (p[1].getIntValue() + 1) + " main";
    if (p[0] == "fs" && p.size() == 3) return "Friction " + juce::String (p[1].getIntValue() + 1) + " " + p[2];
    if (p[0] == "bd" && p.size() == 3) return "Bounce " + juce::String (p[1].getIntValue() + 1) + " " + p[2];
    if (p[0] == "seq" && p.size() == 2) return "Sequencer view " + p[1];
    if (p[0] == "grid" && p.size() == 2) return "Layermax " + p[1];
    if (p[0] == "master") return "Master";
    return dest;
}

juce::String sourceLabel (const nb::Project& P, const nb::SlotSource& s)
{
    switch (s.kind)
    {
        case nb::SourceKind::Pad: return "Lane " + juce::String (s.index + 1);
        case nb::SourceKind::GridRow: return "Layermax row " + juce::String (s.index + 1);
        case nb::SourceKind::Cell:
        {
            if (s.index >= 0 && s.index < (int) P.matrix.oneShot.cells.size())
            {
                const auto& c = P.matrix.oneShot.cells[(size_t) s.index];
                return "Node " + juce::String (s.index + 1) + " (scene " + juce::String::charToString ((juce::juce_wchar) ('A' + c.scene)) + ", lane " + juce::String (c.pad + 1) + ", step " + juce::String (c.step + 1) + ")";
            }
            return "Node " + juce::String (s.index + 1);
        }
        default: return "Nothing";
    }
}

juce::PopupMenu sourceMenu (const nb::Project& P, int base)
{
    juce::PopupMenu m, pads, rows, cells;
    m.addItem (base, "Nothing");
    for (int p = 0; p < nb::kNumPads; ++p) pads.addItem (base + 1000 + p, "Lane " + juce::String (p + 1));
    for (int r = 0; r < 64; ++r) rows.addItem (base + 2000 + r, "Layermax row " + juce::String (r + 1));
    for (int c = 0; c < (int) P.matrix.oneShot.cells.size(); ++c) cells.addItem (base + 3000 + c, sourceLabel (P, { nb::SourceKind::Cell, c }));
    m.addSubMenu ("A lane (every hit)", pads);
    m.addSubMenu ("A Layermax row (tagged boxes)", rows);
    m.addSubMenu ("A node", cells, ! P.matrix.oneShot.cells.empty());
    return m;
}

nb::SlotSource sourceFromMenu (int r, int base)
{
    r -= base;
    if (r >= 3000) return { nb::SourceKind::Cell, r - 3000 };
    if (r >= 2000) return { nb::SourceKind::GridRow, r - 2000 };
    if (r >= 1000) return { nb::SourceKind::Pad, r - 1000 };
    return {};
}

void Ctx::openWindow (bool fs, int slot, int cell)
{
    winCell = cell;
    if (fs) { fsSlot = juce::jlimit (0, nb::kNumSlots - 1, slot); fsOpen = true; bdOpen = false; }
    else { bdSlot = juce::jlimit (0, nb::kNumSlots - 1, slot); bdOpen = true; fsOpen = false; }
    if (refreshAll) refreshAll();
}
} // namespace ui
