// Batchcut 2.0 - the kit room: the pool of one-shots, the 16 lanes of a NeoBeat kit, the selected lane's
// layers, and the kit played by one of NeoBeat's 128 patterns.
#include "Editor.h"

using namespace bcui;

namespace
{
juce::String lengthText (float seconds) { return seconds < 1.0f ? juce::String ((int) std::round (seconds * 1000.0f)) + " ms" : juce::String (seconds, 2) + " s"; }

void paintStars (juce::Graphics& g, juce::Rectangle<float> r, float stars)
{
    const float w = r.getWidth() / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        juce::Path p; p.addStar ({ r.getX() + w * ((float) i + 0.5f), r.getCentreY() }, 5, w * 0.2f, w * 0.46f);
        g.setColour ((float) i < stars ? C::amber() : juce::Colour (0x38222b27));
        g.fillPath (p);
        if ((float) i < stars) { g.setColour (juce::Colour (0x70000000)); g.strokePath (p, juce::PathStrokeType (0.6f)); }
    }
}
int starAt (juce::Rectangle<int> r, int x) { return juce::jlimit (1, 5, (x - r.getX()) * 5 / std::max (1, r.getWidth()) + 1); }

// ---------------------------------------------------------------- the pool
class PoolList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PoolList (View& v) : view (v)
    {
        list.setModel (this); list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }
    std::function<void()> onChange;
    int mode = 0;                      // 0: what fits the selected lane first, 1: as gathered

    void rebuild()
    {
        auto& p = view.proc;
        order.clear();
        if (mode == 0) order = p.kitCandidates (view.kitLane);
        else for (int i = 0; i < (int) p.kit.pool.size(); ++i) order.push_back (i);
        used.assign (p.kit.pool.size(), -1);
        for (int l = 0; l < bc::kLanes; ++l) for (const auto& ly : p.kit.lanes[(size_t) l].layers) if (ly.item >= 0 && ly.item < (int) used.size()) used[(size_t) ly.item] = l;
        list.updateContent(); list.repaint();
    }
    void resized() override { list.setBounds (getLocalBounds().reduced (3)); }
    void paint (juce::Graphics& g) override
    {
        paintWell (g, getLocalBounds().toFloat(), 8.0f);
        if (order.empty())
        {
            g.setFont (font (11.5f)); g.setColour (C::dim());
            g.drawFittedText ("The pool is empty.\n\nTHIS FILE or ALL FILES gathers the one-shots Batchcut would export.\n\nFOLDER adds one-shots you already have (or drop a folder here).",
                              getLocalBounds().reduced (18), juce::Justification::centred, 9);
        }
    }

private:
    int getNumRows() override { return (int) order.size(); }
    juce::Rectangle<int> starsArea (int w, int h) const { return { w - 64, 4, 58, h - 8 }; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) order.size()) return;
        const int item = order[(size_t) row];
        const auto* it = view.proc.kit.item (item);
        if (it == nullptr) return;
        if (item == view.kitItem) { g.setColour (C::glow().withAlpha (0.45f)); g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 5.0f); }
        // what it would play in a kit
        const auto tag = juce::Rectangle<float> (4.0f, 5.0f, 58.0f, (float) h - 10.0f);
        g.setColour (C::group (it->bestRole).withAlpha (0.85f)); g.fillRoundedRectangle (tag, 4.0f);
        g.setFont (caps (7.5f)); g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawText (juce::String (nb::roleName (it->bestRole)).toUpperCase(), tag.toNearestInt(), juce::Justification::centred, true);
        const int lane = item < (int) used.size() ? used[(size_t) item] : -1;
        g.setFont (font (11.5f, lane >= 0)); g.setColour (C::text());
        g.drawText (juce::String::fromUTF8 (it->name.c_str()), 68, 0, w - 68 - 116, h, juce::Justification::centredLeft, true);
        g.setFont (font (9.5f)); g.setColour (C::dim());
        g.drawText (lane >= 0 ? "lane " + juce::String (lane + 1) : lengthText (it->seconds()), w - 114, 0, 46, h, juce::Justification::centredRight, true);
        paintStars (g, starsArea (w, h).toFloat(), view.proc.kitRating (item));
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) order.size()) return;
        const int item = order[(size_t) row];
        view.kitItem = item;
        const auto stars = starsArea (list.getWidth(), list.getRowHeight());
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m, lanes, rate;
            for (int l = 0; l < bc::kLanes; ++l) lanes.addItem (100 + l, juce::String (l + 1) + "  " + nb::roleName (l));
            for (int s = 5; s >= 1; --s) rate.addItem (200 + s, juce::String::repeatedString ("*", s));
            rate.addItem (200, "No rating");
            m.addItem (1, "Put on lane " + juce::String (view.kitLane + 1) + ", layer " + juce::String (view.kitLayer + 1));
            m.addSubMenu ("Put on lane", lanes); m.addSubMenu ("My rating", rate);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, item] (int r)
            {
                if (r == 1) view.proc.kitAssign (view.kitLane, view.kitLayer, item, true);
                else if (r >= 200) view.proc.kitRate (item, (float) (r - 200));
                else if (r >= 100) { view.kitLane = r - 100; view.kitLayer = 0; view.proc.kitAssign (view.kitLane, 0, item, true); }
                if (r != 0 && onChange) onChange();
            });
            list.repaint();
            return;
        }
        if (stars.contains (e.x, e.y % std::max (1, list.getRowHeight())) || e.x >= stars.getX())
        {
            const float s = (float) starAt (stars, e.x);
            view.proc.kitRate (item, view.proc.kitRating (item) == s ? 0.0f : s);     // the same star again takes the rating away
        }
        else view.proc.kitPlayItem (item);
        if (onChange) onChange();
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) order.size() || e.x >= starsArea (list.getWidth(), 26).getX()) return;
        view.proc.kitAssign (view.kitLane, view.kitLayer, order[(size_t) row], true);
        view.proc.kitPlayLane (view.kitLane);
        if (onChange) onChange();
    }

    View& view;
    juce::ListBox list;
    std::vector<int> order, used;
};

