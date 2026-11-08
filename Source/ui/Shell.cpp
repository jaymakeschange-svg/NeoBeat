// NeoBeat 2.0 — the shell: header, tree, rooms, inspector, mod dock, and the overlays.
#include "UI.h"

namespace ui
{
// ================================================================== header
class Header final : public juce::Component, public Part
{
public:
    explicit Header (Ctx& c) : ctx (c), rooms ({ "KIT", "MATRIXXXER", "GRID", "KEY MAP" }),
                               master (c, "MASTER", -30.0f, 6.0f, 0.0f)
    {
        addAndMakeVisible (rooms);
        rooms.onSelect = [this] (int i) { ctx.room = i; if (ctx.refreshAll) ctx.refreshAll(); };
        addAndMakeVisible (lcd);
        for (auto* b : { &fsButton, &bdButton }) addAndMakeVisible (b);
        fsButton.onClick = [this] { ctx.openWindow (true, ctx.fsSlot); };
        bdButton.onClick = [this] { ctx.openWindow (false, ctx.bdSlot); };
        addAndMakeVisible (status);
        addAndMakeVisible (master);
        master.attach (ctx.proc.apvts.getParameter ("master"));
        master.toText = [] (float v) { return juce::String (v, 1) + " dB"; };
        master.setDest ("master");
        addAndMakeVisible (play);
        play.setClickingTogglesState (true);
        play.onClick = [this] { ctx.proc.setParam ("run", play.getToggleState() ? 1.0f : 0.0f); };
        play.setTooltip ("Standalone: run the sequencer without a host transport");
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        paintChassis (g, getLocalBounds().toFloat());
        g.setColour (juce::Colour (0xff7f8b85)); g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
        auto brand = juce::Rectangle<float> (16, 10, 150, 52);
        paintPearl (g, brand, 14.0f);
        paintGem (g, { brand.getX() + 14, brand.getCentreY() - 5, 10, 10 });
        g.setFont (caps (17.0f)); g.setColour (C::text());
        g.drawText ("NEOBEAT", brand.toNearestInt().withTrimmedLeft (32).withTrimmedBottom (14), juce::Justification::centredLeft);
        paintLabel (g, "2.0 | NJS WORKSTATION", brand.toNearestInt().withTrimmedLeft (32).withTrimmedTop (30), juce::Justification::centredLeft, 6.5f);
        paintLabel (g, "WINDOWS", { 822, 4, 120, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        rooms.setBounds (178, 18, 360, 36);
        lcd.setBounds (548, 12, 230, 48);
        fsButton.setBounds (822, 18, 124, 36);
        bdButton.setBounds (952, 18, 124, 36);
        status.setBounds (1084, 18, 190, 36);
        master.setBounds (1280, 4, 70, 66);
        play.setBounds (1366, 16, 40, 40);
    }

    void refresh() override
    {
        rooms.setSelected (ctx.room);
        int fsN = 0, bdN = 0;
        for (const auto& s : ctx.P().fs) fsN += s.on ? 1 : 0;
        for (const auto& s : ctx.P().bd) bdN += s.on ? 1 : 0;
        fsButton.setButtonText ("FRICTION " + juce::String (fsN) + "/8");
        bdButton.setButtonText ("BOUNCE " + juce::String (bdN) + "/8");
        fsButton.setToggleState (ctx.fsOpen, juce::dontSendNotification);
        bdButton.setToggleState (ctx.bdOpen, juce::dontSendNotification);
    }

    void tick() override
    {
        const int idx = ctx.proc.currentPreset.load();
        const auto mode = (int) ctx.proc.apvts.getRawParameterValue ("mode")->load();
        const juce::String m = mode == 0 ? (ctx.proc.hostSynced.load() ? "HOST PLAYING" : "HOST") : mode == 1 ? "KEY" : "PADS";
        lcd.set (juce::String (idx + 1).paddedLeft ('0', 3) + "  " + juce::String (nb::patternPreset (idx).name).toUpperCase(),
                 juce::String (ctx.proc.displayBpm.load(), 1) + " BPM | SWING " + juce::String (juce::roundToInt (ctx.proc.apvts.getRawParameterValue ("swing")->load() * 100)) + "% | " + m);
        auto& r = ctx.proc.renderer();
        if (r.busy()) status.set ("RENDERING " + juce::String (r.jobsDone()) + "/" + juce::String (r.jobsTotal()), juce::String (r.currentJob()).toUpperCase());
        else status.set ("READY", "VOICES " + juce::String (ctx.proc.workstation().kit().activeVoices() + ctx.proc.workstation().activePlayers()));
        master.syncFromParam();
        play.setToggleState (ctx.proc.apvts.getRawParameterValue ("run")->load() > 0.5f, juce::dontSendNotification);
        play.setButtonText (play.getToggleState() ? "||" : ">");
    }

private:
    Ctx& ctx;
    Segmented rooms;   // the HOST / KEY / PADS modes live in the kit room's toolbar
    LcdBox lcd, status;
    Pill fsButton, bdButton, play;
    Knob master;
};

// ================================================================== tree
class Tree final : public juce::Component, public Part
{
public:
    explicit Tree (Ctx& c) : ctx (c) { refresh(); }

    struct Row { int kind; int index; juce::String text; juce::Array<juce::Colour> dots; bool selected; bool head; int indent; bool on; };
    enum { Head, PadRow, ViewRow, GridRow, LayerRow, FsRow, BdRow };

    void refresh() override
    {
        rows.clear();
        auto& P = ctx.P();
        const auto& ks = nb::kits();
        rows.add ({ Head, 0, "KIT | " + juce::String (ks[(size_t) juce::jlimit (0, (int) ks.size() - 1, ctx.proc.currentKit.load())].name).toUpperCase(), {}, false, true, 0, true });
        for (int p = 0; p < nb::kNumPads; ++p)
        {
            Row r { PadRow, p, juce::String (p + 1).paddedLeft ('0', 2) + "  " + ctx.proc.padName (p), {}, ctx.room == 0 && ctx.pad == p, false, 0, true };
            for (const auto& ins : P.padFx[(size_t) p].inserts) if (ins.kind != nb::CellFx::None) r.dots.add (C::fxColour (ins));
            for (const auto& c : P.matrix.oneShot.cells) if (c.pad == p && r.dots.size() < 5) r.dots.add (C::fxColour (c.fx));
            for (const auto& s : P.fs) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) r.dots.add (C::amber());
            for (const auto& s : P.bd) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) r.dots.add (C::family ("BOUNCE"));
            rows.add (r);
        }
        rows.add ({ Head, 0, "MATRIXXXER", {}, false, true, 0, true });
        const juce::String vn[3] = { "Sequencer | " + juce::String (P.matrix.seq.name), "One Shot | " + juce::String (P.matrix.oneShot.name), "Sample | " + juce::String (P.matrix.sample.name) };
        const bool on[3] = { P.matrix.seqOn, P.matrix.oneShotOn, P.matrix.sampleOn };
        for (int v = 0; v < 3; ++v) rows.add ({ ViewRow, v, vn[v], {}, ctx.room == 1 && P.matrix.view == v, false, 0, on[v] });
        rows.add ({ Head, 0, "GRID", {}, false, true, 0, true });
        rows.add ({ GridRow, 0, "Pattern " + juce::String (P.grid.current + 1) + (P.grid.play ? " | playing" : " | off"), {}, ctx.room == 2, false, 0, P.grid.play });
        rows.add ({ Head, 0, "KEY MAP", {}, false, true, 0, true });
        for (int l = 0; l < (int) P.keymap.layers.size() && l < 5; ++l)
            rows.add ({ LayerRow, l, juce::String (P.keymap.layers[(size_t) l].name), { juce::Colour (P.keymap.layers[(size_t) l].colour) }, ctx.room == 3, false, 0, P.keymap.layers[(size_t) l].on });
        rows.add ({ Head, 0, "WINDOWS", {}, false, true, 0, true });
        int fsN = 0, bdN = 0;
        for (const auto& s : P.fs) fsN += s.on ? 1 : 0;
        for (const auto& s : P.bd) bdN += s.on ? 1 : 0;
        rows.add ({ FsRow, 0, "Friction Stretch | " + juce::String (fsN) + " of 8", { C::amber() }, ctx.fsOpen, false, 0, true });
        rows.add ({ BdRow, 0, "Bounce Delay | " + juce::String (bdN) + " of 8", { C::family ("BOUNCE") }, ctx.bdOpen, false, 0, true });
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        paintPlate (g, getLocalBounds().toFloat());
        paintLabel (g, "WHERE YOU ARE", { 12, 8, 190, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        int y = 24;
        rects.clear();
        for (const auto& r : rows)
        {
            const int h = r.head ? 18 : 20;
            auto rr = juce::Rectangle<int> (8, y, getWidth() - 16, h);
            rects.add (rr);
            if (r.head) paintLabel (g, r.text, rr.withTrimmedLeft (4).withTrimmedTop (6), juce::Justification::centredLeft, 7.5f);
            else
            {
                if (r.selected) { paintPearl (g, rr.toFloat(), 8.0f, true); }
                else if (rr.contains (hover)) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (rr.toFloat(), 8.0f); }
                g.setColour (r.on ? C::text() : C::dim());
                g.setFont (font (12.0f, r.kind == PadRow ? false : true));
                const int dotsW = r.dots.size() * 9;
                g.drawText (r.text, rr.withTrimmedLeft (6).withTrimmedRight (dotsW + 4), juce::Justification::centredLeft, true);
                int x = rr.getRight() - 6 - dotsW;
                for (auto c : r.dots)
                {
                    g.setColour (c); g.fillEllipse ((float) x, (float) rr.getCentreY() - 3.5f, 7.0f, 7.0f);
                    g.setColour (juce::Colour (0x701f2925)); g.drawEllipse ((float) x, (float) rr.getCentreY() - 3.5f, 7.0f, 7.0f, 0.8f);
                    x += 9;
                }
            }
            y += h;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < rows.size() && i < rects.size(); ++i)
        {
            if (! rects[i].contains (e.getPosition()) || rows[i].head) continue;
            const auto& r = rows[i];
            switch (r.kind)
            {
                case PadRow: ctx.room = 0; ctx.select ({ Sel::Pad, r.index }); if (! e.mods.isPopupMenu()) ctx.proc.audition (r.index, 0.85f); break;
                case ViewRow: ctx.room = 1; ctx.P().matrix.view = r.index; ctx.select ({ Sel::None, 0 }); break;
                case GridRow: ctx.room = 2; ctx.select ({ Sel::Grid, 0 }); break;
                case LayerRow: ctx.room = 3; if (ctx.refreshAll) ctx.refreshAll(); break;
                case FsRow: ctx.openWindow (true, ctx.fsSlot); break;
                case BdRow: ctx.openWindow (false, ctx.bdSlot); break;
                default: break;
            }
            return;
        }
    }

private:
    Ctx& ctx;
    juce::Array<Row> rows;
    juce::Array<juce::Rectangle<int>> rects;
    juce::Point<int> hover { -1, -1 };
};

// ================================================================== mod dock
class Dock final : public juce::Component, public Part
{
public:
    explicit Dock (Ctx& c) : ctx (c), rate (c, "RATE", 0.0625f, 16.0f, 1.0f, true), envA (c, "ATTACK", 0.0005f, 1.0f, 0.004f, true),
                             envD (c, "DECAY", 0.01f, 4.0f, 0.35f, true), scope ({ "KIT", "PAD", "CELL" })
    {
        for (auto* k : { &rate, &envA, &envD }) addChildComponent (k);
        addChildComponent (shape); addChildComponent (sync); addChildComponent (scope);
        shape.addItemList ({ "SINE", "TRIANGLE", "SAW", "SQUARE", "DRAWN" }, 1);
        shape.onChange = [this] { if (auto* l = lfo()) { l->shape = shape.getSelectedId() - 1; ctx.changed (false); repaint(); } };
        sync.setButtonText ("SYNC"); sync.setClickingTogglesState (true);
        sync.onClick = [this] { if (auto* l = lfo()) { l->sync = sync.getToggleState(); ctx.changed (false); refresh(); } };
        scope.onSelect = [this] (int i)
        {
            if (auto* l = lfo()) l->scope = (nb::Scope) i;
            else if (ctx.dockSrc == nb::ModSrc::Random) ctx.P().mod.randScope = (nb::Scope) i;
            ctx.changed (false);
        };
        rate.onChange = [this] (float v)
        {
            if (auto* l = lfo()) { if (l->sync) l->beats = v; else l->hz = v; }
            else if (ctx.dockSrc == nb::ModSrc::Random) ctx.P().mod.randBeats = v;
            ctx.changed (false);
        };
        envA.onChange = [this] (float v) { ctx.P().mod.envA = v; ctx.changed (false); };
        envD.onChange = [this] (float v) { ctx.P().mod.envD = v; ctx.changed (false); };
        envA.toText = envD.toText = [] (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000)) + " ms" : juce::String (v, 2) + " s"; };
        addAndMakeVisible (listAll); listAll.setButtonText ("LIST ALL");
        listAll.onClick = [this] { ctx.listOpen = ! ctx.listOpen; if (ctx.refreshAll) ctx.refreshAll(); };
        refresh();
    }

