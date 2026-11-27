// NeoBeat 3.0 — the inspector: whatever is selected, in three tabs (SOUND, FX, MOD).
#include "UI.h"

namespace ui
{
// ------------------------------------------------------------------ a panel that flows its widgets
class FlowPanel final : public juce::Component
{
public:
    explicit FlowPanel (Ctx& c) : ctx (c) {}

    void section (const juce::String& s) { closeRow(); rows.push_back ({ Row::Section, {}, s, 22 }); }
    void text (const juce::String& s, int h = 34) { closeRow(); rows.push_back ({ Row::Text, {}, s, h }); }

    Knob& knob (const juce::String& label, float lo, float hi, float v, bool log, std::function<void (float)> set,
                const juce::String& dest = {}, std::function<juce::String (float)> fmt = {})
    {
        auto* k = own (new Knob (ctx, label, lo, hi, v, log));
        k->setValue (v);
        k->onChange = std::move (set);
        k->setDest (dest);
        if (fmt) k->toText = std::move (fmt);
        if (current.empty() || currentKind != Row::Knobs || current.size() >= 4) { closeRow(); currentKind = Row::Knobs; }
        current.push_back (k);
        return *k;
    }

    juce::ComboBox& combo (const juce::String& label, const juce::StringArray& items, int selected, std::function<void (int)> onPick)
    {
        closeRow();
        auto* c = own (new juce::ComboBox());
        c->addItemList (items, 1);
        c->setSelectedItemIndex (selected, juce::dontSendNotification);
        c->onChange = [c, onPick] { if (onPick) onPick (c->getSelectedItemIndex()); };
        rows.push_back ({ Row::Combo, { c }, label, 30 });
        return *c;
    }

    Pill& button (const juce::String& t, std::function<void()> fn, bool toggle = false, bool state = false)
    {
        auto* b = own (new Pill (t, toggle));
        b->setToggleState (state, juce::dontSendNotification);
        b->onClick = std::move (fn);
        if (currentKind != Row::Buttons || current.size() >= 3) { closeRow(); currentKind = Row::Buttons; }
        current.push_back (b);
        return *b;
    }

    void custom (juce::Component* c, int h) { closeRow(); own (c); rows.push_back ({ Row::Custom, { c }, {}, h }); }

    int contentHeight()
    {
        closeRow();
        int y = 4;
        for (auto& r : rows) y += r.h + 4;
        return y;
    }

    void resized() override
    {
        closeRow();
        int y = 4;
        const int w = getWidth();
        for (auto& r : rows)
        {
            r.y = y;
            if (r.kind == Row::Knobs)
            {
                const int kw = std::min (82, w / 4);
                int x = (w - kw * (int) r.items.size()) / 2;
                for (auto* c : r.items) { c->setBounds (x, y, kw, r.h); x += kw; }
            }
            else if (r.kind == Row::Buttons)
            {
                const int bw = (w - 8) / (int) r.items.size();
                int x = 4;
                for (auto* c : r.items) { c->setBounds (x, y + 2, bw - 6, r.h - 4); x += bw; }
            }
            else if (r.kind == Row::Combo) r.items[0]->setBounds (96, y + 2, w - 100, r.h - 4);
            else if (r.kind == Row::Custom) r.items[0]->setBounds (0, y, w, r.h);
            y += r.h + 4;
        }
    }

