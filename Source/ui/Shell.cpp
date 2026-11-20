// NeoBeat 2.0 — the shell: header, tree, rooms, inspector, mod dock, and the overlays.
#include "UI.h"

namespace ui
{
// ================================================================== header
class Header final : public juce::Component, public Part
{
public:
    explicit Header (Ctx& c) : ctx (c), rooms ({ "SEQUENCER", "MATRIXXXER", "LAYERMAX", "KEY MAP" }),
                               master (c, "MASTER", -30.0f, 6.0f, 0.0f)
    {
        addAndMakeVisible (rooms);
        rooms.onSelect = [this] (int i) { ctx.room = i; if (ctx.refreshAll) ctx.refreshAll(); };
        addAndMakeVisible (lcd);
        for (auto* b : { &undo, &redo, &kit, &fsButton, &bdButton }) addAndMakeVisible (b);
        undo.onClick = [this] { ctx.proc.undo (ctx.room); if (ctx.refreshAll) ctx.refreshAll(); };
        redo.onClick = [this] { ctx.proc.redo (ctx.room); if (ctx.refreshAll) ctx.refreshAll(); };
        kit.onClick = [this] { kitMenu(); };
        kit.setTooltip ("Kits and sequences (.NJQ), a folder of samples, export, MIDI out");
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
        auto brand = juce::Rectangle<float> (12, 10, 138, 52);
        paintPearl (g, brand, 14.0f);
        paintGem (g, { brand.getX() + 12, brand.getCentreY() - 5, 10, 10 });
        g.setFont (caps (16.0f)); g.setColour (C::text());
        g.drawText ("NEOBEAT", brand.toNearestInt().withTrimmedLeft (28).withTrimmedBottom (14), juce::Justification::centredLeft);
        paintLabel (g, "3.0 | NJS WORKSTATION", brand.toNearestInt().withTrimmedLeft (28).withTrimmedTop (30), juce::Justification::centredLeft, 6.0f);
        paintLabel (g, "THIS ROOM", { 568, 4, 92, 12 }, juce::Justification::centred, 6.5f, C::dim());
        paintLabel (g, "WINDOWS", { 968, 4, 120, 12 }, juce::Justification::centredLeft, 6.5f, C::dim());
    }

    void resized() override
    {
        rooms.setBounds (158, 18, 402, 36);
        undo.setBounds (568, 18, 44, 36); redo.setBounds (614, 18, 44, 36);
        lcd.setBounds (666, 12, 212, 48);
        kit.setBounds (886, 18, 74, 36);
        fsButton.setBounds (968, 18, 100, 36);
        bdButton.setBounds (1072, 18, 100, 36);
        status.setBounds (1180, 18, 122, 36);
        master.setBounds (1306, 4, 64, 66);
        play.setBounds (1384, 16, 40, 40);
    }

    void refresh() override
    {
        rooms.setSelected (ctx.room);
        int fsN = 0, bdN = 0;
        for (const auto& s : ctx.P().fs) fsN += s.on ? 1 : 0;
        for (const auto& s : ctx.P().bd) bdN += s.on ? 1 : 0;
        fsButton.setButtonText ("FRICTION " + juce::String (fsN));
        bdButton.setButtonText ("BOUNCE " + juce::String (bdN));
        fsButton.setToggleState (ctx.fsOpen, juce::dontSendNotification);
        bdButton.setToggleState (ctx.bdOpen, juce::dontSendNotification);
    }