    nb::LfoSpec* lfo() { return ctx.dockSrc == nb::ModSrc::Lfo1 ? &ctx.P().mod.lfo1 : ctx.dockSrc == nb::ModSrc::Lfo2 ? &ctx.P().mod.lfo2 : nullptr; }

    juce::Rectangle<int> tileRect (int i) const { return { 106 + i * 92, 10, 86, getHeight() - 20 }; }
    juce::Rectangle<int> editorArea() const { return { 1032, 6, 286, getHeight() - 12 }; }

    void refresh() override
    {
        auto* l = lfo();
        const auto src = ctx.dockSrc;
        const bool isRand = src == nb::ModSrc::Random, isEnv = src == nb::ModSrc::Env1;
        shape.setVisible (l != nullptr); sync.setVisible (l != nullptr); scope.setVisible (l != nullptr || isRand);
        rate.setVisible (l != nullptr || isRand); envA.setVisible (isEnv); envD.setVisible (isEnv);
        if (l != nullptr)
        {
            shape.setSelectedId (l->shape + 1, juce::dontSendNotification);
            sync.setToggleState (l->sync, juce::dontSendNotification);
            scope.setSelected ((int) l->scope);
            if (l->sync) { rate.setRange (0.0625f, 16.0f, true); rate.setValue (l->beats); rate.toText = [] (float v) { return v >= 1.0f ? juce::String (v, v < 4 ? 2 : 0) + " beats" : "1/" + juce::String (juce::roundToInt (4.0f / v)); }; }
            else { rate.setRange (0.02f, 20.0f, true); rate.setValue (l->hz); rate.toText = [] (float v) { return juce::String (v, 2) + " Hz"; }; }
            rate.setDest ({});
        }
        if (isRand) { rate.setRange (0.0625f, 4.0f, true); rate.setValue (ctx.P().mod.randBeats); scope.setSelected ((int) ctx.P().mod.randScope); }
        if (isEnv) { envA.setValue (ctx.P().mod.envA); envD.setValue (ctx.P().mod.envD); }
        listAll.setToggleState (ctx.listOpen, juce::dontSendNotification);
        resized();
        repaint();
    }

