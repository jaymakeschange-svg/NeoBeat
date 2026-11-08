// NeoBeat 2.0 — the GRID room (Blip-style: paint a 64 x 64 grid, red volume, green pan, blue pitch)
// and the KEY MAP room (zones on a key x velocity grid, in layers: splits, velocity layers, round robin).
#include "UI.h"

namespace ui
{
// ================================================================== grid
class GridCanvas final : public juce::Component
{
public:
    explicit GridCanvas (Ctx& c) : ctx (c) {}
    float cell() const { return (float) getWidth() / 64.0f; }
    nb::GridPattern& pat() { auto& g = ctx.P().grid; return g.patterns[(size_t) g.current]; }
    uint32_t colour = nb::GridPattern::pack (200, 128, 128, nb::GridPattern::kOn);
    int tool = 0;   // 0 pencil, 1 line, 2 airbrush, 3 erase, 4 pick
    std::function<void()> onPick;

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff07120d)); g.fillRoundedRectangle (getLocalBounds().toFloat().expanded (3), 6.0f);
        const float c = cell();
        auto& p = pat();
        for (int row = 0; row < 64; ++row)
            for (int col = 0; col < 64; ++col)
            {
                const uint32_t v = p.cells[(size_t) (row * 64 + col)];
                juce::Rectangle<float> r (col * c + 0.5f, row * c + 0.5f, c - 1.0f, c - 1.0f);
                if (v & nb::GridPattern::kOn)
                {
                    g.setColour (juce::Colour ((juce::uint8) nb::GridPattern::red (v), (juce::uint8) nb::GridPattern::green (v), (juce::uint8) nb::GridPattern::blue (v)));
                    g.fillRect (r);
                    if (nb::GridPattern::flags (v) & nb::GridPattern::kFs) { g.setColour (C::amber()); g.fillRect (r.getX(), r.getY(), 3.0f, 3.0f); }
                    if (nb::GridPattern::flags (v) & nb::GridPattern::kBd) { g.setColour (C::family ("BOUNCE")); g.fillRect (r.getRight() - 3, r.getY(), 3.0f, 3.0f); }
                }
                else { g.setColour (col % 4 == 0 ? juce::Colour (0xff12251c) : juce::Colour (0xff0c1912)); g.fillRect (r); }
            }
        if (p.length < 64) { g.setColour (juce::Colour (0xaa000000)); g.fillRect (p.length * c, 0.0f, (64 - p.length) * c, (float) getHeight()); }
        const int col = ctx.proc.workstation().gridCol.load();
        if (col >= 0) { g.setColour (C::glow().withAlpha (0.6f)); g.drawRect (col * c, 0.0f, c, (float) getHeight(), 1.5f); }
        if (lineActive) { g.setColour (juce::Colours::white.withAlpha (0.7f)); g.drawLine (lineA.x * c + c / 2, lineA.y * c + c / 2, lineB.x * c + c / 2, lineB.y * c + c / 2, 1.5f); }
    }

    juce::Point<int> at (juce::Point<float> p) const { return { juce::jlimit (0, 63, (int) (p.x / cell())), juce::jlimit (0, 63, (int) (p.y / cell())) }; }
    void set (juce::Point<int> q, bool erase)
    {
        auto& v = pat().cells[(size_t) (q.y * 64 + q.x)];
        v = erase ? 0u : colour;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto q = at (e.position);
        const bool erase = tool == 3 || e.mods.isPopupMenu();
        if (tool == 4) { const auto v = pat().cells[(size_t) (q.y * 64 + q.x)]; if (v & nb::GridPattern::kOn) { colour = v; if (onPick) onPick(); } return; }
        if (tool == 1 && ! erase) { lineActive = true; lineA = lineB = q; repaint(); return; }
        paintAt (e.position, erase);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const bool erase = tool == 3 || e.mods.isPopupMenu();
        if (lineActive) { lineB = at (e.position); repaint(); return; }
        if (tool != 4) paintAt (e.position, erase);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (lineActive)
        {
            lineActive = false;
            const int n = std::max (std::abs (lineB.x - lineA.x), std::abs (lineB.y - lineA.y));
            for (int i = 0; i <= n; ++i)
            {
                const float t = n == 0 ? 0.0f : (float) i / n;
                set ({ juce::roundToInt (lineA.x + t * (lineB.x - lineA.x)), juce::roundToInt (lineA.y + t * (lineB.y - lineA.y)) }, false);
            }
        }
        ctx.changed (false);
        repaint();
    }