    void paint (juce::Graphics& g) override
    {
        for (const auto& r : rows)
        {
            if (r.kind == Row::Section) paintLabel (g, r.label, { 4, r.y + 8, getWidth() - 8, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
            if (r.kind == Row::Combo) paintLabel (g, r.label, { 4, r.y, 90, r.h }, juce::Justification::centredLeft, 7.5f);
            if (r.kind == Row::Text) { g.setColour (C::text()); g.setFont (font (12.0f)); g.drawFittedText (r.label, juce::Rectangle<int> (4, r.y, getWidth() - 8, r.h), juce::Justification::topLeft, 4, 0.9f); }
        }
    }

private:
    struct Row { enum Kind { Section, Text, Knobs, Combo, Buttons, Custom } kind; std::vector<juce::Component*> items; juce::String label; int h; int y = 0; };
    template <class T> T* own (T* c) { owned.add (c); addAndMakeVisible (c); return c; }
    void closeRow()
    {
        if (current.empty()) return;
        rows.push_back ({ currentKind, current, {}, currentKind == Row::Knobs ? 92 : 32 });
        current.clear();
    }
    Ctx& ctx;
    juce::OwnedArray<juce::Component> owned;
    std::vector<Row> rows;
    std::vector<juce::Component*> current;
    Row::Kind currentKind = Row::Knobs;
};

// one route: source, destination, depth, remove
class RouteRow final : public juce::Component
{
public:
    RouteRow (Ctx& c, int i) : ctx (c), index (i)
    {
        const auto& r = ctx.P().mod.routes[(size_t) i];
        slider.setSliderStyle (juce::Slider::LinearHorizontal); slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (-100, 100, 1); slider.setValue (r.depth * 100, juce::dontSendNotification);
        slider.setColour (juce::Slider::trackColourId, C::mod (r.src));
        slider.onValueChange = [this] { auto& rs = ctx.P().mod.routes; if (index < (int) rs.size()) { rs[(size_t) index].depth = (float) slider.getValue() / 100.0f; ctx.changed (false); repaint(); } };
        addAndMakeVisible (slider);
        remove.onClick = [this] { auto& rs = ctx.P().mod.routes; if (index < (int) rs.size()) { rs.erase (rs.begin() + index); ctx.changed(); } };
        addAndMakeVisible (remove);
    }
    void resized() override { slider.setBounds (170, 4, getWidth() - 214, 22); remove.setBounds (getWidth() - 38, 4, 32, 22); }
    void paint (juce::Graphics& g) override
    {
        const auto& rs = ctx.P().mod.routes;
        if (index >= (int) rs.size()) return;
        const auto& r = rs[(size_t) index];
        paintWell (g, getLocalBounds().toFloat().reduced (1), 8.0f);
        g.setColour (C::mod (r.src)); g.fillEllipse (8, 10, 9, 9);
        g.setColour (C::text()); g.setFont (font (11.0f, true));
        g.drawText (nb::modSrcName (r.src), 22, 2, 70, 13, juce::Justification::centredLeft);
        g.setFont (font (10.5f)); g.setColour (C::dim());
        g.drawText ("-> " + destLabel (r.dest), 22, 15, 146, 13, juce::Justification::centredLeft);
        g.setColour (C::text()); g.setFont (font (10.0f));
        g.drawText (juce::String (juce::roundToInt (r.depth * 100)) + "%", getWidth() - 74, 4, 34, 22, juce::Justification::centredRight);
    }
private:
    Ctx& ctx;
    int index;
    juce::Slider slider;
    Pill remove { "X" };
};

// ------------------------------------------------------------------ the inspector
class Inspector final : public juce::Component, public Part
{
public:
    explicit Inspector (Ctx& c) : ctx (c), tabs ({ "SOUND", "FX", "MOD" })
    {
        addAndMakeVisible (lcd);
        addAndMakeVisible (tabs);
        tabs.onSelect = [this] (int i) { ctx.tab = i; refresh(); };
        addAndMakeVisible (view);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (8);
    }

    void paint (juce::Graphics& g) override
    {
        paintPlate (g, getLocalBounds().toFloat());
        paintLabel (g, "INSPECTOR", { 14, 10, 120, 12 }, juce::Justification::centredLeft, 8.0f);
        g.setColour (C::dim()); g.setFont (font (10.5f));
        g.drawText (crumb, getWidth() - 214, 8, 200, 14, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        lcd.setBounds (12, 28, getWidth() - 24, 46);
        tabs.setBounds (12, 80, getWidth() - 24, 32);
        view.setBounds (8, 118, getWidth() - 12, getHeight() - 124);
        if (body) body->setSize (view.getWidth() - 10, std::max (body->contentHeight(), view.getHeight()));
    }

    void refresh() override
    {
        tabs.setSelected (ctx.tab);
        const int scroll = view.getViewPositionY();
        auto panel = std::make_unique<FlowPanel> (ctx);
        build (*panel);
        view.setViewedComponent (nullptr, false);
        body = std::move (panel);
        body->setSize (view.getWidth() - 10, std::max (body->contentHeight(), view.getHeight()));
        view.setViewedComponent (body.get(), false);
        view.setViewPosition (0, sameSel () ? scroll : 0);
        lastSel = ctx.sel; lastTab = ctx.tab;
        repaint();
    }

private:
    bool sameSel() const { return lastSel == ctx.sel && lastTab == ctx.tab; }

    void build (FlowPanel& f)
    {
        auto& P = ctx.P();
        const auto& s = ctx.sel;
        if (s.kind == Sel::Cell && s.index >= 0 && s.index < (int) P.matrix.oneShot.cells.size()) { buildCell (f, s.index); return; }
        if (s.kind == Sel::Node) { buildNode (f, s.index / nb::Pattern::kMaxSteps, s.index % nb::Pattern::kMaxSteps); return; }
        if (s.kind == Sel::SampleOp && s.index >= 0 && s.index < (int) P.matrix.sample.ops.size()) { buildOp (f, s.index); return; }
        if (s.kind == Sel::Mark && s.index >= 0 && s.index < (int) P.matrix.seq.marks.size()) { buildMark (f, s.index); return; }
        if (s.kind == Sel::Zone && s.index >= 0 && s.index < (int) P.keymap.zones.size()) { buildZone (f, s.index); return; }
        if (ctx.room == 1 && s.kind != Sel::Pad) { buildMatrixView (f); return; }
        if (ctx.room == 2 || s.kind == Sel::Grid) { buildGrid (f); return; }
        buildPad (f, ctx.pad);
    }

    void routes (FlowPanel& f, const juce::String& prefix, const juce::String& prefix2 = {})
    {
        f.section ("ROUTES | DROP A SOURCE FROM THE DOCK ON ANY KNOB");
        auto& rs = ctx.P().mod.routes;
        int n = 0;
        for (int i = 0; i < (int) rs.size(); ++i)
        {
            const juce::String d (rs[(size_t) i].dest);
            if (! d.startsWith (prefix) && (prefix2.isEmpty() || ! d.startsWith (prefix2))) continue;
            f.custom (new RouteRow (ctx, i), 30);
            ++n;
        }
        if (n == 0) f.text ("Nothing moves this yet. Drag LFO, envelope, random, velocity, key, height, step or XY from the dock onto a knob.", 46);
    }

    // knobs for an effect's settings; `get` returns the live CellFx (fetched again each time)
    void fxKnobs (FlowPanel& f, std::function<nb::CellFx*()> get, const juce::String& destPrefix, std::function<void()> openWindow)
    {
        auto* fx = get();
        if (fx == nullptr || fx->kind == nb::CellFx::None) return;
        auto ch = [this] { ctx.changed (false); };
        if (fx->kind == nb::CellFx::Library)
        {
            const auto& info = nb::matrixFxInfo()[(size_t) fx->fx];
            for (int k = 0; k < (int) info.params.size() && k < (int) fx->settings.p.size(); ++k)
            {
                const auto& q = info.params[(size_t) k];
                const bool lg = q.lo > 0.0f && q.hi / q.lo >= 50.0f;
                const juce::String unit (q.unit);
                f.knob (juce::String (q.name).toUpperCase(), q.lo, q.hi, fx->settings.p[(size_t) k], lg,
                        [get, k, ch] (float v) { if (auto* x = get()) if (k < (int) x->settings.p.size()) { x->settings.p[(size_t) k] = v; ch(); } }, {},
                        [unit] (float v) { return (std::fabs (v) >= 100 ? juce::String (juce::roundToInt (v)) : juce::String (v, std::fabs (v) < 10 ? 2 : 1)) + (unit.isEmpty() ? "" : " " + unit); });
            }
        }
        else if (fx->kind == nb::CellFx::FrictionStretchFx)
        {
            f.knob ("STRETCH", 1.0f, 64.0f, fx->stretch.stretch, true, [get, ch] (float v) { if (auto* x = get()) { x->stretch.stretch = v; ch(); } }, {}, [] (float v) { return "x" + juce::String (v, 1); });
            f.knob ("PEEL", 0, 1, fx->stretch.peel, false, [get, ch] (float v) { if (auto* x = get()) { x->stretch.peel = v; ch(); } });
            f.knob ("ROLL", 0, 1, fx->stretch.roll, false, [get, ch] (float v) { if (auto* x = get()) { x->stretch.roll = v; ch(); } });
            f.knob ("DECAY", 20, 4000, fx->stretch.decayMs, true, [get, ch] (float v) { if (auto* x = get()) { x->stretch.decayMs = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
            if (openWindow) f.button ("FULL VIEW | FRICTION WINDOW", openWindow);
        }
        else if (fx->kind == nb::CellFx::BounceDelayFx)
        {
            f.knob ("BOUNCE", 0.3f, 0.95f, fx->bounce.bounciness, false, [get, ch] (float v) { if (auto* x = get()) { x->bounce.bounciness = v; ch(); } });
            f.knob ("FAR", 1, 30, fx->bounce.distEnd, false, [get, ch] (float v) { if (auto* x = get()) { x->bounce.distEnd = v; ch(); } }, {}, [] (float v) { return juce::String (v, 1) + " m"; });
            f.knob ("SPEED", 0, 1, fx->bounce.speed, false, [get, ch] (float v) { if (auto* x = get()) { x->bounce.speed = v; ch(); } });
            f.knob ("PAN RAND", 0, 1, fx->bounce.panRandom, false, [get, ch] (float v) { if (auto* x = get()) { x->bounce.panRandom = v; ch(); } });
            if (openWindow) f.button ("FULL VIEW | BOUNCE WINDOW", openWindow);
        }
        juce::ignoreUnused (destPrefix);
    }

    // ---------------------------------------------------------------- a pad
    void buildPad (FlowPanel& f, int p)
    {
        crumb = "SEQUENCER > LANE " + juce::String (p + 1);
        lcd.set ("LANE " + juce::String (p + 1).paddedLeft ('0', 2) + " | " + ctx.proc.padName (p).toUpperCase(), ctx.proc.padGroup (p).toUpperCase());
        const juce::String dp = "pad/" + juce::String (p) + "/";
        if (ctx.tab == 0)
        {
            juce::StringArray names;
            for (const auto& sd : ctx.proc.library().sounds) names.add (juce::String (sd.group) + " | " + sd.name);
            auto* soundParam = ctx.proc.apvts.getParameter (ctx.proc.paramId (p, "sound"));
            f.combo ("SOUND", names, (int) ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "sound"))->load(), [this, p, soundParam] (int i)
            {
                if (soundParam) soundParam->setValueNotifyingHost (soundParam->convertTo0to1 ((float) i));
                ctx.proc.clearSample (p); ctx.proc.audition (p, 0.85f); if (ctx.refreshAll) ctx.refreshAll();
            });
            const char* ids[] = { "tune", "decay", "tone", "drive", "hollow", "evolve", "pan", "level", "width", "bands" };
            const char* labels[] = { "TUNE", "DECAY", "TONE", "DRIVE", "HOLLOW", "EVOLVE", "PAN", "LEVEL", "WIDTH", "BANDS" };
            for (int i = 0; i < 10; ++i)
            {
                auto* prm = ctx.proc.apvts.getParameter (ctx.proc.paramId (p, ids[i]));
                // BANDS is a send, not a sound parameter: no modulation destination for it (yet)
                auto& k = f.knob (labels[i], 0, 1, 0, false, nullptr, i == 9 ? juce::String() : dp + ids[i]);
                k.attach (prm);
                k.toText = [prm] (float v) { return prm != nullptr ? prm->getText (prm->convertTo0to1 (v), 8) + (prm->getLabel().isEmpty() ? "" : " " + prm->getLabel()) : juce::String (v); };
                if (i == 1) k.setRange (0.25f, 4.0f, true);
                if (i == 7) k.setTooltip ("Up to +24 dB, so a quiet recording can sit beside the stock sounds");
                if (i == 8) k.setTooltip ("Stereo: 0 is mono, 100 % is the sound as it is, 200 % is wide (the widening disappears in mono, it never thins the sound)");
                if (i == 9) k.setTooltip ("How much of this lane goes through BANDS, the spectral delay (open it from the header). "
                                          "At full the lane sounds as BANDS is set: its own sound at the Dry level, the bank over it at Wet. At 0 it is untouched.");
            }
            if (ctx.proc.padUsesSample (p))
            {
                f.button ("MATCH LEVEL", [this, p] { ctx.proc.matchLevel (p); if (ctx.refreshAll) ctx.refreshAll(); }).setTooltip ("Set LEVEL so this sample is as loud as the stock sounds");
                // your rating teaches NeoBeat (and Batchcut) what you like
                const int stars = juce::roundToInt (ctx.proc.laneRating (p));
                f.combo ("MY RATING", { "not rated", "1 star", "2 stars", "3 stars", "4 stars", "5 stars" }, juce::jlimit (0, 5, stars), [this, p] (int i) { ctx.proc.rateLane (p, (float) i); });
            }
            f.combo ("CHOKE", { "NONE", "GROUP 1", "GROUP 2", "GROUP 3", "GROUP 4" }, (int) ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "choke"))->load(),
                     [this, p] (int i) { ctx.proc.setParam (ctx.proc.paramId (p, "choke"), (float) i); });
            const bool muted = ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "mute"))->load() > 0.5f;
            f.button ("PLAY", [this, p] { ctx.proc.audition (p, 0.9f); });
            f.button ("MUTE", [this, p] { const auto id = ctx.proc.paramId (p, "mute"); ctx.proc.setParam (id, ctx.proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); if (ctx.refreshAll) ctx.refreshAll(); }, true, muted);
            f.button (ctx.proc.padUsesSample (p) ? "CLEAR SAMPLE" : "LOAD SAMPLE", [this, p]
            {
                if (ctx.proc.padUsesSample (p)) { ctx.proc.clearSample (p); if (ctx.refreshAll) ctx.refreshAll(); return; }
                chooser = std::make_unique<juce::FileChooser> ("Load a sample onto lane " + juce::String (p + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, p] (const juce::FileChooser& fc)
                { if (fc.getResult().existsAsFile() && ctx.proc.loadSample (p, fc.getResult())) { ctx.proc.matchLevel (p); ctx.proc.teachLane (p); if (ctx.refreshAll) ctx.refreshAll(); } });
            });
            f.text ("Every knob is a mod destination: drag a source from the dock onto it. The rings show what moves it.", 40);
        }
        else if (ctx.tab == 1)
        {
            f.section ("SIGNAL PATH | HIT > NODE'S EFFECT > INSERTS > WINDOWS > MIX");
            f.text ("Inserts are on every hit of this lane and run live: turn a knob and hear it now. (Friction Stretch is the one effect prepared ahead.)", 46);
            juce::StringArray items { "None" }; items.addArray (fxMenuNames());
            for (int i = 0; i < nb::kNumInserts; ++i)
            {
                auto get = [this, p, i] () -> nb::CellFx* { return &ctx.P().padFx[(size_t) p].inserts[(size_t) i]; };
                f.combo ("INSERT " + juce::String (i + 1), items, fxIndex (*get()) + 1, [this, p, i] (int sel)
                {
                    ctx.P().padFx[(size_t) p].inserts[(size_t) i] = sel <= 0 ? nb::CellFx() : fxFromIndex (sel - 1);
                    ctx.changed();
                });
                fxKnobs (f, get, {}, nullptr);
            }
            f.section ("WINDOWS LISTENING TO THIS LANE");
            auto& P = ctx.P();
            int n = 0;
            for (int s = 0; s < nb::kNumSlots; ++s)
                for (int w = 0; w < 2; ++w)
                {
                    const bool fs = w == 0;
                    const bool on = fs ? P.fs[(size_t) s].on : P.bd[(size_t) s].on;
                    const auto src = fs ? P.fs[(size_t) s].src : P.bd[(size_t) s].src;
                    if (! on || src.kind != nb::SourceKind::Pad || src.index != p) continue;
                    ++n;
                    f.knob ((fs ? "FRICTION " : "BOUNCE ") + juce::String (s + 1), 0, 1.5f, fs ? P.fs[(size_t) s].level : P.bd[(size_t) s].level, false,
                            [this, fs, s] (float v) { (fs ? ctx.P().fs[(size_t) s].level : ctx.P().bd[(size_t) s].level) = v; ctx.changed (false); });
                }
            if (n == 0) f.text ("None yet. Send this lane to a window and its hits feed it too.", 32);
            f.button ("SEND TO FRICTION", [this, p] { assign (true, p); });
            f.button ("SEND TO BOUNCE", [this, p] { assign (false, p); });
        }
        else
        {
            routes (f, dp, "pad/*/");
            f.section ("ALGORITHM | REAL TIME ON EVERY HIT");
            auto& a = ctx.P().mod.algos[(size_t) p];
            juce::StringArray types; for (int t = 0; t <= 5; ++t) types.add (nb::padAlgoName (t));
            f.combo ("TYPE", types, a.type, [this, p] (int t) { ctx.P().mod.algos[(size_t) p].type = t; ctx.changed(); });
            if (a.type > 0)
            {
                f.knob ("AMOUNT", 0, 1, a.amount, false, [this, p] (float v) { ctx.P().mod.algos[(size_t) p].amount = v; ctx.changed (false); });
                f.knob ("MIX", 0, 1, a.mix, false, [this, p] (float v) { ctx.P().mod.algos[(size_t) p].mix = v; ctx.changed (false); });
                f.knob ("BY SOURCE", -1, 1, a.srcDepth, false, [this, p] (float v) { ctx.P().mod.algos[(size_t) p].srcDepth = v; ctx.changed (false); });
                juce::StringArray srcs; for (int s = 0; s < (int) nb::ModSrc::Count; ++s) srcs.add (nb::modSrcName ((nb::ModSrc) s));
                f.combo ("SOURCE", srcs, (int) a.src, [this, p] (int s) { ctx.P().mod.algos[(size_t) p].src = (nb::ModSrc) s; ctx.changed (false); });
            }
            juce::StringArray pres { "Presets..." };
            for (const auto& ap : nb::algoPresets()) pres.add (ap.name);
            f.combo ("PRESET", pres, 0, [this, p] (int i) { if (i > 0) { ctx.P().mod.algos[(size_t) p] = nb::algoPresets()[(size_t) i - 1].algo; ctx.changed(); } });
        }
    }