    void resized() override
    {
        auto e = editorArea();
        auto top = e.removeFromTop (28);
        shape.setBounds (top.removeFromLeft (110).reduced (0, 2));
        sync.setBounds (top.removeFromLeft (64).reduced (3, 2));
        scope.setBounds (top.reduced (0, 1));
        auto row = e.withTrimmedTop (4);
        rate.setBounds (row.removeFromLeft (76));
        envA.setBounds (e.withWidth (76).withTrimmedTop (-28));
        envD.setBounds (e.withWidth (76).withX (e.getX() + 80).withTrimmedTop (-28));
        listAll.setBounds (getWidth() - 84, getHeight() / 2 - 16, 74, 32);
    }

    void paint (juce::Graphics& g) override
    {
        paintPlate (g, getLocalBounds().toFloat());
        paintLabel (g, "MOD DOCK", { 14, 14, 90, 12 }, juce::Justification::centredLeft, 8.0f);
        g.setColour (C::dim()); g.setFont (font (10.5f));
        g.drawFittedText ("Drag a source onto any knob. Right-click a knob to remove.", juce::Rectangle<int> (14, 30, 86, 70), juce::Justification::topLeft, 5);
        auto& m = ctx.P().mod;
        for (int i = 0; i < (int) nb::ModSrc::Count; ++i)
        {
            const auto s = (nb::ModSrc) i;
            auto r = tileRect (i).toFloat();
            const bool sel = s == ctx.dockSrc;
            paintPearl (g, r, 12.0f, sel);
            if (sel) { g.setColour (C::mod (s)); g.drawRoundedRectangle (r.expanded (1.0f), 13.0f, 2.0f); }
            g.setColour (C::mod (s)); g.fillEllipse (r.getX() + 8, r.getY() + 9, 8, 8);
            paintLabel (g, nb::modSrcName (s), juce::Rectangle<int> ((int) r.getX() + 20, (int) r.getY() + 6, (int) r.getWidth() - 22, 14), juce::Justification::centredLeft, 6.5f);
            // its shape
            auto sh = r.reduced (8).withTrimmedTop (16).withTrimmedBottom (26);
            juce::Path p;
            for (int k = 0; k <= 40; ++k)
            {
                const float t = k / 40.0f;
                float y = 0.0f;
                switch (s)
                {
                    case nb::ModSrc::Lfo1: y = nb::lfoShape (m.lfo1.shape, t * 2.0, m.table); break;
                    case nb::ModSrc::Lfo2: y = nb::lfoShape (m.lfo2.shape, t, m.table); break;
                    case nb::ModSrc::Env1: y = 2.0f * (t < 0.08f ? t / 0.08f : std::exp (-(t - 0.08f) * 5.0f)) - 1.0f; break;
                    case nb::ModSrc::Random: y = nb::hashUnit ((uint32_t) (t * 8) * 77u + 3u); break;
                    case nb::ModSrc::Velocity: y = 2.0f * t - 1.0f; break;
                    case nb::ModSrc::Key: y = 2.0f * (std::floor (t * 12) / 11.0f) - 1.0f; break;
                    case nb::ModSrc::Height: y = nb::hashUnit ((uint32_t) (t * 10) * 31u + 9u) * 0.8f; break;
                    case nb::ModSrc::Step: y = 2.0f * ((int) (t * 8) % 4) / 3.0f - 1.0f; break;
                    case nb::ModSrc::XyX: y = 2.0f * m.xyX - 1.0f; break;
                    default: y = 2.0f * m.xyY - 1.0f; break;
                }
                const float px = sh.getX() + t * sh.getWidth(), py = sh.getCentreY() - y * sh.getHeight() * 0.45f;
                if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (C::mod (s)); g.strokePath (p, juce::PathStrokeType (1.8f));
            int n = 0; for (const auto& r2 : m.routes) n += r2.src == s ? 1 : 0;
            g.setColour (C::dim()); g.setFont (font (10.0f));
            g.drawText (n == 0 ? juce::String ("not used") : juce::String (n) + (n == 1 ? " target" : " targets"), r.reduced (8, 4).removeFromBottom (14).toNearestInt(), juce::Justification::centredLeft);
        }
        // the editor of the selected source
        auto e = editorArea().toFloat();
        paintWell (g, e.expanded (4, 2), 10.0f);
        const auto s = ctx.dockSrc;
        if (s == nb::ModSrc::Lfo2)
        {
            tableRect = editorArea().withTrimmedTop (34).withTrimmedLeft (84).reduced (2);
            paintLcd (g, tableRect.toFloat(), 6.0f);
            juce::Path p;
            for (int k = 0; k < 64; ++k)
            {
                const float px = tableRect.getX() + (k + 0.5f) * tableRect.getWidth() / 64.0f, py = tableRect.getCentreY() - m.table[(size_t) k] * tableRect.getHeight() * 0.42f;
                if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (juce::Colour (0xffffd1f3)); g.strokePath (p, juce::PathStrokeType (2.0f));
            paintLabel (g, "DRAW", tableRect.reduced (4).removeFromTop (10), juce::Justification::topRight, 6.5f, C::lcdDim());
        }
        else tableRect = {};
        if (s == nb::ModSrc::XyX || s == nb::ModSrc::XyY)
        {
            xyRect = editorArea().reduced (6).withWidth (150);
            paintLcd (g, xyRect.toFloat(), 6.0f);
            const float px = xyRect.getX() + m.xyX * xyRect.getWidth(), py = xyRect.getBottom() - m.xyY * xyRect.getHeight();
            g.setColour (C::lcdText().withAlpha (0.3f)); g.drawHorizontalLine ((int) py, (float) xyRect.getX(), (float) xyRect.getRight()); g.drawVerticalLine ((int) px, (float) xyRect.getY(), (float) xyRect.getBottom());
            g.setColour (C::lcdText()); g.fillEllipse (px - 5, py - 5, 10, 10);
            g.setColour (C::dim()); g.setFont (font (11.0f));
            g.drawFittedText ("X " + juce::String (juce::roundToInt (m.xyX * 100)) + "  Y " + juce::String (juce::roundToInt (m.xyY * 100)) + "\nDrag the point while it plays.",
                              editorArea().withTrimmedLeft (160), juce::Justification::centredLeft, 3);
        }
        else xyRect = {};
        if (s == nb::ModSrc::Velocity || s == nb::ModSrc::Key || s == nb::ModSrc::Height || s == nb::ModSrc::Step)
        {
            const char* what[] = { "How hard each hit is (0 to 1).", "The key that played it, around C3 (-1 to 1).",
                                   "A One Shot cell's height (0 to 1). Height already sets each cell's main control; route it to reach a second knob.",
                                   "Where the hit sits in the main pattern (0 to 1)." };
            const int wi = s == nb::ModSrc::Velocity ? 0 : s == nb::ModSrc::Key ? 1 : s == nb::ModSrc::Height ? 2 : 3;
            g.setColour (C::text()); g.setFont (font (12.0f));
            g.drawFittedText (what[wi], editorArea().reduced (10), juce::Justification::centredLeft, 4);
        }
        if (s == nb::ModSrc::Env1)
        {
            g.setColour (C::dim()); g.setFont (font (11.0f));
            g.drawFittedText ("Restarts on every hit.", editorArea().withTrimmedLeft (166).reduced (6), juce::Justification::centredLeft, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) nb::ModSrc::Count; ++i)
            if (tileRect (i).contains (e.getPosition())) { ctx.dockSrc = (nb::ModSrc) i; dragTile = i; refresh(); return; }
        dragTile = -1;
        drawTable (e);
        dragXY (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragTile >= 0 && e.getDistanceFromDragStart() > 6)
        {
            if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                juce::Image img (juce::Image::ARGB, 60, 26, true);
                juce::Graphics g (img);
                g.setColour (C::mod ((nb::ModSrc) dragTile)); g.fillRoundedRectangle (0, 0, 60, 26, 13);
                g.setColour (juce::Colours::white); g.setFont (caps (7.5f)); g.drawText (nb::modSrcName ((nb::ModSrc) dragTile), 0, 0, 60, 26, juce::Justification::centred);
                dc->startDragging ("mod:" + juce::String (dragTile), this, juce::ScaledImage (img), true);
            }
            dragTile = -1;
            return;
        }
        drawTable (e);
        dragXY (e);
    }

private:
    void drawTable (const juce::MouseEvent& e)
    {
        if (tableRect.isEmpty() || ! tableRect.contains (e.getPosition())) return;
        const int k = juce::jlimit (0, 63, (int) ((e.x - tableRect.getX()) * 64 / tableRect.getWidth()));
        const float v = juce::jlimit (-1.0f, 1.0f, (tableRect.getCentreY() - e.y) / (tableRect.getHeight() * 0.42f));
        auto& t = ctx.P().mod.table;
        t[(size_t) k] = v;
        if (k > 0) t[(size_t) k - 1] = 0.5f * (t[(size_t) k - 1] + v);
        ctx.changed (false); repaint();
    }
    void dragXY (const juce::MouseEvent& e)
    {
        if (xyRect.isEmpty() || ! xyRect.expanded (6).contains (e.getPosition())) return;
        ctx.P().mod.xyX = juce::jlimit (0.0f, 1.0f, (float) (e.x - xyRect.getX()) / xyRect.getWidth());
        ctx.P().mod.xyY = juce::jlimit (0.0f, 1.0f, (float) (xyRect.getBottom() - e.y) / xyRect.getHeight());
        ctx.changed (false); repaint();
    }
    Ctx& ctx;
    Knob rate, envA, envD;
    juce::ComboBox shape;
    Pill sync, listAll;
    Segmented scope;
    juce::Rectangle<int> tableRect, xyRect;
    int dragTile = -1;
};

// ================================================================== route list (LIST ALL)
class RouteList final : public juce::Component, public Part
{
public:
    explicit RouteList (Ctx& c) : ctx (c) { addAndMakeVisible (close); close.setButtonText ("CLOSE"); close.onClick = [this] { ctx.listOpen = false; if (ctx.refreshAll) ctx.refreshAll(); }; }

