// MidiJaQ's window: the line across the middle, one row a lane, and the knobs that move through the
// family around it.
#include "Editor.h"
#include "OrbPad.h"
#include "RoomPad.h"
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
    Segmented mode { { "Free", "Follow", "Bass", "Parts", "Keys" } }, clock { { "Pattern", "Song", "Gate" } }, direction { { "Forward", "Back", "Pendulum" } };
    juce::ComboBox rootBox, scaleBox, gridBox;
    juce::ComboBox slotBox, trackBox, writerBox, chordBarBox, chordRootBox, chordKindBox, chordLengthBox, cueBox, chordInversionBox;
    Pill trackOn { "Line on", true }, fill { "Fill", true };
    Pill openBank { "Open patterns" }, saveBank { "Save .NJQ" };
    Knob chain { "CHAIN", 1.0f, 128.0f, 1.0f }, amount { "CHANGE", 0.0f, 1.0f, 0.5f }, hits { "HITS", 0.0f, 32.0f, 5.0f };
    Knob rotate { "ROTATE", 0.0f, 31.0f, 0.0f }, cueN { "ON", 1.0f, 16.0f, 1.0f }, cueOf { "EVERY", 1.0f, 16.0f, 2.0f };
    Knob bassNote { "BASS", 24.0f, 72.0f, 48.0f };
    int chordBar = 0;
    // the side panel is tabbed, so the line, the multi and the Orb each have room of their own
    Segmented sideTabs { { "Line", "Voices", "Orb", "Room" } };
    Segmented voicePick { { "1", "2", "3", "4" } };
    Pill openInst { "Open instrument" }, closeInst { "Clear" };
    Knob level { "LEVEL", 0.0f, 1.5f, 0.8f };
    Knob lowKey { "LOW", 0.0f, 127.0f, 0.0f }, highKey { "HIGH", 0.0f, 127.0f, 127.0f };
    // the Orb
    mjq::OrbPad orbPad;
    Pill orbOn { "Orb on", true }, orbPut { "Put line here" }, orbClearCorner { "Empty it" };
    Knob orbGrip { "GRIP", 0.5f, 8.0f, 3.0f };
    juce::ComboBox orbFxBox[mjq::kOrbStack];
    Knob orbFxMix[mjq::kOrbStack] { { "MIX", 0.0f, 1.0f, 1.0f }, { "MIX", 0.0f, 1.0f, 1.0f }, { "MIX", 0.0f, 1.0f, 1.0f }, { "MIX", 0.0f, 1.0f, 1.0f } };
    int orbCorner = 0;
    // the stereo room
    mjq::RoomPad roomPad;
    mjq::RoomCurve roomCurve;
    Pill roomOn { "Room on", true }, roomFlatten { "Flatten" };
    Segmented roomMove { { "Still", "Left-right", "Near-far", "Both" } };
    Knob roomWidth { "WIDTH", 0.0f, 1.0f, 0.5f }, roomDepth { "DEPTH", 0.0f, 1.0f, 0.5f }, roomBars { "BARS", 1.0f, 8.0f, 1.0f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> head, body, side, under, ranges, orbStack;
};

