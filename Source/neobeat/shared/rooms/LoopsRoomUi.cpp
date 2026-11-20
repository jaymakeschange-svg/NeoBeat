// Batchcut 3.0 - the LOOPS room: where the sounds come from (the pool the Kit room shares) on the left, the
// layers of chops in the middle, the selected layer and chop on the right.
#include "RoomView.h"
#include "../Ui.h"

using namespace nsui;

namespace
{
juce::Colour layerColour (int i)
{
    static const juce::uint32 c[] = { 0xff4fd9c8, 0xfff5b25a, 0xffe58ad8, 0xff8fb8ff, 0xffc6f27a, 0xffff8f7a, 0xfff2ee7a, 0xffb59cff };
    return juce::Colour (c[(size_t) ((i % 8) + 8) % 8]);
}
juce::String lengthText (double seconds) { return seconds < 1.0 ? juce::String ((int) std::round (seconds * 1000.0)) + " ms" : juce::String (seconds, 2) + " s"; }
const char* gridName (int perBar) { return perBar == 8 ? "8ths" : perBar == 12 ? "8th triplets" : perBar == 24 ? "16th triplets" : "16ths"; }

// ---------------------------------------------------------------- where a layer's sounds come from
class SourceList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit SourceList (bc::RoomView& v) : view (v)
    {
        list.setModel (this); list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }
    std::function<void (const bc::LoopSource&)> onPick;

    void rebuild()
    {
        rows.clear();
        const auto& pool = view.rooms.kit.pool;
        Row all; all.src = bc::LoopSource(); all.count = (int) pool.size(); rows.push_back (all);
        for (const auto& s : view.rooms.loopsSources())
        {
            Row r; r.src.kind = bc::LoopSource::From; r.src.from = s;
            for (const auto& it : pool) r.count += it->source == s ? 1 : 0;
            rows.push_back (r);
        }
        for (int f = 0; f < 12; ++f)
        {
            Row r; r.src.kind = bc::LoopSource::Family; r.src.family = f; r.heading = f == 0;
            for (const auto& it : pool) r.count += it->f.valid && bc::KitModel::familyOf (it->bestRole) == f ? 1 : 0;
            if (r.count > 0) rows.push_back (r);
        }
        list.updateContent(); list.repaint();
    }
    void resized() override { list.setBounds (getLocalBounds().reduced (3)); }
    void paint (juce::Graphics& g) override
    {
        paintWell (g, getLocalBounds().toFloat(), 8.0f);
        if (view.rooms.kit.pool.empty())
        {
            g.setFont (font (11.5f)); g.setColour (C::dim());
            g.drawFittedText ("The pool is empty.\n\nTHIS FILE or ALL FILES gathers the one-shots of your recordings; FOLDER adds sounds you already have (or drop a folder here).\n\nThe Kit room shares this pool.",
                              getLocalBounds().reduced (18), juce::Justification::centred, 10);
        }
    }

private:
    struct Row { bc::LoopSource src; int count = 0; bool heading = false; };
    int getNumRows() override { return view.rooms.kit.pool.empty() ? 0 : (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        const auto& r = rows[(size_t) row];
        const auto& lp = view.rooms.loops;
        const int sel = juce::jlimit (0, lp.numLayers - 1, view.loopLayer);
        const bool current = lp.layers[(size_t) sel].source == r.src;
        if (current) { g.setColour (C::glow().withAlpha (0.45f)); g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 5.0f); }
        juce::String name; juce::Colour tag = C::dim();
        if (r.src.kind == bc::LoopSource::Everything) { name = "Whole pool"; tag = C::teal(); }
        else if (r.src.kind == bc::LoopSource::From) { name = juce::String::fromUTF8 (r.src.from.c_str()); tag = C::amber(); }
        else { name = bc::familyName (r.src.family); tag = C::group (r.src.family); }
        const auto chip = juce::Rectangle<float> (6.0f, 6.0f, 52.0f, (float) h - 12.0f);
        g.setColour (tag.withAlpha (0.85f)); g.fillRoundedRectangle (chip, 4.0f);
        g.setFont (caps (7.0f)); g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawText (r.src.kind == bc::LoopSource::Everything ? "ALL" : r.src.kind == bc::LoopSource::From ? "FROM" : "KIND", chip.toNearestInt(), juce::Justification::centred);
        g.setFont (font (11.5f, current)); g.setColour (C::text());
        g.drawText (name, 66, 0, w - 66 - 48, h, juce::Justification::centredLeft, true);
        g.setFont (font (9.5f)); g.setColour (C::dim());
        g.drawText (juce::String (r.count), w - 46, 0, 40, h, juce::Justification::centredRight);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        if (onPick) onPick (rows[(size_t) row].src);
        list.repaint();
    }
    bc::RoomView& view;
    juce::ListBox list;
    std::vector<Row> rows;
};

