// MidiJaQ's window: the line across the middle, one row a lane, and the knobs that move through the
// family around it.
#include "Editor.h"
#include "../neobeat/shared/NjqInstrument.h"

using namespace mjqui;

namespace
{
constexpr int kLabelW = 96;      // the lane names down the left of the grid
constexpr int kRuler = 18;       // the step numbers along the top

juce::String degreeText (const mjq::Line& L, int step)
{
    return gen::noteName (mjq::noteOf (L, step));
}

// The grid: a row for each lane, a column for each step. A click changes what it is under; dragging up
// and down on the note row moves the note through the key.
class Grid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Grid (MjqEditor& e) : ed (e)
    {
        setTooltip ("The line, a row to a lane. Click a cell to change it, drag up and down on the notes. "
                    "The lock beside a row keeps it through the next JAQ.");
    }
    std::function<void()> onEdit;

    int stepAt (float x) const noexcept
    {
        const int n = std::max (1, ed.proc.line.numSteps);
        const float w = (float) (getWidth() - kLabelW) / (float) n;
        return juce::jlimit (0, n - 1, (int) std::floor ((x - (float) kLabelW) / std::max (1.0f, w)));
    }
    int laneAt (float y) const noexcept { return juce::jlimit (0, mjq::numLanes - 1, (int) std::floor ((y - (float) kRuler) / rowH())); }
    float rowH() const noexcept { return (float) std::max (18, getHeight() - kRuler) / (float) mjq::numLanes; }