    void refresh() override
    {
        sliders.clear(); removes.clear();
        auto& rs = ctx.P().mod.routes;
        for (size_t i = 0; i < rs.size(); ++i)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
            s->setRange (-100.0, 100.0, 1.0); s->setValue (rs[i].depth * 100.0, juce::dontSendNotification);
            s->setColour (juce::Slider::trackColourId, C::mod (rs[i].src));
            s->onValueChange = [this, i, s] { auto& r2 = ctx.P().mod.routes; if (i < r2.size()) { r2[i].depth = (float) s->getValue() / 100.0f; ctx.changed (false); repaint(); } };
            addAndMakeVisible (s);
            auto* b = removes.add (new Pill ("X"));
            b->onClick = [this, i] { auto& r2 = ctx.P().mod.routes; if (i < r2.size()) { r2.erase (r2.begin() + (long) i); ctx.changed(); } };
            addAndMakeVisible (b);
        }
        resized(); repaint();
    }

    void resized() override
    {
        close.setBounds (getWidth() - 96, 12, 80, 30);
        for (int i = 0; i < sliders.size(); ++i)
        {
            const int y = 60 + i * 28;
            sliders[i]->setBounds (430, y, 300, 24);
            removes[i]->setBounds (744, y + 1, 30, 22);
            sliders[i]->setVisible (y < getHeight() - 30); removes[i]->setVisible (y < getHeight() - 30);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0x66000000));
        auto r = getLocalBounds().toFloat();
        paintPlate (g, r);
        paintLabel (g, "EVERY ROUTE | SOURCE TO DESTINATION | DEPTH", { 18, 18, 600, 16 }, juce::Justification::centredLeft, 9.0f);
        auto& rs = ctx.P().mod.routes;
        if (rs.empty())
        {
            g.setColour (C::dim()); g.setFont (font (14.0f));
            g.drawText ("No routes yet. Drag a source from the dock onto any knob.", getLocalBounds().withTrimmedTop (60).withHeight (40), juce::Justification::centred);
        }
        for (size_t i = 0; i < rs.size(); ++i)
        {
            const int y = 60 + (int) i * 28;
            if (y > getHeight() - 30) break;
            g.setColour (C::mod (rs[i].src)); g.fillEllipse (20, (float) y + 8, 9, 9);
            g.setColour (C::text()); g.setFont (font (13.0f, true));
            g.drawText (nb::modSrcName (rs[i].src), 36, y, 130, 24, juce::Justification::centredLeft);
            g.setFont (font (13.0f));
            g.drawText ("-> " + destLabel (rs[i].dest), 170, y, 250, 24, juce::Justification::centredLeft);
            g.drawText (juce::String (juce::roundToInt (rs[i].depth * 100)) + " %", 780, y, 60, 24, juce::Justification::centredRight);
        }
    }

