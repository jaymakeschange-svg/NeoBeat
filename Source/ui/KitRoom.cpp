// NeoBeat 2.0 — the KIT room: 16 pads and the main step sequencer.
#include "UI.h"

namespace ui
{
static juce::Colour groupColour (const juce::String& g)
{
    if (g.startsWith ("Round 5")) return juce::Colour (0xff45cdb8);
    if (g.startsWith ("City")) return juce::Colour (0xfff2a541);
    if (g.startsWith ("Round 4")) return juce::Colour (0xffd45db0);
    if (g.startsWith ("IronHead")) return juce::Colour (0xff9bd25b);
    if (g.startsWith ("Sample")) return juce::Colour (0xff7f9cf5);
    return juce::Colour (0xff9aa8a2);
}

// ------------------------------------------------------------------ pads
class PadGrid final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit PadGrid (Ctx& c) : ctx (c) {}
    juce::Rectangle<float> padRect (int p) const
    {
        const float w = (getWidth() - 3 * 8.0f) / 4.0f, h = (getHeight() - 3 * 8.0f) / 4.0f;
        const int row = 3 - p / 4, col = p % 4;   // pad 1 bottom-left, like hardware
        return { col * (w + 8.0f), row * (h + 8.0f), w, h };
    }
    int padAt (juce::Point<int> pt) const { for (int p = 0; p < 16; ++p) if (padRect (p).contains (pt.toFloat())) return p; return -1; }

