// NeoBeat 3.0 — the panels that open over the shell (export, MIDI out, importing a folder, "make this a
// default sequence?", notices), and the node helpers every sequencer shares.
#include "UI.h"

namespace ui
{
// ================================================================== nodes
int nodeCell (Ctx& ctx, int lane, int step)
{
    const auto& cs = ctx.P().matrix.oneShot.cells;
    const int scene = ctx.scene();
    for (int i = 0; i < (int) cs.size(); ++i) if (cs[(size_t) i].scene == scene && cs[(size_t) i].pad == lane && cs[(size_t) i].step == step) return i;
    return -1;
}

void sendNode (Ctx& ctx, int lane, int step, int fxIdx)
{
    auto& cs = ctx.P().matrix.oneShot.cells;
    const int i = nodeCell (ctx, lane, step);
    if (fxIdx < 0)
    {   // Init: no effect
        if (i >= 0) cs.erase (cs.begin() + i);
        ctx.select ({ Sel::Node, lane * nb::Pattern::kMaxSteps + step });
        ctx.changed();
        return;
    }
    // a node is a hit: sending an empty box somewhere puts a hit in it
    auto st = ctx.proc.pattern.get (lane, step);
    if (st.vel == 0) { st.vel = 96; ctx.proc.pattern.set (lane, step, st); }
    const auto fx = fxFromIndex (fxIdx);
    int at = i;
    if (i >= 0) cs[(size_t) i].fx = fx;
    else if (cs.size() < 2048)
    {
        nb::OneShotCell c; c.pad = lane; c.step = step; c.scene = ctx.scene(); c.fx = fx; c.span = fx.kind == nb::CellFx::FrictionStretchFx ? 6 : 3;
        cs.push_back (c); at = (int) cs.size() - 1;
    }
    ctx.P().matrix.oneShotOn = true;
    if (fx.kind == nb::CellFx::FrictionStretchFx)
        ctx.notify ("friction", "FRICTION STRETCH IS PREPARED AHEAD",
                    "Every other effect runs live: you hear a change the moment you make it. Friction Stretch cannot: it stretches a hit to many times its length, "
                    "so it needs the whole hit before it can start, and NeoBeat prepares it in the background (PREPARING, top right).\n\n"
                    "After you change a Friction Stretch setting, the new sound is in after a moment; until then you hear the last one.");
    if (at >= 0) ctx.select ({ Sel::Cell, at }, 1);
    ctx.changed();
}

void addSendMenu (juce::PopupMenu& m, int idBase, int current)
{
    juce::PopupMenu send;
    send.addItem (idBase, "Init (no effect)", true, current < 0);
    send.addSeparator();
    juce::String family;
    for (int i = 0; i < 26; ++i)
    {
        const auto fx = fxFromIndex (i);
        const auto fam = fxFamily (fx);
        if (fam != family) { send.addSectionHeader (fam); family = fam; }
        send.addItem (idBase + 1 + i, fxName (fx) + (fx.kind == nb::CellFx::FrictionStretchFx ? "   (prepared ahead)" : ""), true, current == i);
    }
    m.addSubMenu ("Send node to", send);
}

void copyNode (Ctx& ctx, int lane, int step)
{
    ctx.clipStep = ctx.proc.pattern.get (lane, step);
    const int i = nodeCell (ctx, lane, step);
    ctx.clipFx = i >= 0 ? ctx.P().matrix.oneShot.cells[(size_t) i].fx : nb::CellFx();
    if (i >= 0) { ctx.clipSpan = ctx.P().matrix.oneShot.cells[(size_t) i].span; ctx.clipHeight = ctx.P().matrix.oneShot.cells[(size_t) i].height; }
    ctx.clipHas = true;
}

void pasteNode (Ctx& ctx, int lane, int step)
{
    if (! ctx.clipHas) return;
    ctx.proc.pattern.set (lane, step, ctx.clipStep);
    auto& cs = ctx.P().matrix.oneShot.cells;
    const int i = nodeCell (ctx, lane, step);
    if (ctx.clipFx.kind == nb::CellFx::None) { if (i >= 0) cs.erase (cs.begin() + i); }
    else if (i >= 0) { cs[(size_t) i].fx = ctx.clipFx; cs[(size_t) i].span = ctx.clipSpan; cs[(size_t) i].height = ctx.clipHeight; }
    else if (cs.size() < 2048) { nb::OneShotCell c; c.pad = lane; c.step = step; c.scene = ctx.scene(); c.fx = ctx.clipFx; c.span = ctx.clipSpan; c.height = ctx.clipHeight; cs.push_back (c); }
    ctx.changed();
}

// ================================================================== shared bits
namespace
{
void paintPanel (juce::Graphics& g, juce::Component& c, const juce::String& title)
{
    g.fillAll (juce::Colour (0x55000000));
    paintPlate (g, c.getLocalBounds().toFloat());
    paintLabel (g, title, { 20, 16, c.getWidth() - 140, 16 }, juce::Justification::centredLeft, 10.0f);
}
void paintText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float size = 12.5f, juce::Colour col = C::text())
{
    g.setColour (col); g.setFont (font (size));
    g.drawFittedText (t, r, juce::Justification::topLeft, 12, 0.95f);
}

// a number you drag (or wheel) up and down
class NumBox final : public juce::Component
{
public:
    NumBox (int lo, int hi) : minV (lo), maxV (hi) {}
    std::function<void (int)> onChange;
    std::function<juce::String (int)> toText;
    void set (int v) { value = juce::jlimit (minV, maxV, v); repaint(); }
    int get() const { return value; }
    void paint (juce::Graphics& g) override
    {
        paintLcd (g, getLocalBounds().toFloat().reduced (1), 5.0f);
        g.setColour (C::lcdText()); g.setFont (font (11.5f, true));
        g.drawText (toText ? toText (value) : juce::String (value), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { grab = value; }
    void mouseDrag (const juce::MouseEvent& e) override { change (grab - e.getDistanceFromDragStartY() / 5); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { change (value + (w.deltaY > 0 ? 1 : -1)); }
private:
    void change (int v) { v = juce::jlimit (minV, maxV, v); if (v != value) { value = v; repaint(); if (onChange) onChange (v); } }
    int minV, maxV, value = 0, grab = 0;
};

// a file you can drag out of NeoBeat (into the host's timeline, or a folder)
class DragFile final : public juce::Component
{
public:
    juce::File file; juce::String label;
    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat().reduced (1), 10.0f, isMouseOver());
        paintLabel (g, label, getLocalBounds().reduced (10, 0).removeFromTop (getHeight() / 2 + 4), juce::Justification::centredLeft, 8.0f);
        g.setColour (C::dim()); g.setFont (font (10.5f));
        g.drawText (file.getFileName(), getLocalBounds().reduced (10, 0).removeFromBottom (getHeight() / 2), juce::Justification::centredLeft, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! file.existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [this] { dragging = false; });
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
private:
    bool dragging = false;
};
} // namespace

// ================================================================== export
class ExportPanel final : public juce::Component, public Part
{
public:
    explicit ExportPanel (Ctx& c) : ctx (c), what ({ "THIS SCENE", "THE CHAIN", "BARS" }), from (1, 32), to (1, 32)
    {
        addAndMakeVisible (what); what.onSelect = [this] (int i) { mode = i; refresh(); };
        for (auto* b : { &wav, &stems, &midi, &straight }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
        wav.setToggleState (true, juce::dontSendNotification); midi.setToggleState (true, juce::dontSendNotification);
        straight.setTooltip ("Leave the swing out of the MIDI file (for a host that applies its own groove)");
        for (auto* b : { &choose, &go, &close }) addAndMakeVisible (b);
        addAndMakeVisible (from); addAndMakeVisible (to);
        from.set (1); to.set (2);
        from.toText = [] (int v) { return "bar " + juce::String (v); }; to.toText = from.toText;
        from.onChange = [this] (int v) { if (to.get() < v) to.set (v); }; to.onChange = [this] (int v) { if (from.get() > v) from.set (v); };
        addAndMakeVisible (name); name.setText ("NeoBeat pattern", false);
        name.setFont (font (13.0f)); name.setJustification (juce::Justification::centredLeft);
        close.onClick = [this] { ctx.overlay = 0; if (ctx.refreshAll) ctx.refreshAll(); };
        choose.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Export into this folder", folder());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (! fc.getResult().isDirectory()) return;
                auto st = shared::Settings::load(); st.exportFolder = fc.getResult().getFullPathName(); st.save(); repaint();
            });
        };
        go.onClick = [this]
        {
            NeoBeatProcessor::ExportOptions o;
            o.folder = folder(); o.name = name.getText().trim();
            o.range.mode = mode; o.range.scene = ctx.scene(); o.range.barFrom = from.get() - 1; o.range.barTo = to.get();
            o.wav = wav.getToggleState(); o.stems = stems.getToggleState(); o.midi = midi.getToggleState(); o.straightMidi = straight.getToggleState();
            if (! o.wav && ! o.stems && ! o.midi) return;
            if (ctx.proc.startExport (o)) { running = true; drags.clear(); repaint(); }
        };
    }
    juce::File folder() const
    {
        const auto st = shared::Settings::load();
        return st.exportFolder.isNotEmpty() ? juce::File (st.exportFolder) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Marbelite").getChildFile ("Exports");
    }
    void refresh() override
    {
        if (! isVisible()) return;
        what.setSelected (mode);
        const bool chain = ctx.P().song.chainOn && ctx.P().song.numLinks > 0;
        if (mode == 1 && ! chain) mode = 0;
        from.setVisible (mode == 2); to.setVisible (mode == 2);
        repaint();
    }
    void tick() override
    {
        if (! isVisible()) return;
        if (running)
        {
            repaint();
            if (! ctx.proc.exporting())
            {
                running = false;
                drags.clear();
                for (const auto& path : ctx.proc.exportedFiles())
                {
                    if (drags.size() >= 6) break;
                    auto* d = drags.add (new DragFile()); d->file = juce::File (path);
                    d->label = d->file.hasFileExtension ("mid") ? "DRAG THE MIDI" : "DRAG THE WAV";
                    addAndMakeVisible (d);
                }
                resized();
            }
        }
    }
    void resized() override
    {
        close.setBounds (getWidth() - 100, 12, 84, 30);
        what.setBounds (20, 78, 380, 34);
        from.setBounds (414, 80, 90, 30); to.setBounds (512, 80, 90, 30);
        wav.setBounds (20, 152, 120, 34); stems.setBounds (146, 152, 150, 34); midi.setBounds (302, 152, 110, 34); straight.setBounds (418, 152, 190, 34);
        name.setBounds (20, 226, 400, 30);
        choose.setBounds (getWidth() - 130, 286, 110, 30);
        go.setBounds (20, 340, 170, 40);
        for (int i = 0; i < drags.size(); ++i) drags[i]->setBounds (20 + (i % 3) * 208, 420 + (i / 3) * 46, 200, 40);
    }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, *this, "EXPORT THE PATTERN | SOUND AND MIDI");
        paintLabel (g, "WHAT", { 20, 60, 200, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        paintLabel (g, "AS", { 20, 134, 200, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        paintLabel (g, "NAME", { 20, 208, 200, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        paintLabel (g, "INTO", { 20, 272, 200, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        paintText (g, folder().getFullPathName(), { 20, 290, getWidth() - 170, 34 }, 12.0f);
        const bool chain = ctx.P().song.chainOn && ctx.P().song.numLinks > 0;
        paintText (g, mode == 0 ? "Scene " + juce::String::charToString ((juce::juce_wchar) ('A' + ctx.scene())) + ", once through (" + juce::String (ctx.proc.pattern.length() / 16.0, 2) + " bars)."
                     : mode == 1 ? (chain ? juce::String ("The chain, once through.") : juce::String ("The chain is off: this exports the scene."))
                     : juce::String ("Those bars of the scene shown."), { 20, 114, 600, 18 }, 11.0f, C::dim());
        paintText (g, "The WAV is what you hear, effects included; their tails ring out past the last bar. 24 bit, at the project's sample rate.", { 20, 188, 620, 18 }, 11.0f, C::dim());
        if (running)
        {
            auto bar = juce::Rectangle<float> (204, 352, 420, 16);
            paintWell (g, bar, 8.0f);
            g.setColour (C::teal()); g.fillRoundedRectangle (bar.reduced (3).withWidth ((bar.getWidth() - 6) * ctx.proc.exportProgress()), 5.0f);
        }
        else paintText (g, ctx.proc.exportResult(), { 204, 344, 430, 36 }, 12.0f);
        if (! drags.isEmpty()) paintLabel (g, "DRAG A FILE STRAIGHT INTO YOUR HOST", { 20, 400, 400, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
    }
private:
    Ctx& ctx;
    Segmented what;
    NumBox from, to;
    Pill wav { "WAV | THE MIX" }, stems { "WAV | EACH LANE" }, midi { "MIDI FILE" }, straight { "MIDI WITHOUT SWING" }, choose { "CHOOSE..." }, go { "EXPORT" }, close { "CLOSE" };
    juce::TextEditor name;
    juce::OwnedArray<DragFile> drags;
    std::unique_ptr<juce::FileChooser> chooser;
    int mode = 0;
    bool running = false;
};

// ================================================================== MIDI out
class MidiPanel final : public juce::Component, public Part
{
public:
    explicit MidiPanel (Ctx& c) : ctx (c), modes ({ "SOUND + MIDI", "MIDI ONLY", "SOUND ONLY" }), length (c, "NOTE LENGTH", 0.1f, 8.0f, 0.5f, true)
    {
        addAndMakeVisible (modes);
        modes.onSelect = [this] (int i) { ctx.P().midi.mode = i; ctx.changed (false); repaint(); };
        addAndMakeVisible (length);
        length.onChange = [this] (float v) { ctx.P().midi.lengthSteps = v; ctx.changed (false); };
        length.toText = [] (float v) { return juce::String (v, 2) + " steps"; };
        for (auto* b : { &rolls, &transpose }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
        rolls.onClick = [this] { ctx.P().midi.rolls = rolls.getToggleState(); ctx.changed (false); };
        transpose.onClick = [this] { ctx.P().midi.transpose = transpose.getToggleState(); ctx.changed (false); };
        rolls.setTooltip ("A roll sends each of its hits as its own note (off: one note for the step)");
        transpose.setTooltip ("Key mode: the held key moves the notes too");
        for (auto* b : { &rack, &close }) addAndMakeVisible (b);
        close.onClick = [this] { ctx.overlay = 0; if (ctx.refreshAll) ctx.refreshAll(); };
        rack.onClick = [this] { ctx.P().midi = nb::MidiSettings(); ctx.changed(); };
        for (int i = 0; i < nb::kNumPads; ++i)
        {
            auto* n = notes.add (new NumBox (0, 127)); auto* ch = chans.add (new NumBox (1, 16)); auto* t = tests.add (new Pill ("TEST"));
            n->toText = [] (int v) { return noteName (v) + "  " + juce::String (v); };
            n->onChange = [this, i] (int v) { ctx.P().midi.note[(size_t) i] = (uint8_t) v; ctx.changed (false); };
            ch->onChange = [this, i] (int v) { ctx.P().midi.channel[(size_t) i] = (uint8_t) v; ctx.changed (false); };
            t->onClick = [this, i] { ctx.proc.midiTest (i); };
            t->setTooltip ("Send this lane's note once, without its sound");
            addAndMakeVisible (n); addAndMakeVisible (ch); addAndMakeVisible (t);
        }
    }
    void refresh() override
    {
        if (! isVisible()) return;
        const auto& m = ctx.P().midi;
        modes.setSelected (m.mode); length.setValue (m.lengthSteps);
        rolls.setToggleState (m.rolls, juce::dontSendNotification); transpose.setToggleState (m.transpose, juce::dontSendNotification);
        for (int i = 0; i < nb::kNumPads; ++i) { notes[i]->set (m.note[(size_t) i]); chans[i]->set (m.channel[(size_t) i]); }
        repaint();
    }
    juce::Rectangle<int> row (int i) const { return { 20 + (i / 8) * 400, 196 + (i % 8) * 34, 392, 30 }; }
    void resized() override
    {
        close.setBounds (getWidth() - 100, 12, 84, 30);
        modes.setBounds (20, 62, 420, 34);
        length.setBounds (456, 44, 86, 92);
        rolls.setBounds (552, 62, 120, 34); transpose.setBounds (676, 62, 144, 34);
        rack.setBounds (20, 480, 230, 30);
        for (int i = 0; i < nb::kNumPads; ++i)
        {
            auto r = row (i);
            tests[i]->setBounds (r.removeFromRight (58).reduced (2));
            chans[i]->setBounds (r.removeFromRight (50).reduced (2));
            notes[i]->setBounds (r.removeFromRight (92).reduced (2));
        }
    }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, *this, "MIDI OUT | NEOBEAT AS A SEQUENCER FOR OTHER INSTRUMENTS");
        const int mode = ctx.P().midi.mode;
        paintText (g, mode == 0 ? "The lanes sound, and every hit also goes out as a MIDI note."
                     : mode == 1 ? "The lanes are silent: NeoBeat only sends notes. Its sequencer plays your other instruments."
                     : "No notes go out.", { 20, 100, 420, 34 }, 11.5f, C::dim());
        for (int col = 0; col < 2; ++col)
        {
            const int x = 20 + col * 400;
            paintLabel (g, "LANE", { x + 4, 178, 120, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
            paintLabel (g, "NOTE", { x + 192, 178, 92, 12 }, juce::Justification::centred, 7.0f, C::dim());
            paintLabel (g, "CHANNEL", { x + 284, 178, 50, 12 }, juce::Justification::centred, 7.0f, C::dim());
        }
        for (int i = 0; i < nb::kNumPads; ++i)
        {
            auto r = row (i);
            g.setColour (C::text()); g.setFont (font (12.0f, i == ctx.pad));
            g.drawText (juce::String (i + 1).paddedLeft ('0', 2) + "  " + ctx.proc.padName (i), r.withWidth (186).withTrimmedLeft (4), juce::Justification::centredLeft, true);
        }
        paintLabel (g, "THE PATCH, IN ABLETON LIVE", { 20, 528, 400, 12 }, juce::Justification::centredLeft, 8.0f);
        paintText (g, "1.  NeoBeat sits on a MIDI track of its own. Choose MIDI ONLY above if that track should make no sound.\n"
                      "2.  On the track of the instrument NeoBeat should play (a Drum Rack, a synth): set MIDI From to NeoBeat's track, and in the box under it choose NeoBeat itself (not Pre FX). Set Monitor to In.\n"
                      "3.  Press play, or TEST on a lane to send one note. A Drum Rack plays lane 1 on C1, lane 2 on C#1 and so on, which is what DRUM RACK FROM C1 sets.\n\n"
                      "Live merges all MIDI channels into one, so there the note decides which sound plays; the channels matter in hosts that keep them apart (Reaper, Bitwig, FL Studio).",
                   { 20, 546, getWidth() - 40, 140 }, 12.0f);
        paintText (g, "Drag a number up or down, or use the wheel.", { 264, 486, 400, 20 }, 11.0f, C::dim());
    }
private:
    Ctx& ctx;
    Segmented modes;
    Knob length;
    Pill rolls { "ROLLS AS NOTES" }, transpose { "FOLLOW THE KEY" }, rack { "DRUM RACK FROM C1, CHANNEL 1" }, close { "CLOSE" };
    juce::OwnedArray<NumBox> notes, chans;
    juce::OwnedArray<Pill> tests;
};

// ================================================================== importing a folder
class ImportPanel final : public juce::Component, public Part
{
public:
    explicit ImportPanel (Ctx& c) : ctx (c)
    {
        for (auto* b : { &all, &pick, &close }) addAndMakeVisible (b);
        close.onClick = [this] { ctx.overlay = 0; report = {}; done = false; if (ctx.refreshAll) ctx.refreshAll(); };
        all.onClick = [this] { run (false); };
        pick.onClick = [this] { run (true); };
    }
    void run (bool musical)
    {
        juce::MouseCursor::showWaitCursor();           // (a big folder takes a few seconds: every file is listened to)
        report = ctx.proc.importFolder (juce::File (ctx.overlayFile), musical);
        juce::MouseCursor::hideWaitCursor();
        done = true; wasMusical = musical;
        ctx.select ({ Sel::Pad, ctx.pad });
        ctx.changed();
    }
    void refresh() override { if (isVisible()) { all.setVisible (! done); pick.setVisible (! done); close.setButtonText (done ? "DONE" : "CANCEL"); repaint(); } }
    void resized() override
    {
        close.setBounds (getWidth() - 100, 12, 84, 30);
        all.setBounds (20, 150, 300, 46); pick.setBounds (340, 150, 300, 46);
    }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, *this, "A FOLDER OF SAMPLES INTO THE LANES");
        paintText (g, ctx.overlayFile, { 20, 50, getWidth() - 40, 20 }, 12.0f, C::dim());
        if (! done)
        {
            paintText (g, "NeoBeat listens to every sound in the folder, works out which lane each could play (kick, snare, hats and so on), "
                          "and puts them on the lanes in place of the stock sounds, each brought to the stock sounds' loudness.", { 20, 82, getWidth() - 40, 56 });
            paintText (g, "Every sound gets a place: the 16 that fit the lanes best go there, and all of them (up to 64) become Layermax's rows.", { 20, 204, 300, 70 }, 11.5f, C::dim());
            paintText (g, "Only clean single hits are considered (not clipped, clear of their noise, a usable length), no two alike, and what you rated highly before counts.", { 340, 204, 300, 70 }, 11.5f, C::dim());
            paintText (g, "One undo step in the sequencer's room takes it all back.", { 20, 290, 600, 20 }, 11.5f, C::dim());
            return;
        }
        paintText (g, report.found == 0 ? juce::String ("No audio files in that folder.")
                      : juce::String (report.found) + " files found, " + juce::String (report.lanes) + " lanes filled" + (report.rows > 0 ? ", " + juce::String (report.rows) + " Layermax rows" : juce::String())
                        + (wasMusical ? " (the most musical)." : " (all).") + "  UNDO in the sequencer's room takes it back.", { 20, 82, getWidth() - 40, 36 });
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain))); g.setColour (C::text());
        for (int i = 0; i < report.lines.size() && i < 16; ++i) g.drawText (report.lines[i], 20, 128 + i * 22, getWidth() - 40, 20, juce::Justification::centredLeft, true);
        paintText (g, "Not the lane you would have chosen? Drop that file on the pad you want: NeoBeat learns from where you put sounds, and from the stars in the inspector.", { 20, 490, getWidth() - 40, 40 }, 11.5f, C::dim());
    }
private:
    Ctx& ctx;
    Pill all { "IMPORT ALL" }, pick { "PICK THE MOST MUSICAL" }, close { "CANCEL" };
    NeoBeatProcessor::ImportReport report;
    bool done = false, wasMusical = false;
};

// ================================================================== "make this a default sequence?"
class DefaultPrompt final : public juce::Component, public Part
{
public:
    explicit DefaultPrompt (Ctx& c) : ctx (c)
    {
        for (auto* b : { &add, &start, &no }) addAndMakeVisible (b);
        addAndMakeVisible (never);
        never.setButtonText ("Don't ask again");
        add.onClick = [this] { finish (true, false); };
        start.onClick = [this] { finish (true, true); };
        no.onClick = [this] { finish (false, false); };
    }
    void finish (bool make, bool startWith)
    {
        if (never.getToggleState()) { auto st = shared::Settings::load(); st.askDefault = false; st.save(); }
        juce::String err;
        if (make && ! ctx.proc.makeDefault (juce::File (ctx.overlayFile), startWith, err)) { ctx.noticesShown.removeString ("default"); ctx.notify ("default", "COULD NOT DO THAT", err); }
        ctx.overlay = 0;
        if (ctx.refreshAll) ctx.refreshAll();
    }
    void resized() override
    {
        add.setBounds (20, 170, 180, 44); start.setBounds (208, 170, 210, 44); no.setBounds (426, 170, 154, 44);
        never.setBounds (20, 236, 220, 26);
    }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, *this, "MAKE THIS A DEFAULT SEQUENCE?");
        g.setColour (C::text()); g.setFont (font (15.0f, true));
        g.drawText (juce::File (ctx.overlayFile).getFileNameWithoutExtension(), 20, 46, getWidth() - 40, 24, juce::Justification::centredLeft, true);
        paintText (g, "ADD TO MY SEQUENCES keeps it in your preset folder, so it is in the list (KIT / FILE > My sequences) in every project.\n"
                      "ADD AND START WITH IT also makes every new NeoBeat open with it instead of the stock pattern.", { 20, 78, getWidth() - 40, 84 }, 12.5f);
        paintText (g, "You can change either later from KIT / FILE.", { 250, 240, 330, 20 }, 11.0f, C::dim());
    }
private:
    Ctx& ctx;
    Pill add { "ADD TO MY SEQUENCES" }, start { "ADD AND START WITH IT" }, no { "NOT NOW" };
    juce::ToggleButton never;
};

// ================================================================== a notice
class Notice final : public juce::Component, public Part
{
public:
    explicit Notice (Ctx& c) : ctx (c)
    {
        addAndMakeVisible (ok);
        ok.onClick = [this] { ctx.noticeText.clear(); ctx.noticeTitle.clear(); if (ctx.refreshAll) ctx.refreshAll(); };
    }
    void refresh() override { repaint(); }
    void resized() override { ok.setBounds (getWidth() - 120, getHeight() - 52, 100, 36); }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, *this, ctx.noticeTitle);
        paintText (g, ctx.noticeText, { 20, 48, getWidth() - 40, getHeight() - 108 }, 12.5f);
    }
private:
    Ctx& ctx;
    Pill ok { "OK" };
};

std::unique_ptr<juce::Component> makeExportPanel (Ctx& c) { return std::make_unique<ExportPanel> (c); }
std::unique_ptr<juce::Component> makeMidiPanel (Ctx& c) { return std::make_unique<MidiPanel> (c); }
std::unique_ptr<juce::Component> makeImportPanel (Ctx& c) { return std::make_unique<ImportPanel> (c); }
std::unique_ptr<juce::Component> makeDefaultPrompt (Ctx& c) { return std::make_unique<DefaultPrompt> (c); }
std::unique_ptr<juce::Component> makeNotice (Ctx& c) { return std::make_unique<Notice> (c); }
} // namespace ui