    void paint (juce::Graphics& g) override
    {
        auto& L = ed.proc.line;
        const int n = std::max (1, L.numSteps);
        const float w = (float) (getWidth() - kLabelW) / (float) n;
        paintLcd (g, getLocalBounds().toFloat(), 10.0f);

        const int now = ed.proc.stepNow();
        for (int s = 0; s < n; ++s)
        {
            const float x = (float) kLabelW + w * (float) s;
            const bool on = mjq::stepOn (L, s);
            if (s == now) { g.setColour (C::amber().withAlpha (0.18f)); g.fillRect (x, (float) kRuler, w, rowH() * (float) mjq::numLanes); }
            if (s % 4 == 0) { g.setColour (C::lcdDim().withAlpha (0.35f)); g.drawVerticalLine ((int) x, (float) kRuler, (float) kRuler + rowH() * (float) mjq::numLanes); }
            g.setFont (font (8.5f, s % 4 == 0));
            g.setColour (on ? C::lcdText() : C::lcdDim());
            g.drawText (juce::String (s + 1), (int) x, 2, (int) w, kRuler - 4, juce::Justification::centred);
        }

        for (int lane = 0; lane < mjq::numLanes; ++lane)
        {
            const float y = (float) kRuler + rowH() * (float) lane;
            g.setColour (C::lcdDim().withAlpha (0.25f));
            g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
            g.setFont (font (10.5f, lane == ed.chosenLane));
            g.setColour (ed.proc.line.locked[(size_t) lane] ? C::amber() : C::lcdText());
            g.drawText (juce::String (mjq::laneName ((mjq::Lane) lane)) + (L.locked[(size_t) lane] ? "  L" : ""),
                        8, (int) y, kLabelW - 14, (int) rowH(), juce::Justification::centredLeft);

            for (int s = 0; s < n; ++s)
            {
                const auto& st = L.steps[(size_t) s];
                const bool on = mjq::stepOn (L, s);
                auto cell = juce::Rectangle<float> ((float) kLabelW + w * (float) s + 1.0f, y + 3.0f, w - 2.0f, rowH() - 6.0f);
                if (! on && lane != (int) mjq::Lane::Rhythm) { g.setColour (C::lcdDim().withAlpha (0.25f)); g.drawRoundedRectangle (cell, 3.0f, 1.0f); continue; }

                juce::String txt;
                float fill = 0.0f;
                switch ((mjq::Lane) lane)
                {
                    case mjq::Lane::Rhythm: fill = (float) st.weight / 3200.0f; txt = on ? "" : ""; break;
                    case mjq::Lane::Note:   txt = degreeText (L, s); fill = 0.35f; break;
                    case mjq::Lane::Accent: txt = juce::String (mjq::velocityOf (L, s)); fill = 0.2f + 0.3f * (float) st.accent; break;
                    case mjq::Lane::Gate:   txt = st.gate == 0 ? "." : st.gate == 1 ? "-" : "_"; fill = 0.2f + 0.25f * (float) st.gate; break;
                    case mjq::Lane::Octave: txt = st.octave == 0 ? "0" : (st.octave > 0 ? "+" : "") + juce::String ((int) st.octave); fill = 0.3f; break;
                    case mjq::Lane::Roll:   txt = st.roll > 1 ? juce::String ((int) st.roll) : ""; fill = st.roll > 1 ? 0.45f : 0.12f; break;
                    case mjq::Lane::Slide:  txt = st.slide ? "/" : ""; fill = st.slide ? 0.45f : 0.12f; break;
                    default: break;
                }
                g.setColour (C::glow().withAlpha (juce::jlimit (0.06f, 0.6f, fill * (on ? 1.0f : 0.3f))));
                g.fillRoundedRectangle (cell, 3.0f);
                if (s == ed.chosenStep && lane == ed.chosenLane) { g.setColour (C::lcdText()); g.drawRoundedRectangle (cell, 3.0f, 1.4f); }
                if (txt.isNotEmpty())
                {
                    g.setFont (font (w > 34.0f ? 10.0f : 8.5f));
                    g.setColour (C::lcdText());
                    g.drawText (txt, cell.toNearestInt(), juce::Justification::centred, false);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& L = ed.proc.line;
        const int lane = laneAt (e.position.y);
        ed.chosenLane = lane;
        if (e.position.x < (float) kLabelW)                       // the lane name: the lock
        {
            ed.proc.mark();
            L.locked[(size_t) lane] = ! L.locked[(size_t) lane];
            ed.proc.lineChanged();
            if (onEdit) onEdit();
            return;
        }
        const int s = stepAt (e.position.x);
        ed.chosenStep = s;
        ed.proc.mark();
        auto& st = L.steps[(size_t) s];
        switch ((mjq::Lane) lane)
        {
            case mjq::Lane::Rhythm: st.weight = (uint16_t) (mjq::stepOn (L, s) ? 0 : 65535); break;   // out of the line, or always in it
            case mjq::Lane::Note:   dragFrom = st.freeDegree; dragging = true; break;
            case mjq::Lane::Accent: st.accent = (uint8_t) ((st.accent + 1) % 3); break;
            case mjq::Lane::Gate:   st.gate = (uint8_t) ((st.gate + 1) % 3); break;
            case mjq::Lane::Octave: st.octave = (int8_t) (st.octave >= L.octHigh ? L.octLow : st.octave + 1); break;
            case mjq::Lane::Roll:   st.roll = (uint8_t) (st.roll >= 4 ? 1 : st.roll + 1); break;
            case mjq::Lane::Slide:  st.slide = ! st.slide; break;
            default: break;
        }
        ed.proc.lineChanged();
        if (onEdit) onEdit();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || ed.chosenStep < 0) return;
        auto& st = ed.proc.line.steps[(size_t) ed.chosenStep];
        const int moved = (int) std::lround (-e.getDistanceFromDragStartY() / 12.0);
        const int size = std::max (1, gen::scale (ed.proc.line.scaleIndex).size());
        const int want = juce::jlimit (0, size - 1, dragFrom + moved);
        if (want != st.freeDegree) { st.freeDegree = (int16_t) want; st.acidDegree = (int16_t) want; ed.proc.lineChanged(); if (onEdit) onEdit(); }
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    MjqEditor& ed;
    bool dragging = false;
    int dragFrom = 0;
};
} // namespace

struct MjqEditor::Parts
{
    explicit Parts (MjqEditor& e) : grid (e) {}
    Grid grid;
    Pill jaq { "JAQ" }, undo { "Undo" }, redo { "Redo" }, lane { "Reroll row" };
    Knob dialect { "DIALECT", 0.0f, 1.0f, 0.5f }, traffic { "TRAFFIC", 0.0f, 1.0f, 0.5f };
    Knob accent { "ACCENT", 0.0f, 1.0f, 0.5f }, gate { "GATE", 0.1f, 2.0f, 1.0f }, swing { "SWING", 0.5f, 0.75f, 0.5f };
    Knob steps { "STEPS", 1.0f, 32.0f, 16.0f }, offset { "OFFSET", 0.0f, 31.0f, 0.0f }, transpose { "TRANSPOSE", -24.0f, 24.0f, 0.0f };
    Segmented mode { { "Free", "Follow", "Bass" } }, clock { { "Pattern", "Song", "Gate" } }, direction { { "Forward", "Back", "Pendulum" } };
    juce::ComboBox rootBox, scaleBox, gridBox;
    Pill openInst { "Open .NJQ" }, closeInst { "Send MIDI only" };
    Knob level { "LEVEL", 0.0f, 1.5f, 0.8f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> head, body, side;
};

MjqEditor::MjqEditor (MjqProcessor& p) : juce::AudioProcessorEditor (&p), proc (p), parts (std::make_unique<Parts> (*this))
{
    setLookAndFeel (&look);
    auto& q = *parts;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.grid, &q.jaq, &q.undo, &q.redo, &q.lane, &q.dialect, &q.traffic,
            &q.accent, &q.gate, &q.swing, &q.steps, &q.offset, &q.transpose, &q.mode, &q.clock, &q.direction, &q.rootBox, &q.scaleBox, &q.gridBox,
            &q.openInst, &q.closeInst, &q.level })
        addAndMakeVisible (c);

    auto done = [this] { proc.lineChanged(); refresh(); };

    q.jaq.setTooltip ("Write a new family of patterns (J). Locked rows are kept.");
    q.jaq.onClick = [this] { proc.roll(); refresh(); };
    q.lane.setTooltip ("Write this row again, and leave the rest alone");
    q.lane.onClick = [this] { proc.rollLane ((mjq::Lane) chosenLane); refresh(); };
    q.undo.onClick = [this] { proc.undo(); refresh(); };
    q.redo.onClick = [this] { proc.redo(); refresh(); };
    q.undo.setTooltip ("Undo (Ctrl+Z)"); q.redo.setTooltip ("Redo (Ctrl+Shift+Z)");

    q.dialect.main = true; q.traffic.main = true;
    q.dialect.setTooltip ("What the line plays: from free intervals to the root and its octave. Nothing but the pitches moves.");
    q.traffic.setTooltip ("How many steps play. A busier line keeps every note of a sparser one.");
    q.dialect.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.traffic.toText = q.dialect.toText;
    q.accent.toText = q.dialect.toText;
    q.gate.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.swing.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.steps.step = 1.0f; q.offset.step = 1.0f; q.transpose.step = 1.0f;
    q.steps.toText = [] (float v) { return juce::String ((int) std::round (v)); };
    q.offset.toText = q.steps.toText;
    q.transpose.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String ((int) std::round (v)); };