private:
    Ctx& ctx;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<Pill> removes;
    Pill close;
};

std::unique_ptr<juce::Component> makeHeader (Ctx& c) { return std::make_unique<Header> (c); }
std::unique_ptr<juce::Component> makeTree (Ctx& c) { return std::make_unique<Tree> (c); }
std::unique_ptr<juce::Component> makeDock (Ctx& c) { return std::make_unique<Dock> (c); }
std::unique_ptr<juce::Component> makeRouteList (Ctx& c) { return std::make_unique<RouteList> (c); }

// ================================================================== the shell
Shell::Shell (NeoBeatProcessor& p) : ctx (p)
{
    setLookAndFeel (&look);
    header = makeHeader (ctx); tree = makeTree (ctx); inspector = makeInspector (ctx); dock = makeDock (ctx);
    rooms[0] = makeKitRoom (ctx); rooms[1] = makeMatrixRoom (ctx); rooms[2] = makeGridRoom (ctx); rooms[3] = makeKeyMapRoom (ctx);
    fsWindow = makeFrictionWindow (ctx); bdWindow = makeBounceWindow (ctx); routeList = makeRouteList (ctx);
    for (auto* c : { header.get(), tree.get(), inspector.get(), dock.get() }) addAndMakeVisible (c);
    for (auto& r : rooms) addChildComponent (r.get());
    for (auto* c : { fsWindow.get(), bdWindow.get(), routeList.get() }) addChildComponent (c);
    for (auto* c : { header.get(), tree.get(), inspector.get(), dock.get(), rooms[0].get(), rooms[1].get(), rooms[2].get(), rooms[3].get(), fsWindow.get(), bdWindow.get(), routeList.get() })
        if (auto* part = dynamic_cast<Part*> (c)) parts.push_back (part);
    ctx.refreshAll = [this] { triggerAsyncUpdate(); };
    ctx.refreshNow = [this] { cancelPendingUpdate(); refresh(); };
    setSize (1440, 900);
    refresh();
    startTimerHz (30);
}