MjqEditor::MjqEditor (MjqProcessor& p) : juce::AudioProcessorEditor (&p), proc (p), parts (std::make_unique<Parts> (*this))
{
    setLookAndFeel (&look);
    auto& q = *parts;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.grid, &q.jaq, &q.undo, &q.redo, &q.lane, &q.dialect, &q.traffic,
            &q.accent, &q.gate, &q.swing, &q.steps, &q.offset, &q.transpose, &q.mode, &q.clock, &q.direction, &q.rootBox, &q.scaleBox, &q.gridBox,
            &q.openInst, &q.closeInst, &q.level, &q.sideTabs, &q.voicePick, &q.lowKey, &q.highKey,
            &q.orbPad, &q.orbOn, &q.orbPut, &q.orbClearCorner, &q.orbGrip,
            &q.orbFxBox[0], &q.orbFxBox[1], &q.orbFxBox[2], &q.orbFxBox[3],
            &q.orbFxMix[0], &q.orbFxMix[1], &q.orbFxMix[2], &q.orbFxMix[3],
            &q.roomPad, &q.roomCurve, &q.roomOn, &q.roomFlatten, &q.roomMove,
            &q.roomWidth, &q.roomDepth, &q.roomBars,
            &q.slotBox, &q.trackBox, &q.writerBox, &q.chordBarBox,
            &q.chordRootBox, &q.chordKindBox, &q.chordLengthBox, &q.cueBox, &q.trackOn, &q.fill,
            &q.chain, &q.amount, &q.hits, &q.rotate, &q.cueN, &q.cueOf, &q.bassNote,
            &q.chordInversionBox, &q.openBank, &q.saveBank })
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
    q.mode.setItemTooltip (2, "Bass: play under the chord track (or under keys you hold)");
    q.mode.setItemTooltip (3, "Parts: give this line one voice of the chord track");
    q.mode.setItemTooltip (4, "Keys: the line keeps its shape and moves to the key you hold. Hold a triad and you get three copies of it.");
    q.mode.onSelect = [this, done] (int i) { proc.line.mode = (mjq::Mode) i; done(); };
    q.clock.setItemTooltip (0, "Pattern: the line starts again whenever the transport does");
    q.clock.setItemTooltip (1, "Song: the line sits where the song sits");
    q.clock.setItemTooltip (2, "Gate: it plays only while keys are held");
    q.clock.onSelect = [this, done] (int i) { proc.clockMode = (mjq::ClockMode) i; done(); };
    q.direction.onSelect = [this, done] (int i) { proc.line.direction = (mjq::Direction) i; done(); };

    q.openInst.setTooltip ("Open an instrument made in Batchcut's KEYS room: MidiJaQ then plays its own line, "
                           "instead of only sending MIDI to another plugin");
    q.sideTabs.setItemTooltip (0, "The line: how it plays, what key it is in, how long a step is");
    q.sideTabs.setItemTooltip (1, "The voices: what each one plays, and the keys it answers to");
    q.sideTabs.setItemTooltip (2, "The Orb");
    q.sideTabs.setItemTooltip (3, "The room: where each voice stands, and how it moves while it plays");
    q.sideTabs.onSelect = [this] (int) { resized(); refresh(); };

    q.voicePick.setTooltip ("Which of the four voices the controls below belong to");
    q.voicePick.onSelect = [this] (int i) { proc.chooseTrack (i); refresh(); };

    q.openInst.setTooltip ("The instrument this voice plays: a .NJQi, or a .NJQ instrument from Batchcut's KEYS room");
    q.openInst.onClick = [this]
    {
        auto& pr = *parts;
        const int track = proc.chosenTrack();
        pr.chooser = std::make_unique<juce::FileChooser> ("The instrument voice " + juce::String (track + 1) + " plays",
                                                          shared::instrumentsFolder(), "*.njqi;*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, track] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String err;
            proc.openInstrument (track, f, err);
            refresh();
        });
    };
    q.closeInst.setTooltip ("Take this voice's instrument away, so it makes no sound");
    q.closeInst.onClick = [this] { proc.closeInstrument (proc.chosenTrack()); refresh(); };

    q.level.setTooltip ("How loud this voice plays, so the layers of a multi can be balanced");
    q.level.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.level.onChange = [this] (float v) { proc.voicesFor (proc.chosenTrack()).level.store (v); };
    q.level.onCommit = [this] { proc.changeTrackLevel (proc.chosenTrack(), parts->level.getValue()); refresh(); };

    // the keys a voice answers to. Ranges may overlap: that is how one key starts two voices at once.
    auto noteText = [] (float v) { return gen::noteName ((int) std::round (v)); };
    q.lowKey.toText = noteText; q.highKey.toText = noteText;
    q.lowKey.step = 1.0f; q.highKey.step = 1.0f;
    q.lowKey.setTooltip ("The lowest key this voice answers to. Ranges may overlap.");
    q.highKey.setTooltip ("The highest key this voice answers to. Ranges may overlap.");
    auto setRange = [this]
    {
        auto& pr = *parts;
        int lo = (int) std::round (pr.lowKey.getValue()), hi = (int) std::round (pr.highKey.getValue());
        if (lo > hi) std::swap (lo, hi);
        proc.changeTrackRange (proc.chosenTrack(), lo, hi);
        refresh();
    };
    q.lowKey.onCommit = setRange;
    q.highKey.onCommit = setRange;

    // ---- the Orb
    q.orbPad.setTooltip ("Drag the knob to morph between the corners. Click a corner to travel to it, "
                         "shift-click to put the line you are playing into it.");
    q.orbPad.onMove = [this] (float x, float y) { proc.moveOrb (x, y); refresh(); };
    q.orbPad.onTravel = [this] (int c) { proc.recallOrbCorner (proc.chosenTrack(), c); refresh(); };
    q.orbPad.onPut = [this] (int c) { proc.putOrbCorner (proc.chosenTrack(), c); refresh(); };
    q.orbPad.onPick = [this] (int c) { parts->orbCorner = c; refresh(); };

    q.orbOn.setTooltip ("Let the Orb play this voice. With it off the line plays as it is written.");
    q.orbOn.onClick = [this] { proc.setOrbOn (proc.chosenTrack(), parts->orbOn.getToggleState()); refresh(); };

    q.orbPut.setTooltip ("Put the line this voice is playing into the chosen corner");
    q.orbPut.onClick = [this] { proc.putOrbCorner (proc.chosenTrack(), parts->orbCorner); refresh(); };
    q.orbClearCorner.setTooltip ("Take the chosen corner out, so the Orb passes through it with nothing to say");
    q.orbClearCorner.onClick = [this] { proc.clearOrbCorner (proc.chosenTrack(), parts->orbCorner); refresh(); };

    q.orbGrip.setTooltip ("How sharply a corner takes over as you reach it. Low is a wash of all eight; high snaps from one to the next.");
    q.orbGrip.toText = [] (float v) { return juce::String (v, 1); };
    q.orbGrip.onCommit = [this]
    {
        const int t = proc.chosenTrack();
        const float g = parts->orbGrip.getValue();
        for (int p = 0; p < mjq::kSessionSlots; ++p) proc.session.slots[(size_t) p].tracks[(size_t) t].orb.grip = g;
        proc.lineChanged();
        refresh();
    };

    // ---- the randomiser: right-click any of the five played controls
    auto randomiserMenu = [this] (mjq::Cycled what)
    {
        const int t = proc.chosenTrack();
        const auto& cyc = proc.session.current().tracks[(size_t) t].randomiser.cycles[(size_t) what];

        auto change = [this, t, what] (std::function<void (mjq::Cycle&)> edit)
        {
            for (int p = 0; p < mjq::kSessionSlots; ++p)
                edit (proc.session.slots[(size_t) p].tracks[(size_t) t].randomiser.cycles[(size_t) what]);
            proc.lineChanged();
            refresh();
        };

        juce::PopupMenu m;
        m.addSectionHeader (juce::String (mjq::cycledName (what)));
        m.addItem (1, cyc.on ? "Stop cycling (lands back here)" : "Cycle", true, cyc.on);
        m.addItem (2, "Roll a different take");
        m.addSeparator();

        juce::PopupMenu rate;
        const float rates[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        const char* rateNames[] = { "every 4 beats", "every 2 beats", "every beat", "twice a beat", "four a beat" };
        for (int i = 0; i < 5; ++i) rate.addItem (10 + i, rateNames[i], true, std::abs (cyc.rate - rates[i]) < 0.01f);
        m.addSubMenu ("Speed", rate);

        juce::PopupMenu sw;
        const float swings[] = { 0.5f, 0.58f, 0.66f, 0.72f };
        const char* swingNames[] = { "straight", "a little", "swung", "hard" };
        for (int i = 0; i < 4; ++i) sw.addItem (20 + i, swingNames[i], true, std::abs (cyc.swing - swings[i]) < 0.01f);
        m.addSubMenu ("Swing", sw);

        juce::PopupMenu ex;
        const float amounts[] = { 0.15f, 0.35f, 0.6f, 1.0f };
        const char* amountNames[] = { "a nudge", "some", "a lot", "anywhere" };
        for (int i = 0; i < 4; ++i) ex.addItem (30 + i, amountNames[i], true, std::abs (cyc.extremity - amounts[i]) < 0.01f);
        m.addSubMenu ("Extremity", ex);

        m.showMenuAsync (juce::PopupMenu::Options{}, [change, what, this] (int r)
        {
            if (r == 0) return;
            if (r == 1)
                change ([this, what] (mjq::Cycle& c)
                {
                    if (! c.on) c.home = currentValueOf (what);     // remember where to land
                    c.on = ! c.on;
                });
            else if (r == 2) change ([] (mjq::Cycle& c) { c.seed = (uint64_t) juce::Random::getSystemRandom().nextInt64() | 1ull; });
            else if (r >= 10 && r < 15) { static constexpr float kRates[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f }; const float v = kRates[r - 10]; change ([v] (mjq::Cycle& c) { c.rate = v; }); }
            else if (r >= 20 && r < 24) { static constexpr float kSwings[] { 0.5f, 0.58f, 0.66f, 0.72f }; const float v = kSwings[r - 20]; change ([v] (mjq::Cycle& c) { c.swing = v; }); }
            else if (r >= 30 && r < 34) { static constexpr float kReach[] { 0.15f, 0.35f, 0.6f, 1.0f }; const float v = kReach[r - 30]; change ([v] (mjq::Cycle& c) { c.extremity = v; }); }
        });
    };

    q.dialect.onRightClick = [randomiserMenu] { randomiserMenu (mjq::Cycled::Dialect); };
    q.traffic.onRightClick = [randomiserMenu] { randomiserMenu (mjq::Cycled::Traffic); };
    q.accent.onRightClick  = [randomiserMenu] { randomiserMenu (mjq::Cycled::Accent); };
    q.gate.onRightClick    = [randomiserMenu] { randomiserMenu (mjq::Cycled::Gate); };
    q.swing.onRightClick   = [randomiserMenu] { randomiserMenu (mjq::Cycled::Swing); };

    // ---- the stereo room
    auto eachSlot = [this] (std::function<void (mjq::Place&)> change)
    {
        const int t = proc.chosenTrack();
        for (int p = 0; p < mjq::kSessionSlots; ++p) change (proc.session.slots[(size_t) p].tracks[(size_t) t].place);
        proc.lineChanged();
        refresh();
    };

    q.roomPad.setTooltip ("Drag a voice around the room: left and right across, close and far up the page. "
                          "You are at the bottom. All four voices are shown, so a multi can be arranged by eye.");
    q.roomPad.onMove = [eachSlot] (float x, float y) { eachSlot ([x, y] (mjq::Place& p) { p.x = x; p.y = y; }); };

    q.roomOn.setTooltip ("Put this voice in the room. With it off the voice goes straight out, as it always did.");
    q.roomOn.onClick = [this, eachSlot] { const bool on = parts->roomOn.getToggleState(); eachSlot ([on] (mjq::Place& p) { p.on = on; }); };

    q.roomWidth.setTooltip ("How wide the voice sits: a point, or spread across the picture");
    q.roomWidth.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.roomWidth.onCommit = [this, eachSlot] { const float w = parts->roomWidth.getValue(); eachSlot ([w] (mjq::Place& p) { p.width = w; }); };

    q.roomMove.setTooltip ("What the drawn curve moves this voice through");
    q.roomMove.onSelect = [eachSlot] (int i) { eachSlot ([i] (mjq::Place& p) { p.move = (mjq::RoomMove) i; }); };

    q.roomDepth.setTooltip ("How far the curve pushes it");
    q.roomDepth.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    q.roomDepth.onCommit = [this, eachSlot] { const float d = parts->roomDepth.getValue(); eachSlot ([d] (mjq::Place& p) { p.depth = d; }); };

    q.roomBars.setTooltip ("How many bars the curve is read over");
    q.roomBars.step = 1.0f;
    q.roomBars.toText = [] (float v) { return juce::String ((int) std::round (v)); };
    q.roomBars.onCommit = [this, eachSlot] { const int b = (int) std::round (parts->roomBars.getValue()); eachSlot ([b] (mjq::Place& p) { p.bars = b; }); };

    q.roomFlatten.setTooltip ("Rub the drawn curve out, so the voice stands still again");
    q.roomFlatten.onClick = [eachSlot] { eachSlot ([] (mjq::Place& p) { p.curve.fill (0.0f); }); };

    q.roomCurve.setTooltip ("Draw how the voice moves through the bar. Drag across to draw.");
    q.roomCurve.onDraw = [this] (int i, float v)
    {
        const int t = proc.chosenTrack();
        for (int p = 0; p < mjq::kSessionSlots; ++p)
        {
            auto& place = proc.session.slots[(size_t) p].tracks[(size_t) t].place;
            if (i >= 0 && i < mjq::kRoomCurve) place.curve[(size_t) i] = v;
        }
        refresh();
    };
    q.roomCurve.onDone = [this] { proc.lineChanged(); refresh(); };

    // the four stacked effects of the chosen corner
    for (int s = 0; s < mjq::kOrbStack; ++s)
    {
        auto& box = q.orbFxBox[s];
        box.addItem ("none", 1);
        const auto& all = nb::matrixFxInfo();
        for (int i = 0; i < (int) all.size(); ++i) box.addItem (all[(size_t) i].name, i + 2);
        box.setSelectedId (1, juce::dontSendNotification);
        box.setTooltip ("An effect stacked on this corner. Corners that hold the same effect blend its settings; "
                        "where they hold different ones, the nearest corner's wins and fades in as you reach it.");
        box.onChange = [this, s]
        {
            const int t = proc.chosenTrack(), c = parts->orbCorner;
            const int chosenFx = parts->orbFxBox[s].getSelectedId() - 2;   // -1 is "none"
            for (int p = 0; p < mjq::kSessionSlots; ++p)
            {
                auto& slot = proc.session.slots[(size_t) p].tracks[(size_t) t].orb.corners[(size_t) c].stack[(size_t) s];
                if (slot.fx != chosenFx)
                {
                    slot = mjq::OrbFx {};
                    slot.fx = chosenFx;
                    slot.mix = 1.0f;
                    if (chosenFx >= 0)
                    {
                        const auto& info = nb::matrixFxInfo()[(size_t) chosenFx];
                        for (int k = 0; k < (int) info.params.size() && k < mjq::kOrbFxParams; ++k)
                            slot.p[(size_t) k] = info.params[(size_t) k].def;
                    }
                }
            }
            proc.lineChanged();
            refresh();
        };

        q.orbFxMix[s].toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
        q.orbFxMix[s].setTooltip ("How far in this effect is on this corner");
        q.orbFxMix[s].onCommit = [this, s]
        {
            const int t = proc.chosenTrack(), c = parts->orbCorner;
            const float m = parts->orbFxMix[s].getValue();
            for (int p = 0; p < mjq::kSessionSlots; ++p)
                proc.session.slots[(size_t) p].tracks[(size_t) t].orb.corners[(size_t) c].stack[(size_t) s].mix = m;
            proc.lineChanged();
            refresh();
        };
    }

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

    // Slots save automatically when another is chosen. A chain plays one slot per bar.
    for (int i = 0; i < 128; ++i) q.slotBox.addItem ("Slot " + juce::String (i + 1), i + 1);
    for (int i = 0; i < mjq::kSessionTracks; ++i) q.trackBox.addItem ("Line " + juce::String (i + 1), i + 1);
    for (int i = 0; i < 5; ++i) q.writerBox.addItem (mjq::generatorName ((mjq::Generator) i), i + 1);
    for (int i = 0; i < 8; ++i) q.chordBarBox.addItem ("Bar " + juce::String (i + 1), i + 1);
    for (int i = 0; i < 12; ++i) q.chordRootBox.addItem (gen::pitchName (i), i + 1);
    for (int i = 0; i < 9; ++i) q.chordKindBox.addItem (mjq::chordKindName ((mjq::ChordKind) i), i + 1);
    for (int i = 1; i <= 8; ++i) q.chordLengthBox.addItem (juce::String (i) + " bars", i);
    for (int i = 0; i < 4; ++i) q.chordInversionBox.addItem ("Invert " + juce::String (i), i + 1);
    for (int i = 0; i < 9; ++i)
    {
        static const char* names[] = { "Always", "Chance", "Every N", "First", "Not first", "Fill", "Not fill", "Prev", "Not prev" };
        q.cueBox.addItem (names[i], i + 1);
    }
    q.slotBox.setTooltip ("Choose one of 128 saved patterns. Every line and chord is kept in its slot.");
    q.trackBox.setTooltip ("Choose which of the four lines to edit; each sends MIDI on a separate channel.");
    q.writerBox.setTooltip ("JAQ remembers its family. HABIT turns a loop slowly, DRIFT remembers notes, ORBIT spaces hits, CUES chooses when they play.");
    q.chain.setTooltip ("How many consecutive slots play in order, one slot per bar.");
    q.trackOn.setTooltip ("Turn this line on or off. Other lines keep playing.");
    q.chordBarBox.setTooltip ("Choose which bar of the chord progression to edit.");
    q.chordLengthBox.setTooltip ("How many bars the chord progression repeats over.");
    q.cueBox.setTooltip ("CUES decides when a step may play as the line loops.");
    q.cueN.setTooltip ("For Every N, which repetition of the loop should play the notes.");
    q.cueOf.setTooltip ("For Every N, how many loop repetitions are in the cycle.");
    q.rotate.setTooltip ("For ORBIT, move the evenly spaced notes around the grid.");
    q.bassNote.setTooltip ("Choose the octave in which the chord progression sits, from C0 to C4.");
    q.chordInversionBox.setTooltip ("Lift low chord voices by an octave to change its shape.");
    q.fill.setTooltip ("Tell CUES that this pattern is a fill.");
    q.openBank.setTooltip ("Open MidiJaQ pattern slots from a .NJQ file. Instruments and kits are not replaced.");
    q.saveBank.setTooltip ("Save all your MidiJaQ slots and chord progressions as a .NJQ file.");
    q.openBank.onClick = [this]
    {
        auto& q = *parts;
        q.chooser = std::make_unique<juce::FileChooser> ("Open MidiJaQ patterns", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.njq");
        q.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (! file.existsAsFile()) return;
            juce::String error;
            if (! proc.loadPatterns (file, error)) proc.message = error;
            refresh();
        });
    };
    q.saveBank.onClick = [this]
    {
        auto& q = *parts;
        q.chooser = std::make_unique<juce::FileChooser> ("Save MidiJaQ patterns", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MidiJaQ patterns.njq"), "*.njq");
        q.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File()) return;
            juce::String error;
            if (! proc.savePatterns (file.withFileExtension ("njq"), error)) proc.message = error;
            refresh();
        });
    };
    q.slotBox.onChange = [this] { proc.chooseSlot (parts->slotBox.getSelectedId() - 1); refresh(); };
    q.trackBox.onChange = [this] { proc.chooseTrack (parts->trackBox.getSelectedId() - 1); refresh(); };
    q.writerBox.onChange = [this] { proc.changeWriter ((mjq::Generator) (parts->writerBox.getSelectedId() - 1)); refresh(); };
    q.trackOn.onClick = [this] { proc.changeTrackEnabled (parts->trackOn.getToggleState()); refresh(); };
    q.chain.step = 1.0f; q.hits.step = 1.0f;
    q.rotate.step = 1.0f; q.cueN.step = 1.0f; q.cueOf.step = 1.0f; q.bassNote.step = 1.0f;
    q.chain.toText = [] (float v) { return juce::String ((int) std::round (v)); };
    q.hits.toText = q.chain.toText;
    q.rotate.toText = q.chain.toText; q.cueN.toText = q.chain.toText; q.cueOf.toText = q.chain.toText;
    q.bassNote.toText = [] (float v) { return gen::noteName ((int) std::lround (v)); };
    q.amount.toText = q.dialect.toText;
    q.chain.onChange = [this] (float v) { proc.changeChain ((int) std::round (v)); };
    q.amount.onChange = [this] (float v)
    {
        auto& g = proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator;
        g.amount = v;
        g.cue.chance = v;
        proc.rebuildSession();
    };
    q.hits.onChange = [this] (float v)
    {
        proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator.hits = (int) std::round (v);
        proc.rebuildSession();
    };
    q.rotate.onChange = [this] (float v)
    {
        proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator.rotate = (int) std::lround (v);
        proc.rebuildSession();
    };
    q.cueN.onChange = [this] (float v)
    {
        proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator.cue.n = (uint8_t) std::lround (v);
        proc.rebuildSession();
    };
    q.cueOf.onChange = [this] (float v)
    {
        proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator.cue.of = (uint8_t) std::lround (v);
        proc.rebuildSession();
    };
    q.cueBox.onChange = [this]
    {
        auto& g = proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator;
        g.cue.kind = (gen::Cue::Kind) juce::jlimit (0, 8, parts->cueBox.getSelectedId() - 1);
        proc.rebuildSession();
    };
    q.fill.onClick = [this]
    {
        proc.session.current().tracks[(size_t) proc.session.selectedTrack].generator.fill = parts->fill.getToggleState();
        proc.rebuildSession();
    };
    q.chordBarBox.onChange = [this] { parts->chordBar = parts->chordBarBox.getSelectedId() - 1; refresh(); };
    auto chordChange = [this]
    {
        auto c = proc.session.current().chords.bars[(size_t) parts->chordBar];
        c.root = parts->chordRootBox.getSelectedId() - 1;
        c.kind = (mjq::ChordKind) (parts->chordKindBox.getSelectedId() - 1);
        c.inversion = parts->chordInversionBox.getSelectedId() - 1;
        c.bassNote = (int) std::lround (parts->bassNote.getValue());
        proc.changeChord (parts->chordBar, c);
        refresh();
    };
    q.chordRootBox.onChange = chordChange;
    q.chordKindBox.onChange = chordChange;
    q.chordInversionBox.onChange = chordChange;
    q.bassNote.onChange = [this, chordChange] (float) { chordChange(); };
    q.chordLengthBox.onChange = [this]
    {
        proc.session.current().chords.length = parts->chordLengthBox.getSelectedId();
        proc.rebuildSession();
    };

    setSize (1240, 680);
    setResizable (true, true);
    setResizeLimits (1050, 610, 1800, 1000);
    refresh();
    startTimerHz (20);
}

