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
    Segmented mode { { "Free", "Follow", "Bass", "Parts" } }, clock { { "Pattern", "Song", "Gate" } }, direction { { "Forward", "Back", "Pendulum" } };
    juce::ComboBox rootBox, scaleBox, gridBox;
    juce::ComboBox slotBox, trackBox, writerBox, chordBarBox, chordRootBox, chordKindBox, chordLengthBox, cueBox, chordInversionBox;
    Pill trackOn { "Line on", true }, fill { "Fill", true };
    Pill openBank { "Open patterns" }, saveBank { "Save .NJQ" };
    Knob chain { "CHAIN", 1.0f, 128.0f, 1.0f }, amount { "CHANGE", 0.0f, 1.0f, 0.5f }, hits { "HITS", 0.0f, 32.0f, 5.0f };
    Knob rotate { "ROTATE", 0.0f, 31.0f, 0.0f }, cueN { "ON", 1.0f, 16.0f, 1.0f }, cueOf { "EVERY", 1.0f, 16.0f, 2.0f };
    Knob bassNote { "BASS", 24.0f, 72.0f, 48.0f };
    int chordBar = 0;
    Pill openInst { "Open .NJQ" }, closeInst { "Send MIDI only" };
    Knob level { "LEVEL", 0.0f, 1.5f, 0.8f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> head, body, side, under;
};

MjqEditor::MjqEditor (MjqProcessor& p) : juce::AudioProcessorEditor (&p), proc (p), parts (std::make_unique<Parts> (*this))
{
    setLookAndFeel (&look);
    auto& q = *parts;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.grid, &q.jaq, &q.undo, &q.redo, &q.lane, &q.dialect, &q.traffic,
            &q.accent, &q.gate, &q.swing, &q.steps, &q.offset, &q.transpose, &q.mode, &q.clock, &q.direction, &q.rootBox, &q.scaleBox, &q.gridBox,
            &q.openInst, &q.closeInst, &q.level, &q.slotBox, &q.trackBox, &q.writerBox, &q.chordBarBox,
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
    q.dialect.setValue (L.dialect); q.traffic.setValue (L.traffic); q.accent.setValue (L.accent);
    q.gate.setValue (L.gate); q.swing.setValue (L.swing);
    q.steps.setValue ((float) L.numSteps); q.offset.setValue ((float) L.offset); q.transpose.setValue ((float) L.transpose);
    q.mode.setSelected ((int) L.mode);
    q.slotBox.setSelectedId (proc.session.selected + 1, juce::dontSendNotification);
    q.trackBox.setSelectedId (proc.session.selectedTrack + 1, juce::dontSendNotification);
    const auto& tr = proc.session.current().tracks[(size_t) proc.session.selectedTrack];
    q.trackOn.setToggleState (tr.enabled, juce::dontSendNotification);
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

    paintLabel (g, "Key", { q.rootBox.getX(), q.rootBox.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Scale", { q.scaleBox.getX(), q.scaleBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Step", { q.gridBox.getX(), q.gridBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Instrument", { q.openInst.getX(), q.openInst.getY() - 14, 110, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Mode", { q.mode.getX(), q.mode.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Clock", { q.clock.getX(), q.clock.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Direction", { q.direction.getX(), q.direction.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 8.0f);
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