Shell::~Shell()
{
    stopTimer();
    cancelPendingUpdate();
    juce::PopupMenu::dismissAllActiveMenus();   // their callbacks get 0 and touch nothing
    ctx.refreshAll = nullptr; ctx.refreshNow = nullptr;
    setLookAndFeel (nullptr);
}

void Shell::paint (juce::Graphics& g) { paintChassis (g, getLocalBounds().toFloat()); }

void Shell::resized()
{
    header->setBounds (0, 0, 1440, 72);
    tree->setBounds (16, 84, 216, 664);
    for (auto& r : rooms) r->setBounds (244, 84, 820, 664);
    inspector->setBounds (1076, 84, 348, 664);
    dock->setBounds (16, 760, 1408, 124);
    fsWindow->setBounds (130, 60, 1180, 760);
    bdWindow->setBounds (130, 60, 1180, 760);
    routeList->setBounds (250, 120, 880, 560);
}

void Shell::refresh()
{
    for (int i = 0; i < 4; ++i) rooms[(size_t) i]->setVisible (i == ctx.room);
    fsWindow->setVisible (ctx.fsOpen);
    bdWindow->setVisible (ctx.bdOpen);
    routeList->setVisible (ctx.listOpen);
    if (ctx.fsOpen) fsWindow->toFront (false);
    if (ctx.bdOpen) bdWindow->toFront (false);
    if (ctx.listOpen) routeList->toFront (false);
    for (auto* p : parts) p->refresh();
}

void Shell::timerCallback()
{
    for (auto* p : parts) p->tick();
}
} // namespace ui