private:
    void paintAt (juce::Point<float> p, bool erase)
    {
        const auto q = at (p);
        if (tool == 2 && ! erase)
        {
            for (int k = 0; k < 6; ++k)
            {
                const int dx = rnd.nextInt (7) - 3, dy = rnd.nextInt (7) - 3;
                if (dx * dx + dy * dy > 9) continue;
                set ({ juce::jlimit (0, 63, q.x + dx), juce::jlimit (0, 63, q.y + dy) }, false);
            }
        }
        else set (q, erase);
        ctx.changed (false);
        repaint();
    }
    Ctx& ctx;
    bool lineActive = false;
    juce::Point<int> lineA, lineB;
    juce::Random rnd;
};

class GridRoom final : public juce::Component, public Part
{
public:
    explicit GridRoom (Ctx& c) : ctx (c), canvas (c), tools ({ "PENCIL", "LINE", "AIR", "ERASE", "PICK" })
    {
        for (int i = 0; i < 16; ++i)
        {
            auto* b = patButtons.add (new Pill (juce::String (i + 1)));
            b->onClick = [this, i] { ctx.P().grid.current = i; ctx.changed(); };
            addAndMakeVisible (b);
        }
        for (auto* b : { &play, &folder, &useKit, &clear, &fsTag, &bdTag, &fill }) addAndMakeVisible (b);
        play.setClickingTogglesState (true);
        play.onClick = [this] { ctx.P().grid.play = play.getToggleState(); ctx.changed(); };
        folder.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("A folder of samples (up to 64, one per row)", juce::File());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) { ctx.proc.loadGridFolder (fc.getResult()); if (ctx.refreshAll) ctx.refreshAll(); }
            });
        };
        useKit.onClick = [this] { ctx.P().grid.useKit = true; ctx.changed(); };
        clear.onClick = [this] { auto& g = ctx.P().grid; g.patterns[(size_t) g.current].cells.fill (0u); ctx.changed(); };
        fill.onClick = [this] { swungStarter(); };
        fsTag.setClickingTogglesState (true); bdTag.setClickingTogglesState (true);
        fsTag.onClick = bdTag.onClick = [this] { applyFlags(); };
        addAndMakeVisible (canvas);
        addAndMakeVisible (tools);
        tools.onSelect = [this] (int i) { canvas.tool = i; };
        canvas.onPick = [this] { syncSliders(); };
        const char* names[] = { "VOL", "PAN", "PITCH" };
        for (int i = 0; i < 3; ++i)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
            s->setRange (0, 255, 1);
            s->setColour (juce::Slider::trackColourId, i == 0 ? juce::Colour (0xffd04040) : i == 1 ? juce::Colour (0xff40b060) : juce::Colour (0xff4070d0));
            s->onValueChange = [this] { applyFlags(); };
            addAndMakeVisible (s);
            sliderNames.add (names[i]);
        }
        syncSliders();
        refresh();
    }

    void applyFlags()
    {
        uint32_t f = nb::GridPattern::kOn;
        if (fsTag.getToggleState()) f |= nb::GridPattern::kFs;
        if (bdTag.getToggleState()) f |= nb::GridPattern::kBd;
        canvas.colour = nb::GridPattern::pack ((int) sliders[0]->getValue(), (int) sliders[1]->getValue(), (int) sliders[2]->getValue(), f);
        repaint();
    }
    void syncSliders()
    {
        const auto c = canvas.colour;
        sliders[0]->setValue (nb::GridPattern::red (c), juce::dontSendNotification);
        sliders[1]->setValue (nb::GridPattern::green (c), juce::dontSendNotification);
        sliders[2]->setValue (nb::GridPattern::blue (c), juce::dontSendNotification);
        fsTag.setToggleState ((nb::GridPattern::flags (c) & nb::GridPattern::kFs) != 0, juce::dontSendNotification);
        bdTag.setToggleState ((nb::GridPattern::flags (c) & nb::GridPattern::kBd) != 0, juce::dontSendNotification);
        repaint();
    }
    // a swung starting point: kicks, snares, hats and a few colours in the upper rows
    void swungStarter()
    {
        auto& g = ctx.P().grid;
        auto& p = g.patterns[(size_t) g.current];
        p.cells.fill (0u); p.length = 32; p.rate = 1;
        auto put = [&] (int row, int col, int r, int gg, int b) { p.cells[(size_t) (row * 64 + col)] = nb::GridPattern::pack (r, gg, b, nb::GridPattern::kOn); };
        for (int col : { 0, 6, 10, 16, 22, 26 }) put (0, col, 230, 128, 128);
        for (int col : { 4, 12, 20, 28 }) put (2, col, 220, 128, 128);
        for (int col = 0; col < 32; col += 2) put (4, col, 120 + (col % 4) * 20, 70 + (col % 8) * 12, 128);
        for (int col : { 7, 15, 23, 31 }) put (11, col, 150, 200, 160);
        for (int col : { 3, 19 }) put (14, col, 170, 128, 100);
        g.play = true;
        ctx.changed();
    }

    juce::String rowName (int row) const
    {
        auto& G = ctx.P().grid;
        if (G.useKit || G.files.empty()) return ctx.proc.padName (row % 16);
        return juce::File (G.files[(size_t) (row % (int) G.files.size())]).getFileNameWithoutExtension();
    }

    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat(), 18.0f);
        const auto cb = canvas.getBounds();
        const float c = cb.getHeight() / 64.0f;
        g.setFont (font (std::min (9.5f, c)));
        for (int row = 0; row < 64; row += (c < 8 ? 2 : 1))
        {
            g.setColour (row % 16 == 0 ? C::text() : C::dim());
            g.drawText (juce::String (row + 1) + " " + rowName (row), 8, (int) (cb.getY() + row * c), cb.getX() - 12, (int) c + 1, juce::Justification::centredRight, true);
        }
        auto panel = juce::Rectangle<int> (cb.getRight() + 12, cb.getY(), getWidth() - cb.getRight() - 22, cb.getHeight());
        paintWell (g, panel.toFloat(), 12.0f);
        paintLabel (g, "TOOL", { panel.getX() + 8, panel.getY() + 6, 80, 12 }, juce::Justification::centredLeft, 7.5f);
        paintLabel (g, "COLOUR", { panel.getX() + 8, panel.getY() + 214, 80, 12 }, juce::Justification::centredLeft, 7.5f);
        for (int i = 0; i < 3; ++i) paintLabel (g, sliderNames[i], sliders[i]->getBounds().withY (sliders[i]->getBottom() + 2).withHeight (12), juce::Justification::centred, 6.5f);
        const auto sw = juce::Rectangle<int> (panel.getX() + 8, panel.getY() + 384, panel.getWidth() - 16, 22);
        g.setColour (juce::Colour ((juce::uint8) nb::GridPattern::red (canvas.colour), (juce::uint8) nb::GridPattern::green (canvas.colour), (juce::uint8) nb::GridPattern::blue (canvas.colour)));
        g.fillRoundedRectangle (sw.toFloat(), 5.0f);
        paintLabel (g, "TAG FOR WINDOWS", { panel.getX() + 8, panel.getY() + 414, panel.getWidth(), 12 }, juce::Justification::centredLeft, 6.5f);
        auto& G = ctx.P().grid;
        paintLabel (g, G.useKit || G.files.empty() ? "ROWS: THE KIT" : "ROWS: " + juce::File (G.folder).getFileName(), { 14, 50, 500, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        auto top = juce::Rectangle<int> (12, 10, getWidth() - 24, 32);
        for (auto* b : patButtons) b->setBounds (top.removeFromLeft (30).reduced (1, 2));
        top.removeFromLeft (8);
        play.setBounds (top.removeFromLeft (64).reduced (1, 2));
        folder.setBounds (top.removeFromLeft (74).reduced (1, 2));
        useKit.setBounds (top.removeFromLeft (70).reduced (1, 2));
        canvas.setBounds (150, 66, 576, 576);
        const int px = canvas.getRight() + 12, pw = getWidth() - canvas.getRight() - 22;
        tools.setBounds (px + 4, 66 + 20, pw - 8, 0);
        for (int i = 0; i < 5; ++i) {}
        tools.setVisible (false);
        for (int i = 0; i < toolButtons.size(); ++i) toolButtons[i]->setBounds (px + 6, 66 + 22 + i * 36, pw - 12, 30);
        for (int i = 0; i < 3; ++i) sliders[i]->setBounds (px + 6 + i * (pw - 12) / 3, 66 + 232, (pw - 12) / 3, 130);
        fsTag.setBounds (px + 6, 66 + 430, (pw - 16) / 2, 28);
        bdTag.setBounds (px + 10 + (pw - 16) / 2, 66 + 430, (pw - 16) / 2, 28);
        fill.setBounds (px + 6, 66 + 474, pw - 12, 30);
        clear.setBounds (px + 6, 66 + 510, pw - 12, 30);
    }

    void refresh() override
    {
        if (toolButtons.isEmpty())
        {
            const char* names[] = { "PENCIL", "LINE", "AIRBRUSH", "ERASE", "PICK" };
            for (int i = 0; i < 5; ++i)
            {
                auto* b = toolButtons.add (new Pill (names[i]));
                b->onClick = [this, i] { canvas.tool = i; for (int k = 0; k < toolButtons.size(); ++k) toolButtons[k]->setToggleState (k == i, juce::dontSendNotification); };
                addAndMakeVisible (b);
            }
            toolButtons[0]->setToggleState (true, juce::dontSendNotification);
            resized();
        }
        auto& G = ctx.P().grid;
        for (int i = 0; i < 16; ++i)
        {
            const bool used = ! G.patterns[(size_t) i].empty();
            patButtons[i]->setToggleState (i == G.current, juce::dontSendNotification);
            patButtons[i]->setButtonText (juce::String (i + 1) + (used ? "*" : ""));
        }
        play.setToggleState (G.play, juce::dontSendNotification);
        play.setButtonText (G.play ? "PLAYING" : "PLAY");
        repaint(); canvas.repaint();
    }

    void tick() override { const int c = ctx.proc.workstation().gridCol.load(); if (c != lastCol) { lastCol = c; canvas.repaint(); } }

private:
    Ctx& ctx;
    GridCanvas canvas;
    Segmented tools;
    juce::OwnedArray<Pill> patButtons, toolButtons;
    juce::OwnedArray<juce::Slider> sliders;
    juce::StringArray sliderNames;
    Pill play { "PLAY" }, folder { "FOLDER" }, useKit { "USE KIT" }, clear { "CLEAR" }, fsTag { "FS" }, bdTag { "BD" }, fill { "STARTER" };
    std::unique_ptr<juce::FileChooser> chooser;
    int lastCol = -2;
};