// ---------------------------------------------------------------- the layers of chops
class LayerGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LayerGrid (bc::RoomView& v) : view (v)
    {
        setTooltip ("Click a layer to choose it, a chop to hear it; drag a chop to move it. Right-click a chop for more. M mutes a layer, S solos it.");
    }
    std::function<void()> onChange;
    double beat = -1.0;
    static constexpr int kLabelW = 132, kRowH = 52, kRuler = 18;

    juce::Rectangle<float> timeline() const { return { (float) kLabelW, (float) kRuler, (float) getWidth() - (float) kLabelW - 6.0f, (float) (bc::kLoopLayers * kRowH) }; }
    // a step's place on screen: x from the swung beat, against the whole loop
    float xOf (const bc::LoopLayer& L, int step, double loopBeats) const
    {
        const auto t = timeline();
        return t.getX() + (float) (gen::swung (step, L.perBar, view.rooms.loops.swing) / loopBeats) * t.getWidth();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& lp = view.rooms.loops;
        const auto t = timeline();
        const double beats = lp.beats();
        const int bars = lp.bars();
        // the ruler: bars and beats
        for (int b = 0; b < bars; ++b)
        {
            const float x = t.getX() + t.getWidth() * (float) b / (float) bars;
            g.setFont (font (9.5f, true)); g.setColour (C::dim());
            g.drawText ("Bar " + juce::String (b + 1), (int) x + 3, 0, 60, kRuler, juce::Justification::centredLeft);
        }
        for (int i = 0; i < bc::kLoopLayers; ++i)
        {
            const auto row = juce::Rectangle<float> (0.0f, (float) (kRuler + i * kRowH), (float) getWidth(), (float) kRowH).reduced (0.0f, 2.0f);
            const bool exists = i < lp.numLayers, sel = i == view.loopLayer;
            g.setColour (sel ? C::glow().withAlpha (0.38f) : juce::Colours::white.withAlpha (exists ? (i % 2 ? 0.12f : 0.22f) : 0.06f));
            g.fillRoundedRectangle (row, 6.0f);
            if (sel) { g.setColour (C::teal()); g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f); }
            if (! exists) continue;
            const auto& L = lp.layers[(size_t) i];
            const bool heard = lp.audible (i);
            // the label: colour, name, where from, M and S
            g.setColour (layerColour (i).withAlpha (heard ? 0.95f : 0.35f)); g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, row.getY() + 6.0f, 6.0f, row.getHeight() - 12.0f), 3.0f);
            g.setFont (font (11.5f, true)); g.setColour (heard ? C::text() : C::dim());
            g.drawText ("L" + juce::String (i + 1) + "  " + bc::loopModeName (L.mode), 18, (int) row.getY() + 4, kLabelW - 66, 18, juce::Justification::centredLeft, true);
            g.setFont (font (9.5f)); g.setColour (C::dim());
            g.drawText (juce::String (L.source.label()) + (L.random ? "  rnd" : ""), 18, (int) row.getY() + 22, kLabelW - 66, 14, juce::Justification::centredLeft, true);
            g.drawText (juce::String (L.bars) + " bar" + (L.bars > 1 ? "s" : "") + ", " + gridName (L.perBar), 18, (int) row.getY() + 34, kLabelW - 22, 13, juce::Justification::centredLeft, true);
            key (g, msRect (i, 0), "M", L.mute, C::red()); key (g, msRect (i, 1), "S", L.solo, C::amber());
            // the grid: bars and beats, the layer's repeats paler
            const auto lane = juce::Rectangle<float> (t.getX(), row.getY() + 4.0f, t.getWidth(), row.getHeight() - 8.0f);
            g.setColour (juce::Colour (0x24222b27)); g.fillRoundedRectangle (lane, 4.0f);
            for (int b = 1; b < bars * 4; ++b)
            {
                const float x = lane.getX() + lane.getWidth() * (float) b / (float) (bars * 4);
                g.setColour (juce::Colour (b % 4 == 0 ? 0x40222b27u : 0x18222b27u)); g.drawVerticalLine ((int) x, lane.getY(), lane.getBottom());
            }
            const double layerBeats = 4.0 * L.bars;
            for (double rep = 0.0; rep < beats - 1e-9; rep += layerBeats)
                for (int h = 0; h < (int) L.hits.size(); ++h)
                {
                    const auto& hit = L.hits[(size_t) h];
                    const float x0 = lane.getX() + (float) ((rep + gen::swung (hit.step, L.perBar, lp.swing)) / beats) * lane.getWidth();
                    const float x1 = lane.getX() + (float) ((rep + gen::swung (hit.step + std::max (1, hit.len), L.perBar, lp.swing)) / beats) * lane.getWidth();
                    auto r = juce::Rectangle<float> (x0 + 0.6f, lane.getY() + 3.0f, std::max (3.0f, x1 - x0 - 1.2f), lane.getHeight() - 6.0f);
                    // a chop's height and tone: loud chops taller; tuned chops sit by their pitch
                    const float hgt = r.getHeight() * (0.55f + 0.45f * std::min (1.0f, hit.vel));
                    r = r.withY (r.getBottom() - hgt).withHeight (hgt);
                    if (L.mode == bc::LoopMode::Melody) r = r.translated (0.0f, -juce::jlimit (-6.0f, 6.0f, hit.semis * 0.5f));
                    const bool chosen = sel && h == view.loopHit && rep == 0.0;
                    const auto col = layerColour (i).withAlpha (rep > 0.0 ? 0.35f : heard ? 0.85f : 0.3f);
                    g.setColour (chosen ? C::amber() : col); g.fillRoundedRectangle (r, 2.5f);
                    g.setColour (juce::Colour (rep > 0.0 ? 0x18000000u : 0x38000000u)); g.drawRoundedRectangle (r.reduced (0.25f), 2.5f, 0.6f);
                    if (hit.ratchet > 1) for (int k = 1; k < hit.ratchet; ++k) { g.setColour (juce::Colour (0x60000000)); g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / (float) hit.ratchet), r.getY(), r.getBottom()); }
                    if (hit.locked && rep == 0.0) { g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRect (r.getX() + 1.5f, r.getY() + 1.5f, 4.0f, 4.0f); }
                    if (hit.reverse) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.drawLine (r.getRight() - 2.0f, r.getY() + 2.0f, r.getX() + 2.0f, r.getBottom() - 2.0f, 1.0f); }
                    if (chosen) { g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawRoundedRectangle (r, 2.5f, 1.2f); }
                }
        }
        // the playhead
        if (beat >= 0.0)
        {
            const float x = t.getX() + t.getWidth() * (float) (std::fmod (beat, beats) / beats);
            g.setColour (C::amber()); g.fillRect (x - 1.0f, (float) kRuler, 2.0f, t.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& p = view.rooms; auto& lp = p.loops;
        const int i = (e.y - kRuler) / kRowH;
        dragHit = -1;
        if (e.y < kRuler || i < 0 || i >= bc::kLoopLayers) return;
        if (i >= lp.numLayers) { if (onChange) onChange(); return; }
        if (view.loopLayer != i) view.loopHit = -1;
        view.loopLayer = i;
        auto& L = lp.layers[(size_t) i];
        if (msRect (i, 0).contains (e.position)) { L.mute = ! L.mute; p.loopsChanged(); p.loopsMark(); if (onChange) onChange(); return; }
        if (msRect (i, 1).contains (e.position)) { L.solo = ! L.solo; p.loopsChanged(); p.loopsMark(); if (onChange) onChange(); return; }
        const int h = hitAt (i, e.position);
        if (h >= 0)
        {
            view.loopHit = h;
            if (e.mods.isPopupMenu()) { menu (i, h); return; }
            dragHit = h; dragFrom = L.hits[(size_t) h].step; moved = false;
            p.loopsPlayHit (i, h);
        }
        else view.loopHit = -1;
        if (onChange) onChange();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& lp = view.rooms.loops;
        const int i = view.loopLayer;
        if (dragHit < 0 || i < 0 || i >= lp.numLayers) return;
        auto& L = lp.layers[(size_t) i];
        if (dragHit >= (int) L.hits.size()) return;
        const int step = stepAt (L, e.position.x);
        if (step == L.hits[(size_t) dragHit].step) return;
        for (int k = 0; k < (int) L.hits.size(); ++k) if (k != dragHit && L.hits[(size_t) k].step == step) return;   // one chop to a step
        L.hits[(size_t) dragHit].step = step; moved = true;
        // the hits stay in order (the selection follows the moved one)
        const auto keep = L.hits[(size_t) dragHit];
        std::stable_sort (L.hits.begin(), L.hits.end(), [] (const bc::LoopHit& a, const bc::LoopHit& b) { return a.step < b.step; });
        for (int k = 0; k < (int) L.hits.size(); ++k) if (L.hits[(size_t) k] == keep) { dragHit = k; view.loopHit = k; }
        view.rooms.loopsChanged();
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragHit >= 0 && moved) { view.rooms.loopsMark(); if (onChange) onChange(); }
        dragHit = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // an empty step: the selected chop again there (or the layer's first)
        auto& lp = view.rooms.loops;
        const int i = (e.y - kRuler) / kRowH;
        if (i < 0 || i >= lp.numLayers || e.x < kLabelW) return;
        auto& L = lp.layers[(size_t) i];
        if (hitAt (i, e.position) >= 0 || L.hits.empty()) return;
        const int step = stepAt (L, e.position.x);
        for (const auto& h : L.hits) if (h.step == step) return;
        bc::LoopHit h = L.hits[(size_t) juce::jlimit (0, (int) L.hits.size() - 1, view.loopHit >= 0 ? view.loopHit : 0)];
        h.step = step; h.locked = false; h.len = 1; h.ratchet = 1;
        L.hits.push_back (h);
        std::stable_sort (L.hits.begin(), L.hits.end(), [] (const bc::LoopHit& a, const bc::LoopHit& b) { return a.step < b.step; });
        for (int k = 0; k < (int) L.hits.size(); ++k) if (L.hits[(size_t) k].step == step) view.loopHit = k;
        view.rooms.loopsChanged(); view.rooms.loopsMark();
        if (onChange) onChange();
    }