    void paint (juce::Graphics& g) override
    {
        auto& P = ctx.P();
        for (int p = 0; p < 16; ++p)
        {
            auto r = padRect (p);
            paintPearl (g, r, 12.0f, ctx.pad == p);
            if (flash[(size_t) p] > 0.01f) { g.setColour (C::glow().withAlpha (0.5f * flash[(size_t) p])); g.fillRoundedRectangle (r, 12.0f); }
            const auto col = groupColour (ctx.proc.padGroup (p));
            g.setColour (col); g.fillEllipse (r.getX() + 8, r.getY() + 8, 7, 7);
            // the top row: number and cell count on the left, badges (inserts, windows, algorithm) and MUTE on
            // the right, so the name below has room for two lines
            int cells = 0; for (const auto& c : P.matrix.oneShot.cells) cells += c.pad == p ? 1 : 0;
            const auto top = r.toNearestInt().reduced (8, 4).removeFromTop (14);
            int x = (int) r.getRight() - 9;
            const float dy = (float) top.getCentreY() - 3.5f;
            auto dot = [&] (juce::Colour c) { g.setColour (c); g.fillEllipse ((float) x - 7, dy, 7, 7); g.setColour (juce::Colour (0x701f2925)); g.drawEllipse ((float) x - 7, dy, 7, 7, 0.8f); x -= 9; };
            for (const auto& ins : P.padFx[(size_t) p].inserts) if (ins.kind != nb::CellFx::None) dot (C::fxColour (ins));
            for (const auto& s : P.fs) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) dot (C::amber());
            for (const auto& s : P.bd) if (s.on && s.src.kind == nb::SourceKind::Pad && s.src.index == p) dot (C::family ("BOUNCE"));
            if (P.mod.algos[(size_t) p].type > 0) dot (C::mod (P.mod.algos[(size_t) p].src));
            if (ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "mute"))->load() > 0.5f)
            {
                paintLabel (g, "MUTE", top.withRight (x - 2).withLeft (x - 32), juce::Justification::centredRight, 7.0f, juce::Colour (0xffb8322a));
                x -= 32;
            }
            // number and cell count in what is left of the row (shorter forms when the badges take the room)
            const auto label = top.withTrimmedLeft (12).withRight (x - 3);
            const auto f = font (10.5f);
            const juce::String num = juce::String (p + 1).paddedLeft ('0', 2);
            juce::String text = num;
            if (cells > 0)
            {
                text = num + "  |  " + juce::String (cells) + (cells == 1 ? " cell" : " cells");
                if (textW (f, text) > (float) label.getWidth()) text = num + " | " + juce::String (cells) + "c";
            }
            g.setColour (C::dim()); g.setFont (f);
            g.drawText (text, label, juce::Justification::centredLeft, true);
            g.setColour (C::text()); g.setFont (font (12.0f, true));
            g.drawFittedText (ctx.proc.padName (p), r.toNearestInt().reduced (8, 5).withTrimmedTop (16), juce::Justification::centredLeft, 2, 0.85f);
        }
    }

    void tick()
    {
        bool any = false;
        for (int p = 0; p < 16; ++p)
        {
            const auto h = ctx.proc.hitCount (p);
            if (h != lastHits[(size_t) p]) { lastHits[(size_t) p] = h; flash[(size_t) p] = 1.0f; any = true; }
            else if (flash[(size_t) p] > 0.0f) { flash[(size_t) p] *= 0.75f; any = true; }
        }
        if (any) repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int p = padAt (e.getPosition());
        if (p < 0) return;
        if (e.mods.isPopupMenu()) { padMenu (p); return; }
        const auto r = padRect (p);
        const float vel = juce::jlimit (0.25f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight() * 0.75f);
        ctx.proc.audition (p, vel);
        ctx.select ({ Sel::Pad, p });
    }

    void padMenu (int p)
    {
        juce::PopupMenu m;
        m.addItem (1, "Load a sample...");
        m.addItem (2, "Back to the library sound", ctx.proc.padUsesSample (p));
        m.addItem (3, "Mute in the sequencer", true, ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "mute"))->load() > 0.5f);
        m.addSeparator();
        m.addItem (4, "Send to a Friction Stretch slot");
        m.addItem (5, "Send to a Bounce Delay slot");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, p] (int r)
        {
            if (r == 1)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load a sample onto pad " + juce::String (p + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, p] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile() && ctx.proc.loadSample (p, fc.getResult()) && ctx.refreshAll) ctx.refreshAll();
                });
            }
            if (r == 2) { ctx.proc.clearSample (p); if (ctx.refreshAll) ctx.refreshAll(); }
            if (r == 3) { const auto id = ctx.proc.paramId (p, "mute"); ctx.proc.setParam (id, ctx.proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); repaint(); }
            if (r == 4 || r == 5) assignSlot (r == 4, p);
        });
    }

    void assignSlot (bool fs, int p)
    {
        auto& P = ctx.P();
        int free = -1;
        for (int s = 0; s < nb::kNumSlots && free < 0; ++s) if (! (fs ? P.fs[(size_t) s].on : P.bd[(size_t) s].on)) free = s;
        if (free < 0) free = 0;
        if (fs) { P.fs[(size_t) free].on = true; P.fs[(size_t) free].src = { nb::SourceKind::Pad, p }; }
        else { P.bd[(size_t) free].on = true; P.bd[(size_t) free].src = { nb::SourceKind::Pad, p }; }
        ctx.proc.projectChanged();
        ctx.openWindow (fs, free);
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        const int p = padAt ({ x, y });
        if (p >= 0 && ! files.isEmpty() && ctx.proc.loadSample (p, juce::File (files[0]))) { ctx.select ({ Sel::Pad, p }); }
    }

private:
    Ctx& ctx;
    std::array<uint32_t, 16> lastHits {};
    std::array<float, 16> flash {};
    std::unique_ptr<juce::FileChooser> chooser;
};

// ------------------------------------------------------------------ the step grid
class StepGrid final : public juce::Component
{
public:
    explicit StepGrid (Ctx& c) : ctx (c) {}
    static constexpr int kLabel = 132;

    int length() const { return ctx.proc.pattern.length.load(); }
    float stepW() const { return (float) (getWidth() - kLabel - 4) / (float) length(); }
    float rowH() const { return (float) (getHeight() - 18) / 16.0f; }