// ---------------------------------------------------------------- the 16 lanes
class LaneList final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LaneList (View& v) : view (v) {}
    std::function<void()> onChange;
    const nb::Pattern* pattern = nullptr;
    int step = -1;

    void paint (juce::Graphics& g) override
    {
        const auto& kit = view.proc.kit;
        const int W = getWidth(); const float rh = (float) getHeight() / (float) bc::kLanes;
        const Zones z (W);
        const int len = pattern != nullptr ? std::max (1, pattern->length) : 16;
        const int shown = std::min (32, len), page = step >= 0 ? (step / shown) * shown : 0;
        for (int l = 0; l < bc::kLanes; ++l)
        {
            const auto row = juce::Rectangle<float> (0.0f, rh * (float) l, (float) W, rh).reduced (0.0f, 1.5f);
            const auto& ln = kit.lanes[(size_t) l];
            const bool sel = l == view.kitLane, has = ! ln.empty(), muted = view.proc.kitPlayer.mute[(size_t) l].load();
            g.setColour (sel ? C::glow().withAlpha (0.42f) : juce::Colours::white.withAlpha (l % 2 ? 0.10f : 0.22f));
            g.fillRoundedRectangle (row, 6.0f);
            if (sel) { g.setColour (C::teal()); g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f); }
            const int y = (int) row.getY(), h = (int) row.getHeight();
            paintGem (g, juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ (float) z.gem + 11.0f, row.getCentreY() }), has && ! muted, has ? C::group (l) : juce::Colour());
            g.setFont (font (9.0f, true)); g.setColour (C::dim());
            g.drawText (juce::String (l + 1), z.gem + 22, y, 16, h, juce::Justification::centred);
            paintLabel (g, nb::roleName (l), { z.role, y, z.name - z.role - 4, h }, juce::Justification::centredLeft, 8.5f, has ? C::text() : C::dim());
            // the sound, and how many layers lie on it
            juce::String name = "-"; int layers = 0;
            for (const auto& ly : ln.layers) if (const auto* it = kit.item (ly.item)) { if (layers++ == 0) name = juce::String::fromUTF8 (it->name.c_str()); }
            g.setFont (font (11.5f)); g.setColour (has ? C::text() : C::dim().withAlpha (0.6f));
            g.drawText (name, z.name, y, z.prev - z.name - (layers > 1 ? 30 : 4), h, juce::Justification::centredLeft, true);
            if (layers > 1)
            {
                const auto b = juce::Rectangle<float> ((float) z.prev - 28.0f, row.getCentreY() - 7.0f, 22.0f, 14.0f);
                g.setColour (C::teal().withAlpha (0.85f)); g.fillRoundedRectangle (b, 4.0f);
                g.setFont (font (9.0f, true)); g.setColour (juce::Colours::white); g.drawText ("x" + juce::String (layers), b.toNearestInt(), juce::Justification::centred);
            }
            auto key = [&] (int x, int w, const juce::String& t, bool on, juce::Colour onCol)
            {
                const auto b = juce::Rectangle<float> ((float) x, row.getCentreY() - 10.0f, (float) w, 20.0f);
                g.setColour (on ? onCol : juce::Colour (0x24222b27)); g.fillRoundedRectangle (b, 5.0f);
                g.setColour (juce::Colour (0x50000000)); g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 0.8f);
                g.setFont (font (9.5f, true)); g.setColour (on ? juce::Colours::white : C::label()); g.drawText (t, b.toNearestInt(), juce::Justification::centred);
            };
            key (z.prev, 20, "<", false, C::teal()); key (z.next, 20, ">", false, C::teal());
            key (z.lock, 38, "LOCK", ln.locked, C::teal()); key (z.mute, 22, "M", muted, C::red());
            // where the pattern plays this lane
            const auto strip = juce::Rectangle<float> ((float) z.strip, row.getY() + 5.0f, (float) (W - 6 - z.strip), row.getHeight() - 10.0f);
            const float cw = strip.getWidth() / (float) shown;
            for (int s = 0; s < shown; ++s)
            {
                const int st = page + s;
                const auto c = juce::Rectangle<float> (strip.getX() + cw * (float) s + 0.6f, strip.getY(), cw - 1.2f, strip.getHeight());
                const int vel = pattern != nullptr && st < len ? pattern->steps[(size_t) l][(size_t) st].vel : 0;
                if (st >= len) g.setColour (juce::Colour (0x0a000000));
                else if (vel > 0) g.setColour ((has ? C::teal() : C::dim()).withAlpha (0.35f + 0.65f * (float) vel / 127.0f));
                else g.setColour (juce::Colour ((s / 4) % 2 ? 0x1c222b27u : 0x2c222b27u));
                g.fillRoundedRectangle (c, 1.6f);
                if (st == step) { g.setColour (C::amber().withAlpha (vel > 0 ? 0.95f : 0.4f)); g.fillRoundedRectangle (c, 1.6f); }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int l = juce::jlimit (0, bc::kLanes - 1, e.y * bc::kLanes / std::max (1, getHeight()));
        const Zones z (getWidth());
        auto& p = view.proc;
        const bool otherLane = l != view.kitLane;
        view.kitLane = l; if (otherLane) view.kitLayer = 0;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Put the selected pool sound here", p.kit.item (view.kitItem) != nullptr);
            m.addItem (2, "Next best sound"); m.addItem (3, "Previous");
            m.addSeparator();
            m.addItem (4, "Lock (Auto-fill, Shuffle, Random and Clear lanes leave it alone)", true, p.kit.lanes[(size_t) l].locked);
            m.addItem (5, "Clear the lane", ! p.kit.lanes[(size_t) l].empty());
            m.showMenuAsync (juce::PopupMenu::Options(), [this, l] (int r)
            {
                auto& pr = view.proc;
                if (r == 1) pr.kitAssign (l, view.kitLayer, view.kitItem, true);
                else if (r == 2) pr.kitNext (l, 1);
                else if (r == 3) pr.kitNext (l, -1);
                else if (r == 4) { pr.kit.lanes[(size_t) l].locked = ! pr.kit.lanes[(size_t) l].locked; pr.kitChanged (l); pr.kitMark(); }
                else if (r == 5) { pr.kit.lanes[(size_t) l].layers = {}; pr.kitChanged (l); pr.kitMark(); }
                if (r != 0 && onChange) onChange();
            });
        }
        else if (e.x >= z.prev && e.x < z.prev + 20) p.kitNext (l, -1);
        else if (e.x >= z.next && e.x < z.next + 20) p.kitNext (l, 1);
        else if (e.x >= z.lock && e.x < z.lock + 38) { p.kit.lanes[(size_t) l].locked = ! p.kit.lanes[(size_t) l].locked; p.kitChanged (l); p.kitMark(); }
        else if (e.x >= z.mute && e.x < z.mute + 22) p.kitPlayer.mute[(size_t) l].store (! p.kitPlayer.mute[(size_t) l].load());
        else p.kitPlayLane (l);
        if (onChange) onChange();
    }