    void assign (bool fs, int p)
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

    // ---------------------------------------------------------------- a node that is sent to an effect
    void buildCell (FlowPanel& f, int i)
    {
        auto& c = ctx.P().matrix.oneShot.cells[(size_t) i];
        const juce::String sceneName = juce::String::charToString ((juce::juce_wchar) ('A' + c.scene));
        crumb = "SCENE " + sceneName + " > LANE " + juce::String (c.pad + 1) + " > NODE";
        lcd.set ("NODE > " + fxName (c.fx).toUpperCase(), "SCENE " + sceneName + " | LANE " + juce::String (c.pad + 1) + " | STEP " + juce::String (c.step / 16 + 1) + "." + juce::String (c.step % 16 + 1) + " | REACH " + juce::String (c.span) + " | HEIGHT " + juce::String (c.height, 2));
        auto get = [this, i] () -> nb::CellFx* { auto& cs = ctx.P().matrix.oneShot.cells; return i < (int) cs.size() ? &cs[(size_t) i].fx : nullptr; };
        if (ctx.tab == 2) { routes (f, "cell/" + juce::String (i) + "/"); return; }
        juce::StringArray items { "Init (no effect)" }; items.addArray (fxMenuNames());
        const int lane = c.pad, step = c.step;
        f.combo ("SEND NODE TO", items, fxIndex (c.fx) + 1, [this, lane, step] (int sel) { sendNode (ctx, lane, step, sel - 1); });
        const auto mc = nb::mainControl (c.fx);
        auto& h = f.knob ("HEIGHT", 0, 1, c.height, false, [this, i] (float v) { auto& cs = ctx.P().matrix.oneShot.cells; if (i < (int) cs.size()) { cs[(size_t) i].height = v; ctx.changed (false); } },
                          "cell/" + juce::String (i) + "/main", [mc] (float v) { return juce::String (mc.valueAt (v), mc.hi > 50 ? 0 : 2); });
        h.setMain (true);
        h.setTooltip ("Height sets the main control: " + juce::String (mc.name));
        f.knob ("REACH", 1, 64, (float) c.span, false, [this, i] (float v) { auto& cs = ctx.P().matrix.oneShot.cells; if (i < (int) cs.size()) { cs[(size_t) i].span = juce::roundToInt (v); ctx.changed (false); } }, {},
                [] (float v) { return juce::String (juce::roundToInt (v)) + " steps"; });
        f.text (c.fx.kind == nb::CellFx::FrictionStretchFx
                    ? juce::String ("Friction Stretch is prepared ahead: a change is in after a moment (PREPARING, top right). HEIGHT is its ") + mc.name + "."
                    : "Live: every knob here is heard as you turn it, also on a hit that is still ringing. HEIGHT is " + juce::String (mc.name) + "; REACH is how far the effect rings past the hit.", 48);
        f.section ("SETTINGS");
        fxKnobs (f, get, {}, [this, i, get] { if (auto* x = get()) ctx.openWindow (x->kind == nb::CellFx::FrictionStretchFx, 0, i); });
        f.button ("PLAY THE HIT", [this, i] { auto& cs = ctx.P().matrix.oneShot.cells; if (i < (int) cs.size()) ctx.proc.audition (cs[(size_t) i].pad, 0.9f); });
        f.button ("BACK TO INIT", [this, lane, step] { sendNode (ctx, lane, step, -1); });
    }

