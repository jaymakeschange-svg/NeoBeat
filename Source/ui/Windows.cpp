// NeoBeat 2.0 — the effect windows. Friction Stretch: a sonogram of the stretched sound with its
// numbered lines (Fusion-style). Bounce Delay: a room seen from behind the listener, with a sphere
// for every bounce (SpaceVR-style). Eight slots each; a slot listens to a pad, a grid row or a cell.
#include "UI.h"
#include <juce_dsp/juce_dsp.h>

namespace ui
{
static const std::vector<std::pair<const char*, double>>& divisions()
{
    static const std::vector<std::pair<const char*, double>> d { { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },
                                                                 { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/64", 0.0625 } };
    return d;
}
static int divisionIndex (double beats)
{
    int best = 0; double err = 1e9;
    for (int i = 0; i < (int) divisions().size(); ++i) if (std::fabs (divisions()[(size_t) i].second - beats) < err) { err = std::fabs (divisions()[(size_t) i].second - beats); best = i; }
    return best;
}

// The frame both windows share: title, slot tabs, source, level, replace, play, close.
class WindowBase : public juce::Component, public Part
{
public:
    WindowBase (Ctx& c, bool isFs) : ctx (c), fs (isFs), slots ({ "1", "2", "3", "4", "5", "6", "7", "8" }), level (c, "LEVEL", 0.0f, 1.5f, 1.0f)
    {
        addAndMakeVisible (slots);
        slots.onSelect = [this] (int i) { (fs ? ctx.fsSlot : ctx.bdSlot) = i; ctx.winCell = -1; if (ctx.refreshAll) ctx.refreshAll(); };
        for (auto* b : { &onOff, &source, &replace, &play, &close, &tempo }) addAndMakeVisible (b);
        onOff.setClickingTogglesState (true); replace.setClickingTogglesState (true); tempo.setClickingTogglesState (true);
        onOff.onClick = [this] { if (ctx.winCell < 0) { (fs ? ctx.P().fs[(size_t) slot()].on : ctx.P().bd[(size_t) slot()].on) = onOff.getToggleState(); ctx.changed(); } };
        replace.onClick = [this] { if (ctx.winCell < 0) { (fs ? ctx.P().fs[(size_t) slot()].replace : ctx.P().bd[(size_t) slot()].replace) = replace.getToggleState(); ctx.changed (false); } };
        source.onClick = [this]
        {
            sourceMenu (ctx.P(), 100).showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
            {
                if (r < 100) return;
                auto src = sourceFromMenu (r, 100);
                if (fs) { auto& s = ctx.P().fs[(size_t) slot()]; s.src = src; s.on = src.kind != nb::SourceKind::None; }
                else { auto& s = ctx.P().bd[(size_t) slot()]; s.src = src; s.on = src.kind != nb::SourceKind::None; }
                ctx.changed();
            });
        };
        play.onClick = [this] { audition(); };
        close.onClick = [this] { (fs ? ctx.fsOpen : ctx.bdOpen) = false; ctx.winCell = -1; if (ctx.refreshAll) ctx.refreshAll(); };
        addAndMakeVisible (level);
        level.onChange = [this] (float v) { if (ctx.winCell < 0) { (fs ? ctx.P().fs[(size_t) slot()].level : ctx.P().bd[(size_t) slot()].level) = v; ctx.changed (false); } };
        addAndMakeVisible (pages);
        addAndMakeVisible (bank);
    }

    int slot() const { return fs ? ctx.fsSlot : ctx.bdSlot; }
    bool cellMode() const
    {
        if (ctx.winCell < 0 || ctx.winCell >= (int) ctx.P().matrix.oneShot.cells.size()) return false;
        const auto k = ctx.P().matrix.oneShot.cells[(size_t) ctx.winCell].fx.kind;
        return fs ? k == nb::CellFx::FrictionStretchFx : k == nb::CellFx::BounceDelayFx;
    }
    nb::SlotSource currentSource() const
    {
        if (cellMode()) return { nb::SourceKind::Cell, ctx.winCell };
        return fs ? ctx.P().fs[(size_t) slot()].src : ctx.P().bd[(size_t) slot()].src;
    }
    void audition()
    {
        const auto s = currentSource();
        if (s.kind == nb::SourceKind::Pad) ctx.proc.audition (s.index, 0.9f);
        if (s.kind == nb::SourceKind::Cell && s.index < (int) ctx.P().matrix.oneShot.cells.size()) ctx.proc.audition (ctx.P().matrix.oneShot.cells[(size_t) s.index].pad, 0.9f);
    }
    juce::String destFor (const juce::String& param) const { return cellMode() ? juce::String() : juce::String (fs ? "fs/" : "bd/") + juce::String (slot()) + "/" + param; }

    void paintFrame (juce::Graphics& g, const juce::String& title)
    {
        g.fillAll (juce::Colour (0x00000000));
        auto r = getLocalBounds().toFloat();
        juce::DropShadow (juce::Colour (0x99000000), 30, { 0, 10 }).drawForRectangle (g, getLocalBounds());
        paintPlate (g, r, 20.0f);
        paintLabel (g, title, { 22, 16, 260, 18 }, juce::Justification::centredLeft, 11.0f);
        juce::String sub;
        if (cellMode())
        {
            const auto& c = ctx.P().matrix.oneShot.cells[(size_t) ctx.winCell];
            sub = "EDITING NODE " + juce::String (ctx.winCell + 1) + " | LANE " + juce::String (c.pad + 1) + " | STEP " + juce::String (c.step + 1);
        }
        else sub = "SLOT " + juce::String (slot() + 1) + " | LISTENS TO " + sourceLabel (ctx.P(), currentSource()).toUpperCase();
        paintLabel (g, sub, { 22, 36, 600, 14 }, juce::Justification::centredLeft, 7.5f, C::dim());
    }

    void layoutFrame()
    {
        slots.setBounds (300, 14, 300, 32);
        slots.setVisible (! cellMode());
        auto right = juce::Rectangle<int> (610, 14, getWidth() - 626, 32);
        close.setBounds (right.removeFromRight (80).reduced (2, 0));
        play.setBounds (right.removeFromRight (70).reduced (2, 0));
        replace.setBounds (right.removeFromRight (96).reduced (2, 0));
        source.setBounds (right.removeFromRight (190).reduced (2, 0));
        onOff.setBounds (right.removeFromRight (64).reduced (2, 0));
        for (auto* c : { (juce::Component*) &onOff, (juce::Component*) &source, (juce::Component*) &replace }) c->setVisible (! cellMode());
        level.setBounds (getWidth() - 110, 60, 86, 96);
        level.setVisible (! cellMode());
    }

    void refreshFrame()
    {
        if (! cellMode()) ctx.winCell = -1;
        slots.setSelected (slot());
        const bool on = fs ? ctx.P().fs[(size_t) slot()].on : ctx.P().bd[(size_t) slot()].on;
        onOff.setToggleState (on, juce::dontSendNotification); onOff.setButtonText (on ? "ON" : "OFF");
        const bool rep = fs ? ctx.P().fs[(size_t) slot()].replace : ctx.P().bd[(size_t) slot()].replace;
        replace.setToggleState (rep, juce::dontSendNotification); replace.setButtonText (rep ? "REPLACES" : "ADDS");
        replace.setTooltip ("REPLACES: the effect takes the hit's place (an accent). ADDS: the hit stays and the effect is added.");
        source.setButtonText ("SOURCE: " + sourceLabel (ctx.P(), currentSource()));
        level.setValue (fs ? ctx.P().fs[(size_t) slot()].level : ctx.P().bd[(size_t) slot()].level);
    }

    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks so the shell under it stays put

protected:
    // a page of knobs, rebuilt when the page or the target changes
    struct Bank : public juce::Component
    {
        juce::OwnedArray<juce::Component> items;
        void clear() { items.clear(); }
        template <class T> T* add (T* c) { items.add (c); addAndMakeVisible (c); return c; }
        void resized() override
        {
            int x = 0, y = 0;
            for (auto* c : items)
            {
                const int w = dynamic_cast<Knob*> (c) != nullptr ? 92 : c->getWidth() > 0 ? c->getWidth() : 120;
                if (x + w > getWidth()) { x = 0; y += 102; }
                if (dynamic_cast<Knob*> (c) != nullptr) c->setBounds (x, y, 88, 96);
                else c->setBounds (x, y + 30, w - 6, 32);
                x += w;
            }
        }
    };

    Knob* addKnob (const juce::String& label, float lo, float hi, float v, bool log, std::function<void (float)> set, const juce::String& dest = {}, std::function<juce::String (float)> fmt = {})
    {
        auto* k = bank.add (new Knob (ctx, label, lo, hi, v, log));
        k->setValue (v); k->onChange = std::move (set); k->setDest (dest);
        if (fmt) k->toText = std::move (fmt);
        return k;
    }

    Ctx& ctx;
    bool fs;
    Segmented slots;
    Pill onOff { "ON" }, source { "SOURCE" }, replace { "REPLACE" }, play { "PLAY" }, close { "CLOSE" }, tempo { "TEMPO" };
    Knob level;
    Segmented pages { juce::StringArray { "-" } };
    Bank bank;
    int page = 0;
};

// ================================================================== Friction Stretch
class FrictionWindow final : public WindowBase
{
public:
    explicit FrictionWindow (Ctx& c) : WindowBase (c, true)
    {
        removeChildComponent (&pages);
        pagesFs = std::make_unique<Segmented> (juce::StringArray { "MOTION", "PEEL", "BODY", "SHAPE", "LINES" });
        addAndMakeVisible (*pagesFs);
        pagesFs->onSelect = [this] (int i) { page = i; rebuild(); };
        addLine.onClick = [this] { if (auto* p = params()) if (p->lines.size() < 6) { nb::StretchLine l; l.fLo = 200.0f * (float) (1 + p->lines.size()); l.fHi = l.fLo * 3.0f; l.offset = 0.02f * (float) p->lines.size(); p->lines.push_back (l); line = (int) p->lines.size() - 1; page = 4; ctx.changed (false); refresh(); } };
        delLine.onClick = [this] { if (auto* p = params()) if (line >= 0 && line < (int) p->lines.size()) { p->lines.erase (p->lines.begin() + line); line = -1; ctx.changed (false); refresh(); } };
        addAndMakeVisible (addLine); addAndMakeVisible (delLine);
    }

    nb::StretchParams* params()
    {
        if (cellMode()) return &ctx.P().matrix.oneShot.cells[(size_t) ctx.winCell].fx.stretch;
        return &ctx.P().fs[(size_t) slot()].p;
    }

    void refresh() override
    {
        if (! isVisible()) return;
        refreshFrame();
        pagesFs->setSelected (page);
        rebuild();
        layoutFrame();
        resized();
        repaint();
    }

    void rebuild()
    {
        bank.clear();
        auto* p = params();
        if (p == nullptr) return;
        auto ch = [this] { ctx.changed (false); };
        auto P = [this] { return params(); };
        auto ms = [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; };
        if (page == 0)
        {
            tempo.setToggleState (p->rate.tempo, juce::dontSendNotification);
            tempo.setButtonText (p->rate.tempo ? "TEMPO" : "MS");
            tempo.onClick = [this, P, ch] { if (auto* q = P()) { q->rate.tempo = ! q->rate.tempo; ch(); rebuild(); } };
            if (p->rate.tempo)
                addKnob ("JUMP RATE", 0, (float) divisions().size() - 1, (float) divisionIndex (p->rate.beats), false,
                         [P, ch] (float v) { if (auto* q = P()) { q->rate.beats = divisions()[(size_t) juce::roundToInt (v)].second; ch(); } }, {}, [] (float v) { return juce::String (divisions()[(size_t) juce::roundToInt (v)].first); });
            else addKnob ("JUMP RATE", 10, 1000, (float) p->rate.ms, true, [P, ch] (float v) { if (auto* q = P()) { q->rate.ms = v; ch(); } }, {}, ms);
            addKnob ("SWING", 0.5f, 0.75f, p->rate.swing, false, [P, ch] (float v) { if (auto* q = P()) { q->rate.swing = v; ch(); } }, {}, pct);
            addKnob ("STRETCH", 1, 64, p->stretch, true, [P, ch] (float v) { if (auto* q = P()) { q->stretch = v; ch(); } }, destFor ("stretch"), [] (float v) { return "x" + juce::String (v, 1); })->setMain (true);
            addKnob ("CREEP", 0, 1, p->creep, false, [P, ch] (float v) { if (auto* q = P()) { q->creep = v; ch(); } }, destFor ("creep"), pct);
            addKnob ("YANK", 1, 200, p->slipMs, true, [P, ch] (float v) { if (auto* q = P()) { q->slipMs = v; ch(); } }, {}, ms);
            addKnob ("GRIT", 0, 1, p->grit, false, [P, ch] (float v) { if (auto* q = P()) { q->grit = v; ch(); } }, {}, pct);
            addKnob ("HOLD FADE", 0, 1, p->holdFade, false, [P, ch] (float v) { if (auto* q = P()) { q->holdFade = v; ch(); } }, {}, pct);
            addKnob ("SNAP", 0, 1, p->snap, false, [P, ch] (float v) { if (auto* q = P()) { q->snap = v; ch(); } }, {}, pct);
            addKnob ("TAPE", 0, 1, p->tape, false, [P, ch] (float v) { if (auto* q = P()) { q->tape = v; ch(); } }, {}, pct);
            addKnob ("ATTACK", 0, 200, p->attackMs, false, [P, ch] (float v) { if (auto* q = P()) { q->attackMs = v; ch(); } }, {}, ms);
        }
        else if (page == 1)
        {
            addKnob ("PEEL", 0, 1, p->peel, false, [P, ch] (float v) { if (auto* q = P()) { q->peel = v; ch(); } }, destFor ("peel"), pct);
            addKnob ("FREEZE", 0, 1, p->freeze, false, [P, ch] (float v) { if (auto* q = P()) { q->freeze = v; ch(); } }, destFor ("freeze"), pct);
            addKnob ("SCREECH", 100, 4000, p->screechHz, true, [P, ch] (float v) { if (auto* q = P()) { q->screechHz = v; ch(); } }, destFor ("screech"), [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; });
            addKnob ("ADHESIVE", 0, 1, p->adhesive, false, [P, ch] (float v) { if (auto* q = P()) { q->adhesive = v; ch(); } }, destFor ("adhesive"), pct);
            addKnob ("GRAIN", 1, 60, p->grainMs, true, [P, ch] (float v) { if (auto* q = P()) { q->grainMs = v; ch(); } }, {}, ms);
            addKnob ("RASP", 0, 1, p->rasp, false, [P, ch] (float v) { if (auto* q = P()) { q->rasp = v; ch(); } }, destFor ("rasp"), pct);
            addKnob ("WIDTH", 0, 1, p->width, false, [P, ch] (float v) { if (auto* q = P()) { q->width = v; ch(); } }, destFor ("width"), pct);
        }
        else if (page == 2)
        {
            addKnob ("ROLL", 0, 1, p->roll, false, [P, ch] (float v) { if (auto* q = P()) { q->roll = v; ch(); } }, destFor ("roll"), pct);
            static const char* bodies[] = { "Basketball", "Bucket", "Can", "Clank", "Pan", "PVC pipe", "Tube clank" };
            addKnob ("ROLL BODY", 0, 6, (float) p->rollBody, false, [P, ch] (float v) { if (auto* q = P()) { q->rollBody = juce::roundToInt (v); ch(); } }, {}, [] (float v) { return juce::String (bodies[juce::jlimit (0, 6, juce::roundToInt (v))]); });
            addKnob ("ROLL SIZE", 0.5f, 3.0f, p->rollSize, true, [P, ch] (float v) { if (auto* q = P()) { q->rollSize = v; ch(); } }, {}, [] (float v) { return "x" + juce::String (v, 2); });
            addKnob ("STRIP", 0, 1, p->strip, false, [P, ch] (float v) { if (auto* q = P()) { q->strip = v; ch(); } }, destFor ("strip"), pct);
        }
        else if (page == 3)
        {
            if (p->rate.tempo) addKnob ("GATE", 0, 32, (float) p->gateBeats, false, [P, ch] (float v) { if (auto* q = P()) { q->gateBeats = v; ch(); } }, {}, [] (float v) { return v < 0.01f ? juce::String ("all") : juce::String (v, 2) + " beats"; });
            else addKnob ("GATE", 0, 16000, (float) p->gateMs, false, [P, ch] (float v) { if (auto* q = P()) { q->gateMs = v; ch(); } }, {}, [] (float v) { return v < 1.0f ? juce::String ("all") : juce::String (juce::roundToInt (v)) + " ms"; });
            addKnob ("DECAY", 20, 4000, p->decayMs, true, [P, ch] (float v) { if (auto* q = P()) { q->decayMs = v; ch(); } }, destFor ("decay"), ms);
            addKnob ("WET", 0, 1, p->wet, false, [P, ch] (float v) { if (auto* q = P()) { q->wet = v; ch(); } }, destFor ("wet"), pct);
        }
        else
        {
            if (line >= (int) p->lines.size()) line = (int) p->lines.size() - 1;
            if (line >= 0)
            {
                const int li = line;
                auto L = [P, li] () -> nb::StretchLine* { auto* q = P(); return q != nullptr && li < (int) q->lines.size() ? &q->lines[(size_t) li] : nullptr; };
                addKnob ("OFFSET", -0.5f, 0.5f, p->lines[(size_t) li].offset, false, [L, ch] (float v) { if (auto* l = L()) { l->offset = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v * 1000)) + " ms"; });
                addKnob ("TILT", -0.5f, 0.5f, p->lines[(size_t) li].tilt, false, [L, ch] (float v) { if (auto* l = L()) { l->tilt = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v * 1000)) + " ms"; });
                addKnob ("SPRAY", 0, 1, p->lines[(size_t) li].spray, false, [L, ch] (float v) { if (auto* l = L()) { l->spray = v; ch(); } });
                addKnob ("EDGE", 0, 2, p->lines[(size_t) li].edge, false, [L, ch] (float v) { if (auto* l = L()) { l->edge = v; ch(); } }, {}, [] (float v) { return juce::String (v, 2) + " oct"; });
                addKnob ("LEVEL", 0, 2, p->lines[(size_t) li].level, false, [L, ch] (float v) { if (auto* l = L()) { l->level = v; ch(); } });
                addKnob ("PAN", -1, 1, p->lines[(size_t) li].pan, false, [L, ch] (float v) { if (auto* l = L()) { l->pan = v; ch(); } });
            }
        }
        bank.resized();
    }