private:
    struct Zones
    {
        explicit Zones (int W) : strip (W - 6 - (W * 30) / 100) { mute = strip - 30; lock = mute - 44; next = lock - 28; prev = next - 22; }
        int gem = 4, role = 44, name = 124, prev, next, lock, mute, strip;
    };
    View& view;
};

// ---------------------------------------------------------------- a layer's sound, and the part of it that plays
class WaveView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveView (View& v) : view (v) { setTooltip ("Drag the left and right edges: only what is between them plays"); }
    std::function<void (bool done)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        paintLcd (g, b, 8.0f);
        const auto* ly = layer();
        const auto* it = ly != nullptr ? view.proc.kit.item (ly->item) : nullptr;
        if (it == nullptr || it->l.empty())
        {
            g.setFont (font (11.0f)); g.setColour (C::lcdDim());
            g.drawFittedText ("This layer is empty.\nDouble-click a pool sound to put it here.", getLocalBounds().reduced (10), juce::Justification::centred, 3);
            return;
        }
        const auto in = b.reduced (6.0f, 6.0f);
        const int w = (int) in.getWidth(), n = (int) it->l.size();
        if (peaksFor != it || (int) peaks.size() != w)
        {
            peaks.assign ((size_t) std::max (1, w), 0.0f); peaksFor = it; float top = 1.0e-4f;
            for (int x = 0; x < w; ++x)
            {
                const int a = (int) ((int64_t) x * n / w), e = std::max (a + 1, (int) ((int64_t) (x + 1) * n / w));
                float m = 0.0f; for (int i = a; i < e && i < n; ++i) m = std::max (m, std::max (std::fabs (it->l[(size_t) i]), std::fabs (it->r[(size_t) i])));
                peaks[(size_t) x] = m; top = std::max (top, m);
            }
            for (auto& v : peaks) v /= top;
        }
        const float s = std::min (ly->start, ly->end), e = std::max (ly->start, ly->end);
        for (int x = 0; x < w; ++x)
        {
            const float t = ((float) x + 0.5f) / (float) w, hgt = std::max (1.0f, peaks[(size_t) x] * in.getHeight() * 0.5f);
            g.setColour (t >= s && t <= e ? C::lcdText() : C::lcdDim().withAlpha (0.35f));
            g.fillRect (in.getX() + (float) x, in.getCentreY() - hgt, 1.0f, hgt * 2.0f);
        }
        g.setColour (C::amber());
        for (float edge : { s, e }) g.fillRect (in.getX() + edge * in.getWidth() - 1.0f, in.getY(), 2.0f, in.getHeight());
        if (ly->reverse) { g.setFont (caps (8.0f)); g.setColour (C::amber()); g.drawText ("REVERSED", getLocalBounds().reduced (10, 6), juce::Justification::topRight); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* ly = layer(); if (ly == nullptr || ly->item < 0) { grab = 0; return; }
        const float t = at (e.x);
        grab = std::fabs (t - ly->start) <= std::fabs (t - ly->end) ? 1 : 2;
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* ly = layer(); if (ly == nullptr || grab == 0) return;
        const float t = at (e.x);
        if (grab == 1) ly->start = std::min (t, ly->end - 0.01f); else ly->end = std::max (t, ly->start + 0.01f);
        ly->start = juce::jlimit (0.0f, 0.99f, ly->start); ly->end = juce::jlimit (0.01f, 1.0f, ly->end);
        if (onChange) onChange (false);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (grab != 0 && onChange) onChange (true); grab = 0; }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (auto* ly = layer()) { ly->start = 0.0f; ly->end = 1.0f; if (onChange) onChange (true); repaint(); } }