    // ---------------------------------------------------------------- a node as it starts: Init (no effect)
    void buildNode (FlowPanel& f, int lane, int step)
    {
        const auto st = ctx.proc.pattern.get (lane, step);
        const juce::String sceneName = juce::String::charToString ((juce::juce_wchar) ('A' + ctx.scene()));
        crumb = "SCENE " + sceneName + " > LANE " + juce::String (lane + 1) + " > NODE";
        lcd.set ("NODE > INIT", "SCENE " + sceneName + " | LANE " + juce::String (lane + 1) + " | STEP " + juce::String (step / 16 + 1) + "." + juce::String (step % 16 + 1) + (st.vel == 0 ? " | EMPTY" : " | VELOCITY " + juce::String ((int) st.vel)));
        if (ctx.tab == 2) { f.text ("Send the node to an effect first: its main control is then a destination for the dock's sources.", 46); return; }
        juce::StringArray items { "Init (no effect)" }; items.addArray (fxMenuNames());
        f.combo ("SEND NODE TO", items, 0, [this, lane, step] (int sel) { if (sel > 0) sendNode (ctx, lane, step, sel - 1); });
        f.text ("Init: this node plays its lane's sound as it is. Send it to an effect and that effect is on this one hit only.", 46);
        f.section ("THE HIT");
        auto setStep = [this, lane, step] (std::function<void (nb::Step&)> fn) { auto s2 = ctx.proc.pattern.get (lane, step); fn (s2); ctx.proc.pattern.set (lane, step, s2); };
        f.knob ("VELOCITY", 0, 127, (float) st.vel, false, [setStep] (float v) { setStep ([v] (nb::Step& x) { x.vel = (uint8_t) juce::roundToInt (v); }); }, {}, [] (float v) { return juce::roundToInt (v) == 0 ? juce::String ("off") : juce::String (juce::roundToInt (v)); });
        f.knob ("NUDGE", -50, 50, (float) st.nudge, false, [setStep] (float v) { setStep ([v] (nb::Step& x) { x.nudge = (int8_t) juce::roundToInt (v); }); }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
        f.combo ("ROLL", { "Single hit", "Ratchet x2", "Ratchet x3", "Ratchet x4", "Triplet roll (two steps)" }, st.roll == 0 ? 0 : st.roll - 1,
                 [setStep] (int i) { setStep ([i] (nb::Step& x) { x.roll = (uint8_t) (i == 0 ? 0 : i + 1); if (x.vel == 0) x.vel = 96; }); });
        f.button ("PLAY THE HIT", [this, lane] { ctx.proc.audition (lane, 0.9f); });
        f.button ("COPY", [this, lane, step] { copyNode (ctx, lane, step); });
        f.button ("PASTE", [this, lane, step] { pasteNode (ctx, lane, step); if (ctx.refreshAll) ctx.refreshAll(); });
    }

    // ---------------------------------------------------------------- a Sample view box
    void buildOp (FlowPanel& f, int i)
    {
        auto& o = ctx.P().matrix.sample.ops[(size_t) i];
        crumb = "SAMPLE > BOX " + juce::String (i + 1);
        const float lo = std::min (o.fLo, o.fHi), hi = std::max (o.fLo, o.fHi);
        const float height = juce::jlimit (0.0f, 1.0f, std::log2 (hi / std::max (20.0f, lo)) / std::log2 (16000.0f / 30.0f));
        const auto mc = nb::mainControl (o.fx);
        lcd.set ("BOX " + juce::String (i + 1) + " | " + fxName (o.fx).toUpperCase(), "SLICES " + juce::String (std::min (o.c0, o.c1) + 1) + "-" + juce::String (std::max (o.c0, o.c1)) + " | " + juce::String (juce::roundToInt (lo)) + "-" + juce::String (juce::roundToInt (hi)) + " HZ");
        if (ctx.tab == 2) { routes (f, "sample/"); return; }
        auto get = [this, i] () -> nb::CellFx* { auto& os = ctx.P().matrix.sample.ops; return i < (int) os.size() ? &os[(size_t) i].fx : nullptr; };
        auto op = [this, i] () -> nb::SampleOp* { auto& os = ctx.P().matrix.sample.ops; return i < (int) os.size() ? &os[(size_t) i] : nullptr; };
        f.combo ("EFFECT", fxMenuNames(), fxIndex (o.fx), [this, op] (int sel) { if (auto* x = op()) { x->fx = fxFromIndex (sel); ctx.changed(); } });
        f.text ("The box's height (its band, in octaves) is the main control: " + juce::String (mc.name) + " = " + juce::String (mc.valueAt (height), mc.hi > 50 ? 0 : 2) + ". Taller boxes go further; the rest of the sound passes untouched.", 48);
        f.knob ("BAND LOW", 20, 18000, lo, true, [this, op] (float v) { if (auto* x = op()) { x->fLo = v; ctx.changed (false); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; });
        f.knob ("BAND HIGH", 30, 20000, hi, true, [this, op] (float v) { if (auto* x = op()) { x->fHi = v; ctx.changed (false); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; });
        f.knob ("MIX", 0, 1, o.mix, false, [this, op] (float v) { if (auto* x = op()) { x->mix = v; ctx.changed (false); } });
        f.knob ("FROM", 0, 31, (float) std::min (o.c0, o.c1), false, [this, op] (float v) { if (auto* x = op()) { x->c0 = juce::roundToInt (v); if (x->c1 <= x->c0) x->c1 = x->c0 + 1; ctx.changed (false); } }, {}, [] (float v) { return "slice " + juce::String (juce::roundToInt (v) + 1); });
        f.knob ("TO", 1, 32, (float) std::max (o.c0, o.c1), false, [this, op] (float v) { if (auto* x = op()) { x->c1 = juce::roundToInt (v); if (x->c1 <= x->c0) x->c0 = x->c1 - 1; ctx.changed (false); } }, {}, [] (float v) { return "slice " + juce::String (juce::roundToInt (v)); });
        f.section ("SETTINGS");
        fxKnobs (f, get, {}, nullptr);
        f.button ("DELETE BOX", [this, i] { auto& os = ctx.P().matrix.sample.ops; if (i < (int) os.size()) { os.erase (os.begin() + i); ctx.select ({ Sel::None, 0 }); ctx.changed(); } });
    }

    // ---------------------------------------------------------------- a drawn mark
    void buildMark (FlowPanel& f, int i)
    {
        auto& P = ctx.P().matrix.seq;
        auto& m = P.marks[(size_t) i];
        const char* types[] = { "DOT", "DASH", "WARBLE", "TICK", "SPRAY" };
        crumb = "DRAW > MARK " + juce::String (i + 1);
        lcd.set ("MARK " + juce::String (i + 1) + " | " + types[(int) m.type], noteName (nb::rowNote (P, m.row)) + " | STEP " + juce::String (m.start + 1, 1) + " | " + juce::String (m.length, 1) + " STEPS");
        if (ctx.tab == 2) { routes (f, "seq/"); return; }
        auto mk = [this, i] () -> nb::Mark* { auto& ms = ctx.P().matrix.seq.marks; return i < (int) ms.size() ? &ms[(size_t) i] : nullptr; };
        f.combo ("TYPE", { "Dot (sparkle)", "Dash (drone)", "Warble", "Tick", "Spray (grain cloud)" }, (int) m.type, [this, mk] (int t) { if (auto* x = mk()) { x->type = (nb::MarkType) t; ctx.changed(); } });
        auto ch = [this] { ctx.changed (false); };
        f.knob ("INTENSITY", 0, 1, m.intensity, false, [mk, ch] (float v) { if (auto* x = mk()) { x->intensity = v; ch(); } });
        f.knob ("INERTIA", 0, 0.5f, m.inertia, false, [mk, ch] (float v) { if (auto* x = mk()) { x->inertia = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v * 1000)) + " ms"; });
        f.knob ("LEVEL", 0, 1, m.level, false, [mk, ch] (float v) { if (auto* x = mk()) { x->level = v; ch(); } });
        f.knob ("PAN", -1, 1, m.pan, false, [mk, ch] (float v) { if (auto* x = mk()) { x->pan = v; ch(); } });
        f.knob ("DEPTH", 0, 200, m.depth, false, [mk, ch] (float v) { if (auto* x = mk()) { x->depth = v; ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; });
        f.knob ("RATE", 0.1f, 20, m.rate, true, [mk, ch] (float v) { if (auto* x = mk()) { x->rate = v; ch(); } }, {}, [] (float v) { return juce::String (v, 1) + " Hz"; });
        f.knob ("LENGTH", 0.25f, 64, m.length, true, [mk, ch] (float v) { if (auto* x = mk()) { x->length = v; ch(); } }, {}, [] (float v) { return juce::String (v, 2) + " st"; });
        f.knob ("ROW", 0, (float) nb::SeqPreset::kRows - 1, (float) m.row, false, [this, mk, ch] (float v) { if (auto* x = mk()) { x->row = juce::roundToInt (v); ch(); } }, {},
                [this] (float v) { return noteName (nb::rowNote (ctx.P().matrix.seq, juce::roundToInt (v))); });
        f.button ("DELETE MARK", [this, i] { auto& ms = ctx.P().matrix.seq.marks; if (i < (int) ms.size()) { ms.erase (ms.begin() + i); ctx.select ({ Sel::None, 0 }); ctx.changed(); } });
    }

    // ---------------------------------------------------------------- a Matrixxxer view (nothing selected)
    void buildMatrixView (FlowPanel& f)
    {
        auto& M = ctx.P().matrix;
        const int v = M.view;
        if (v == 0)
        {
            auto& S = M.seq;
            crumb = "MATRIXXXER > DRAW";
            lcd.set ("DRAW | " + juce::String (S.name).toUpperCase(), juce::String (S.marks.size()) + " MARKS | " + juce::String (nb::scaleNames()[(size_t) S.scale]).toUpperCase() + " | " + juce::String (S.bars) + " BARS");
            if (ctx.tab == 2) { routes (f, "seq/"); return; }
            f.combo ("ENGINE", { "ADD | additive partials", "FM | two operators", "FFT | harmonic bands of noise", "SMP | grains of a pad" }, (int) S.engine,
                     [this] (int i) { ctx.P().matrix.seq.engine = (nb::MarkEngine) i; ctx.changed(); });
            juce::StringArray sc; for (const auto& s : nb::scaleNames()) sc.add (s);
            f.combo ("SCALE", sc, S.scale, [this] (int i) { ctx.P().matrix.seq.scale = i; ctx.changed(); });
            f.combo ("ROOT", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, S.root, [this] (int i) { ctx.P().matrix.seq.root = i; ctx.changed(); });
            f.combo ("DIVISION", { "1/16 (swung)", "1/16 triplets", "1/8 triplets", "1/32" }, S.grid, [this] (int i) { ctx.P().matrix.seq.grid = i; ctx.changed(); });
            auto ch = [this] { ctx.changed (false); };
            f.knob ("BARS", 1, 32, (float) S.bars, false, [this, ch] (float x) { ctx.P().matrix.seq.bars = juce::roundToInt (x); ch(); }, {}, [] (float x) { return juce::String (juce::roundToInt (x)); });
            f.knob ("OCTAVE", 1, 6, (float) S.baseOctave, false, [this, ch] (float x) { ctx.P().matrix.seq.baseOctave = juce::roundToInt (x); ch(); }, {}, [] (float x) { return "C" + juce::String (juce::roundToInt (x) - 2); });
            f.knob ("LEVEL", 0, 1, S.level, false, [this, ch] (float x) { ctx.P().matrix.seq.level = x; ch(); }, "seq/level");
            auto& pan = f.knob ("PAN MOD", -1, 1, 0, false, nullptr, "seq/pan"); pan.setTooltip ("A mod destination: drop a source here to move every mark's pan");
            auto& pit = f.knob ("PITCH MOD", -1, 1, 0, false, nullptr, "seq/pitch"); pit.setTooltip ("A mod destination: drop a source here to bend every mark");
            if (S.engine == nb::MarkEngine::Smp)
            {
                f.knob ("GRAIN", 5, 200, S.grainMs, true, [this, ch] (float x) { ctx.P().matrix.seq.grainMs = x; ch(); }, {}, [] (float x) { return juce::String (juce::roundToInt (x)) + " ms"; });
                f.knob ("SPRAY", 0, 1, S.spray, false, [this, ch] (float x) { ctx.P().matrix.seq.spray = x; ch(); });
                juce::StringArray padsList; for (int p = 0; p < 16; ++p) padsList.add ("Lane " + juce::String (p + 1) + " | " + ctx.proc.padName (p));
                f.combo ("GRAINS OF", padsList, std::max (0, S.samplePad), [this] (int i) { ctx.P().matrix.seq.samplePad = i; ctx.changed(); });
            }
            f.button (S.mirror ? "MIRROR MAIN RHYTHM: ON" : "MIRROR MAIN RHYTHM", [this] { auto& s2 = ctx.P().matrix.seq; s2.mirror = ! s2.mirror; ctx.changed(); }, true, S.mirror);
            f.button (M.seqOn ? "VIEW ON" : "VIEW OFF", [this] { ctx.P().matrix.seqOn = ! ctx.P().matrix.seqOn; ctx.changed(); }, true, M.seqOn);
            f.text ("Pick a brush and draw on the canvas: click for dots and ticks, drag for dashes, warbles and sprays. Click a mark to edit it here.", 48);
        }
        else if (v == 1)
        {
            crumb = "MATRIXXXER > ONE SHOT";
            lcd.set ("ONE SHOT | " + juce::String (M.oneShot.name).toUpperCase(), juce::String (M.oneShot.cells.size()) + " NODES SENT");
            if (ctx.tab == 2) { routes (f, "cell/"); return; }
            f.button (M.oneShotOn ? "VIEW ON" : "VIEW OFF", [this] { ctx.P().matrix.oneShotOn = ! ctx.P().matrix.oneShotOn; ctx.changed(); }, true, M.oneShotOn);
            f.button ("ALL BACK TO INIT", [this] { ctx.P().matrix.oneShot.cells.clear(); ctx.changed(); });
            f.text ("Pick an effect in the brush, then click a node to send it there (or right-click any node: Send node to). Drag its right edge for its reach, drag it up or down for its height (the main control). Click it to edit it here.", 76);
        }
        else
        {
            auto& S = M.sample;
            crumb = "MATRIXXXER > SAMPLE";
            lcd.set ("SAMPLE | " + juce::String (S.name).toUpperCase(), juce::String (S.ops.size()) + " BOXES | PLAYS ON LANE " + juce::String (S.pad + 1));
            if (ctx.tab == 2) { routes (f, "sample/"); return; }
            juce::StringArray padsList; for (int p = 0; p < 16; ++p) padsList.add ("Lane " + juce::String (p + 1) + " | " + ctx.proc.padName (p));
            f.combo ("PLAYS ON", padsList, S.pad, [this] (int i) { ctx.P().matrix.sample.pad = i; ctx.changed(); });
            f.text (S.file.empty() ? juce::String ("The sample is that lane's own hit. Load a file to mangle any sound instead.") : "The sample: " + juce::File (S.file).getFileName(), 34);
            f.button ("LOAD FILE", [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("A sample to mangle", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile() && ctx.proc.loadFile (fc.getResult())) { ctx.P().matrix.sample.file = fc.getResult().getFullPathName().toStdString(); ctx.changed(); }
                });
            });
            f.button ("USE THE LANE'S HIT", [this] { ctx.P().matrix.sample.file.clear(); ctx.changed(); });
            f.button (M.sampleOn ? "VIEW ON" : "VIEW OFF", [this] { ctx.P().matrix.sampleOn = ! ctx.P().matrix.sampleOn; ctx.changed(); }, true, M.sampleOn);
            f.button ("CLEAR BOXES", [this] { ctx.P().matrix.sample.ops.clear(); ctx.changed(); });
            f.text ("Drag on the spectrogram to draw a box: its time span and band are where the brush's effect acts. A taller box (a wider band) pushes the effect further.", 62);
        }
    }

    // ---------------------------------------------------------------- Layermax
    void buildGrid (FlowPanel& f)
    {
        auto& G = ctx.P().grid;
        auto& pat = G.patterns[(size_t) G.current];
        crumb = "LAYERMAX > PATTERN " + juce::String (G.current + 1);
        lcd.set ("LAYERMAX | PATTERN " + juce::String (G.current + 1), G.useKit || G.files.empty() ? juce::String ("ROWS PLAY THE 16 LANES' SOUNDS") : juce::String (G.files.size()) + " SAMPLES | " + juce::File (G.folder).getFileName().toUpperCase());
        if (ctx.tab == 2) { routes (f, "grid/"); return; }
        auto ch = [this] { ctx.changed (false); };
        if (ctx.tab == 1)
        {   // a row that follows a lane of the sequencer
            const int row = juce::jlimit (0, nb::GridPattern::kSize - 1, ctx.gridRow);
            auto fol = [this, row] () -> nb::GridFollow& { return ctx.P().grid.follow[(size_t) row]; };
            const auto& fw = fol();
            f.section ("ROW " + juce::String (row + 1) + " FOLLOWS THE SEQUENCER");
            f.knob ("ROW", 1, 64, (float) (row + 1), false, [this] (float v) { const int r = juce::roundToInt (v) - 1; if (r != ctx.gridRow) { ctx.gridRow = r; if (ctx.refreshAll) ctx.refreshAll(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)); });
            f.button (fw.on ? "FOLLOWING" : "FOLLOW A LANE", [this, fol] { fol().on = ! fol().on; ctx.changed(); }, true, fw.on);
            juce::StringArray lanes; for (int p = 0; p < 16; ++p) lanes.add ("Lane " + juce::String (p + 1) + " | " + ctx.proc.padName (p));
            f.combo ("LANE", lanes, fw.lane, [this, fol] (int i) { fol().lane = i; ctx.changed (false); });
            f.combo ("PLAYS", { "With every hit (layer)", "In the gaps the lane leaves (fill)", "With every Nth hit" }, fw.mode, [this, fol] (int i) { fol().mode = i; ctx.changed(); });
            if (fw.mode == 2) f.knob ("EVERY", 2, 16, (float) fw.nth, false, [fol, ch] (float v) { fol().nth = juce::roundToInt (v); ch(); }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + ". hit"; });
            f.knob ("OFFSET", -120, 120, fw.offsetMs, false, [fol, ch] (float v) { fol().offsetMs = std::fabs (v) < 1.0f ? 0.0f : v; ch(); }, {}, [] (float v) { return std::fabs (v) < 1.0f ? juce::String ("on it") : juce::String (juce::roundToInt (v)) + " ms"; });
            f.knob ("LEVEL", 0, 1.5f, fw.level, false, [fol, ch] (float v) { fol().level = v; ch(); });
            f.knob ("PAN", -1, 1, fw.pan, false, [fol, ch] (float v) { fol().pan = v; ch(); });
            f.knob ("PITCH", -24, 24, fw.semis, false, [fol, ch] (float v) { fol().semis = std::round (v); ch(); }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " st"; });
            f.button ("AS HARD AS THE HIT", [this, fol] { fol().velocity = ! fol().velocity; ctx.changed(); }, true, fw.velocity);
            f.button ("UNLINK AND KEEP", [this, row] { bakeFollow (row); }).setTooltip ("Write what this row plays into the Layermax pattern (16ths, the scene shown) and stop following");
            int n = 0; juce::String who;
            for (int r = 0; r < (int) G.follow.size(); ++r) if (G.follow[(size_t) r].on) { ++n; if (who.length() < 60) who << (who.isEmpty() ? "" : ", ") << "row " << (r + 1) << " > lane " << (G.follow[(size_t) r].lane + 1); }
            f.text (n == 0 ? juce::String ("No row follows a lane yet. A following row plays its sound with (or between) that lane's hits, in every scene, whether or not Layermax's own pattern is playing, even when the lane is muted.")
                           : "Following now: " + who + ".", 62);
            return;
        }
        f.combo ("RATE", { "1/8 (swung)", "1/16 (swung)", "1/16 triplets", "1/32" }, pat.rate, [this] (int i) { auto& g = ctx.P().grid; g.patterns[(size_t) g.current].rate = i; ctx.changed(); });
        f.knob ("LENGTH", 1, 64, (float) pat.length, false, [this, ch] (float v) { auto& g = ctx.P().grid; g.patterns[(size_t) g.current].length = juce::roundToInt (v); ch(); }, {}, [] (float v) { return juce::String (juce::roundToInt (v)) + " cols"; });
        f.knob ("SWING", 0.49f, 0.75f, pat.swing < 0 ? 0.49f : pat.swing, false, [this, ch] (float v) { auto& g = ctx.P().grid; g.patterns[(size_t) g.current].swing = v < 0.5f ? -1.0f : v; ch(); }, {},
                [] (float v) { return v < 0.5f ? juce::String ("main") : juce::String (juce::roundToInt (v * 100)) + " %"; });
        f.knob ("LEVEL", 0, 1.5f, G.level, false, [this, ch] (float v) { ctx.P().grid.level = v; ch(); }, "grid/level");
        auto& pm = f.knob ("PITCH MOD", -1, 1, 0, false, nullptr, "grid/pitch"); pm.setTooltip ("A mod destination: drop a source here to bend Layermax");
        f.text ("Paint boxes: red is volume, green is pan, blue is pitch. Tag boxes FS or BD and they also feed the Friction or Bounce window listening to their row. The FX tab makes a row follow a lane of the sequencer.", 62);
    }