    void tick() override
    {
        const int idx = ctx.proc.currentPreset.load();
        const auto mode = (int) ctx.proc.apvts.getRawParameterValue ("mode")->load();
        const juce::String m = mode == 0 ? (ctx.proc.hostSynced.load() ? "HOST PLAYING" : "HOST") : mode == 1 ? "KEY" : "PADS";
        const juce::String name = ctx.proc.currentNjq.isNotEmpty() ? juce::File (ctx.proc.currentNjq).getFileNameWithoutExtension() : juce::String (nb::patternPreset (idx).name);
        lcd.set ((ctx.proc.currentNjq.isNotEmpty() ? juce::String ("NJQ") : juce::String (idx + 1).paddedLeft ('0', 3)) + "  " + name.toUpperCase(),
                 juce::String (ctx.proc.displayBpm.load(), 1) + " BPM | SWING " + juce::String (juce::roundToInt (ctx.proc.apvts.getRawParameterValue ("swing")->load() * 100)) + "% | " + m);
        auto& r = ctx.proc.renderer();
        const int fx = ctx.proc.workstation().activeUnits();
        if (ctx.proc.exporting()) status.set ("EXPORTING", juce::String (juce::roundToInt (ctx.proc.exportProgress() * 100.0f)) + " %");
        else if (r.busy()) status.set ("PREPARING " + juce::String (r.jobsDone()) + "/" + juce::String (r.jobsTotal()), juce::String (r.currentJob()).toUpperCase());
        else status.set ("LIVE", "VOICES " + juce::String (ctx.proc.workstation().kit().activeVoices() + ctx.proc.workstation().activePlayers()) + " | FX " + juce::String (fx));
        master.syncFromParam();
        play.setToggleState (ctx.proc.apvts.getRawParameterValue ("run")->load() > 0.5f, juce::dontSendNotification);
        play.setButtonText (play.getToggleState() ? "||" : ">");
        const bool cu = ctx.proc.canUndo (ctx.room), cr = ctx.proc.canRedo (ctx.room);
        undo.setEnabled (cu); redo.setEnabled (cr);
        undo.setTooltip (cu ? "Undo: " + ctx.proc.undoLabel (ctx.room) + "  (Ctrl+Z). Each room keeps its own history." : juce::String ("Nothing to undo in this room"));
        redo.setTooltip (cr ? "Redo: " + ctx.proc.redoLabel (ctx.room) + "  (Ctrl+Shift+Z)" : juce::String ("Nothing to redo in this room"));
    }

private:
    void afterNjq (const juce::File& f)
    {
        if (shared::Settings::load().askDefault) { ctx.overlay = 4; ctx.overlayFile = f.getFullPathName(); }
        ctx.commit();
        if (ctx.refreshAll) ctx.refreshAll();
    }
    void kitMenu()
    {
        const auto st = shared::Settings::load();
        njqList = ctx.proc.njqFiles();
        juce::PopupMenu m, mine;
        for (int i = 0; i < njqList.size() && i < 400; ++i)
            mine.addItem (2000 + i, njqList[i].getFileNameWithoutExtension(), true, njqList[i].getFullPathName() == ctx.proc.currentNjq);
        if (njqList.isEmpty()) mine.addItem (1999, "(none yet: save one, or make one in Batchcut's Kit room)", false);
        m.addSectionHeader ("KITS AND SEQUENCES (.NJQ)");
        m.addSubMenu ("My sequences", mine);
        m.addItem (1, "Load a .NJQ file...");
        m.addItem (2, "Save as .NJQ...");
        m.addItem (7, "Make this a default sequence...", ctx.proc.currentNjq.isNotEmpty());
        m.addItem (8, "Start with the stock pattern again", st.startupNjq.isNotEmpty());
        m.addItem (6, "Preset folder: " + st.presets().getFullPathName() + " ...");
        m.addSectionHeader ("SAMPLES");
        m.addItem (3, "Import a folder of samples into the lanes...");
        m.addSectionHeader ("OUT");
        m.addItem (4, "Export the pattern as WAV and MIDI...");
        m.addItem (5, "MIDI out: NeoBeat as a sequencer...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kit), [this] (int r)
        {
            if (r <= 0) return;
            const auto st2 = shared::Settings::load();
            if (r >= 2000 && r - 2000 < njqList.size())
            {
                juce::String err; const auto f = njqList[r - 2000];
                if (ctx.proc.loadNjq (f, err)) { ctx.commit(); if (ctx.refreshAll) ctx.refreshAll(); }
                else ctx.notify ("njq:" + err, "COULD NOT LOAD", err);
            }
            if (r == 1)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load a kit and its sequence", st2.presets(), "*.njq;*.NJQ");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    juce::String err;
                    if (ctx.proc.loadNjq (f, err)) afterNjq (f); else { ctx.noticesShown.removeString ("njq"); ctx.notify ("njq", "COULD NOT LOAD", err); }
                });
            }
            if (r == 2)
            {
                chooser = std::make_unique<juce::FileChooser> ("Save the kit and its sequence", st2.presets().getChildFile ("My sequence.NJQ"), "*.NJQ");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f == juce::File()) return;
                    f = f.withFileExtension ("NJQ");
                    juce::String err;
                    if (ctx.proc.saveNjq (f, f.getFileNameWithoutExtension(), err)) afterNjq (f); else { ctx.noticesShown.removeString ("njq"); ctx.notify ("njq", "COULD NOT SAVE", err); }
                });
            }
            if (r == 3)
            {
                chooser = std::make_unique<juce::FileChooser> ("A folder of samples", st2.importFolder.isNotEmpty() ? juce::File (st2.importFolder) : juce::File());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    if (! fc.getResult().isDirectory()) return;
                    ctx.overlay = 3; ctx.overlayFile = fc.getResult().getFullPathName();
                    if (ctx.refreshAll) ctx.refreshAll();
                });
            }
            if (r == 4 || r == 5) { ctx.overlay = r == 4 ? 1 : 2; if (ctx.refreshAll) ctx.refreshAll(); }
            if (r == 6)
            {
                chooser = std::make_unique<juce::FileChooser> ("The folder your .NJQ kits live in (Batchcut saves there too)", st2.presets());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [] (const juce::FileChooser& fc)
                {
                    if (! fc.getResult().isDirectory()) return;
                    auto st3 = shared::Settings::load(); st3.presetFolder = fc.getResult().getFullPathName(); st3.save();
                });
            }
            if (r == 7) { ctx.overlay = 4; ctx.overlayFile = ctx.proc.currentNjq; if (ctx.refreshAll) ctx.refreshAll(); }
            if (r == 8) { auto st3 = shared::Settings::load(); st3.startupNjq.clear(); st3.save(); }
        });
    }

    Ctx& ctx;
    Segmented rooms;   // the HOST / KEY / PADS modes live in the sequencer room's toolbar
    LcdBox lcd, status;
    Pill undo { "UNDO" }, redo { "REDO" }, kit { "KIT / FILE" }, fsButton, bdButton, play;
    Knob master;
    juce::Array<juce::File> njqList;
    std::unique_ptr<juce::FileChooser> chooser;
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
        rows.add ({ Head, 0, "LANES | " + juce::String (ks[(size_t) juce::jlimit (0, (int) ks.size() - 1, ctx.proc.currentKit.load())].name).toUpperCase(), {}, false, true, 0, true });
        for (int p = 0; p < nb::kNumPads; ++p)
        {
            Row r { PadRow, p, juce::String (p + 1).paddedLeft ('0', 2) + "  " + ctx.proc.padName (p), {}, ctx.room == 0 && ctx.pad == p, false, 0, true };
            for (const auto& ins : P.padFx[(size_t) p].inserts) if (ins.kind != nb::CellFx::None) r.dots.add (C::fxColour (ins));
            for (const auto& c : P.matrix.oneShot.cells) if (c.pad == p && c.scene == ctx.scene() && r.dots.size() < 5) r.dots.add (C::fxColour (c.fx));
            for (const auto& s : P.fs) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) r.dots.add (C::amber());
            for (const auto& s : P.bd) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) r.dots.add (C::family ("BOUNCE"));
            rows.add (r);
        }
        rows.add ({ Head, 0, "MATRIXXXER", {}, false, true, 0, true });
        const juce::String vn[3] = { "Draw | " + juce::String (P.matrix.seq.name), "One Shot | " + juce::String (P.matrix.oneShot.name), "Sample | " + juce::String (P.matrix.sample.name) };
        const bool on[3] = { P.matrix.seqOn, P.matrix.oneShotOn, P.matrix.sampleOn };
        for (int v = 0; v < 3; ++v) rows.add ({ ViewRow, v, vn[v], {}, ctx.room == 1 && P.matrix.view == v, false, 0, on[v] });
        rows.add ({ Head, 0, "LAYERMAX", {}, false, true, 0, true });
        {
            // rows that follow a lane play with the sequencer even when the pattern itself is off
            int follows = 0; for (const auto& f : P.grid.follow) follows += f.on ? 1 : 0;
            const juce::String state = P.grid.play ? juce::String (" | playing")
                                     : follows > 0 ? " | " + juce::String (follows) + (follows == 1 ? " row follows" : " rows follow")
                                                   : juce::String (" | off");
            rows.add ({ GridRow, 0, "Pattern " + juce::String (P.grid.current + 1) + state, {}, ctx.room == 2, false, 0, P.grid.play || follows > 0 });
        }
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
                             envD (c, "DECAY", 0.01f, 4.0f, 0.35f, true), scope ({ "KIT", "LANE", "NODE" })
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
                                   "A node's height (0 to 1). Height already sets each node's main control; route it to reach a second knob.",
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
    // NeoBeat's own values for the shared look, so merging the drawing code changes nothing on screen
    nsui::setSkin (nsui::skins::chromePearlDeep());
    look.apply();
    setLookAndFeel (&look);
    header = makeHeader (ctx); tree = makeTree (ctx); inspector = makeInspector (ctx); dock = makeDock (ctx);
    rooms[0] = makeKitRoom (ctx); rooms[1] = makeMatrixRoom (ctx); rooms[2] = makeGridRoom (ctx); rooms[3] = makeKeyMapRoom (ctx);
    fsWindow = makeFrictionWindow (ctx); bdWindow = makeBounceWindow (ctx); routeList = makeRouteList (ctx);
    overlays[0] = makeExportPanel (ctx); overlays[1] = makeMidiPanel (ctx); overlays[2] = makeImportPanel (ctx); overlays[3] = makeDefaultPrompt (ctx); overlays[4] = makeNotice (ctx);
    for (auto* c : { header.get(), tree.get(), inspector.get(), dock.get() }) addAndMakeVisible (c);
    for (auto& r : rooms) addChildComponent (r.get());
    for (auto* c : { fsWindow.get(), bdWindow.get(), routeList.get() }) addChildComponent (c);
    for (auto& o : overlays) { addChildComponent (o.get()); if (auto* part = dynamic_cast<Part*> (o.get())) parts.push_back (part); }
    addMouseListener (this, true);       // every child's mouse-up ends a gesture
    setWantsKeyboardFocus (true);
    lateSeen = ctx.proc.lateHits();
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
    removeMouseListener (this);
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
    overlays[0]->setBounds (390, 150, 660, 520);
    overlays[1]->setBounds (300, 96, 840, 700);
    overlays[2]->setBounds (390, 150, 660, 560);
    overlays[3]->setBounds (420, 250, 600, 300);
    overlays[4]->setBounds (420, 270, 600, 260);
}