MjqEditor::~MjqEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MjqEditor::refresh()
{
    auto& q = *parts;
    const auto& L = proc.line;
    {
        // a cycling control shows where it is now, with a ring to say it is moving on its own
        const auto& rnd = proc.session.current().tracks[(size_t) proc.session.selectedTrack].randomiser;
        const auto now = mjq::cycleRead (rnd, proc.displayBeat());
        mjqui::Knob* const knobs[] = { &q.dialect, &q.traffic, &q.accent, &q.gate, &q.swing };
        const float written[] = { L.dialect, L.traffic, L.accent, L.gate, L.swing };
        for (int i = 0; i < (int) mjq::Cycled::Count; ++i)
        {
            knobs[i]->cycling = now.live[(size_t) i];
            knobs[i]->setValue (now.live[(size_t) i] ? now.value[(size_t) i] : written[i]);
        }
    }
    q.steps.setValue ((float) L.numSteps); q.offset.setValue ((float) L.offset); q.transpose.setValue ((float) L.transpose);
    q.mode.setSelected ((int) L.mode);
    q.slotBox.setSelectedId (proc.session.selected + 1, juce::dontSendNotification);
    q.trackBox.setSelectedId (proc.session.selectedTrack + 1, juce::dontSendNotification);
    const auto& tr = proc.session.current().tracks[(size_t) proc.session.selectedTrack];
    q.trackOn.setToggleState (tr.enabled, juce::dontSendNotification);
    // the multi: the chosen voice, the keys it answers to, how loud it is
    q.voicePick.setSelected (proc.session.selectedTrack);
    q.lowKey.setValue ((float) tr.lowKey);
    q.highKey.setValue ((float) tr.highKey);
    q.level.setValue (tr.level);
    q.closeInst.setEnabled (proc.instrumentFileFor (proc.session.selectedTrack).isNotEmpty());

    // the Orb
    {
        const auto& orb = tr.orb;
        q.orbCorner = juce::jlimit (0, mjq::kOrbCorners - 1, q.orbCorner);
        q.orbPad.setOrb (orb, proc.session.orbX, proc.session.orbY, q.orbCorner);
        q.orbOn.setToggleState (orb.on, juce::dontSendNotification);
        q.orbOn.setEnabled (orb.anySet());
        q.orbGrip.setValue (orb.grip);
        q.orbClearCorner.setEnabled (orb.corners[(size_t) q.orbCorner].set);
        const auto& corner = orb.corners[(size_t) q.orbCorner];
        for (int s = 0; s < mjq::kOrbStack; ++s)
        {
            q.orbFxBox[s].setSelectedId (corner.stack[(size_t) s].fx + 2, juce::dontSendNotification);
            q.orbFxMix[s].setValue (corner.stack[(size_t) s].mix);
            q.orbFxBox[s].setEnabled (corner.set);
            q.orbFxMix[s].setEnabled (corner.set && corner.stack[(size_t) s].fx >= 0);
        }
    }

    // the room
    {
        const auto& place = tr.place;
        std::array<mjq::Place, 4> all {};
        const auto& here = proc.session.current();
        for (int v = 0; v < mjq::kSessionTracks && v < 4; ++v) all[(size_t) v] = here.tracks[(size_t) v].place;
        const double beat = proc.displayBeat();
        q.roomPad.setVoices (all, proc.session.selectedTrack, beat, tr.line.perBar);
        q.roomCurve.setCurve (place, beat);
        q.roomOn.setToggleState (place.on, juce::dontSendNotification);
        q.roomWidth.setValue (place.width);
        q.roomMove.setSelected ((int) place.move);
        q.roomDepth.setValue (place.depth);
        q.roomBars.setValue ((float) place.bars);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &q.roomWidth, &q.roomMove, &q.roomDepth, &q.roomBars, &q.roomCurve, &q.roomFlatten })
            c->setEnabled (place.on);
    }
    q.writerBox.setSelectedId ((int) tr.generator.kind + 1, juce::dontSendNotification);
    q.chain.setValue ((float) proc.session.chainLength);
    q.amount.setValue (tr.generator.amount);
    q.hits.setValue ((float) tr.generator.hits);
    q.rotate.setValue ((float) tr.generator.rotate);
    q.cueN.setValue ((float) tr.generator.cue.n);
    q.cueOf.setValue ((float) tr.generator.cue.of);
    q.fill.setToggleState (tr.generator.fill, juce::dontSendNotification);
    q.cueBox.setSelectedId ((int) tr.generator.cue.kind + 1, juce::dontSendNotification);
    q.chordBarBox.setSelectedId (q.chordBar + 1, juce::dontSendNotification);
    const auto& ch = proc.session.current().chords.bars[(size_t) q.chordBar];
    q.chordRootBox.setSelectedId (ch.root + 1, juce::dontSendNotification);
    q.chordKindBox.setSelectedId ((int) ch.kind + 1, juce::dontSendNotification);
    q.chordInversionBox.setSelectedId (ch.inversion + 1, juce::dontSendNotification);
    q.bassNote.setValue ((float) ch.bassNote);
    q.chordLengthBox.setSelectedId (proc.session.current().chords.length, juce::dontSendNotification);
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