    // what a following row plays, written into the pattern (so it can be edited box by box)
    void bakeFollow (int row)
    {
        auto& G = ctx.P().grid;
        auto& fw = G.follow[(size_t) row];
        auto& pat = G.patterns[(size_t) G.current];
        const int len = std::min (nb::GridPattern::kSize, ctx.proc.pattern.length());
        pat.rate = 1; pat.length = std::max (pat.length, len);
        int ordinal = 0;
        for (int s = 0; s < len; ++s)
        {
            const auto st = ctx.proc.pattern.get (fw.lane, s);
            bool on = false; float vel = 0.8f;
            if (fw.mode == 1) on = st.vel == 0;
            else if (st.vel > 0) { on = fw.mode == 0 || ordinal % std::max (1, fw.nth) == 0; ++ordinal; vel = fw.velocity ? st.vel / 127.0f : 0.8f; }
            if (! on) continue;
            const int red = juce::jlimit (1, 255, juce::roundToInt (255.0f * std::pow (juce::jlimit (0.0f, 1.0f, fw.level * vel / 0.8f), 1.0f / 1.5f)));
            pat.cells[(size_t) (row * nb::GridPattern::kSize + s)] = nb::GridPattern::pack (red, juce::jlimit (0, 255, 128 + juce::roundToInt (fw.pan * 127.0f)), juce::jlimit (0, 255, 128 + juce::roundToInt (fw.semis / 12.0f * 128.0f)), nb::GridPattern::kOn);
        }
        fw.on = false; G.play = true;
        ctx.changed();
    }