    bool cellAt (juce::Point<int> pos, int& track, int& step) const
    {
        if (pos.x < kLabel || pos.y < 18) return false;
        track = juce::jlimit (0, 15, (int) ((pos.y - 18) / rowH()));
        step = juce::jlimit (0, length() - 1, (int) ((pos.x - kLabel) / stepW()));
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        const int len = length();
        const float sw = stepW(), rh = rowH();
        // beat ruler
        for (int s = 0; s < len; s += 4)
        {
            paintLabel (g, juce::String (s / 16 + 1) + "." + juce::String ((s / 4) % 4 + 1), juce::Rectangle<int> (kLabel + (int) (s * sw) + 2, 2, 40, 12), juce::Justification::centredLeft, 7.0f, C::dim());
            g.setColour (juce::Colour (0x10304840)); g.fillRect ((float) kLabel + s * sw, 18.0f, sw * 4, (float) getHeight() - 18);
            if ((s / 4) % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect ((float) kLabel + s * sw, 18.0f, sw * 4, (float) getHeight() - 18); }
        }
        auto& P = ctx.P();
        const int play = ctx.proc.playStep.load();
        for (int t = 0; t < 16; ++t)
        {
            const float y = 18 + t * rh;
            const bool sel = t == ctx.pad;
            if (sel) { g.setColour (C::glow().withAlpha (0.22f)); g.fillRoundedRectangle (2, y + 1, (float) getWidth() - 4, rh - 2, 6.0f); }
            const bool muted = ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (t, "mute"))->load() > 0.5f;
            g.setColour (muted ? C::dim().withAlpha (0.5f) : C::text());
            g.setFont (font (11.5f, sel));
            g.drawText (juce::String (t + 1).paddedLeft ('0', 2) + " " + ctx.proc.padName (t), juce::Rectangle<int> (6, (int) y, kLabel - 10, (int) rh), juce::Justification::centredLeft, true);
            for (int s = 0; s < len; ++s)
            {
                const auto st = ctx.proc.pattern.get (t, s);
                const float cx = kLabel + (s + 0.5f) * sw, cy = y + rh * 0.5f;
                if (st.vel == 0) { g.setColour (juce::Colour (0x40586560)); g.fillEllipse (cx - 2.5f, cy - 2.5f, 5, 5); continue; }
                const float size = std::min (sw, rh) * (st.vel > 110 ? 0.82f : st.vel < 60 ? 0.5f : 0.66f);
                auto r = juce::Rectangle<float> (size, size).withCentre ({ cx, cy });
                if (st.vel > 110) paintGem (g, r);
                else
                {
                    juce::ColourGradient cg (juce::Colours::white, r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.3f, juce::Colour (0xff57635d), r.getRight(), r.getBottom(), true);
                    cg.addColour (0.3, juce::Colour (0xffe6ebe8)); cg.addColour (0.62, juce::Colour (0xff98a49e));
                    g.setGradientFill (cg); g.fillEllipse (r);
                    if (st.vel < 60) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.fillEllipse (r); }
                }
                if (st.roll > 0)
                {
                    g.setColour (C::text()); g.setFont (font (8.5f, true));
                    g.drawText (st.roll == 5 ? "3:2" : "x" + juce::String (st.roll), juce::Rectangle<float> (cx - sw * 0.5f, y, sw, rh * 0.42f).toNearestInt(), juce::Justification::centred);
                }
                if (st.nudge != 0) { g.setColour (C::teal()); g.drawLine (cx, cy + size * 0.6f, cx + st.nudge * 0.1f * sw, cy + size * 0.6f, 1.5f); }
            }
            // One Shot cells on this track
            if (P.matrix.oneShotOn)
                for (const auto& c : P.matrix.oneShot.cells)
                    if (c.pad == t && c.step < len)
                    {
                        g.setColour (C::fxColour (c.fx));
                        g.fillRoundedRectangle (kLabel + c.step * sw + 1, y + 1, std::min ((float) c.span, (float) (len - c.step)) * sw - 2, 3.5f, 1.5f);
                    }
        }
        if (play >= 0 && play < len)
        {
            g.setColour (C::teal().withAlpha (0.5f));
            g.drawRect (kLabel + play * sw, 18.0f, sw, (float) getHeight() - 18, 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < kLabel && e.y >= 18)
        {
            const int t = juce::jlimit (0, 15, (int) ((e.y - 18) / rowH()));
            if (e.mods.isPopupMenu()) { const auto id = ctx.proc.paramId (t, "mute"); ctx.proc.setParam (id, ctx.proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); repaint(); return; }
            ctx.proc.audition (t, 0.85f);
            ctx.select ({ Sel::Pad, t });
            return;
        }
        int t, s;
        if (! cellAt (e.getPosition(), t, s)) return;
        if (e.mods.isPopupMenu()) { stepMenu (t, s); return; }
        auto st = ctx.proc.pattern.get (t, s);
        if (st.vel > 0 && ! e.mods.isShiftDown() && ! e.mods.isAltDown()) { dragMode = -1; st = {}; }
        else
        {
            dragMode = 1;
            dragVel = e.mods.isShiftDown() ? 127 : e.mods.isAltDown() ? 40 : 96;
            st.vel = dragVel;
            ctx.proc.audition (t, dragVel / 127.0f);
        }
        ctx.proc.pattern.set (t, s, st);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        int t, s;
        if (dragMode == 0 || ! cellAt (e.getPosition(), t, s)) return;
        auto st = ctx.proc.pattern.get (t, s);
        if (dragMode > 0 && st.vel == 0) { st.vel = dragVel; ctx.proc.pattern.set (t, s, st); }
        if (dragMode < 0 && st.vel > 0) { ctx.proc.pattern.set (t, s, {}); }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { dragMode = 0; }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int t, s;
        if (! cellAt (e.getPosition(), t, s)) return;
        auto st = ctx.proc.pattern.get (t, s);
        if (st.vel == 0) return;
        st.vel = (uint8_t) juce::jlimit (1, 127, (int) st.vel + (w.deltaY > 0 ? 8 : -8));
        ctx.proc.pattern.set (t, s, st);
        repaint();
    }

    void stepMenu (int t, int s)
    {
        auto st = ctx.proc.pattern.get (t, s);
        juce::PopupMenu m;
        m.addItem (1, "Single hit", true, st.roll == 0);
        m.addItem (2, "Ratchet x2", true, st.roll == 2);
        m.addItem (3, "Ratchet x3", true, st.roll == 3);
        m.addItem (4, "Ratchet x4", true, st.roll == 4);
        m.addItem (5, "Triplet roll (over two steps)", true, st.roll == 5);
        m.addSeparator();
        m.addItem (10, "Accent"); m.addItem (11, "Normal"); m.addItem (12, "Ghost");
        m.addSeparator();
        m.addItem (20, "Nudge early (-20 %)"); m.addItem (21, "On the grid"); m.addItem (22, "Nudge late (+20 %)");
        m.addSeparator();
        m.addItem (30, "Add a One Shot cell here (brush: " + fxName (fxFromIndex (ctx.brush)) + ")");
        m.addItem (31, "Clear");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, s] (int r)
        {
            if (r <= 0) return;   // dismissed (also what an editor closing with the menu open sends)
            auto st2 = ctx.proc.pattern.get (t, s);
            if (st2.vel == 0 && r > 0 && r < 30) st2.vel = 96;
            if (r >= 1 && r <= 5) st2.roll = (uint8_t) (r == 1 ? 0 : r);
            if (r == 10) st2.vel = 127; if (r == 11) st2.vel = 96; if (r == 12) st2.vel = 40;
            if (r == 20) st2.nudge = -20; if (r == 21) st2.nudge = 0; if (r == 22) st2.nudge = 20;
            if (r == 31) st2 = {};
            if (r == 30)
            {
                nb::OneShotCell c; c.pad = t; c.step = s; c.fx = fxFromIndex (ctx.brush);
                ctx.P().matrix.oneShot.cells.push_back (c);
                ctx.select ({ Sel::Cell, (int) ctx.P().matrix.oneShot.cells.size() - 1 }, 1);
                ctx.changed();
            }
            if (r > 0 && r != 30) ctx.proc.pattern.set (t, s, st2);
            repaint();
        });
    }