private:
    void key (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& t, bool on, juce::Colour onCol)
    {
        g.setColour (on ? onCol : juce::Colour (0x24222b27)); g.fillRoundedRectangle (b, 5.0f);
        g.setColour (juce::Colour (0x50000000)); g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 0.8f);
        g.setFont (font (9.5f, true)); g.setColour (on ? juce::Colours::white : C::label()); g.drawText (t, b.toNearestInt(), juce::Justification::centred);
    }
    juce::Rectangle<float> msRect (int i, int which) const { return { (float) (kLabelW - 46 + which * 22), (float) (kRuler + i * kRowH + 8), 20.0f, 18.0f }; }
    int stepAt (const bc::LoopLayer& L, float x) const
    {
        const auto t = timeline();
        const double beats = view.rooms.loops.beats();
        double b = (double) (x - t.getX()) / std::max (1.0f, t.getWidth()) * beats;
        b = std::fmod (std::max (0.0, b), 4.0 * L.bars);
        // the nearest step on the swung grid
        int best = 0; double d = 1e9;
        for (int s = 0; s < L.steps(); ++s) { const double ds = std::fabs (gen::swung (s, L.perBar, view.rooms.loops.swing) - b); if (ds < d) { d = ds; best = s; } }
        return best;
    }
    int hitAt (int i, juce::Point<float> p) const
    {
        const auto& lp = view.rooms.loops;
        if (i < 0 || i >= lp.numLayers || p.x < kLabelW) return -1;
        const auto& L = lp.layers[(size_t) i];
        const auto t = timeline();
        const double beats = lp.beats(), b = std::fmod ((double) (p.x - t.getX()) / std::max (1.0f, t.getWidth()) * beats, 4.0 * L.bars);
        for (int h = 0; h < (int) L.hits.size(); ++h)
        {
            const auto& hit = L.hits[(size_t) h];
            const double a = gen::swung (hit.step, L.perBar, lp.swing), e = gen::swung (hit.step + std::max (1, hit.len), L.perBar, lp.swing);
            if (b >= a - 0.02 && b < e) return h;
        }
        return -1;
    }
    void menu (int i, int h)
    {
        juce::PopupMenu m;
        const auto& hit = view.rooms.loops.layers[(size_t) i].hits[(size_t) h];
        m.addItem (1, "Hear it"); m.addItem (2, "Another chop here (re-roll)");
        m.addItem (3, "Lock (a new roll leaves it)", true, hit.locked); m.addItem (4, "Backwards", true, hit.reverse);
        juce::PopupMenu r; for (int k = 1; k <= 4; ++k) r.addItem (10 + k, k == 1 ? juce::String ("One hit") : juce::String (k) + " hits in the step", true, hit.ratchet == k);
        m.addSubMenu ("Ratchet", r);
        m.addSeparator(); m.addItem (5, "Delete");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, i, h] (int res)
        {
            auto& p = view.rooms;
            if (i >= p.loops.numLayers || h >= (int) p.loops.layers[(size_t) i].hits.size()) return;
            auto& H = p.loops.layers[(size_t) i].hits;
            if (res == 1) p.loopsPlayHit (i, h);
            else if (res == 2) p.loopsRerollHit (i, h);
            else if (res == 3) { H[(size_t) h].locked = ! H[(size_t) h].locked; p.loopsChanged(); p.loopsMark(); }
            else if (res == 4) { H[(size_t) h].reverse = ! H[(size_t) h].reverse; p.loopsChanged(); p.loopsMark(); p.loopsPlayHit (i, h); }
            else if (res >= 11 && res <= 14) { H[(size_t) h].ratchet = res - 10; p.loopsChanged(); p.loopsMark(); }
            else if (res == 5) { H.erase (H.begin() + h); view.loopHit = -1; p.loopsChanged(); p.loopsMark(); }
            if (res != 0 && onChange) onChange();
        });
    }
    bc::RoomView& view;
    int dragHit = -1, dragFrom = 0;
    bool moved = false;
};
} // namespace