bool Shell::keyPressed (const juce::KeyPress& k)
{
    const int code = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode());
    if (k.getModifiers().isCommandDown() && code == 'z')
    {
        if (k.getModifiers().isShiftDown()) ctx.proc.redo (ctx.room); else ctx.proc.undo (ctx.room);
        refresh();
        return true;
    }
    if (k.getModifiers().isCommandDown() && code == 'y') { ctx.proc.redo (ctx.room); refresh(); return true; }
    return false;
}

void Shell::mouseUp (const juce::MouseEvent&)
{
    // after the handlers that this mouse-up runs (and any change they make) are done
    juce::Component::SafePointer<Shell> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->ctx.commit(); safe->sinceCommit = 0; } });
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
    for (int i = 0; i < 4; ++i) { overlays[(size_t) i]->setVisible (ctx.overlay == i + 1); if (ctx.overlay == i + 1) overlays[(size_t) i]->toFront (false); }
    overlays[4]->setVisible (ctx.noticeText.isNotEmpty());
    if (ctx.noticeText.isNotEmpty()) overlays[4]->toFront (false);
    for (auto* p : parts) p->refresh();
}

void Shell::timerCallback()
{
    for (auto* p : parts) p->tick();
    // anything that changed without a mouse-up (a menu's choice, a key, a dropped file) still becomes an undo step
    if (++sinceCommit >= 15 && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) { sinceCommit = 0; ctx.commit(); }
    // the one case a hit has to be late: say so, once
    const uint32_t late = ctx.proc.lateHits();
    if (late != lateSeen)
    {
        lateSeen = late;
        ctx.notify ("late", "ONE HIT NEEDED A SHORT BUFFER",
                    "A hit played by hand (a key, a click) went through a spectral effect before NeoBeat had a copy of that lane's sound ready, or through several spectral effects in a row. "
                    "Those effects need the sound 43 ms ahead, so that hit sounded late by up to that much per effect.\n\n"
                    "Hits the sequencer plays are never late (it sees them coming), and neither are hand-played hits through one spectral effect once the lane's sound is prepared, which takes a moment after you change it.");
    }
}
} // namespace ui