    void resized() override
    {
        layoutFrame();
        display = { 22, 60, getWidth() - 160, 410 };
        pagesFs->setBounds (22, 484, 480, 32);
        tempo.setBounds (512, 484, 90, 32);
        tempo.setVisible (page == 0);
        addLine.setBounds (612, 484, 110, 32); delLine.setBounds (728, 484, 110, 32);
        delLine.setVisible (page == 4 && line >= 0);
        bank.setBounds (22, 526, getWidth() - 44, getHeight() - 540);
        bank.resized();
    }

    void paint (juce::Graphics& g) override
    {
        paintFrame (g, "FRICTION STRETCH");
        paintLcd (g, display.toFloat(), 10.0f);
        auto d = ctx.proc.renderer().display();
        nb::RenderedPtr r;
        if (d)
        {
            if (cellMode()) { if (ctx.winCell < (int) d->cells.size()) r = d->cells[(size_t) ctx.winCell]; }
            else r = d->fs[(size_t) slot()];
        }
        drawSonogram (g, r);
        auto* p = params();
        if (p != nullptr)
        {
            // the lines: numbered bands on the sonogram
            lineRects.clear();
            for (int i = 0; i < (int) p->lines.size(); ++i)
            {
                const auto& l = p->lines[(size_t) i];
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
            g.setColour (C::lcdDim()); g.setFont (font (11.0f));
            g.drawText (p->lines.empty() ? "ONE LINE OVER EVERYTHING | ADD LINES TO STRETCH BANDS ON THEIR OWN (DRAG A LINE UP OR DOWN)" : "DRAG A LINE TO MOVE ITS BAND | CLICK IT TO EDIT | LINES PAGE",
                        display.reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
            // the side info
            auto info = juce::Rectangle<int> (display.getRight() + 14, 170, getWidth() - display.getRight() - 30, 300);
            g.setColour (C::text()); g.setFont (font (12.0f));
            g.drawFittedText ("The hit sticks and slips like duct tape pulled off a roll: the jumps land on the swung grid, the peel streams grains of the hit, the roll rings with each yank.\n\nSTRETCH is the main control (1x to 64x).",
                              info, juce::Justification::topLeft, 14);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < lineRects.size(); ++i)
            if (lineRects[i].contains (e.position)) { line = i; page = 4; grabY = e.position.y; if (auto* p = params()) grabLine = p->lines[(size_t) i]; refresh(); return; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* p = params();
        if (p == nullptr || line < 0 || line >= (int) p->lines.size()) return;
        auto& l = p->lines[(size_t) line];
        const float ratio = fOf (yOf (grabLine.fLo) + (e.position.y - grabY)) / std::max (20.0f, grabLine.fLo);
        l.fLo = juce::jlimit (20.0f, 18000.0f, grabLine.fLo * ratio); l.fHi = juce::jlimit (30.0f, 20000.0f, grabLine.fHi * ratio);
        ctx.changed (false); repaint();
    }

    void tick() override
    {
        if (! isVisible()) return;
        auto d = ctx.proc.renderer().display();
        const void* now = d ? (cellMode() ? (ctx.winCell < (int) d->cells.size() ? (const void*) d->cells[(size_t) ctx.winCell].get() : nullptr) : (const void*) d->fs[(size_t) slot()].get()) : nullptr;
        if (now != lastShown) { lastShown = now; repaint(); }
    }

private:
    float yOf (float f) const { return display.getBottom() - 6 - std::log (juce::jlimit (30.0f, 16000.0f, f) / 30.0f) / std::log (16000.0f / 30.0f) * (display.getHeight() - 12); }
    float fOf (float y) const { return 30.0f * std::pow (16000.0f / 30.0f, juce::jlimit (0.0f, 1.0f, (display.getBottom() - 6 - y) / (float) (display.getHeight() - 12))); }

    void drawSonogram (juce::Graphics& g, const nb::RenderedPtr& r)
    {
        if (! r || r->size() < 2048)
        {
            g.setColour (C::lcdDim()); g.setFont (font (15.0f));
            g.drawText (currentSource().kind == nb::SourceKind::None && ! cellMode() ? "PICK A SOURCE FOR THIS SLOT (A LANE, A LAYERMAX ROW OR A NODE)" : "PREPARING...", display, juce::Justification::centred);
            return;
        }
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
                for (int i = 0; i < N && start + i < len; ++i) buf[(size_t) i] = 0.5f * (r->l[(size_t) (start + i)] + r->r[(size_t) (start + i)]) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
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
        g.setColour (C::lcdDim()); g.setFont (font (11.0f));
        g.drawText (juce::String (r->size() / r->sampleRate, 1) + " s", display.reduced (12, 6).removeFromTop (16), juce::Justification::centredRight);
    }

    std::unique_ptr<Segmented> pagesFs;
    Pill addLine { "+ LINE" }, delLine { "DELETE LINE" };
    juce::Rectangle<int> display;
    juce::Image image;
    const void* imageSrc = nullptr;
    const void* lastShown = nullptr;
    juce::Array<juce::Rectangle<float>> lineRects;
    int line = -1;
    float grabY = 0.0f;
    nb::StretchLine grabLine;
};

// ================================================================== Bounce Delay
class BounceWindow final : public WindowBase
{
public:
    explicit BounceWindow (Ctx& c) : WindowBase (c, false)
    {
        removeChildComponent (&pages);
        pagesBd = std::make_unique<Segmented> (juce::StringArray { "MOTION", "SPACE" });
        addAndMakeVisible (*pagesBd);
        pagesBd->onSelect = [this] (int i) { page = i; rebuild(); };
    }

    nb::BounceParams* params()
    {
        if (cellMode()) return &ctx.P().matrix.oneShot.cells[(size_t) ctx.winCell].fx.bounce;
        return &ctx.P().bd[(size_t) slot()].p;
    }

    void refresh() override
    {
        if (! isVisible()) return;
        refreshFrame();
        pagesBd->setSelected (page);
        rebuild();
        layoutFrame();
        resized();
        repaint();
    }

    void rebuild()
    {
        bank.clear();
        auto* p = params();
        if (p == nullptr) return;
        auto ch = [this] { ctx.changed (false); repaint(); };
        auto P = [this] { return params(); };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; };
        auto m = [] (float v) { return juce::String (v, 1) + " m"; };
        if (page == 0)
        {
            tempo.setToggleState (p->rate.tempo, juce::dontSendNotification);
            tempo.setButtonText (p->rate.tempo ? "TEMPO" : "MS");
            tempo.onClick = [this, P, ch] { if (auto* q = P()) { q->rate.tempo = ! q->rate.tempo; ch(); rebuild(); } };
            if (p->rate.tempo)
            {
                addKnob ("FIRST GAP", 0, (float) divisions().size() - 1, (float) divisionIndex (p->rate.beats), false,
                         [P, ch] (float v) { if (auto* q = P()) { q->rate.beats = divisions()[(size_t) juce::roundToInt (v)].second; ch(); } }, {}, [] (float v) { return juce::String (divisions()[(size_t) juce::roundToInt (v)].first); });
                addKnob ("DIVISION", 0, (float) divisions().size() - 1, (float) divisionIndex (p->gridBeats), false,
                         [P, ch] (float v) { if (auto* q = P()) { q->gridBeats = divisions()[(size_t) juce::roundToInt (v)].second; ch(); } }, {}, [] (float v) { return juce::String (divisions()[(size_t) juce::roundToInt (v)].first); });
            }
            else
            {
                addKnob ("FIRST GAP", 20, 2000, (float) p->rate.ms, true, [P, ch] (float v) { if (auto* q = P()) { q->rate.ms = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
                addKnob ("DIVISION", 5, 500, (float) p->gridMs, true, [P, ch] (float v) { if (auto* q = P()) { q->gridMs = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
            }
            addKnob ("SWING", 0.5f, 0.75f, p->rate.swing, false, [P, ch] (float v) { if (auto* q = P()) { q->rate.swing = v; ch(); } }, {}, pct);
            addKnob ("BOUNCINESS", 0.3f, 0.95f, p->bounciness, false, [P, ch] (float v) { if (auto* q = P()) { q->bounciness = v; ch(); } }, destFor ("bounciness"), pct)->setMain (true);
            addKnob ("BOUNCES", 1, 64, (float) p->maxBounces, false, [P, ch] (float v) { if (auto* q = P()) { q->maxBounces = juce::roundToInt (v); ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)); });
            addKnob ("SPEED", 0, 1, p->speed, false, [P, ch] (float v) { if (auto* q = P()) { q->speed = v; ch(); } }, destFor ("speed"), pct);
            addKnob ("TAIL", 0.05f, 4, p->tail, true, [P, ch] (float v) { if (auto* q = P()) { q->tail = v; ch(); } }, {}, [] (float v) { return juce::String (v, 2) + " s"; });
        }
        else
        {
            addKnob ("NEAR", 0.3f, 30, p->distStart, true, [P, ch] (float v) { if (auto* q = P()) { q->distStart = v; ch(); } }, destFor ("distance start"), m);
            addKnob ("FAR", 0.3f, 40, p->distEnd, true, [P, ch] (float v) { if (auto* q = P()) { q->distEnd = v; ch(); } }, destFor ("distance end"), m);
            addKnob ("PAN RANDOM", 0, 1, p->panRandom, false, [P, ch] (float v) { if (auto* q = P()) { q->panRandom = v; ch(); } }, destFor ("pan random"), pct);
            addKnob ("PAN CENTRE", -1, 1, p->panCenter, false, [P, ch] (float v) { if (auto* q = P()) { q->panCenter = v; ch(); } });
            addKnob ("WIDTH", 0, 1, p->width, false, [P, ch] (float v) { if (auto* q = P()) { q->width = v; ch(); } }, destFor ("width"), pct);
            addKnob ("ROOM", 0, 1, p->room, false, [P, ch] (float v) { if (auto* q = P()) { q->room = v; ch(); } }, destFor ("room"), pct);
            addKnob ("WET", 0, 1, p->wet, false, [P, ch] (float v) { if (auto* q = P()) { q->wet = v; ch(); } }, destFor ("wet"), pct);
            addKnob ("SEED", 1, 99, (float) p->seed, false, [P, ch] (float v) { if (auto* q = P()) { q->seed = (uint32_t) juce::roundToInt (v); ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)); });
        }
        bank.resized();
    }

    void resized() override
    {
        layoutFrame();
        room = { 22, 60, getWidth() - 160, 410 };
        pagesBd->setBounds (22, 484, 260, 32);
        tempo.setBounds (292, 484, 90, 32);
        tempo.setVisible (page == 0);
        bank.setBounds (22, 526, getWidth() - 44, getHeight() - 540);
        bank.resized();
    }

    // perspective from just behind the listener: x = pan (-1..1), z = distance (m), y = height (m)
    juce::Point<float> project (float x, float y, float z) const
    {
        const float f = 4.0f, s = f / (std::max (0.0f, z) + 0.5f + f);
        const float horizon = room.getY() + room.getHeight() * 0.16f;
        const float k = room.getHeight() * 0.80f / (1.6f * (f / (0.5f + f)));
        return { room.getCentreX() + x * room.getWidth() * 0.55f * s, horizon + (1.6f - y) * k * s };
    }

    void paint (juce::Graphics& g) override
    {
        paintFrame (g, "BOUNCE DELAY");
        paintLcd (g, room.toFloat(), 10.0f);
        auto* p = params();
        if (p == nullptr) return;
        const double bpm = std::max (20.0, ctx.proc.displayBpm.load());
        nb::BounceDelay bd;
        const auto plan = bd.plan (*p, bpm, 0.0, 0.0);
        const float far = std::max (12.0f, std::max (p->distStart, p->distEnd) * 1.2f);
        // the floor grid
        g.setColour (C::lcdText().withAlpha (0.12f));
        for (int i = 0; i <= 10; ++i)
        {
            const float z = far * i / 10.0f;
            g.drawLine (juce::Line<float> (project (-1.2f, 0, z), project (1.2f, 0, z)), 1.0f);
        }
        for (int i = -6; i <= 6; ++i) g.drawLine (juce::Line<float> (project (i / 5.0f, 0, 0), project (i / 5.0f, 0, far)), 1.0f);
        g.setColour (C::lcdText().withAlpha (0.25f)); g.drawLine (juce::Line<float> (project (-1.2f, 3.0f, far), project (1.2f, 3.0f, far)), 1.0f);
        // the listener's head, seen from behind
        const juce::Point<float> head (room.getCentreX(), room.getBottom() - 48.0f);
        g.setColour (juce::Colour (0xff2a4a3c)); g.fillEllipse (head.x - 28, head.y - 34, 56, 64);
        g.setColour (C::lcdText().withAlpha (0.6f)); g.drawEllipse (head.x - 28, head.y - 34, 56, 64, 1.5f);
        g.fillEllipse (head.x - 34, head.y - 8, 8, 16); g.fillEllipse (head.x + 26, head.y - 8, 8, 16);
        // the bounces: where each lands, how far, how loud
        juce::Path arc;
        const double total = plan.empty() ? 1.0 : std::max (0.1, plan.back().t);
        int shown = 0;
        for (size_t i = 0; i < plan.size(); ++i)
        {
            const auto& b = plan[i];
            const auto pt = project (b.pan, 0.0f, b.dist);
            const float sz = 8.0f + 26.0f * std::sqrt (std::max (0.0f, b.gain)) * 6.0f / (b.dist + 6.0f);
            if (i == 0) arc.startNewSubPath (pt); else
            {
                const auto prev = project (plan[i - 1].pan, 0.0f, plan[i - 1].dist);
                const auto top = project (0.5f * (b.pan + plan[i - 1].pan), 1.4f * std::max (0.2f, (float) (b.t - plan[i - 1].t) * 4.0f), 0.5f * (b.dist + plan[i - 1].dist));
                arc.quadraticTo (top, pt);
                juce::ignoreUnused (prev);
            }
            const bool lit = animT >= 0.0 && animT >= b.t && (i + 1 >= plan.size() || animT < plan[i + 1].t);
            g.setColour (juce::Colour (0xff7fe0d0).withAlpha (lit ? 1.0f : 0.35f + 0.6f * std::min (1.0f, b.gain * 1.5f)));
            g.fillEllipse (pt.x - sz * 0.5f, pt.y - sz * 0.5f, sz, sz);
            g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillEllipse (pt.x - sz * 0.2f, pt.y - sz * 0.3f, sz * 0.22f, sz * 0.22f);
            if (sz > 14 && shown < 24) { g.setColour (juce::Colour (0xff0a1611)); g.setFont (font (10.0f, true)); g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (pt.x - 10, pt.y - 7, 20, 14).toNearestInt(), juce::Justification::centred); ++shown; }
        }
        g.setColour (C::lcdText().withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved), juce::AffineTransform());
        g.setColour (C::lcdDim()); g.setFont (font (11.0f));
        g.drawText (juce::String (plan.size()) + " BOUNCES OVER " + juce::String (total, 2) + " S | NEAR " + juce::String (p->distStart, 1) + " M > FAR " + juce::String (p->distEnd, 1) + " M | SPHERE SIZE = LOUDNESS",
                    room.reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
        auto info = juce::Rectangle<int> (room.getRight() + 14, 170, getWidth() - room.getRight() - 30, 300);
        g.setColour (C::text()); g.setFont (font (12.0f));
        g.drawFittedText ("Each repeat is a bounce of the hit: the gaps shrink like a real ball but land on the swung grid. Far bounces are quieter, darker and narrower; travel speed bends the pitch.\n\nBOUNCINESS is the main control.",
                          info, juce::Justification::topLeft, 14);
    }

    void tick() override
    {
        if (! isVisible()) return;
        // follow the source's hits: light each bounce as it lands
        const auto s = currentSource();
        int pad = s.kind == nb::SourceKind::Pad ? s.index : -1;
        if (s.kind == nb::SourceKind::Cell && s.index < (int) ctx.P().matrix.oneShot.cells.size()) pad = ctx.P().matrix.oneShot.cells[(size_t) s.index].pad;
        if (pad >= 0)
        {
            const auto h = ctx.proc.hitCount (pad);
            if (h != lastHit) { lastHit = h; hitAt = juce::Time::getMillisecondCounterHiRes(); }
        }
        const double t = (juce::Time::getMillisecondCounterHiRes() - hitAt) / 1000.0;
        const double newT = t < 8.0 ? t : -1.0;
        if (newT >= 0.0 || animT >= 0.0) { animT = newT; repaint (room); }
    }

private:
    std::unique_ptr<Segmented> pagesBd;
    juce::Rectangle<int> room;
    uint32_t lastHit = 0;
    double hitAt = -1.0e9, animT = -1.0;
};

std::unique_ptr<juce::Component> makeFrictionWindow (Ctx& c) { return std::make_unique<FrictionWindow> (c); }
std::unique_ptr<juce::Component> makeBounceWindow (Ctx& c) { return std::make_unique<BounceWindow> (c); }
} // namespace ui