private:
    bc::LaneLayer* layer() { return &view.proc.kit.lanes[(size_t) juce::jlimit (0, bc::kLanes - 1, view.kitLane)].layers[(size_t) juce::jlimit (0, bc::kLayers - 1, view.kitLayer)]; }
    float at (int x) const { return juce::jlimit (0.0f, 1.0f, ((float) x - 6.0f) / std::max (1.0f, (float) getWidth() - 12.0f)); }
    View& view;
    std::vector<float> peaks; const bc::PoolItem* peaksFor = nullptr;
    int grab = 0;
};

class StarRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onRate;
    float stars = 0.0f;
    void paint (juce::Graphics& g) override { paintStars (g, getLocalBounds().toFloat(), stars); }
    void mouseDown (const juce::MouseEvent& e) override { const float s = (float) starAt (getLocalBounds(), e.x); if (onRate) onRate (s == stars ? 0.0f : s); }
};
} // namespace

// ---------------------------------------------------------------- the room
struct KitRoom::Parts
{
    explicit Parts (View& v) : pool (v), lanes (v), wave (v) {}
    PoolList pool; LaneList lanes; WaveView wave; StarRow stars;
    Pill fromFile { "This file" }, fromAll { "All files" }, folder { "Folder" }, clear { "Clear all" };
    Segmented sort { { "Fits the lane", "As gathered" } };
    juce::TextEditor name;
    Pill fill { "Auto-fill" }, shuffle { "Shuffle" }, random { "Random" }, clearLanes { "Clear lanes" }, undo { "Undo" }, redo { "Redo" };
    Gem anyLane { "Any lane" };
    Pill play { "Play", true }, pPrev { "<" }, pNext { ">" }, save { "Save .NJQ" };
    juce::ComboBox patternBox;
    Knob bpm { "BPM", 50.0f, 200.0f, 100.0f }, swing { "SWING", 50.0f, 75.0f, 62.0f };
    Knob laneLevel { "LEVEL", -30.0f, 24.0f, 0.0f }, lanePan { "PAN", -1.0f, 1.0f, 0.0f }, laneWidth { "WIDTH", 0.0f, 1.0f, 1.0f };
    Segmented layerTabs { { "Layer 1", "Layer 2", "Layer 3" } };
    Knob level { "LEVEL", -36.0f, 24.0f, 0.0f }, offset { "OFFSET", -50.0f, 200.0f, 0.0f }, pitch { "PITCH", -24.0f, 24.0f, 0.0f }, fade { "FADE", 0.0f, 300.0f, 4.0f };
    Gem reverse { "Reverse" };
    Pill usePool { "Use pool sound" }, clearLayer { "Clear layer" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<nb::Preset> preset; int presetIndex = -1;
    juce::Rectangle<int> poolPlate, kitPlate, lanePlate, laneTitle, layerName, hint;
};

KitRoom::KitRoom (View& v) : view (v), parts (std::make_unique<Parts> (v))
{
    auto& p = *parts; auto& proc = view.proc;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.pool, &p.lanes, &p.wave, &p.stars, &p.fromFile, &p.fromAll, &p.folder, &p.clear, &p.sort, &p.name,
            &p.fill, &p.shuffle, &p.random, &p.anyLane, &p.clearLanes, &p.undo, &p.redo, &p.play, &p.pPrev, &p.pNext, &p.save, &p.patternBox, &p.bpm, &p.swing, &p.laneLevel, &p.lanePan, &p.laneWidth,
            &p.layerTabs, &p.level, &p.offset, &p.pitch, &p.fade, &p.reverse, &p.usePool, &p.clearLayer })
        addAndMakeVisible (c);

    // ---- the pool
    p.fromFile.setTooltip ("Gather the one-shots of the open file: the same cuts EXPORT would write");
    p.fromAll.setTooltip ("Gather the one-shots of every file in the list (this takes as long as exporting them)");
    p.folder.setTooltip ("Add a folder of one-shots you already have (its sub-folders too)");
    p.clear.setTooltip ("Clear all: empty the pool. Sounds still on a lane stay (Clear lanes first to empty it completely).");
    p.sort.setItemTooltip (0, "Best for the selected lane first: how well it fits, how usable it is, how you rate sounds like it");
    p.sort.setItemTooltip (1, "In the order they were gathered");
    p.fromFile.onClick = [this] { view.proc.kitGather (false); refresh(); };
    p.fromAll.onClick = [this] { view.proc.kitGather (true); refresh(); };
    p.clear.onClick = [this] { view.proc.kitClearPool(); view.kitItem = -1; refresh(); };
    p.folder.onClick = [this]
    {
        auto& pr = *parts;
        const auto settings = shared::Settings::load();
        pr.chooser = std::make_unique<juce::FileChooser> ("A folder of one-shots", settings.importFolder.isNotEmpty() ? juce::File (settings.importFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            auto s = shared::Settings::load(); s.importFolder = dir.getFullPathName(); s.save();
            view.proc.kitAddFolder (dir);
            refresh();
        });
    };
    p.sort.onSelect = [this] (int i) { parts->pool.mode = i; parts->pool.rebuild(); };
    p.pool.onChange = [this] { refresh(); };

    // ---- the kit
    p.name.setTooltip ("The kit's name: the file is called this");
    p.name.setFont (font (13.0f, true)); p.name.setJustification (juce::Justification::centredLeft);
    p.name.onTextChange = [this] { view.proc.kit.name = parts->name.getText().trim().toStdString(); ++view.proc.kitVersion; seenVersion = view.proc.kitVersion; view.proc.stateChanged(); };
    p.name.onReturnKey = [this] { unfocusAllComponents(); };
    p.fill.setTooltip ("A sound on every lane that is not locked: the best fit for each, no sound twice, no two alike");
    p.shuffle.setTooltip ("Another kit from the same pool: each open lane takes one of its four best");
    p.random.setTooltip ("Fresh sounds on every open lane (R): dealt from every sound that fits the lane, ignoring stars and ranking. No sound comes back until all of them have played.");
    p.anyLane.setTooltip ("Random ignores the lanes' roles: any sound can land on any lane, for stranger kits");
    p.clearLanes.setTooltip ("Empty every lane that is not locked (Undo brings them back)");
    p.fill.onClick = [this] { view.proc.kitChoose(); refresh(); };
    p.shuffle.onClick = [this] { view.proc.kitShuffle(); refresh(); };
    p.random.onClick = [this] { view.proc.kitRandom (view.kitLane); refresh(); };
    p.anyLane.onClick = [this] { view.proc.kitAnyLane = parts->anyLane.getToggleState(); view.proc.stateChanged(); };
    p.clearLanes.onClick = [this] { view.proc.kitClearLanes(); refresh(); };
    p.undo.onClick = [this] { view.proc.kitUndo(); refresh(); };
    p.redo.onClick = [this] { view.proc.kitRedo(); refresh(); };
    p.undo.setTooltip ("Undo the last change to the lanes (Ctrl+Z)"); p.redo.setTooltip ("Redo (Ctrl+Shift+Z)");
    p.lanes.setTooltip ("Click a lane to hear it. < and > step through the sounds that fit. LOCK keeps a lane through Auto-fill, Shuffle, Random and Clear lanes. M mutes it in the pattern. Right-click for more.");
    p.lanes.onChange = [this] { refresh(); };

    // ---- the pattern
    for (int i = 0; i < nb::numPatterns(); ++i) p.patternBox.addItem (juce::String (i + 1).paddedLeft ('0', 3) + "  " + juce::String (nb::patternPreset (i).name), i + 1);
    p.patternBox.setTooltip ("The pattern the kit is heard in: NeoBeat's 128. It is saved in the .NJQ as scene A.");
    p.patternBox.onChange = [this]
    {
        auto& pr = view.proc;
        const int i = parts->patternBox.getSelectedId() - 1;
        if (i < 0 || i == pr.kit.pattern) return;
        pr.kitSetPattern (i, true);       // a pattern comes with its tempo; the knobs change it
        refresh();
    };
    p.pPrev.onClick = [this] { parts->patternBox.setSelectedId ((view.proc.kit.pattern + nb::numPatterns() - 1) % nb::numPatterns() + 1, juce::sendNotificationSync); };
    p.pNext.onClick = [this] { parts->patternBox.setSelectedId ((view.proc.kit.pattern + 1) % nb::numPatterns() + 1, juce::sendNotificationSync); };
    p.play.setTooltip ("Play the kit in the pattern (Space)");
    p.play.onClick = [this] { view.proc.kitPlayer.playing.store (parts->play.getToggleState()); refresh(); };
    p.bpm.main = true; p.bpm.step = 1.0f;
    p.bpm.toText = [] (float v) { return juce::String ((int) std::round (v)); };
    p.swing.toText = [] (float v) { return juce::String ((int) std::round (v)) + "%"; };
    p.bpm.setTooltip ("The kit's tempo: saved in the .NJQ, and changeable in NeoBeat");
    p.swing.setTooltip ("How late the off-beat sixteenths are (50% straight)");
    p.bpm.onChange = [this] (float v) { view.proc.kit.bpm = v; view.proc.kitPlayer.bpm.store (v); view.proc.stateChanged(); ++view.proc.kitVersion; seenVersion = view.proc.kitVersion; };
    p.swing.onChange = [this] (float v) { view.proc.kit.swing = v / 100.0f; view.proc.kitPlayer.swing.store (v / 100.0f); view.proc.stateChanged(); ++view.proc.kitVersion; seenVersion = view.proc.kitVersion; };
    p.save.setTooltip ("Save the kit, its pattern and its tempo as one .NJQ file in your NeoBeat preset folder");
    p.save.onClick = [this]
    {
        auto& pr = *parts;
        const auto dir = shared::Settings::load().presets();
        auto nm = juce::File::createLegalFileName (juce::String (view.proc.kit.name).trim()); if (nm.isEmpty()) nm = "My kit";
        pr.chooser = std::make_unique<juce::FileChooser> ("Save the kit for NeoBeat", dir.getChildFile (nm + ".njq"), "*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (! view.proc.kitSaveNjq (f.withFileExtension ("njq"), err)) view.proc.kitMessage = "Not saved: " + err;
            refresh();
        });
    };

    // ---- the selected lane and its layers
    auto db = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    p.laneLevel.toText = db; p.level.toText = db;
    p.lanePan.toText = [] (float v) { return std::fabs (v) < 0.01f ? juce::String ("C") : juce::String ((int) std::round (std::fabs (v) * 100.0f)) + (v < 0 ? " L" : " R"); };
    p.laneWidth.toText = [] (float v) { return v < 0.01f ? juce::String ("MONO") : juce::String ((int) std::round (v * 100.0f)) + "%"; };
    p.offset.toText = [] (float v) { return juce::String (v, 1) + " ms"; };
    p.pitch.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; };
    p.fade.toText = [] (float v) { return juce::String ((int) std::round (v)) + " ms"; };
    p.laneLevel.setTooltip ("The lane's level in the kit. Every sound is first matched to the loudness of NeoBeat's own sounds.");
    p.laneWidth.setTooltip ("From mono to the sound's own stereo. NeoBeat's WIDTH knob can widen it further.");
    p.offset.setTooltip ("When this layer starts against the others: later (or, below 0, earlier)");
    p.pitch.setTooltip ("This layer's pitch, in semitones");
    p.fade.setTooltip ("The fade at the end of the part that plays");
    auto lane = [this] () -> bc::KitLane& { return view.proc.kit.lanes[(size_t) view.kitLane]; };
    auto layer = [this] () -> bc::LaneLayer& { return view.proc.kit.lanes[(size_t) view.kitLane].layers[(size_t) view.kitLayer]; };
    auto live = [this] { view.proc.kitChanged (view.kitLane); seenVersion = view.proc.kitVersion; parts->lanes.repaint(); };
    auto done = [this] { view.proc.kitMark(); view.proc.kitPlayLane (view.kitLane); refresh(); };
    p.laneLevel.onChange = [lane, live] (float v) { lane().levelDb = v; live(); };
    p.lanePan.onChange = [lane, live] (float v) { lane().pan = v; live(); };
    p.laneWidth.onChange = [lane, live] (float v) { lane().width = v; live(); };
    p.level.onChange = [layer, live] (float v) { layer().levelDb = v; live(); };
    p.offset.onChange = [layer, live] (float v) { layer().offsetMs = v; live(); };
    p.pitch.onChange = [layer, live] (float v) { layer().semis = v; live(); };
    p.fade.onChange = [layer, live] (float v) { layer().fadeMs = v; live(); };
    for (auto* k : { &p.laneLevel, &p.lanePan, &p.laneWidth, &p.level, &p.offset, &p.pitch, &p.fade }) k->onCommit = done;
    p.layerTabs.onSelect = [this] (int i) { view.kitLayer = i; refresh(); };
    p.reverse.setTooltip ("Play this layer backwards");
    p.reverse.onClick = [this, layer, done] { layer().reverse = parts->reverse.getToggleState(); view.proc.kitChanged (view.kitLane); done(); };
    p.usePool.setTooltip ("Put the sound selected in the pool on this layer (the same as double-clicking it)");
    p.usePool.onClick = [this] { view.proc.kitAssign (view.kitLane, view.kitLayer, view.kitItem, true); view.proc.kitPlayLane (view.kitLane); refresh(); };
    p.clearLayer.onClick = [this] { view.proc.kitAssign (view.kitLane, view.kitLayer, -1, false); refresh(); };
    p.wave.onChange = [this] (bool finished) { view.proc.kitChanged (view.kitLane); seenVersion = view.proc.kitVersion; if (finished) { view.proc.kitMark(); view.proc.kitPlayLane (view.kitLane); refresh(); } };
    p.stars.setTooltip ("Your rating of this sound. Batchcut and NeoBeat learn from it which sounds to offer first.");
    p.stars.onRate = [this, layer] (float s) { view.proc.kitRate (layer().item, s); refresh(); };
    startTimerHz (30);
}