    q.dialect.onChange = [this] (float v) { proc.line.dialect = v; proc.lineChanged(); };
    q.traffic.onChange = [this] (float v) { proc.line.traffic = v; proc.lineChanged(); };
    q.accent.onChange = [this] (float v) { proc.line.accent = v; proc.lineChanged(); };
    q.gate.onChange = [this] (float v) { proc.line.gate = v; proc.lineChanged(); };
    q.swing.onChange = [this] (float v) { proc.line.swing = v; proc.lineChanged(); };
    q.steps.onChange = [this] (float v) { proc.line.numSteps = juce::jlimit (1, mjq::kMaxSteps, (int) std::lround (v)); proc.lineChanged(); };
    q.offset.onChange = [this] (float v) { proc.line.offset = (int) std::lround (v); proc.lineChanged(); };
    q.transpose.onChange = [this] (float v) { proc.line.transpose = (int) std::lround (v); proc.lineChanged(); };
    for (auto* k : { &q.dialect, &q.traffic, &q.accent, &q.gate, &q.swing, &q.steps, &q.offset, &q.transpose }) k->onCommit = [this] { refresh(); };

    q.mode.setItemTooltip (0, "Free: MidiJaQ writes its own line");
    q.mode.setItemTooltip (1, "Follow: the keys you hold give the pitches, the line gives the rhythm");
    q.mode.setItemTooltip (2, "Bass: it reads the chord you hold and plays under it");
    q.mode.onSelect = [this, done] (int i) { proc.line.mode = (mjq::Mode) i; done(); };
    q.clock.setItemTooltip (0, "Pattern: the line starts again whenever the transport does");
    q.clock.setItemTooltip (1, "Song: the line sits where the song sits");
    q.clock.setItemTooltip (2, "Gate: it plays only while keys are held");
    q.clock.onSelect = [this, done] (int i) { proc.clockMode = (mjq::ClockMode) i; done(); };
    q.direction.onSelect = [this, done] (int i) { proc.line.direction = (mjq::Direction) i; done(); };