// ================================================================== key map
class ZoneGrid final : public juce::Component
{
public:
    explicit ZoneGrid (Ctx& c) : ctx (c) {}
    static constexpr int kLo = 12, kKeys = 96;   // C-1 .. B6 (C3 = 60)
    int layer = 0;
    bool drawMode = false;
    float keyW() const { return (float) getWidth() / kKeys; }
    int gridH() const { return getHeight() - 70; }
    juce::Rectangle<float> zoneRect (const nb::Zone& z) const
    {
        const float x0 = (z.loKey - kLo) * keyW(), x1 = (z.hiKey - kLo + 1) * keyW();
        const float y0 = gridH() * (1.0f - z.hiVel / 127.0f), y1 = gridH() * (1.0f - (z.loVel - 1) / 127.0f);
        return juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1);
    }
    int zoneAt (juce::Point<float> p) const
    {
        auto& zs = ctx.P().keymap.zones;
        for (int i = (int) zs.size() - 1; i >= 0; --i) if (zoneRect (zs[(size_t) i]).contains (p)) return i;
        return -1;
    }
    juce::Colour layerColour (int l) const
    {
        auto& L = ctx.P().keymap.layers;
        return l >= 0 && l < (int) L.size() ? juce::Colour (L[(size_t) l].colour) : juce::Colour (0xffc7d3ff);
    }

    void paint (juce::Graphics& g) override
    {
        auto grid = juce::Rectangle<float> (0, 0, (float) getWidth(), (float) gridH());
        paintWell (g, grid, 8.0f);
        for (int k = 0; k <= kKeys; k += 12) { g.setColour (juce::Colour (0x401f2925)); g.drawVerticalLine ((int) (k * keyW()), 0, grid.getBottom()); }
        for (int v : { 32, 64, 96 }) { g.setColour (juce::Colour (0x201f2925)); g.drawHorizontalLine ((int) (gridH() * (1 - v / 127.0f)), 0, grid.getRight()); }
        auto& zs = ctx.P().keymap.zones;
        auto& L = ctx.P().keymap.layers;
        for (int i = 0; i < (int) zs.size(); ++i)
        {
            const auto& z = zs[(size_t) i];
            auto r = zoneRect (z).reduced (0.5f);
            const bool on = z.layer < 0 || z.layer >= (int) L.size() || L[(size_t) z.layer].on;
            const bool sel = ctx.sel.kind == Sel::Zone && ctx.sel.index == i;
            g.setColour (layerColour (z.layer).withAlpha (on ? 0.85f : 0.25f)); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (sel ? C::teal() : juce::Colour (0x701f2925)); g.drawRoundedRectangle (r, 3.0f, sel ? 2.5f : 0.8f);
            if (z.fx.kind != nb::CellFx::None) { g.setColour (C::fxColour (z.fx)); g.fillRect (r.getX() + 2, r.getY() + 2, std::min (r.getWidth() - 4, 10.0f), 3.0f); }
            g.setColour (C::text()); g.setFont (font (10.0f, true));
            const juce::String label = z.name.empty() ? juce::String ("Zone ") + juce::String (i + 1) : juce::String (z.name);
            auto lr = r.getIntersection (getLocalBounds().toFloat()).reduced (3);
            if (lr.getWidth() > 30) g.drawFittedText (label, lr.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
            const float rx = (z.root - kLo + 0.5f) * keyW();
            if (z.keytrack && rx >= r.getX() && rx <= r.getRight()) { g.setColour (C::text()); g.fillEllipse (rx - 3, r.getBottom() - 8, 6, 6); }
        }
        if (drawing) { g.setColour (layerColour (layer)); g.drawRect (juce::Rectangle<float> (dragA, dragB), 1.5f); }
        // the keyboard
        auto kb = juce::Rectangle<float> (0, (float) gridH() + 8, (float) getWidth(), 60);
        for (int k = 0; k < kKeys; ++k)
        {
            const int note = kLo + k, pc = note % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            auto kr = juce::Rectangle<float> (k * keyW(), kb.getY(), keyW() - 0.5f, black ? 38.0f : 60.0f);
            juce::Colour cover = juce::Colours::transparentBlack;
            for (const auto& z : zs) if (note >= z.loKey && note <= z.hiKey) { cover = layerColour (z.layer); break; }
            g.setColour (black ? juce::Colour (0xff1c2620) : juce::Colours::white); g.fillRect (kr);
            if (! cover.isTransparent()) { g.setColour (cover); g.fillRect (kr.removeFromBottom (5)); }
            if (note == lastNote) { g.setColour (C::glow().withAlpha (0.6f)); g.fillRect (juce::Rectangle<float> (k * keyW(), kb.getY(), keyW(), 60)); }
            if (pc == 0) { g.setColour (C::dim()); g.setFont (font (9.0f)); g.drawText (noteName (note), (int) (k * keyW()) + 1, (int) kb.getY() + 44, 30, 12, juce::Justification::centredLeft); }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& zs = ctx.P().keymap.zones;
        if (e.y > gridH())
        {
            lastNote = kLo + juce::jlimit (0, kKeys - 1, (int) (e.x / keyW()));
            ctx.proc.auditionNote (lastNote, 0.85f);
            repaint();
            return;
        }
        const int hit = zoneAt (e.position);
        drawing = false; drag = -1;
        if (hit >= 0 && ! drawMode)
        {
            if (e.mods.isPopupMenu()) { zs.erase (zs.begin() + hit); ctx.select ({ Sel::None, 0 }); ctx.changed(); return; }
            drag = hit; grab = zs[(size_t) hit];
            const auto r = zoneRect (grab);
            mode = e.position.x < r.getX() + 5 ? 1 : e.position.x > r.getRight() - 5 ? 2 : e.position.y < r.getY() + 5 ? 3 : e.position.y > r.getBottom() - 5 ? 4 : 0;
            ctx.select ({ Sel::Zone, hit }, 0);
            return;
        }
        if (e.mods.isPopupMenu()) return;
        drawing = true; dragA = dragB = e.position;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drawing) { dragB = e.position; repaint(); return; }
        auto& zs = ctx.P().keymap.zones;
        if (drag < 0 || drag >= (int) zs.size()) return;
        auto& z = zs[(size_t) drag];
        const int dk = juce::roundToInt (e.getDistanceFromDragStartX() / keyW());
        const int dv = -juce::roundToInt (e.getDistanceFromDragStartY() / (gridH() / 127.0f));
        switch (mode)
        {
            case 0:
            {
                const int w = grab.hiKey - grab.loKey, vh = grab.hiVel - grab.loVel;
                z.loKey = juce::jlimit (0, 127 - w, grab.loKey + dk); z.hiKey = z.loKey + w;
                z.loVel = juce::jlimit (1, 127 - vh, grab.loVel + dv); z.hiVel = z.loVel + vh;
                z.root = juce::jlimit (0, 127, grab.root + (z.loKey - grab.loKey));
                break;
            }
            case 1: z.loKey = juce::jlimit (0, z.hiKey, grab.loKey + dk); break;
            case 2: z.hiKey = juce::jlimit (z.loKey, 127, grab.hiKey + dk); break;
            case 3: z.hiVel = juce::jlimit (z.loVel, 127, grab.hiVel + dv); break;
            default: z.loVel = juce::jlimit (1, z.hiVel, grab.loVel + dv); break;
        }
        ctx.changed (false);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        auto& zs = ctx.P().keymap.zones;
        if (drawing)
        {
            drawing = false;
            const int k0 = kLo + juce::jlimit (0, kKeys - 1, (int) (std::min (dragA.x, dragB.x) / keyW()));
            const int k1 = kLo + juce::jlimit (0, kKeys - 1, (int) (std::max (dragA.x, dragB.x) / keyW()));
            const int v1 = juce::jlimit (1, 127, juce::roundToInt (127.0f * (1.0f - std::min (dragA.y, dragB.y) / gridH())));
            const int v0 = juce::jlimit (1, v1, juce::roundToInt (127.0f * (1.0f - std::max (dragA.y, dragB.y) / gridH())) + 1);
            if (zs.size() < 128)
            {
                nb::Zone z; z.loKey = k0; z.hiKey = k1; z.root = k0; z.loVel = std::abs (v1 - v0) < 8 ? 1 : v0; z.hiVel = std::abs (v1 - v0) < 8 ? 127 : v1;
                z.layer = juce::jlimit (0, std::max (0, (int) ctx.P().keymap.layers.size() - 1), layer);
                z.index = ctx.pad; z.keytrack = k1 > k0; z.name = "Pad " + std::to_string (ctx.pad + 1);
                zs.push_back (z);
                ctx.select ({ Sel::Zone, (int) zs.size() - 1 }, 0);
            }
            ctx.changed();
            return;
        }
        if (drag >= 0) { drag = -1; ctx.changed(); }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

    int lastNote = -1;
private:
    Ctx& ctx;
    bool drawing = false;
    juce::Point<float> dragA, dragB;
    int drag = -1, mode = 0;
    nb::Zone grab;
};

class KeyMapRoom final : public juce::Component, public Part
{
public:
    explicit KeyMapRoom (Ctx& c) : ctx (c), grid (c), tools ({ "SELECT", "DRAW ZONE" })
    {
        addAndMakeVisible (tools);
        tools.onSelect = [this] (int i) { grid.drawMode = i == 1; };
        addAndMakeVisible (layouts);
        layouts.setTextWhenNothingSelected ("LAYOUTS...");
        int id = 1;
        for (const auto& n : nb::keyMapNames()) layouts.addItem (n, id++);
        layouts.onChange = [this]
        {
            const int i = layouts.getSelectedId() - 1;
            if (i >= 0) { ctx.P().keymap = nb::makeKeyMap (i); ctx.select ({ Sel::None, 0 }); ctx.changed(); }
            layouts.setSelectedId (0, juce::dontSendNotification);
        };
        for (auto* b : { &addLayer, &save, &load }) addAndMakeVisible (b);
        addLayer.onClick = [this]
        {
            auto& L = ctx.P().keymap.layers;
            static const juce::uint32 cols[] = { 0xff7fe0d0, 0xfff5b25a, 0xffffd1f3, 0xffc7d3ff, 0xffd6ffd0, 0xffffc9a8, 0xffe6d6ff, 0xfffff1b3 };
            if (L.size() < 8) { L.push_back ({ "Layer " + std::to_string (L.size() + 1), cols[L.size() % 8], true }); grid.layer = (int) L.size() - 1; ctx.changed(); }
        };
        save.onClick = [this]
        {
            auto* w = new juce::AlertWindow ("Save the key map", "A name (kept with its samples):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "My key map"); w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) { if (r == 1) ctx.proc.saveUserPreset ("KeyMap", w->getTextEditorContents ("name")); }), true);
        };
        load.onClick = [this]
        {
            juce::PopupMenu m;
            const auto names = ctx.proc.userPresets ("KeyMap");
            for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
            if (names.isEmpty()) m.addItem (-1, "No saved key maps yet", false);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, names] (int r) { if (r > 0) { ctx.proc.loadUserPreset ("KeyMap", names[r - 1]); ctx.select ({ Sel::None, 0 }); ctx.changed(); } });
        };
        addAndMakeVisible (grid);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat(), 18.0f);
        paintLabel (g, "LAYERS", { 14, 54, 60, 14 }, juce::Justification::centredLeft, 7.5f);
        paintLabel (g, "VELOCITY ^  KEYS >  |  BOXES ARE ZONES | COLOUR IS THE LAYER | DOT IS THE ROOT | DRAG A ZONE'S EDGES TO RESIZE", { 14, 84, 790, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "CLICK THE KEYS TO PLAY | EVERY NOTE (MIDI, SEQUENCER, PADS) LANDS HERE", { 14, getHeight() - 20, 790, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        auto top = juce::Rectangle<int> (12, 10, getWidth() - 24, 34);
        tools.setBounds (top.removeFromLeft (220));
        layouts.setBounds (top.removeFromLeft (280).reduced (8, 3));
        addLayer.setBounds (top.removeFromLeft (100).reduced (2, 3));
        save.setBounds (top.removeFromLeft (70).reduced (2, 3));
        load.setBounds (top.removeFromLeft (70).reduced (2, 3));
        int x = 74;
        for (auto* b : layerButtons) { const int w = (int) textW (caps (8.0f), b->getButtonText()) + 30; b->setBounds (x, 50, w, 24); x += w + 6; }
        grid.setBounds (14, 100, getWidth() - 28, getHeight() - 126);
    }

    void refresh() override
    {
        layerButtons.clear();
        auto& L = ctx.P().keymap.layers;
        for (int i = 0; i < (int) L.size(); ++i)
        {
            auto* b = layerButtons.add (new Pill (L[(size_t) i].name));
            b->setToggleState (L[(size_t) i].on, juce::dontSendNotification);
            b->setTooltip ("Click: draw new zones into this layer. Right-click: turn the layer on or off.");
            b->onClick = [this, i]
            {
                if (juce::ModifierKeys::currentModifiers.isPopupMenu() || juce::ModifierKeys::currentModifiers.isAltDown())
                { auto& L2 = ctx.P().keymap.layers; L2[(size_t) i].on = ! L2[(size_t) i].on; ctx.changed(); return; }
                grid.layer = i; refresh();
            };
            addAndMakeVisible (b);
        }
        for (int i = 0; i < layerButtons.size(); ++i) layerButtons[i]->setButtonText ((i == grid.layer ? "> " : "") + juce::String (L[(size_t) i].name) + (L[(size_t) i].on ? "" : " (off)"));
        resized(); repaint(); grid.repaint();
    }

private:
    Ctx& ctx;
    ZoneGrid grid;
    Segmented tools;
    juce::ComboBox layouts;
    Pill addLayer { "+ LAYER" }, save { "SAVE" }, load { "LOAD" };
    juce::OwnedArray<Pill> layerButtons;
};

std::unique_ptr<juce::Component> makeGridRoom (Ctx& c) { return std::make_unique<GridRoom> (c); }
std::unique_ptr<juce::Component> makeKeyMapRoom (Ctx& c) { return std::make_unique<KeyMapRoom> (c); }
} // namespace ui