KitRoom::~KitRoom() { stopTimer(); }

void KitRoom::visibilityChanged() { if (! isVisible()) { view.proc.kitPlayer.playing.store (false); parts->play.setToggleState (false, juce::dontSendNotification); } }

void KitRoom::refresh()
{
    auto& p = *parts; auto& proc = view.proc; auto& kit = proc.kit;
    view.kitLane = juce::jlimit (0, bc::kLanes - 1, view.kitLane); view.kitLayer = juce::jlimit (0, bc::kLayers - 1, view.kitLayer);
    if (kit.item (view.kitItem) == nullptr) view.kitItem = -1;
    if (p.presetIndex != kit.pattern || p.preset == nullptr) { p.preset = std::make_unique<nb::Preset> (nb::patternPreset (kit.pattern)); p.presetIndex = kit.pattern; }
    p.lanes.pattern = &p.preset->pattern;
    p.patternBox.setSelectedId (kit.pattern + 1, juce::dontSendNotification);
    if (! p.name.hasKeyboardFocus (true)) p.name.setText (juce::String (kit.name), false);
    p.bpm.setValue ((float) kit.bpm); p.swing.setValue (kit.swing * 100.0f);
    const bool playing = proc.kitPlayer.playing.load();
    p.play.setToggleState (playing, juce::dontSendNotification); p.play.setButtonText (playing ? "Stop" : "Play");
    const auto& ln = kit.lanes[(size_t) view.kitLane]; const auto& ly = ln.layers[(size_t) view.kitLayer];
    p.laneLevel.setValue (ln.levelDb); p.lanePan.setValue (ln.pan); p.laneWidth.setValue (ln.width);
    p.layerTabs.setSelected (view.kitLayer);
    const bool has = kit.item (ly.item) != nullptr;
    p.level.setValue (ly.levelDb); p.offset.setValue (ly.offsetMs); p.pitch.setValue (ly.semis); p.fade.setValue (ly.fadeMs);
    p.reverse.setToggleState (ly.reverse, juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.level, &p.offset, &p.pitch, &p.fade, &p.reverse, &p.clearLayer, &p.stars }) c->setEnabled (has);
    p.usePool.setEnabled (kit.item (view.kitItem) != nullptr);
    p.stars.stars = has ? proc.kitRating (ly.item) : 0.0f;
    const bool busy = proc.session.status().busy;
    p.fromFile.setEnabled (view.doc != nullptr && ! busy); p.fromAll.setEnabled (! proc.session.files.empty() && ! busy);
    p.fill.setEnabled (! kit.pool.empty()); p.shuffle.setEnabled (! kit.pool.empty()); p.random.setEnabled (! kit.pool.empty());
    p.anyLane.setToggleState (proc.kitAnyLane, juce::dontSendNotification);
    bool clearable = false; for (const auto& l : kit.lanes) clearable = clearable || (! l.locked && ! l.empty());
    p.clearLanes.setEnabled (clearable);
    p.clear.setEnabled (! kit.pool.empty());
    p.undo.setEnabled (proc.kitCanUndo()); p.redo.setEnabled (proc.kitCanRedo());
    bool any = false; for (const auto& l : kit.lanes) any = any || ! l.empty();
    p.save.setEnabled (any);
    p.pool.rebuild();
    seenVersion = proc.kitVersion;
    repaint();
}