    q.openInst.setTooltip ("Open an instrument made in Batchcut's KEYS room: MidiJaQ then plays its own line, "
                           "instead of only sending MIDI to another plugin");
    q.openInst.onClick = [this]
    {
        auto& pr = *parts;
        pr.chooser = std::make_unique<juce::FileChooser> ("Open an instrument", shared::instrumentsFolder(), "*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String err;
            proc.openInstrument (f, err);
            refresh();
        });
    };
    q.closeInst.setTooltip ("Put the instrument away and go back to sending MIDI alone");
    q.closeInst.onClick = [this] { proc.closeInstrument(); refresh(); };
    q.level.setTooltip ("How loud the instrument plays");
    q.level.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.level.onChange = [this] (float v) { proc.voices.level.store (v); };

    for (int i = 0; i < 12; ++i) q.rootBox.addItem (gen::pitchName (i), i + 1);
    for (int i = 0; i < gen::numScales(); ++i) q.scaleBox.addItem (gen::scale (i).name, i + 1);
    q.gridBox.addItem ("8ths", 1); q.gridBox.addItem ("8th triplets", 2); q.gridBox.addItem ("16ths", 3); q.gridBox.addItem ("16th triplets", 4);
    q.rootBox.setTooltip ("The key the line is in"); q.scaleBox.setTooltip ("Every note is kept in this scale"); q.gridBox.setTooltip ("How long a step is");
    q.rootBox.onChange = [this, done] { proc.line.root = parts->rootBox.getSelectedId() - 1; done(); };
    q.scaleBox.onChange = [this, done] { proc.line.scaleIndex = parts->scaleBox.getSelectedId() - 1; done(); };
    q.gridBox.onChange = [this, done]
    {
        static const int bars[] = { 8, 12, 16, 24 };
        proc.line.perBar = bars[juce::jlimit (0, 3, parts->gridBox.getSelectedId() - 1)];
        done();
    };

    setSize (1120, 480);
    setResizable (true, true);
    setResizeLimits (900, 420, 1800, 900);
    refresh();
    startTimerHz (20);
}

MjqEditor::~MjqEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MjqEditor::refresh()
{
    auto& q = *parts;
    const auto& L = proc.line;
    q.dialect.setValue (L.dialect); q.traffic.setValue (L.traffic); q.accent.setValue (L.accent);
    q.gate.setValue (L.gate); q.swing.setValue (L.swing);
    q.steps.setValue ((float) L.numSteps); q.offset.setValue ((float) L.offset); q.transpose.setValue ((float) L.transpose);
    q.mode.setSelected ((int) L.mode);
    q.clock.setSelected ((int) proc.clockMode);
    q.direction.setSelected ((int) L.direction);
    q.rootBox.setSelectedId (L.root + 1, juce::dontSendNotification);
    q.scaleBox.setSelectedId (L.scaleIndex + 1, juce::dontSendNotification);
    q.gridBox.setSelectedId (L.perBar == 8 ? 1 : L.perBar == 12 ? 2 : L.perBar == 24 ? 4 : 3, juce::dontSendNotification);
    q.undo.setEnabled (proc.canUndo()); q.redo.setEnabled (proc.canRedo());
    const bool hasInst = proc.instrument() != nullptr;
    q.closeInst.setEnabled (hasInst);
    q.level.setEnabled (hasInst);
    q.level.setValue (proc.voices.level.load());
    if (chosenStep >= L.numSteps) chosenStep = -1;
    seenVersion = proc.version.load();
    repaint();
}

void MjqEditor::timerCallback()
{
    parts->grid.repaint();
    if (proc.version.load() != seenVersion) refresh();
}