// The multi, drawn: the name of the chosen voice's instrument, and a keyboard strip with all four ranges on
// it. Where two bars sit over the same keys, one key starts both voices — which is the whole point, and is
// much easier to see than to read off two numbers.
// What the Orb is reading out right now, under the joystick, and the chosen corner's effect stack.
void MjqEditor::paintOrb (juce::Graphics& g)
{
    auto& q = *parts;
    const int track = proc.chosenTrack();
    const auto blend = proc.orbBlend (track);
    const auto& orb = proc.session.current().tracks[(size_t) track].orb;

    paintLabel (g, "Corner " + juce::String (q.orbCorner + 1)
                 + (orb.corners[(size_t) q.orbCorner].set ? "" : " (empty)"),
                { q.orbPut.getX(), q.orbPut.getY() - 14, 160, 12 }, juce::Justification::centredLeft, 8.0f);

    // what it is playing: the blend, so you can read the morph as well as hear it
    paintLabel (g, "Reading out", { q.orbStack.getX(), q.orbStack.getY() - 30, 160, 12 }, juce::Justification::centredLeft, 8.0f);
    const juce::Rectangle<int> readout { q.orbStack.getX(), q.orbStack.getY() - 16, q.orbStack.getWidth(), 14 };
    g.setFont (font (9.5f));
    g.setColour (orb.on && orb.anySet() ? C::dim() : C::dim().withAlpha (0.5f));
    g.drawText (orb.on && orb.anySet()
                    ? "dialect " + juce::String ((int) std::round (blend.dialect * 100.0f))
                      + "   traffic " + juce::String ((int) std::round (blend.traffic * 100.0f))
                      + "   gate " + juce::String (blend.gate, 2)
                    : "the line plays as written",
                readout, juce::Justification::centredLeft, false);

    paintLabel (g, "Stacked on this corner", { q.orbStack.getX(), q.orbStack.getY() - 2, 200, 12 }, juce::Justification::centredLeft, 8.0f);
}