private:
    Ctx& ctx;
    int dragMode = 0;
    uint8_t dragVel = 96;
};

// ------------------------------------------------------------------ the room
class KitRoom final : public juce::Component, public Part
{
public:
    explicit KitRoom (Ctx& c) : ctx (c), pads (c), steps (c), lengths ({ "16", "32", "64" }), modes ({ "HOST", "KEY", "PADS" }),
                                swing (c, "SWING", 0.5f, 0.75f, 0.64f), tempo (c, "TEMPO", 60.0f, 200.0f, 120.0f)
    {
        addAndMakeVisible (patternBox);
        for (int i = 0; i < nb::numPatterns(); ++i) patternBox.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  " + nb::patternPreset (i).name, i + 1);
        patternBox.onChange = [this] { const int id = patternBox.getSelectedId(); if (id > 0 && id <= nb::numPatterns()) { ctx.proc.loadPreset (id - 1); ctx.changed(); } else if (id > 1000) loadUser (id); };
        for (auto* b : { &prev, &next, &save, &clear }) addAndMakeVisible (b);
        prev.onClick = [this] { ctx.proc.loadPreset ((ctx.proc.currentPreset.load() + nb::numPatterns() - 1) % nb::numPatterns()); ctx.changed(); };
        next.onClick = [this] { ctx.proc.loadPreset ((ctx.proc.currentPreset.load() + 1) % nb::numPatterns()); ctx.changed(); };
        save.onClick = [this] { saveUser(); };
        clear.onClick = [this] { nb::Pattern p; p.length = ctx.proc.pattern.length.load(); ctx.proc.pattern.load (p); ctx.changed(); };
        addAndMakeVisible (kitBox);
        for (int k = 0; k < (int) nb::kits().size(); ++k) kitBox.addItem ("KIT | " + juce::String (nb::kits()[(size_t) k].name).toUpperCase(), k + 1);
        kitBox.onChange = [this] { ctx.proc.loadKit (kitBox.getSelectedId() - 1); ctx.changed(); };
        addAndMakeVisible (lengths);
        lengths.onSelect = [this] (int i) { ctx.proc.pattern.setLength (16 << i); ctx.changed(); };
        addAndMakeVisible (modes);
        modes.onSelect = [this] (int i) { ctx.proc.setParam ("mode", (float) i); };
        addAndMakeVisible (pads); addAndMakeVisible (steps);
        for (auto* k : { &swing, &tempo }) addAndMakeVisible (k);
        swing.attach (ctx.proc.apvts.getParameter ("swing"));
        swing.toText = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; };
        tempo.attach (ctx.proc.apvts.getParameter ("tempo"));
        tempo.toText = [] (float v) { return juce::String (v, 1); };
        tempo.setTooltip ("Standalone tempo (the host's tempo wins inside a DAW)");
        refresh();
    }

    void loadUser (int id)
    {
        const auto names = ctx.proc.userPresets ("Pattern");
        const int i = id - 1001;
        if (i >= 0 && i < names.size()) { ctx.proc.loadUserPreset ("Pattern", names[i]); ctx.changed(); }
    }
    void saveUser()
    {
        auto* w = new juce::AlertWindow ("Save the pattern", "A name for your pattern (kept in your NeoBeat presets):", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", nb::patternPreset (ctx.proc.currentPreset.load()).name + " (mine)");
        w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1) { ctx.proc.saveUserPreset ("Pattern", w->getTextEditorContents ("name")); refresh(); }
        }), true);
    }

    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat(), 18.0f);
        auto info = juce::Rectangle<int> (426, 58, 380, 214);
        paintWell (g, info.toFloat(), 12.0f);
        const int p = ctx.pad;
        paintLabel (g, "PAD " + juce::String (p + 1) + " | " + ctx.proc.padGroup (p), info.reduced (12, 8).removeFromTop (12), juce::Justification::centredLeft, 8.0f);
        g.setColour (C::text()); g.setFont (font (17.0f, true));
        g.drawText (ctx.proc.padName (p), info.reduced (12, 0).withTrimmedTop (24).withHeight (24), juce::Justification::centredLeft, true);
        g.setColour (C::dim()); g.setFont (font (12.0f));
        g.drawFittedText ("Click a pad to hear it (higher on the pad is harder). Its sound, effects and modulation are in the inspector on the right."
                          " Right-click a pad to load a sample or send it to a window; drop audio files onto pads.",
                          info.reduced (12, 0).withTrimmedTop (52).withHeight (64), juce::Justification::topLeft, 4);
        paintLabel (g, "STEPS | CLICK HIT | SHIFT ACCENT | ALT GHOST | RIGHT-CLICK ROLLS | WHEEL VELOCITY", { 14, 282, 780, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        auto top = juce::Rectangle<int> (12, 10, getWidth() - 24, 36);
        patternBox.setBounds (top.removeFromLeft (214).reduced (0, 3));
        prev.setBounds (top.removeFromLeft (34).reduced (2, 3)); next.setBounds (top.removeFromLeft (34).reduced (2, 3));
        save.setBounds (top.removeFromLeft (58).reduced (2, 3));
        clear.setBounds (top.removeFromLeft (62).reduced (2, 3));
        kitBox.setBounds (top.removeFromLeft (136).reduced (4, 3));
        lengths.setBounds (top.removeFromLeft (104).reduced (2, 0));
        modes.setBounds (top.reduced (2, 0));
        pads.setBounds (14, 56, 400, 218);
        swing.setBounds (650, 168, 70, 96);
        tempo.setBounds (726, 168, 70, 96);
        steps.setBounds (8, 296, getWidth() - 16, getHeight() - 304);
    }

    void refresh() override
    {
        patternBox.clear (juce::dontSendNotification);
        for (int i = 0; i < nb::numPatterns(); ++i) patternBox.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  " + nb::patternPreset (i).name, i + 1);
        const auto user = ctx.proc.userPresets ("Pattern");
        if (! user.isEmpty())
        {
            patternBox.addSeparator();
            for (int i = 0; i < user.size(); ++i) patternBox.addItem ("MINE | " + user[i], 1001 + i);
        }
        patternBox.setSelectedId (ctx.proc.currentPreset.load() + 1, juce::dontSendNotification);
        kitBox.setSelectedId (ctx.proc.currentKit.load() + 1, juce::dontSendNotification);
        const int len = ctx.proc.pattern.length.load();
        lengths.setSelected (len <= 16 ? 0 : len <= 32 ? 1 : 2);
        modes.setSelected ((int) ctx.proc.apvts.getRawParameterValue ("mode")->load());
        repaint(); pads.repaint(); steps.repaint();
    }

    void tick() override
    {
        pads.tick();
        swing.syncFromParam(); tempo.syncFromParam();
        const int s = ctx.proc.playStep.load();
        const auto v = ctx.proc.pattern.version.load();
        if (s != lastStep || v != lastVersion) { lastStep = s; lastVersion = v; steps.repaint(); }
        if (ctx.proc.currentPreset.load() + 1 != patternBox.getSelectedId() && patternBox.getSelectedId() < 1000) patternBox.setSelectedId (ctx.proc.currentPreset.load() + 1, juce::dontSendNotification);
    }

private:
    Ctx& ctx;
    PadGrid pads;
    StepGrid steps;
    juce::ComboBox patternBox, kitBox;
    Pill prev { "<" }, next { ">" }, save { "SAVE" }, clear { "CLEAR" };
    Segmented lengths, modes;
    Knob swing, tempo;
    int lastStep = -2;
    uint32_t lastVersion = 0;
};

std::unique_ptr<juce::Component> makeKitRoom (Ctx& c) { return std::make_unique<KitRoom> (c); }
} // namespace ui