bool MjqEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = key.getKeyCode();
    const auto letter = c < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c) : 0;
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && letter == 'z') { if (mods.isShiftDown()) proc.redo(); else proc.undo(); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'y') { proc.redo(); refresh(); return true; }
    if (letter == 'j' && ! mods.isCommandDown()) { proc.roll(); refresh(); return true; }
    if (letter == 'l' && chosenLane >= 0)
    {
        proc.mark();
        proc.line.locked[(size_t) chosenLane] = ! proc.line.locked[(size_t) chosenLane];
        proc.lineChanged(); refresh(); return true;
    }
    if (c == juce::KeyPress::leftKey || c == juce::KeyPress::rightKey)
    {
        const int n = std::max (1, proc.line.numSteps);
        chosenStep = chosenStep < 0 ? 0 : (chosenStep + (c == juce::KeyPress::rightKey ? 1 : n - 1)) % n;
        repaint(); return true;
    }
    if (c == juce::KeyPress::upKey || c == juce::KeyPress::downKey)
    {
        chosenLane = (chosenLane + (c == juce::KeyPress::downKey ? 1 : mjq::numLanes - 1)) % mjq::numLanes;
        repaint(); return true;
    }
    return false;
}

void MjqEditor::paint (juce::Graphics& g)
{
    auto& q = *parts;
    paintChassis (g, getLocalBounds().toFloat());
    paintPlate (g, q.head.toFloat(), 12.0f);
    paintPlate (g, q.side.toFloat(), 12.0f);

    g.setFont (caps (15.0f));
    g.setColour (C::text());
    g.drawText ("MidiJaQ", q.head.getX() + 16, q.head.getY() + 10, 200, 22, juce::Justification::centredLeft);
    paintLabel (g, "the melodic one", { q.head.getX() + 16, q.head.getY() + 32, 220, 14 }, juce::Justification::centredLeft, 8.5f);

    paintLabel (g, "Key", { q.rootBox.getX(), q.rootBox.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Scale", { q.scaleBox.getX(), q.scaleBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Step", { q.gridBox.getX(), q.gridBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Instrument", { q.openInst.getX(), q.openInst.getY() - 14, 110, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Mode", { q.mode.getX(), q.mode.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Clock", { q.clock.getX(), q.clock.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Direction", { q.direction.getX(), q.direction.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);

    g.setFont (font (10.5f));
    g.setColour (C::dim());
    g.drawText (proc.message, 16, getHeight() - 26, getWidth() - 32, 18, juce::Justification::centredLeft, true);
}

void MjqEditor::resized()
{
    auto& q = *parts;
    auto r = getLocalBounds().reduced (10);
    q.head = r.removeFromTop (92);
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (24);
    juce::ignoreUnused (bottom);
    q.side = r.removeFromRight (250);
    q.body = r;
    q.grid.setBounds (q.body.reduced (0, 2));

    // the buttons sit at the right; the knobs share whatever is left between the name and them
    const int ky = q.head.getY() + 10, kh = 72;
    const int buttons = q.head.getRight() - 208;
    q.jaq.setBounds (buttons, ky + 4, 84, 30);
    q.lane.setBounds (buttons, ky + 38, 84, 26);
    q.undo.setBounds (buttons + 92, ky + 4, 44, 30);
    q.redo.setBounds (buttons + 140, ky + 4, 44, 30);

    const int kx = q.head.getX() + 196;
    mjqui::Knob* knobs[] = { &q.dialect, &q.traffic, &q.accent, &q.gate, &q.swing, &q.steps, &q.offset, &q.transpose };
    const int n = 8;
    const int each = std::max (54, (buttons - 12 - kx) / n);
    for (int i = 0; i < n; ++i) knobs[i]->setBounds (kx + i * each, ky, each - 2, kh);

    const int x = q.side.getX() + 14, w = q.side.getWidth() - 28;
    q.mode.setBounds (x, q.side.getY() + 28, w, 30);
    q.clock.setBounds (x, q.side.getY() + 86, w, 30);
    q.direction.setBounds (x, q.side.getY() + 144, w, 30);
    q.rootBox.setBounds (x, q.side.getY() + 202, w / 2 - 4, 26);
    q.scaleBox.setBounds (x + w / 2 + 4, q.side.getY() + 202, w / 2 - 4, 26);
    q.gridBox.setBounds (x, q.side.getY() + 254, w, 26);
    q.openInst.setBounds (x, q.side.getY() + 310, w - 76, 28);
    q.level.setBounds (x + w - 70, q.side.getY() + 296, 70, 56);
    q.closeInst.setBounds (x, q.side.getY() + 342, w - 76, 24);
}