// ---------------------------------------------------------------- the room
struct bc::LoopsRoom::Parts
{
    explicit Parts (bc::RoomView& v) : sources (v), grid (v) {}
    SourceList sources; LayerGrid grid;
    Pill fromFile { "This file" }, fromAll { "All files" }, folder { "Folder" };
    juce::TextEditor name;
    Pill rollAll { "Roll all" }, undo { "Undo" }, redo { "Redo" }, addLayer { "+ Add a layer" }, clear { "Clear all" };
    Pill play { "Play", true };
    Knob bpm { "BPM", 50.0f, 200.0f, 100.0f }, swing { "SWING", 50.0f, 75.0f, 62.0f };
    juce::ComboBox rootBox, scaleBox;
    Pill wav { "WAV" }, stems { "Stems" }, midi { "MIDI" }, njq { ".NJQ" }, bounce { "Bounce to pool" };
    // the selected layer
    Segmented mode { { "Slice", "Pool", "Stutter", "Melody" } };
    Knob density { "DENSITY", 0.0f, 1.0f, 0.5f }, dejavu { "DEJA VU", 0.0f, 1.0f, 0.6f }, level { "LEVEL", -36.0f, 12.0f, 0.0f }, pan { "PAN", -1.0f, 1.0f, 0.0f };
    Segmented bars { { "1", "2", "4", "8" } };
    juce::ComboBox gridBox, rhythmBox, laneBox;
    Gem random { "Random" };
    Segmented midiMode { { "Trigger", "Slices" } };
    Pill roll { "Roll", false }, remove { "Remove" };
    // the selected chop
    Pill chopReroll { "Re-roll" }, chopLock { "Lock", true };
    Gem chopReverse { "Reverse" };
    Knob chopPitch { "PITCH", -24.0f, 24.0f, 0.0f }, chopLevel { "LEVEL", -24.0f, 12.0f, 0.0f };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> srcPlate, mainPlate, layerPlate, chopBox, layerTitle, chopTitle, hint;
};