void KitRoom::timerCallback()
{
    if (! isVisible()) return;
    auto& proc = view.proc;
    const int st = proc.kitPlayer.step.load();
    if (st != seenStep) { seenStep = st; parts->lanes.step = st; parts->lanes.repaint(); }
    const bool busy = proc.session.status().busy;
    if (proc.kitVersion != seenVersion || busy != wasBusy) { wasBusy = busy; refresh(); }
}

bool KitRoom::key (const juce::KeyPress& key)
{
    auto& proc = view.proc;
    const auto c = key.getKeyCode();
    const auto letter = c < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c) : 0;
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::spaceKey) { proc.kitPlayer.playing.store (! proc.kitPlayer.playing.load()); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'z') { if (mods.isShiftDown()) proc.kitRedo(); else proc.kitUndo(); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'y') { proc.kitRedo(); refresh(); return true; }
    if (c == juce::KeyPress::upKey) { view.kitLane = std::max (0, view.kitLane - 1); view.kitLayer = 0; proc.kitPlayLane (view.kitLane); refresh(); return true; }
    if (c == juce::KeyPress::downKey) { view.kitLane = std::min (bc::kLanes - 1, view.kitLane + 1); view.kitLayer = 0; proc.kitPlayLane (view.kitLane); refresh(); return true; }
    if (c == juce::KeyPress::leftKey) { proc.kitNext (view.kitLane, -1); refresh(); return true; }
    if (c == juce::KeyPress::rightKey) { proc.kitNext (view.kitLane, 1); refresh(); return true; }
    if (c == juce::KeyPress::returnKey) { proc.kitPlayLane (view.kitLane); return true; }
    if (letter == 'r' && ! mods.isCommandDown() && ! mods.isAltDown()) { if (! proc.kit.pool.empty()) proc.kitRandom (view.kitLane); refresh(); return true; }
    return false;
}