    // ---------------------------------------------------------------- a key-map zone
    void buildZone (FlowPanel& f, int i)
    {
        auto& K = ctx.P().keymap;
        auto& z = K.zones[(size_t) i];
        crumb = "KEY MAP > ZONE " + juce::String (i + 1);
        lcd.set ("ZONE | " + juce::String (z.name).toUpperCase(), noteName (z.loKey) + "-" + noteName (z.hiKey) + " | VELOCITY " + juce::String (z.loVel) + "-" + juce::String (z.hiVel) + " | ROOT " + noteName (z.root));
        auto zn = [this, i] () -> nb::Zone* { auto& zs = ctx.P().keymap.zones; return i < (int) zs.size() ? &zs[(size_t) i] : nullptr; };
        if (ctx.tab == 2) { routes (f, "pad/" + juce::String (z.index) + "/"); return; }
        if (ctx.tab == 1)
        {
            juce::StringArray items { "None" }; items.addArray (fxMenuNames());
            f.combo ("EFFECT", items, fxIndex (z.fx) + 1, [this, zn] (int s) { if (auto* x = zn()) { x->fx = s <= 0 ? nb::CellFx() : fxFromIndex (s - 1); ctx.changed(); } });
            f.text ("A zone's effect is only on the hits that land in this zone (hard hits freeze, soft ones don't). It runs live.", 40);
            fxKnobs (f, [zn] () -> nb::CellFx* { auto* x = zn(); return x ? &x->fx : nullptr; }, {}, nullptr);
            return;
        }
        f.combo ("PLAYS", { "A lane", "A sample file", "A whole kit (across the keys)", "A node and its effect", "A Layermax pattern (while held)" }, (int) z.src,
                 [this, zn] (int s) { if (auto* x = zn()) { x->src = (nb::ZoneSource) s; x->index = 0; ctx.changed(); } });
        juce::StringArray idx;
        switch (z.src)
        {
            case nb::ZoneSource::Pad: for (int p = 0; p < 16; ++p) idx.add ("Lane " + juce::String (p + 1) + " | " + ctx.proc.padName (p)); break;
            case nb::ZoneSource::Kit: for (const auto& k : nb::kits()) idx.add (k.name); break;
            case nb::ZoneSource::OneShot: for (int c = 0; c < (int) ctx.P().matrix.oneShot.cells.size(); ++c) idx.add (sourceLabel (ctx.P(), { nb::SourceKind::Cell, c })); break;
            case nb::ZoneSource::GridPattern: for (int g = 0; g < 16; ++g) idx.add ("Layermax pattern " + juce::String (g + 1)); break;
            default: break;
        }
        if (! idx.isEmpty()) f.combo ("WHICH", idx, z.index, [this, zn] (int s) { if (auto* x = zn()) { x->index = s; ctx.changed(); } });
        if (z.src == nb::ZoneSource::Sample)
        {
            f.text (z.file.empty() ? juce::String ("No file yet.") : juce::File (z.file).getFileName(), 22);
            f.button ("LOAD FILE", [this, zn]
            {
                chooser = std::make_unique<juce::FileChooser> ("A sample for this zone", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, zn] (const juce::FileChooser& fc)
                {
                    if (auto* x = zn()) if (fc.getResult().existsAsFile() && ctx.proc.loadFile (fc.getResult())) { x->file = fc.getResult().getFullPathName().toStdString(); x->name = fc.getResult().getFileNameWithoutExtension().toStdString(); ctx.changed(); }
                });
            });
        }
        auto ch = [this] { ctx.changed (false); };
        auto noteFmt = [] (float v) { return noteName (juce::roundToInt (v)); };
        f.knob ("LOW KEY", 0, 127, (float) z.loKey, false, [zn, ch] (float v) { if (auto* x = zn()) { x->loKey = juce::roundToInt (v); x->hiKey = std::max (x->hiKey, x->loKey); ch(); } }, {}, noteFmt);
        f.knob ("HIGH KEY", 0, 127, (float) z.hiKey, false, [zn, ch] (float v) { if (auto* x = zn()) { x->hiKey = juce::roundToInt (v); x->loKey = std::min (x->hiKey, x->loKey); ch(); } }, {}, noteFmt);
        f.knob ("ROOT", 0, 127, (float) z.root, false, [zn, ch] (float v) { if (auto* x = zn()) { x->root = juce::roundToInt (v); ch(); } }, {}, noteFmt);
        f.knob ("LOW VEL", 1, 127, (float) z.loVel, false, [zn, ch] (float v) { if (auto* x = zn()) { x->loVel = juce::roundToInt (v); x->hiVel = std::max (x->hiVel, x->loVel); ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)); });
        f.knob ("HIGH VEL", 1, 127, (float) z.hiVel, false, [zn, ch] (float v) { if (auto* x = zn()) { x->hiVel = juce::roundToInt (v); x->loVel = std::min (x->hiVel, x->loVel); ch(); } }, {}, [] (float v) { return juce::String (juce::roundToInt (v)); });
        f.knob ("TUNE", -24, 24, z.tune, false, [zn, ch] (float v) { if (auto* x = zn()) { x->tune = v; ch(); } }, {}, [] (float v) { return juce::String (v, 1) + " st"; });
        f.knob ("GAIN", -24, 12, z.gainDb, false, [zn, ch] (float v) { if (auto* x = zn()) { x->gainDb = v; ch(); } }, {}, [] (float v) { return juce::String (v, 1) + " dB"; });
        f.knob ("PAN", -1, 1, z.pan, false, [zn, ch] (float v) { if (auto* x = zn()) { x->pan = v; ch(); } });
        f.knob ("ROUND ROBIN", 0, 8, (float) z.rrGroup, false, [zn, ch] (float v) { if (auto* x = zn()) { x->rrGroup = juce::roundToInt (v); ch(); } }, {}, [] (float v) { const int g = juce::roundToInt (v); return g == 0 ? juce::String ("off") : "group " + juce::String (g); });
        f.knob ("CHOKE", 0, 4, (float) z.choke, false, [zn, ch] (float v) { if (auto* x = zn()) { x->choke = juce::roundToInt (v); ch(); } }, {}, [] (float v) { const int g = juce::roundToInt (v); return g == 0 ? juce::String ("none") : juce::String (g); });
        f.button ("KEYTRACK", [this, zn] { if (auto* x = zn()) { x->keytrack = ! x->keytrack; ctx.changed(); } }, true, z.keytrack);
        f.button ("PLAY ROOT", [this, zn] { if (auto* x = zn()) ctx.proc.auditionNote (x->root, 0.9f); });
        juce::StringArray layers; for (const auto& l : K.layers) layers.add (l.name);
        f.combo ("LAYER", layers, z.layer, [this, zn] (int s) { if (auto* x = zn()) { x->layer = s; ctx.changed(); } });
        f.button ("DELETE ZONE", [this, i] { auto& zs = ctx.P().keymap.zones; if (i < (int) zs.size()) { zs.erase (zs.begin() + i); ctx.select ({ Sel::None, 0 }); ctx.changed(); } });
    }

    Ctx& ctx;
    LcdBox lcd;
    Segmented tabs;
    juce::Viewport view;
    std::unique_ptr<FlowPanel> body;
    juce::String crumb;
    Sel lastSel; int lastTab = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};

std::unique_ptr<juce::Component> makeInspector (Ctx& c) { return std::make_unique<Inspector> (c); }
} // namespace ui