bc::LoopsRoom::LoopsRoom (bc::RoomView& v) : view (v), parts (std::make_unique<Parts> (v))
{
    auto& p = *parts;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.sources, &p.grid, &p.fromFile, &p.fromAll, &p.folder, &p.name, &p.rollAll, &p.undo, &p.redo, &p.addLayer, &p.clear,
            &p.play, &p.bpm, &p.swing, &p.rootBox, &p.scaleBox, &p.wav, &p.stems, &p.midi, &p.njq, &p.bounce, &p.mode, &p.density, &p.dejavu, &p.level, &p.pan,
            &p.bars, &p.gridBox, &p.rhythmBox, &p.laneBox, &p.random, &p.midiMode, &p.roll, &p.remove, &p.chopReroll, &p.chopLock, &p.chopReverse, &p.chopPitch, &p.chopLevel })
        addAndMakeVisible (c);
    auto& proc = view.rooms;
    auto layer = [this] () -> bc::LoopLayer& { auto& lp = view.rooms.loops; return lp.layers[(size_t) juce::jlimit (0, lp.numLayers - 1, view.loopLayer)]; };
    auto rollSame = [this] { view.rooms.loopsRoll (view.loopLayer, false); view.loopHit = -1; };   // a setting changed: the same roll, the new setting
    auto done = [this] { view.rooms.loopsMark(); refresh(); };

    // ---- where the sounds come from (the pool is the Kit room's)
    p.fromFile.setTooltip ("Gather the one-shots of the open file into the pool (the same cuts EXPORT makes)");
    p.fromAll.setTooltip ("Gather the one-shots of every file in the list");
    p.folder.setTooltip ("Add a folder of sounds you already have to the pool");
    p.fromFile.onClick = [this] { view.rooms.kitGather (false); refresh(); };
    p.fromAll.onClick = [this] { view.rooms.kitGather (true); refresh(); };
    p.folder.onClick = [this]
    {
        auto& pr = *parts;
        const auto settings = shared::Settings::load();
        pr.chooser = std::make_unique<juce::FileChooser> ("A folder of sounds", settings.importFolder.isNotEmpty() ? juce::File (settings.importFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            auto s = shared::Settings::load(); s.importFolder = dir.getFullPathName(); s.save();
            view.rooms.kitAddFolder (dir);
            refresh();
        });
    };
    p.sources.onPick = [this, layer] (const bc::LoopSource& src)
    {
        layer().source = src;
        view.rooms.loopsRoll (view.loopLayer, false); view.loopHit = -1;
        view.rooms.loopsMark(); refresh();
    };

    // ---- the loop
    p.name.setTooltip ("The loop's name: its files are called this");
    p.name.setFont (font (13.0f, true)); p.name.setJustification (juce::Justification::centredLeft);
    p.name.onTextChange = [this] { view.rooms.loops.name = parts->name.getText().trim().toStdString(); view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; };
    p.name.onReturnKey = [this] { unfocusAllComponents(); };
    p.clear.setTooltip ("Clear all: empty every layer and go back to one. The tempo, swing and key stay, and Undo brings the hits back.");
    p.clear.onClick = [this] { view.rooms.loopsClearAll(); view.loopHit = -1; view.loopLayer = 0; refresh(); };
    p.rollAll.setTooltip ("Roll every layer again with new seeds (Shift+R). Locked chops stay.");
    p.rollAll.onClick = [this] { view.rooms.loopsRoll (-1, true); view.loopHit = -1; refresh(); };
    p.undo.onClick = [this] { view.rooms.loopsUndo(); refresh(); };
    p.redo.onClick = [this] { view.rooms.loopsRedo(); refresh(); };
    p.undo.setTooltip ("Undo (Ctrl+Z)"); p.redo.setTooltip ("Redo (Ctrl+Shift+Z)");
    p.addLayer.setTooltip ("Another layer (up to 8), rolled from the whole pool: choose its sounds on the left");
    p.addLayer.onClick = [this] { const int i = view.rooms.loopsAddLayer(); if (i >= 0) { view.loopLayer = i; view.loopHit = -1; } refresh(); };
    p.play.setTooltip ("Play the loop (Space). In a DAW it follows the song's position while the song plays.");
    p.play.onClick = [this] { view.rooms.loopPlayer.playing.store (parts->play.getToggleState()); refresh(); };
    p.bpm.main = true; p.bpm.step = 1.0f;
    p.bpm.toText = [] (float v) { return juce::String ((int) std::round (v)); };
    p.swing.toText = [] (float v) { return juce::String ((int) std::round (v)) + "%"; };
    p.bpm.setTooltip ("The loop's tempo (in a DAW: the song's). Chops are cut to note values at this tempo.");
    p.swing.setTooltip ("How late every second step is (50% straight), as in NeoBeat");
    p.bpm.onChange = [this] (float v) { view.rooms.loops.bpm = v; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; };
    p.swing.onChange = [this] (float v) { view.rooms.loops.swing = v / 100.0f; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; parts->grid.repaint(); };
    p.bpm.onCommit = done; p.swing.onCommit = done;
    for (int k = 0; k < 12; ++k) p.rootBox.addItem (gen::pitchName (k), k + 1);
    for (int k = 0; k < gen::numScales(); ++k) p.scaleBox.addItem (gen::scale (k).name, k + 1);
    p.rootBox.setTooltip ("The key: Melody layers are tuned into it"); p.scaleBox.setTooltip ("The scale of the key");
    auto keyChanged = [this]
    {
        auto& lp = view.rooms.loops;
        lp.root = juce::jlimit (0, 11, parts->rootBox.getSelectedId() - 1); lp.scale = juce::jlimit (0, gen::numScales() - 1, parts->scaleBox.getSelectedId() - 1);
        for (int i = 0; i < lp.numLayers; ++i) if (lp.layers[(size_t) i].mode == bc::LoopMode::Melody) view.rooms.loopsRoll (i, false);
        view.rooms.loopsChanged(); view.rooms.loopsMark(); refresh();
    };
    p.rootBox.onChange = keyChanged; p.scaleBox.onChange = keyChanged;

    // ---- export
    p.wav.setTooltip ("Write the loop as a WAV file that loops seamlessly (tails wrap round to the start)");
    p.stems.setTooltip ("The loop and each layer as its own WAV, all the same length");
    p.midi.setTooltip ("A MIDI file of the chops, each on its NeoBeat lane's note (C1 up)");
    p.njq.setTooltip ("A .NJQ file for NeoBeat: up to 16 chops as lanes, the loop as a pattern");
    p.bounce.setTooltip ("The loop as one new sound in the pool, to layer or put on a kit lane");
    auto exportAudio = [this] (bool stems)
    {
        auto& pr = *parts;
        const auto dir = view.rooms.exportDir().getChildFile ("Loops");
        dir.createDirectory();
        pr.chooser = std::make_unique<juce::FileChooser> ("A folder for the loop", dir);
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, stems] (const juce::FileChooser& fc)
        {
            const auto d = fc.getResult();
            if (! d.isDirectory()) return;
            juce::String err;
            if (! view.rooms.loopsExportAudio (d, stems, err)) view.rooms.loopsMessage = "Not written: " + err;
            refresh();
        });
    };
    p.wav.onClick = [exportAudio] { exportAudio (false); };
    p.stems.onClick = [exportAudio] { exportAudio (true); };
    p.midi.onClick = [this]
    {
        auto& pr = *parts;
        const auto dir = view.rooms.midiDir();
        auto nm = juce::File::createLegalFileName (juce::String (view.rooms.loops.name).trim()); if (nm.isEmpty()) nm = "Loop";
        pr.chooser = std::make_unique<juce::FileChooser> ("Save the loop's MIDI", dir.getChildFile (nm + ".mid"), "*.mid");
        pr.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            juce::String err; if (! view.rooms.loopsExportMidi (f.withFileExtension ("mid"), err)) view.rooms.loopsMessage = "Not written: " + err;
            refresh();
        });
    };
    p.njq.onClick = [this]
    {
        auto& pr = *parts;
        auto nm = juce::File::createLegalFileName (juce::String (view.rooms.loops.name).trim()); if (nm.isEmpty()) nm = "Loop";
        pr.chooser = std::make_unique<juce::FileChooser> ("Save the loop for NeoBeat", shared::Settings::load().presets().getChildFile (nm + ".njq"), "*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            juce::String err; if (! view.rooms.loopsSaveNjq (f.withFileExtension ("njq"), err)) view.rooms.loopsMessage = "Not saved: " + err;
            refresh();
        });
    };
    p.bounce.onClick = [this] { juce::String err; if (! view.rooms.loopsBounce (err)) view.rooms.loopsMessage = "Not bounced: " + err; refresh(); };

    // ---- the selected layer
    p.mode.setItemTooltip (0, "Slice: one source's chops in a new order, each running into the next");
    p.mode.setItemTooltip (1, "Pool: low, heavy chops on the strong beats, short bright ones between");
    p.mode.setItemTooltip (2, "Stutter: a few chops, repeated and ratcheted");
    p.mode.setItemTooltip (3, "Melody: tuned chops, played in the key");
    p.mode.onSelect = [this, layer, rollSame, done] (int i) { layer().mode = (bc::LoopMode) i; rollSame(); done(); };
    p.density.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    p.dejavu.toText = [] (float v) { return v >= 0.9f ? juce::String ("LOOP") : juce::String ((int) std::round (v * 100.0f)) + "%"; };
    p.level.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    p.pan.toText = [] (float v) { return std::fabs (v) < 0.01f ? juce::String ("C") : juce::String ((int) std::round (std::fabs (v) * 100.0f)) + (v < 0 ? " L" : " R"); };
    p.density.setTooltip ("How many steps play");
    p.dejavu.setTooltip ("How often the layer repeats itself: 0 a new bar every time, all the way a bar that loops for ever");
    p.density.onChange = [this, layer, rollSame] (float v) { layer().density = v; rollSame(); seenVersion = view.rooms.loopsVersion; parts->grid.repaint(); };
    p.dejavu.onChange = [this, layer, rollSame] (float v) { layer().dejavu = v; rollSame(); seenVersion = view.rooms.loopsVersion; parts->grid.repaint(); };
    p.level.onChange = [this, layer] (float v) { layer().levelDb = v; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; };
    p.pan.onChange = [this, layer] (float v) { layer().pan = v; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; };
    for (auto* k : { &p.density, &p.dejavu, &p.level, &p.pan }) k->onCommit = done;
    p.bars.setTooltip ("The layer's length. A shorter layer repeats under a longer one.");
    p.bars.onSelect = [this, layer, rollSame, done] (int i) { static const int b[4] = { 1, 2, 4, 8 }; layer().bars = b[i]; bc::tidyLayer (layer()); rollSame(); done(); };
    for (int k : { 8, 16, 12, 24 }) p.gridBox.addItem (gridName (k), k);
    p.gridBox.setTooltip ("The layer's grid: 8ths, 16ths, or triplets");
    p.gridBox.onChange = [this, layer, rollSame, done] { const int g = parts->gridBox.getSelectedId(); if (g > 0 && g != layer().perBar) { bc::regrid (layer(), g); rollSame(); done(); } };
    p.rhythmBox.addItem ("Its own rhythm", 1);
    for (int k = 0; k < nb::numPatterns(); ++k) p.rhythmBox.addItem ("From " + juce::String (k + 1).paddedLeft ('0', 3) + " " + juce::String (nb::patternPreset (k).name), k + 2);
    p.rhythmBox.setTooltip ("Roll by the rules, or take the rhythm of a lane of one of NeoBeat's 128 patterns");
    p.rhythmBox.onChange = [this, layer, rollSame, done] { const int r = parts->rhythmBox.getSelectedId() - 2; if (r != layer().rhythm) { layer().rhythm = r; rollSame(); done(); } };
    for (int k = 0; k < 16; ++k) p.laneBox.addItem (nb::roleName (k), k + 1);
    p.laneBox.setTooltip ("Which lane of the pattern lends its rhythm");
    p.laneBox.onChange = [this, layer, rollSame, done] { const int l = parts->laneBox.getSelectedId() - 1; if (l >= 0 && l != layer().rhythmLane) { layer().rhythmLane = l; if (layer().rhythm >= 0) rollSame(); done(); } };
    p.random.setTooltip ("Random: chops dealt from every sound of the source, ignoring stars and ranking, none again until all have played (as RANDOM in the Kit room)");
    p.random.onClick = [this, layer, rollSame, done] { layer().random = parts->random.getToggleState(); rollSame(); done(); };
    p.midiMode.setItemTooltip (0, "Trigger: a key plays the loop from its start, transposed (C3 as it is)");
    p.midiMode.setItemTooltip (1, "Slices: each key from C3 up plays one of the layer's chops");
    p.midiMode.onSelect = [this, layer, done] (int i) { layer().midiMode = i; view.rooms.loopsChanged(); done(); };
    p.roll.setTooltip ("Roll this layer again with a new seed (R). Locked chops stay.");
    p.roll.onClick = [this] { view.rooms.loopsRoll (view.loopLayer, true); view.loopHit = -1; refresh(); };
    p.remove.setTooltip ("Take this layer out (Undo brings it back)");
    p.remove.onClick = [this] { view.rooms.loopsRemoveLayer (view.loopLayer); view.loopLayer = std::max (0, view.loopLayer - 1); view.loopHit = -1; refresh(); };

    // ---- the selected chop
    auto hit = [this] () -> bc::LoopHit* { auto& lp = view.rooms.loops; if (view.loopLayer < 0 || view.loopLayer >= lp.numLayers) return nullptr; auto& H = lp.layers[(size_t) view.loopLayer].hits; return view.loopHit >= 0 && view.loopHit < (int) H.size() ? &H[(size_t) view.loopHit] : nullptr; };
    p.chopReroll.setTooltip ("Another chop in this place (the place and length stay)");
    p.chopReroll.onClick = [this] { view.rooms.loopsRerollHit (view.loopLayer, view.loopHit); refresh(); };
    p.chopLock.setTooltip ("A new roll leaves a locked chop where it is (L)");
    p.chopLock.onClick = [this, hit] { if (auto* h = hit()) { h->locked = parts->chopLock.getToggleState(); view.rooms.loopsChanged(); view.rooms.loopsMark(); } refresh(); };
    p.chopReverse.onClick = [this, hit] { if (auto* h = hit()) { h->reverse = parts->chopReverse.getToggleState(); view.rooms.loopsChanged(); view.rooms.loopsMark(); view.rooms.loopsPlayHit (view.loopLayer, view.loopHit); } refresh(); };
    p.chopPitch.step = 1.0f;
    p.chopPitch.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String ((int) std::round (v)) + " st"; };
    p.chopLevel.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    p.chopPitch.onChange = [this, hit] (float v) { if (auto* h = hit()) { h->semis = v; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; parts->grid.repaint(); } };
    p.chopLevel.onChange = [this, hit] (float v) { if (auto* h = hit()) { h->levelDb = v; view.rooms.loopsChanged(); seenVersion = view.rooms.loopsVersion; } };
    p.chopPitch.onCommit = [this] { view.rooms.loopsMark(); view.rooms.loopsPlayHit (view.loopLayer, view.loopHit); refresh(); };
    p.chopLevel.onCommit = [this] { view.rooms.loopsMark(); view.rooms.loopsPlayHit (view.loopLayer, view.loopHit); refresh(); };

    p.grid.onChange = [this] { refresh(); };
    juce::ignoreUnused (proc);
    startTimerHz (30);
}