void KitRoom::paint (juce::Graphics& g)
{
    auto& p = *parts; const auto& kit = view.proc.kit;
    for (auto r : { p.poolPlate, p.kitPlate, p.lanePlate }) paintPlate (g, r.toFloat(), 12.0f);
    paintLabel (g, "Pool", { p.poolPlate.getX() + 14, p.poolPlate.getY() + 8, 120, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, juce::String ((int) kit.pool.size()) + (kit.pool.size() == 1 ? " sound" : " sounds"), { p.poolPlate.getRight() - 134, p.poolPlate.getY() + 8, 120, 16 }, juce::Justification::centredRight, 8.0f, C::dim());
    paintLabel (g, "Kit", { p.kitPlate.getX() + 14, p.kitPlate.getY() + 10, 30, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "Pattern", { p.patternBox.getX(), p.patternBox.getY() - 16, 120, 14 }, juce::Justification::centredLeft, 8.0f);
    g.setFont (font (10.5f)); g.setColour (C::dim());
    g.drawFittedText (p.preset != nullptr ? juce::String (p.preset->family) + ",  " + juce::String (std::max (1, p.preset->pattern.length / 16)) + (p.preset->pattern.length > 16 ? " bars" : " bar") : juce::String(),
                      { p.patternBox.getX(), p.patternBox.getBottom() + 3, p.patternBox.getWidth(), 14 }, juce::Justification::centredLeft, 1);
    g.drawFittedText ("Click: hear it.  Double-click: put it on the selected lane.  Stars: teach it your taste.", p.hint, juce::Justification::centredLeft, 2);
    paintLabel (g, "Lane " + juce::String (view.kitLane + 1) + "   " + nb::roleName (view.kitLane), p.laneTitle, juce::Justification::centredLeft, 9.5f);
    const auto& ly = kit.lanes[(size_t) view.kitLane].layers[(size_t) view.kitLayer];
    const auto* it = kit.item (ly.item);
    g.setFont (font (12.0f, true)); g.setColour (it != nullptr ? C::text() : C::dim());
    g.drawText (it != nullptr ? juce::String::fromUTF8 (it->name.c_str()) : juce::String ("Empty layer"), p.layerName, juce::Justification::centredLeft, true);
    if (it != nullptr)
    {
        g.setFont (font (10.0f)); g.setColour (C::dim());
        g.drawText (lengthText (it->seconds()) + ",  from " + juce::String::fromUTF8 (it->source.c_str()), p.layerName.translated (0, 16), juce::Justification::centredLeft, true);
    }
    paintLabel (g, "My rating", { p.stars.getX() - 84, p.stars.getY(), 80, p.stars.getHeight() }, juce::Justification::centredLeft, 8.0f);
    g.setFont (font (10.5f)); g.setColour (C::dim());
    g.drawFittedText ("Up to three layers make one lane: a click on a thud, a second snare under the first. SAVE .NJQ mixes them into the one sound NeoBeat plays.",
                      { p.lanePlate.getX() + 14, p.clearLayer.getBottom() + 52, p.lanePlate.getWidth() - 28, 60 }, juce::Justification::topLeft, 4);
}

void KitRoom::resized()
{
    auto& p = *parts;
    p.poolPlate = { 12, 8, 316, 720 }; p.kitPlate = { 338, 8, 616, 720 }; p.lanePlate = { 964, 8, 304, 720 };
    // the pool
    { const int x = p.poolPlate.getX() + 10, y = 34, w = (p.poolPlate.getWidth() - 20 - 9) / 4;
      p.fromFile.setBounds (x, y, w, 28); p.fromAll.setBounds (x + w + 3, y, w, 28); p.folder.setBounds (x + 2 * (w + 3), y, w, 28); p.clear.setBounds (x + 3 * (w + 3), y, w, 28);
      p.sort.setBounds (x, y + 34, p.poolPlate.getWidth() - 20, 28);
      p.pool.setBounds (x, y + 68, p.poolPlate.getWidth() - 20, 584);
      p.hint = { x + 4, 690, p.poolPlate.getWidth() - 28, 32 }; }
    // the kit
    { const int x = p.kitPlate.getX();
      p.name.setBounds (x + 46, 12, 300, 26);
      // the tools: three ways to fill the lanes, the switch for the third, and the way to empty them
      p.fill.setBounds (x + 14, 46, 80, 28); p.shuffle.setBounds (x + 98, 46, 70, 28); p.random.setBounds (x + 172, 46, 74, 28);
      p.anyLane.setBounds (x + 252, 48, 88, 24); p.clearLanes.setBounds (x + 344, 46, 92, 28);
      p.undo.setBounds (x + 488, 46, 54, 28); p.redo.setBounds (x + 546, 46, 54, 28);
      p.lanes.setBounds (x + 8, 82, p.kitPlate.getWidth() - 16, 544);
      p.play.setBounds (x + 14, 662, 76, 34);
      p.pPrev.setBounds (x + 100, 664, 26, 30); p.patternBox.setBounds (x + 130, 665, 214, 28); p.pNext.setBounds (x + 348, 664, 26, 30);
      p.bpm.setBounds (x + 384, 634, 66, 88); p.swing.setBounds (x + 452, 634, 66, 88);
      p.save.setBounds (x + 524, 662, 82, 34); }
    // the lane
    { const int x = p.lanePlate.getX() + 14, w = p.lanePlate.getWidth() - 28, k3 = w / 3, k4 = w / 4;
      p.laneTitle = { x, 16, w, 16 };
      p.laneLevel.setBounds (x, 40, k3, 84); p.lanePan.setBounds (x + k3, 40, k3, 84); p.laneWidth.setBounds (x + 2 * k3, 40, k3, 84);
      p.layerTabs.setBounds (x, 136, w, 30);
      p.layerName = { x + 2, 174, w - 4, 16 };
      p.wave.setBounds (x, 212, w, 104);
      p.level.setBounds (x, 326, k4, 84); p.offset.setBounds (x + k4, 326, k4, 84); p.pitch.setBounds (x + 2 * k4, 326, k4, 84); p.fade.setBounds (x + 3 * k4, 326, k4, 84);
      p.reverse.setBounds (x, 420, 90, 24); p.stars.setBounds (x + w - 100, 420, 100, 22);
      p.usePool.setBounds (x, 456, w / 2 - 2, 28); p.clearLayer.setBounds (x + w / 2 + 2, 456, w / 2 - 2, 28); }
}