// Where one of the played controls sits right now: what a cycle remembers, and lands back on.
float MjqEditor::currentValueOf (mjq::Cycled what) const
{
    const auto& L = proc.line;
    switch (what)
    {
        case mjq::Cycled::Dialect: return L.dialect;
        case mjq::Cycled::Traffic: return L.traffic;
        case mjq::Cycled::Accent:  return L.accent;
        case mjq::Cycled::Gate:    return L.gate;
        case mjq::Cycled::Swing:   return L.swing;
        default:                   return 0.5f;
    }
}

void MjqEditor::showSideTab (int tab)
{
    parts->sideTabs.setSelected (juce::jlimit (0, 3, tab));
    resized();
    refresh();
}

void MjqEditor::paintVoices (juce::Graphics& g)
{
    auto& q = *parts;
    const int track = proc.chosenTrack();

    paintLabel (g, "Voice", { q.voicePick.getX(), q.voicePick.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Instrument", { q.openInst.getX(), q.openInst.getY() - 36, 110, 12 }, juce::Justification::centredLeft, 8.0f);

    // what this voice plays
    const juce::Rectangle<int> name { q.openInst.getX(), q.openInst.getY() - 22, q.openInst.getWidth(), 20 };
    paintLcd (g, name.toFloat(), 5.0f);
    const auto file = proc.instrumentFileFor (track);
    g.setFont (font (11.0f));
    g.setColour (file.isEmpty() ? C::lcdDim() : C::lcdText());
    g.drawText (file.isEmpty() ? juce::String ("no instrument") : juce::File (file).getFileNameWithoutExtension(),
                name.reduced (8, 0), juce::Justification::centredLeft, false);

    paintLabel (g, "Keys this voice answers to", { q.ranges.getX(), q.ranges.getY() - 14, 220, 12 }, juce::Justification::centredLeft, 8.0f);

    auto strip = q.ranges.toFloat();
    paintLcd (g, strip, 5.0f);
    const float padX = 5.0f, usable = strip.getWidth() - padX * 2.0f;
    auto xOf = [&] (int note) { return strip.getX() + padX + usable * (float) juce::jlimit (0, 127, note) / 127.0f; };

    // the octaves, so the strip reads as a keyboard
    g.setColour (C::lcdDim().withAlpha (0.35f));
    for (int note = 12; note < 128; note += 12) g.drawVerticalLine ((int) xOf (note), strip.getY() + 2.0f, strip.getBottom() - 2.0f);

    const auto& slot = proc.session.current();
    const float rowH = (strip.getHeight() - 8.0f) / (float) mjq::kSessionTracks;
    for (int t = 0; t < mjq::kSessionTracks; ++t)
    {
        const auto& tr = slot.tracks[(size_t) t];
        const float y = strip.getY() + 4.0f + rowH * (float) t;
        juce::Rectangle<float> bar (xOf (tr.lowKey), y + 1.0f, std::max (2.0f, xOf (tr.highKey) - xOf (tr.lowKey)), rowH - 2.0f);
        const bool silent = ! tr.enabled || proc.instrumentFileFor (t).isEmpty();
        auto colour = t == track ? C::glow() : C::lcdDim();
        g.setColour (colour.withAlpha (silent ? 0.22f : (t == track ? 0.85f : 0.5f)));
        g.fillRoundedRectangle (bar, 2.0f);
        if (t == track) { g.setColour (C::lcdText()); g.drawRoundedRectangle (bar, 2.0f, 1.0f); }
        g.setFont (font (8.5f));
        g.setColour (C::lcdText().withAlpha (silent ? 0.35f : 0.9f));
        g.drawText (juce::String (t + 1), (int) strip.getX() + 1, (int) y, 10, (int) rowH, juce::Justification::centred, false);
    }
}

void MjqEditor::paint (juce::Graphics& g)
{
    auto& q = *parts;
    paintChassis (g, getLocalBounds().toFloat());
    paintPlate (g, q.head.toFloat(), 12.0f);
    paintPlate (g, q.side.toFloat(), 12.0f);
    paintPlate (g, q.under.toFloat(), 12.0f);

    g.setFont (caps (15.0f));
    g.setColour (C::text());
    g.drawText ("MidiJaQ", q.head.getX() + 16, q.head.getY() + 10, 200, 22, juce::Justification::centredLeft);
    paintLabel (g, "3.0 | NJS INSTRUMENT", { q.head.getX() + 16, q.head.getY() + 32, 220, 14 }, juce::Justification::centredLeft, 8.5f);

    if (q.sideTabs.getSelected() == 0)
    {
        paintLabel (g, "Key", { q.rootBox.getX(), q.rootBox.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 8.0f);
        paintLabel (g, "Scale", { q.scaleBox.getX(), q.scaleBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
        paintLabel (g, "Step", { q.gridBox.getX(), q.gridBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
        paintLabel (g, "Mode", { q.mode.getX(), q.mode.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
        paintLabel (g, "Clock", { q.clock.getX(), q.clock.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
        paintLabel (g, "Direction", { q.direction.getX(), q.direction.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    }
    else if (q.sideTabs.getSelected() == 1)
        paintVoices (g);
    else if (q.sideTabs.getSelected() == 2)
        paintOrb (g);
    paintLabel (g, "PATTERNS & LINES", { q.under.getX() + 12, q.under.getY() + 7, 220, 14 }, juce::Justification::centredLeft, 9.0f);
    paintLabel (g, "CHORD TRACK", { q.under.getX() + 12, q.under.getY() + 92, 220, 14 }, juce::Justification::centredLeft, 9.0f);

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
    const bool compact = getHeight() < 590;
    q.under = q.body.removeFromBottom (compact ? 0 : 164);
    if (! compact) q.body.removeFromBottom (8);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.slotBox, &q.trackBox, &q.writerBox, &q.chordBarBox,
            &q.chordRootBox, &q.chordKindBox, &q.chordLengthBox, &q.cueBox, &q.trackOn, &q.fill, &q.chain, &q.amount, &q.hits,
            &q.rotate, &q.cueN, &q.cueOf, &q.bassNote, &q.chordInversionBox, &q.openBank, &q.saveBank })
        c->setVisible (! compact);
    q.grid.setBounds (q.body.reduced (0, 2));

    const int x0 = q.under.getX() + 12, y0 = q.under.getY() + 27;
    q.slotBox.setBounds (x0, y0, 88, 26);
    q.trackBox.setBounds (x0 + 94, y0, 74, 26);
    q.trackOn.setBounds (x0 + 174, y0, 74, 26);
    q.chain.setBounds (x0 + 254, y0 - 7, 55, 65);
    q.writerBox.setBounds (x0 + 315, y0, 91, 26);
    q.amount.setBounds (x0 + 412, y0 - 7, 60, 65);
    q.hits.setBounds (x0 + 477, y0 - 7, 55, 65);
    q.rotate.setBounds (x0 + 539, y0 - 7, 60, 65);
    q.cueN.setBounds (x0 + 604, y0 - 7, 60, 65);
    q.cueOf.setBounds (x0 + 669, y0 - 7, 60, 65);
    q.cueBox.setBounds (x0, y0 + 37, 148, 26);
    q.fill.setBounds (x0 + 155, y0 + 37, 67, 26);
    q.openBank.setBounds (x0 + 230, y0 + 37, 132, 26);
    q.saveBank.setBounds (x0 + 370, y0 + 37, 120, 26);
    const int cy = q.under.getY() + 111;
    q.chordBarBox.setBounds (x0, cy, 84, 27);
    q.chordRootBox.setBounds (x0 + 91, cy, 71, 27);
    q.chordKindBox.setBounds (x0 + 169, cy, 120, 27);
    q.chordLengthBox.setBounds (x0 + 298, cy, 103, 27);
    q.chordInversionBox.setBounds (x0 + 409, cy, 93, 27);
    q.bassNote.setBounds (x0 + 509, cy - 11, 72, 62);

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
    const int tabsY = q.side.getY() + 10;
    q.sideTabs.setBounds (x, tabsY, w, 28);

    const int tab = q.sideTabs.getSelected();
    const int top = tabsY + 40;

    // the line
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.mode, &q.clock, &q.direction, &q.rootBox, &q.scaleBox, &q.gridBox })
        c->setVisible (tab == 0 && ! compact);
    q.mode.setBounds (x, top + 18, w, 30);
    q.clock.setBounds (x, top + 76, w, 30);
    q.direction.setBounds (x, top + 134, w, 30);
    q.rootBox.setBounds (x, top + 192, w / 2 - 4, 26);
    q.scaleBox.setBounds (x + w / 2 + 4, top + 192, w / 2 - 4, 26);
    q.gridBox.setBounds (x, top + 244, w, 26);

    // the voices
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.voicePick, &q.openInst, &q.closeInst, &q.level, &q.lowKey, &q.highKey })
        c->setVisible (tab == 1 && ! compact);
    q.voicePick.setBounds (x, top, w, 28);
    q.openInst.setBounds (x, top + 74, w, 28);
    q.closeInst.setBounds (x, top + 106, w, 24);
    q.lowKey.setBounds (x, top + 142, 66, 62);
    q.highKey.setBounds (x + 72, top + 142, 66, 62);
    q.level.setBounds (x + w - 66, top + 142, 66, 62);
    q.ranges = { x, top + 216, w, 64 };

    // the Orb
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.orbPad, &q.orbOn, &q.orbPut, &q.orbClearCorner, &q.orbGrip })
        c->setVisible (tab == 2 && ! compact);
    for (int s = 0; s < mjq::kOrbStack; ++s)
    {
        q.orbFxBox[s].setVisible (tab == 2 && ! compact);
        q.orbFxMix[s].setVisible (tab == 2 && ! compact);
    }
    q.orbOn.setBounds (x, top, w, 26);
    q.orbPad.setBounds (x, top + 32, w, w);
    const int afterPad = top + 32 + w + 22;      // room for the corner's name under the pad
    q.orbPut.setBounds (x, afterPad, w - 72, 24);
    q.orbGrip.setBounds (x + w - 66, afterPad - 6, 66, 58);
    q.orbClearCorner.setBounds (x, afterPad + 28, w - 72, 22);
    q.orbStack = { x, afterPad + 92, w, 4 * 46 };   // room for the readout and its heading above it
    for (int s = 0; s < mjq::kOrbStack; ++s)
    {
        const int rowY = q.orbStack.getY() + s * 46;
        q.orbFxBox[s].setBounds (x, rowY + 12, w - 60, 24);
        q.orbFxMix[s].setBounds (x + w - 56, rowY + 2, 56, 44);
    }

    // the room
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.roomPad, &q.roomCurve, &q.roomOn,
                                                                       &q.roomFlatten, &q.roomMove, &q.roomWidth,
                                                                       &q.roomDepth, &q.roomBars })
        c->setVisible (tab == 3 && ! compact);
    q.roomOn.setBounds (x, top, w, 26);
    q.roomPad.setBounds (x, top + 32, w, (int) (w * 0.82f));
    const int afterRoom = top + 32 + (int) (w * 0.82f) + 10;
    q.roomWidth.setBounds (x, afterRoom, 66, 58);
    q.roomMove.setBounds (x, afterRoom + 80, w, 26);
    q.roomCurve.setBounds (x, afterRoom + 124, w, 68);
    q.roomDepth.setBounds (x, afterRoom + 198, 66, 58);
    q.roomBars.setBounds (x + 72, afterRoom + 198, 66, 58);
    q.roomFlatten.setBounds (x + w - 72, afterRoom + 214, 72, 24);
}