bc::LoopsRoom::~LoopsRoom() { stopTimer(); }

juce::Component& bc::LoopsRoom::grid() { return parts->grid; }

void bc::LoopsRoom::visibilityChanged() { if (! isVisible()) { view.rooms.loopPlayer.playing.store (false); parts->play.setToggleState (false, juce::dontSendNotification); } }

void bc::LoopsRoom::refresh()
{
    auto& p = *parts; auto& proc = view.rooms; auto& lp = proc.loops;
    view.loopLayer = juce::jlimit (0, lp.numLayers - 1, view.loopLayer);
    const auto& L = lp.layers[(size_t) view.loopLayer];
    if (view.loopHit >= (int) L.hits.size()) view.loopHit = -1;
    if (! p.name.hasKeyboardFocus (true)) p.name.setText (juce::String (lp.name), false);
    p.bpm.setValue ((float) lp.bpm); p.swing.setValue (lp.swing * 100.0f);
    p.rootBox.setSelectedId (lp.root + 1, juce::dontSendNotification); p.scaleBox.setSelectedId (lp.scale + 1, juce::dontSendNotification);
    const bool playing = proc.loopPlayer.playing.load();
    p.play.setToggleState (playing, juce::dontSendNotification); p.play.setButtonText (playing ? "Stop" : "Play");
    p.undo.setEnabled (proc.loopsCanUndo()); p.redo.setEnabled (proc.loopsCanRedo());
    p.addLayer.setEnabled (lp.numLayers < bc::kLoopLayers);
    const bool pool = ! proc.kit.pool.empty(), busy = proc.session.status().busy;
    p.rollAll.setEnabled (pool); p.roll.setEnabled (pool);
    p.fromFile.setEnabled (view.doc != nullptr && ! busy); p.fromAll.setEnabled (! proc.session.files.empty() && ! busy);
    bool any = false; for (int i = 0; i < lp.numLayers; ++i) any = any || ! lp.layers[(size_t) i].hits.empty();
    for (auto* b : { &p.wav, &p.stems, &p.midi, &p.njq, &p.bounce }) b->setEnabled (any);
    p.clear.setEnabled (any || lp.numLayers > 1);
    p.remove.setEnabled (lp.numLayers > 1);
    // the layer
    p.mode.setSelected ((int) L.mode);
    p.density.setValue (L.density); p.dejavu.setValue (L.dejavu); p.level.setValue (L.levelDb); p.pan.setValue (L.pan);
    p.bars.setSelected (L.bars >= 8 ? 3 : L.bars >= 4 ? 2 : L.bars >= 2 ? 1 : 0);
    p.gridBox.setSelectedId (L.perBar, juce::dontSendNotification);
    p.rhythmBox.setSelectedId (L.rhythm + 2, juce::dontSendNotification);
    p.laneBox.setSelectedId (L.rhythmLane + 1, juce::dontSendNotification);
    p.laneBox.setEnabled (L.rhythm >= 0);
    p.random.setToggleState (L.random, juce::dontSendNotification);
    p.midiMode.setSelected (L.midiMode);
    // the chop
    const bc::LoopHit* h = view.loopHit >= 0 ? &L.hits[(size_t) view.loopHit] : nullptr;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.chopReroll, &p.chopLock, &p.chopReverse, &p.chopPitch, &p.chopLevel }) c->setEnabled (h != nullptr);
    if (h != nullptr)
    {
        p.chopLock.setToggleState (h->locked, juce::dontSendNotification); p.chopReverse.setToggleState (h->reverse, juce::dontSendNotification);
        p.chopPitch.setValue (h->semis); p.chopLevel.setValue (h->levelDb);
    }
    if (seenPool != (int) proc.kit.pool.size() || seenVersion != proc.loopsVersion) p.sources.rebuild();
    seenPool = (int) proc.kit.pool.size();
    seenVersion = proc.loopsVersion;
    p.grid.repaint();
    repaint();
}

void bc::LoopsRoom::timerCallback()
{
    if (! isVisible()) return;
    auto& proc = view.rooms;
    const double b = proc.loopPlayer.beatNow.load();
    if (std::fabs (b - seenBeat) > 1e-4) { seenBeat = b; parts->grid.beat = b; parts->grid.repaint(); }
    if (proc.loopsVersion != seenVersion || (int) proc.kit.pool.size() != seenPool) refresh();
}

bool bc::LoopsRoom::key (const juce::KeyPress& key)
{
    auto& proc = view.rooms; auto& lp = proc.loops;
    const auto c = key.getKeyCode();
    const auto letter = c < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c) : 0;
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::spaceKey) { proc.loopPlayer.playing.store (! proc.loopPlayer.playing.load()); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'z') { if (mods.isShiftDown()) proc.loopsRedo(); else proc.loopsUndo(); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'y') { proc.loopsRedo(); refresh(); return true; }
    if (letter == 'r' && ! mods.isCommandDown()) { proc.loopsRoll (mods.isShiftDown() ? -1 : view.loopLayer, true); view.loopHit = -1; refresh(); return true; }
    if (c == juce::KeyPress::upKey) { view.loopLayer = std::max (0, view.loopLayer - 1); view.loopHit = -1; refresh(); return true; }
    if (c == juce::KeyPress::downKey) { view.loopLayer = std::min (lp.numLayers - 1, view.loopLayer + 1); view.loopHit = -1; refresh(); return true; }
    const auto& H = lp.layers[(size_t) juce::jlimit (0, lp.numLayers - 1, view.loopLayer)].hits;
    if ((c == juce::KeyPress::leftKey || c == juce::KeyPress::rightKey) && ! H.empty())
    {
        const int n = (int) H.size();
        view.loopHit = view.loopHit < 0 ? (c == juce::KeyPress::rightKey ? 0 : n - 1) : (view.loopHit + (c == juce::KeyPress::rightKey ? 1 : n - 1)) % n;
        proc.loopsPlayHit (view.loopLayer, view.loopHit); refresh(); return true;
    }
    if (c == juce::KeyPress::returnKey && view.loopHit >= 0) { proc.loopsPlayHit (view.loopLayer, view.loopHit); return true; }
    if ((c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) && view.loopHit >= 0 && view.loopHit < (int) H.size())
    {
        auto& HH = lp.layers[(size_t) view.loopLayer].hits;
        HH.erase (HH.begin() + view.loopHit); view.loopHit = -1;
        proc.loopsChanged(); proc.loopsMark(); refresh(); return true;
    }
    if (letter == 'l' && ! mods.isCommandDown() && view.loopHit >= 0 && view.loopHit < (int) H.size())
    {
        auto& hh = lp.layers[(size_t) view.loopLayer].hits[(size_t) view.loopHit];
        hh.locked = ! hh.locked; proc.loopsChanged(); proc.loopsMark(); refresh(); return true;
    }
    return false;
}

void bc::LoopsRoom::paint (juce::Graphics& g)
{
    auto& p = *parts; const auto& lp = view.rooms.loops;
    for (auto r : { p.srcPlate, p.mainPlate, p.layerPlate }) paintPlate (g, r.toFloat(), 12.0f);
    paintLabel (g, "Sounds", { p.srcPlate.getX() + 14, p.srcPlate.getY() + 8, 140, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, juce::String ((int) view.rooms.kit.pool.size()) + " in the pool", { p.srcPlate.getRight() - 154, p.srcPlate.getY() + 8, 140, 16 }, juce::Justification::centredRight, 8.0f, C::dim());
    g.setFont (font (10.5f)); g.setColour (C::dim());
    g.drawFittedText ("Click a source to give it to the selected layer. One pool, shared with the Kit room. Your stars steer the rolls; RANDOM ignores them.", p.hint, juce::Justification::topLeft, 3);
    paintLabel (g, "Loop", { p.mainPlate.getX() + 14, p.mainPlate.getY() + 10, 40, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "Key", { p.rootBox.getX(), p.rootBox.getY() - 15, 60, 13 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Export", { p.wav.getX(), p.wav.getY() - 15, 80, 13 }, juce::Justification::centredLeft, 8.0f);
    const auto& L = lp.layers[(size_t) juce::jlimit (0, lp.numLayers - 1, view.loopLayer)];
    paintLabel (g, "Layer " + juce::String (view.loopLayer + 1) + "   " + bc::loopModeName (L.mode), p.layerTitle, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "Bars", { p.bars.getX(), p.bars.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 7.5f);
    paintLabel (g, "Grid", { p.gridBox.getX(), p.gridBox.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 7.5f);
    paintLabel (g, "Rhythm", { p.rhythmBox.getX(), p.rhythmBox.getY() - 14, 80, 12 }, juce::Justification::centredLeft, 7.5f);
    paintLabel (g, "MIDI", { p.midiMode.getX(), p.midiMode.getY() - 14, 60, 12 }, juce::Justification::centredLeft, 7.5f);
    // the selected chop
    paintWell (g, p.chopBox.toFloat(), 10.0f);
    const bc::LoopHit* h = view.loopHit >= 0 && view.loopHit < (int) L.hits.size() ? &L.hits[(size_t) view.loopHit] : nullptr;
    paintLabel (g, "Selected chop", p.chopTitle, juce::Justification::centredLeft, 8.5f);
    g.setFont (font (12.0f, true)); g.setColour (h != nullptr ? C::text() : C::dim());
    g.drawText (h != nullptr && h->sound ? juce::String::fromUTF8 (h->sound->name.c_str()) : juce::String ("Click a chop to choose it"), p.chopTitle.translated (0, 18).withHeight (16), juce::Justification::centredLeft, true);
    if (h != nullptr && h->sound)
    {
        g.setFont (font (10.0f)); g.setColour (C::dim());
        g.drawText (lengthText (h->sound->seconds()) + ",  from " + juce::String::fromUTF8 (h->sound->source.c_str()) + ",  step " + juce::String (h->step + 1) + (h->ratchet > 1 ? ",  x" + juce::String (h->ratchet) : juce::String()),
                    p.chopTitle.translated (0, 34).withHeight (14), juce::Justification::centredLeft, true);
    }
    g.setFont (font (10.0f)); g.setColour (C::dim());
    g.drawFittedText ("R rolls the layer, Shift+R all of them. Left and right step through its chops, L locks one, Delete takes it out; double-click an empty step to copy the chop there.",
                      { p.layerPlate.getX() + 14, p.chopBox.getBottom() + 10, p.layerPlate.getWidth() - 28, 60 }, juce::Justification::topLeft, 4);
}

void bc::LoopsRoom::resized()
{
    auto& p = *parts;
    p.srcPlate = { 12, 8, 300, 720 }; p.mainPlate = { 322, 8, 652, 720 }; p.layerPlate = { 984, 8, 284, 720 };
    // the sounds
    { const int x = p.srcPlate.getX() + 10, y = 34, w = (p.srcPlate.getWidth() - 20 - 6) / 3;
      p.fromFile.setBounds (x, y, w, 28); p.fromAll.setBounds (x + w + 3, y, w, 28); p.folder.setBounds (x + 2 * (w + 3), y, w, 28);
      p.sources.setBounds (x, y + 36, p.srcPlate.getWidth() - 20, 592);
      p.hint = { x + 4, 670, p.srcPlate.getWidth() - 28, 48 }; }
    // the loop
    { const int x = p.mainPlate.getX();
      p.name.setBounds (x + 52, 12, 230, 26);
      p.rollAll.setBounds (x + 292, 11, 92, 28); p.clear.setBounds (x + 392, 11, 92, 28); p.undo.setBounds (x + 520, 11, 56, 28); p.redo.setBounds (x + 582, 11, 56, 28);
      p.grid.setBounds (x + 8, 44, p.mainPlate.getWidth() - 16, LayerGrid::kRuler + bc::kLoopLayers * LayerGrid::kRowH);
      const int y = p.grid.getBottom() + 6;
      p.addLayer.setBounds (x + 14, y, 120, 26);
      p.play.setBounds (x + 14, y + 52, 84, 34);
      p.bpm.setBounds (x + 110, y + 34, 64, 84); p.swing.setBounds (x + 178, y + 34, 64, 84);
      p.rootBox.setBounds (x + 256, y + 56, 64, 28); p.scaleBox.setBounds (x + 326, y + 56, 150, 28);
      p.wav.setBounds (x + 488, y + 40, 72, 26); p.stems.setBounds (x + 566, y + 40, 72, 26);
      p.midi.setBounds (x + 488, y + 72, 72, 26); p.njq.setBounds (x + 566, y + 72, 72, 26);
      p.bounce.setBounds (x + 488, y + 104, 150, 26); }
    // the selected layer and chop
    { const int x = p.layerPlate.getX() + 14, w = p.layerPlate.getWidth() - 28, k4 = w / 4;
      p.layerTitle = { x, 14, w, 16 };
      p.mode.setBounds (x, 36, w, 30);
      p.density.setBounds (x, 76, k4, 84); p.dejavu.setBounds (x + k4, 76, k4, 84); p.level.setBounds (x + 2 * k4, 76, k4, 84); p.pan.setBounds (x + 3 * k4, 76, k4, 84);
      p.bars.setBounds (x, 186, w / 2 - 4, 28); p.gridBox.setBounds (x + w / 2 + 4, 186, w / 2 - 4, 28);
      p.rhythmBox.setBounds (x, 236, w * 2 / 3 - 4, 28); p.laneBox.setBounds (x + w * 2 / 3 + 4, 236, w / 3 - 4, 28);
      p.random.setBounds (x, 286, 96, 24); p.midiMode.setBounds (x + w / 2 - 20, 284, w / 2 + 20, 28);
      p.roll.setBounds (x, 326, w / 2 - 4, 30); p.remove.setBounds (x + w / 2 + 4, 326, w / 2 - 4, 30);
      p.chopBox = { x - 4, 372, w + 8, 228 };
      p.chopTitle = { x + 8, 380, w - 16, 16 };
      p.chopReroll.setBounds (x + 6, 436, w / 2 - 10, 28); p.chopLock.setBounds (x + w / 2 + 4, 436, w / 2 - 10, 28);
      p.chopReverse.setBounds (x + 8, 476, 100, 24);
      p.chopPitch.setBounds (x + w / 2 - 70, 508, 72, 84); p.chopLevel.setBounds (x + w / 2 + 8, 508, 72, 84); }
}
