// NeoBeat 3.0 — the SEQUENCER room: the 16 lanes' pads, the four scenes and their nodes.
#include "UI.h"
#include "../shared/PresetBar.h"
#include "../shared/PatternXml.h"

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
            int cells = 0; for (const auto& c : P.matrix.oneShot.cells) cells += c.pad == p && c.scene == ctx.scene() ? 1 : 0;
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
                text = num + "  |  " + juce::String (cells) + " sent";
                if (textW (f, text) > (float) label.getWidth()) text = num + " | " + juce::String (cells);
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
        m.addSectionHeader ("LANE " + juce::String (p + 1));
        m.addItem (1, "Load a sample...");
        m.addItem (2, "Back to the library sound", ctx.proc.padUsesSample (p));
        m.addItem (6, "Match the stock sounds' loudness", ctx.proc.padUsesSample (p));
        m.addItem (3, "Mute in the sequencer", true, ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "mute"))->load() > 0.5f);
        m.addSeparator();
        m.addItem (4, "Send lane to a Friction Stretch window");
        m.addItem (5, "Send lane to a Bounce Delay window");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, p] (int r)
        {
            if (r == 1)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load a sample onto lane " + juce::String (p + 1), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, p] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile() && ctx.proc.loadSample (p, fc.getResult())) { ctx.proc.matchLevel (p); ctx.proc.teachLane (p); if (ctx.refreshAll) ctx.refreshAll(); }
                });
            }
            if (r == 6) { ctx.proc.matchLevel (p); if (ctx.refreshAll) ctx.refreshAll(); }
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
        if (p >= 0 && ! files.isEmpty() && ctx.proc.loadSample (p, juce::File (files[0])))
        {   // as loud as the stock sounds; and this is the lane you say it belongs on
            ctx.proc.matchLevel (p); ctx.proc.teachLane (p);
            ctx.select ({ Sel::Pad, p });
        }
    }

private:
    Ctx& ctx;
    std::array<uint32_t, 16> lastHits {};
    std::array<float, 16> flash {};
    std::unique_ptr<juce::FileChooser> chooser;
};

// ------------------------------------------------------------------ the step grid
// A page of the scene: `viewBars` bars from `viewBar`, so a node is never too small to see or hit,
// however long the scene is (up to 32 bars).
class StepGrid final : public juce::Component
{
public:
    explicit StepGrid (Ctx& c) : ctx (c) {}
    static constexpr int kLabel = 132;

    int first() const { return ctx.firstStep(); }
    int shown() const { return ctx.shownSteps(); }
    int slots() const { return ctx.viewBars * 16; }
    float stepW() const { return (float) (getWidth() - kLabel - 4) / (float) slots(); }
    float rowH() const { return (float) (getHeight() - 18) / 16.0f; }

    // which node is under the mouse (step: in the scene)
    bool cellAt (juce::Point<int> pos, int& track, int& step) const
    {
        if (pos.x < kLabel || pos.y < 18) return false;
        const int col = (int) ((pos.x - kLabel) / stepW());
        if (col < 0 || col >= shown()) return false;
        track = juce::jlimit (0, 15, (int) ((pos.y - 18) / rowH()));
        step = first() + col;
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        const int len = shown(), f0 = first();
        const float sw = stepW(), rh = rowH();
        // beat ruler
        for (int s = 0; s < len; s += 4)
        {
            const int abs = f0 + s;
            paintLabel (g, juce::String (abs / 16 + 1) + "." + juce::String ((abs / 4) % 4 + 1), juce::Rectangle<int> (kLabel + (int) (s * sw) + 2, 2, 40, 12), juce::Justification::centredLeft, 7.0f, abs % 16 == 0 ? C::text() : C::dim());
            g.setColour (juce::Colour (0x10304840)); g.fillRect ((float) kLabel + s * sw, 18.0f, sw * 4, (float) getHeight() - 18);
            if ((abs / 4) % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect ((float) kLabel + s * sw, 18.0f, sw * 4, (float) getHeight() - 18); }
            if (abs % 16 == 0 && s > 0) { g.setColour (C::text().withAlpha (0.35f)); g.fillRect ((float) kLabel + s * sw - 0.75f, 16.0f, 1.5f, (float) getHeight() - 16); }
        }
        if (len < slots())
        {   // past the scene's end
            g.setColour (juce::Colour (0x18000000)); g.fillRect ((float) kLabel + len * sw, 18.0f, (slots() - len) * sw, (float) getHeight() - 18);
        }
        auto& P = ctx.P();
        const int scene = ctx.scene();
        const bool here = ctx.proc.playScene.load() == scene;
        const int play = here ? ctx.proc.playStep.load() - f0 : -1;
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
                const auto st = ctx.proc.pattern.get (t, f0 + s);
                const float cx = kLabel + (s + 0.5f) * sw, cy = y + rh * 0.5f;
                const bool chosen = ctx.sel.kind == Sel::Node && ctx.sel.index == t * nb::Pattern::kMaxSteps + f0 + s;
                if (chosen) { g.setColour (C::teal()); g.drawRoundedRectangle (cx - sw * 0.5f + 1, y + 1, sw - 2, rh - 2, 4.0f, 1.6f); }
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
            // the effects the nodes of this lane are sent to
            if (P.matrix.oneShotOn)
                for (const auto& c : P.matrix.oneShot.cells)
                    if (c.scene == scene && c.pad == t && c.step >= f0 && c.step < f0 + len)
                    {
                        g.setColour (C::fxColour (c.fx));
                        g.fillRoundedRectangle (kLabel + (c.step - f0) * sw + 1, y + 1, std::min ((float) c.span, (float) (f0 + len - c.step)) * sw - 2, 3.5f, 1.5f);
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

    // a node's menu: where it is sent (Init: nowhere), then the hit itself
    void stepMenu (int t, int s)
    {
        auto st = ctx.proc.pattern.get (t, s);
        const int cell = nodeCell (ctx, t, s);
        juce::PopupMenu m;
        m.addSectionHeader ("NODE | LANE " + juce::String (t + 1) + ", STEP " + juce::String (s / 16 + 1) + "." + juce::String (s % 16 + 1));
        addSendMenu (m, 100, cell >= 0 ? fxIndex (ctx.P().matrix.oneShot.cells[(size_t) cell].fx) : -1);
        m.addItem (40, "Copy"); m.addItem (41, "Paste", ctx.clipHas); m.addItem (31, "Clear");
        m.addItem (42, "Show its settings on the right", st.vel > 0);
        m.addSeparator();
        m.addItem (1, "Single hit", true, st.roll == 0);
        m.addItem (2, "Ratchet x2", true, st.roll == 2);
        m.addItem (3, "Ratchet x3", true, st.roll == 3);
        m.addItem (4, "Ratchet x4", true, st.roll == 4);
        m.addItem (5, "Triplet roll (over two steps)", true, st.roll == 5);
        m.addSeparator();
        m.addItem (10, "Accent"); m.addItem (11, "Normal"); m.addItem (12, "Ghost");
        m.addSeparator();
        m.addItem (20, "Nudge early (-20 %)"); m.addItem (21, "On the grid"); m.addItem (22, "Nudge late (+20 %)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, s] (int r)
        {
            if (r <= 0) return;   // dismissed (also what an editor closing with the menu open sends)
            if (r >= 100) { sendNode (ctx, t, s, r - 101); repaint(); return; }
            if (r == 40) { copyNode (ctx, t, s); return; }
            if (r == 41) { pasteNode (ctx, t, s); repaint(); return; }
            if (r == 42) { const int c = nodeCell (ctx, t, s); ctx.select (c >= 0 ? Sel { Sel::Cell, c } : Sel { Sel::Node, t * nb::Pattern::kMaxSteps + s }, c >= 0 ? 1 : 0); return; }
            auto st2 = ctx.proc.pattern.get (t, s);
            if (st2.vel == 0 && r < 30) st2.vel = 96;
            if (r >= 1 && r <= 5) st2.roll = (uint8_t) (r == 1 ? 0 : r);
            if (r == 10) st2.vel = 127; if (r == 11) st2.vel = 96; if (r == 12) st2.vel = 40;
            if (r == 20) st2.nudge = -20; if (r == 21) st2.nudge = 0; if (r == 22) st2.nudge = 20;
            if (r == 31)
            {
                st2 = {};
                const int c = nodeCell (ctx, t, s);
                if (c >= 0) { auto& cs = ctx.P().matrix.oneShot.cells; cs.erase (cs.begin() + c); ctx.select ({ Sel::Pad, t }); ctx.changed(); }
            }
            ctx.proc.pattern.set (t, s, st2);
            repaint();
        });
    }

private:
    Ctx& ctx;
    int dragMode = 0;
    uint8_t dragVel = 96;
};

// ------------------------------------------------------------------ the scene's bars at a glance: click one to go there
class BarStrip final : public juce::Component
{
public:
    explicit BarStrip (Ctx& c) : ctx (c) {}
    int bars() const { return std::max (1, (ctx.proc.pattern.length() + 15) / 16); }
    void paint (juce::Graphics& g) override
    {
        const int n = bars();
        const float w = (float) getWidth() / (float) std::max (n, 8);
        const bool here = ctx.proc.playScene.load() == ctx.scene();
        const int playBar = here && ctx.proc.playStep.load() >= 0 ? ctx.proc.playStep.load() / 16 : -1;
        for (int b = 0; b < n; ++b)
        {
            auto r = juce::Rectangle<float> (b * w + 1, 1, w - 2, (float) getHeight() - 2);
            int hits = 0;
            for (int t = 0; t < 16; ++t) for (int s = b * 16; s < b * 16 + 16 && s < ctx.proc.pattern.length(); ++s) hits += ctx.proc.pattern.get (t, s).vel > 0 ? 1 : 0;
            const bool inView = b >= ctx.viewBar && b < ctx.viewBar + ctx.viewBars;
            g.setColour (inView ? juce::Colours::white.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.3f)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (C::teal().withAlpha (juce::jlimit (0.0f, 0.85f, hits / 40.0f))); g.fillRoundedRectangle (r.reduced (2), 3.0f);
            if (b == playBar) { g.setColour (C::amber()); g.drawRoundedRectangle (r, 4.0f, 2.0f); }
            else if (inView) { g.setColour (C::text().withAlpha (0.6f)); g.drawRoundedRectangle (r, 4.0f, 1.2f); }
            if (w > 16) { g.setColour (C::text()); g.setFont (font (9.0f, inView)); g.drawText (juce::String (b + 1), r.toNearestInt(), juce::Justification::centred); }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int n = bars();
        const float w = (float) getWidth() / (float) std::max (n, 8);
        const int b = juce::jlimit (0, n - 1, (int) (e.x / w));
        const int start = juce::jlimit (0, std::max (0, n - ctx.viewBars), b - b % ctx.viewBars);
        if (start != ctx.viewBar) { ctx.viewBar = start; ctx.follow = false; if (ctx.refreshAll) ctx.refreshAll(); }
    }
private:
    Ctx& ctx;
};

// ------------------------------------------------------------------ the chain: scenes one after another
class ChainStrip final : public juce::Component
{
public:
    explicit ChainStrip (Ctx& c) : ctx (c) {}
    juce::Rectangle<int> link (int i) const { return { i * 46, 0, 42, getHeight() }; }
    void paint (juce::Graphics& g) override
    {
        const auto& sg = ctx.P().song;
        const int playing = sg.chainOn ? ctx.proc.playLink.load() : -1;
        for (int i = 0; i < sg.numLinks; ++i)
        {
            auto r = link (i).toFloat();
            paintPearl (g, r, 8.0f, i == playing);
            g.setColour (sg.chainOn ? C::text() : C::dim()); g.setFont (font (12.5f, true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + sg.chain[(size_t) i].scene)) + (sg.chain[(size_t) i].repeats > 1 ? " x" + juce::String ((int) sg.chain[(size_t) i].repeats) : juce::String()), link (i), juce::Justification::centred);
        }
        if (sg.numLinks < nb::SongSettings::kMaxLinks)
        {
            auto r = link (sg.numLinks);
            g.setColour (C::dim()); g.setFont (font (16.0f, true)); g.drawText ("+", r.withWidth (26), juce::Justification::centred);
        }
        if (sg.numLinks == 0) { g.setColour (C::dim()); g.setFont (font (11.0f)); g.drawText ("click + to add a scene to the chain", 30, 0, 260, getHeight(), juce::Justification::centredLeft); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& sg = ctx.P().song;
        if (sg.numLinks < nb::SongSettings::kMaxLinks && link (sg.numLinks).withWidth (26).contains (e.getPosition()))
        {
            sg.chain[(size_t) sg.numLinks++] = { (uint8_t) ctx.scene(), 1 };
            sg.chainOn = true; ctx.changed(); return;
        }
        for (int i = 0; i < sg.numLinks; ++i)
        {
            if (! link (i).contains (e.getPosition())) continue;
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                for (int sc = 0; sc < nb::kNumScenes; ++sc) m.addItem (1 + sc, "Scene " + juce::String::charToString ((juce::juce_wchar) ('A' + sc)), true, sg.chain[(size_t) i].scene == sc);
                m.addSeparator();
                for (int r : { 1, 2, 3, 4, 8, 16 }) m.addItem (100 + r, "Play it " + juce::String (r) + (r == 1 ? " time" : " times"), true, sg.chain[(size_t) i].repeats == r);
                m.addSeparator(); m.addItem (99, "Remove from the chain");
                m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r)
                {
                    auto& s2 = ctx.P().song;
                    if (r <= 0 || i >= s2.numLinks) return;
                    if (r >= 1 && r <= nb::kNumScenes) s2.chain[(size_t) i].scene = (uint8_t) (r - 1);
                    if (r > 100) s2.chain[(size_t) i].repeats = (uint8_t) (r - 100);
                    if (r == 99) { for (int k = i; k + 1 < s2.numLinks; ++k) s2.chain[(size_t) k] = s2.chain[(size_t) k + 1]; --s2.numLinks; if (s2.numLinks == 0) s2.chainOn = false; }
                    ctx.changed();
                });
                return;
            }
            sg.chain[(size_t) i].scene = (uint8_t) ((sg.chain[(size_t) i].scene + 1) % nb::kNumScenes);
            ctx.changed(); return;
        }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto& sg = ctx.P().song;
        for (int i = 0; i < sg.numLinks; ++i)
            if (link (i).contains (e.getPosition())) { sg.chain[(size_t) i].repeats = (uint8_t) juce::jlimit (1, 32, (int) sg.chain[(size_t) i].repeats + (w.deltaY > 0 ? 1 : -1)); ctx.changed (false); repaint(); }
    }
private:
    Ctx& ctx;
};

// ------------------------------------------------------------------ the room
class KitRoom final : public juce::Component, public Part
{
public:
    explicit KitRoom (Ctx& c) : ctx (c), pads (c), steps (c), strip (c), chainStrip (c), scenes ({ "A", "B", "C", "D" }), zoom ({ "1", "2", "4" }), modes ({ "HOST", "KEY", "PADS" }),
                                swing (c, "SWING", 0.5f, 0.75f, 0.64f), tempo (c, "TEMPO", 60.0f, 200.0f, 120.0f)
    {
        // The preset bar, Batchcut's (the user's choice, 27 Nov - docs/DECISIONS.md, "Next in the
        // same build"): the arrows load as you step, and the list plays each pattern as the mouse
        // passes over it and puts back what was there unless one is clicked. The 128 factory
        // patterns, then the ones saved here (MINE).
        addAndMakeVisible (presets);
        presets.onLoadItem = [this] (int i)
        {
            const int n = nb::numPatterns();
            if (i < n) { ctx.proc.loadPreset (i); userLoaded = -1; }
            else
            {
                const auto user = ctx.proc.userPresets ("Pattern");
                if (i - n >= user.size() || ! ctx.proc.loadUserPreset ("Pattern", user[i - n])) return false;
                userLoaded = i - n;
            }
            seenPreset = ctx.proc.currentPreset.load();
            ctx.changed (false);
            return true;
        };
        // what the scene holds before browsing, so hovering costs nothing
        presets.onSnapshot = [this]
        {
            auto x = std::make_unique<juce::XmlElement> ("BROWSE");
            nb::Pattern pat; ctx.proc.snapshotPattern (pat);
            io::writePattern (*x->createNewChildElement ("PATTERN"), pat);
            x->setAttribute ("swing", ctx.proc.getParam ("swing")); x->setAttribute ("tempo", ctx.proc.getParam ("tempo"));
            x->setAttribute ("preset", ctx.proc.currentPreset.load()); x->setAttribute ("user", userLoaded);
            return x;
        };
        presets.onRestore = [this] (const juce::XmlElement& x)
        {
            if (auto* pe = x.getChildByName ("PATTERN")) { nb::Pattern pat; io::readPattern (*pe, pat); ctx.proc.pattern.load (pat); }
            ctx.proc.setParam ("swing", (float) x.getDoubleAttribute ("swing", 0.64));
            ctx.proc.setParam ("tempo", (float) x.getDoubleAttribute ("tempo", 120.0));
            ctx.proc.currentPreset.store (x.getIntAttribute ("preset", 0));
            seenPreset = ctx.proc.currentPreset.load(); userLoaded = x.getIntAttribute ("user", -1);
            ctx.changed (false);
        };
        // With the host stopped, an audition runs NeoBeat's own transport for about a bar
        presets.onAudition = [this]
        {
            presets.auditionMs = juce::jlimit (800, 6000, (int) (4.0 * 60000.0 / std::max (40.0, ctx.proc.displayBpm.load())));
            ctx.proc.auditionRun.store (true);
        };
        presets.onAuditionStop = [this] { ctx.proc.auditionRun.store (false); };
        for (auto* b : { &save, &clear, &chain, &followBtn }) addAndMakeVisible (b);
        save.onClick = [this] { saveUser(); };
        clear.onClick = [this] { ctx.proc.clearScene (ctx.scene()); ctx.changed(); };
        clear.setTooltip ("Empty this scene (its nodes and where they are sent)");
        addAndMakeVisible (kitBox);
        for (int k = 0; k < (int) nb::kits().size(); ++k) kitBox.addItem ("KIT | " + juce::String (nb::kits()[(size_t) k].name).toUpperCase(), k + 1);
        kitBox.onChange = [this] { ctx.proc.loadKit (kitBox.getSelectedId() - 1); ctx.changed(); };
        addAndMakeVisible (modes);
        modes.onSelect = [this] (int i) { ctx.proc.setParam ("mode", (float) i); };
        addAndMakeVisible (scenes);
        scenes.onSelect = [this] (int i) { ctx.proc.setScene (i); ctx.viewBar = 0; if (ctx.sel.kind == Sel::Node || ctx.sel.kind == Sel::Cell) ctx.sel = { Sel::Pad, ctx.pad }; if (ctx.refreshAll) ctx.refreshAll(); };
        scenes.setTooltip ("Four scenes, each up to 32 bars. While it plays, a new scene starts on the next bar. Right-click: copy or clear.");
        scenes.addMouseListener (this, true);
        addAndMakeVisible (barsBox);
        for (int b : { 1, 2, 4, 8, 16, 32 }) barsBox.addItem (juce::String (b) + (b == 1 ? " BAR" : " BARS"), b);
        barsBox.onChange = [this] { const int b = barsBox.getSelectedId(); if (b > 0) { ctx.proc.pattern.setLength (b * 16); ctx.viewBar = juce::jlimit (0, std::max (0, b - ctx.viewBars), ctx.viewBar); ctx.changed(); } };
        addAndMakeVisible (zoom);
        zoom.onSelect = [this] (int i) { ctx.viewBars = 1 << i; ctx.viewBar -= ctx.viewBar % ctx.viewBars; if (ctx.refreshAll) ctx.refreshAll(); };
        zoom.setTooltip ("How many bars the grid shows at once");
        chain.setClickingTogglesState (true);
        chain.onClick = [this] { auto& sg = ctx.P().song; sg.chainOn = chain.getToggleState() && sg.numLinks > 0; ctx.changed(); };
        chain.setTooltip ("Play the scenes one after another (up to 128 bars and more), instead of looping the one shown");
        followBtn.setClickingTogglesState (true);
        followBtn.onClick = [this] { ctx.follow = followBtn.getToggleState(); };
        followBtn.setTooltip ("Turn the pages with the playhead");
        addAndMakeVisible (pads); addAndMakeVisible (steps); addAndMakeVisible (strip); addAndMakeVisible (chainStrip);
        for (auto* k : { &swing, &tempo }) addAndMakeVisible (k);
        swing.attach (ctx.proc.apvts.getParameter ("swing"));
        swing.toText = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; };
        tempo.attach (ctx.proc.apvts.getParameter ("tempo"));
        tempo.toText = [] (float v) { return juce::String (v, 1); };
        tempo.setTooltip ("Standalone tempo (the host's tempo wins inside a DAW)");
        refresh();
    }
    ~KitRoom() override { scenes.removeMouseListener (this); }

    // right-click on a scene button
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || ! scenes.isParentOf (e.eventComponent)) return;
        const int sc = juce::jlimit (0, 3, e.getEventRelativeTo (&scenes).x * 4 / std::max (1, scenes.getWidth()));
        juce::PopupMenu m;
        const juce::String name = juce::String::charToString ((juce::juce_wchar) ('A' + sc));
        m.addSectionHeader ("SCENE " + name);
        for (int to = 0; to < nb::kNumScenes; ++to) if (to != sc) m.addItem (1 + to, "Copy to scene " + juce::String::charToString ((juce::juce_wchar) ('A' + to)));
        m.addSeparator(); m.addItem (20, "Clear scene " + name);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, sc] (int r)
        {
            if (r >= 1 && r <= nb::kNumScenes) ctx.proc.copyScene (sc, r - 1);
            if (r == 20) ctx.proc.clearScene (sc);
            if (r > 0) ctx.changed();
        });
    }

    void saveUser()
    {
        auto* w = new juce::AlertWindow ("Save the scene", "A name for this scene's pattern (kept in your NeoBeat presets).\nTo keep the kit and all four scenes in one file, use KIT / FILE > Save as .NJQ.", juce::MessageBoxIconType::NoIcon);
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
        auto info = juce::Rectangle<int> (426, 54, 380, 180);
        paintWell (g, info.toFloat(), 12.0f);
        const int p = ctx.pad;
        paintLabel (g, "LANE " + juce::String (p + 1) + " | " + ctx.proc.padGroup (p), info.reduced (12, 8).removeFromTop (12), juce::Justification::centredLeft, 8.0f);
        g.setColour (C::text()); g.setFont (font (17.0f, true));
        g.drawText (ctx.proc.padName (p), info.reduced (12, 0).withTrimmedTop (24).withHeight (24), juce::Justification::centredLeft, true);
        g.setColour (C::dim()); g.setFont (font (11.5f));
        g.drawFittedText ("Click a lane's pad to hear it. Its sound, effects and modulation are on the right. Right-click a pad to load a sample; drop audio files onto pads.",
                          info.reduced (12, 0).withTrimmedTop (50).withWidth (212).withHeight (120), juce::Justification::topLeft, 7);
        paintLabel (g, "SCENE", { 14, 240, 60, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "LENGTH", { 196, 240, 60, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "SHOW BARS", { 300, 240, 70, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        const int q = ctx.proc.queuedScene.load();
        if (q >= 0 && ! ctx.P().song.chainOn) paintLabel (g, "NEXT BAR: " + juce::String::charToString ((juce::juce_wchar) ('A' + q)), { 76, 240, 110, 12 }, juce::Justification::centredLeft, 7.0f, C::amber().darker (0.4f));
        paintLabel (g, "NODES | CLICK HIT | SHIFT ACCENT | ALT GHOST | WHEEL VELOCITY | RIGHT-CLICK: SEND NODE TO, ROLLS, COPY", { 14, 308, 780, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        auto top = juce::Rectangle<int> (12, 10, getWidth() - 24, 36);
        presets.setBounds (top.removeFromLeft (300).reduced (0, 3));
        save.setBounds (top.removeFromLeft (58).reduced (2, 3));
        clear.setBounds (top.removeFromLeft (62).reduced (2, 3));
        kitBox.setBounds (top.removeFromLeft (150).reduced (4, 3));
        modes.setBounds (top.reduced (2, 0));
        pads.setBounds (14, 54, 400, 180);
        swing.setBounds (656, 124, 70, 96);
        tempo.setBounds (730, 124, 70, 96);
        scenes.setBounds (14, 254, 172, 30);
        barsBox.setBounds (196, 255, 96, 28);
        zoom.setBounds (300, 254, 96, 30);
        followBtn.setBounds (402, 256, 72, 26);
        chain.setBounds (482, 256, 64, 26);
        chainStrip.setBounds (552, 254, getWidth() - 566, 30);
        strip.setBounds (8 + StepGrid::kLabel, 288, getWidth() - 20 - StepGrid::kLabel, 18);
        steps.setBounds (8, 322, getWidth() - 16, getHeight() - 330);
    }

    void refresh() override
    {
        {
            juce::StringArray names;
            for (int i = 0; i < nb::numPatterns(); ++i) names.add (juce::String (i + 1).paddedLeft ('0', 3) + "  " + nb::patternPreset (i).name);
            const auto user = ctx.proc.userPresets ("Pattern");
            for (int i = 0; i < user.size(); ++i) names.add ("MINE | " + user[i]);
            if (userLoaded >= user.size()) userLoaded = -1;
            if (names != presetNames) { presetNames = names; presets.setItems (names, presetIndex()); }
            else presets.setCurrent (presetIndex());
        }
        kitBox.setSelectedId (ctx.proc.currentKit.load() + 1, juce::dontSendNotification);
        const int bars = std::max (1, (ctx.proc.pattern.length() + 15) / 16);
        barsBox.setSelectedId (bars, juce::dontSendNotification);
        if (barsBox.getSelectedId() != bars) barsBox.setText (juce::String (ctx.proc.pattern.length()) + " STEPS", juce::dontSendNotification);
        ctx.viewBar = juce::jlimit (0, std::max (0, bars - 1), ctx.viewBar);
        scenes.setSelected (ctx.scene());
        zoom.setSelected (ctx.viewBars == 1 ? 0 : ctx.viewBars == 2 ? 1 : 2);
        chain.setToggleState (ctx.P().song.chainOn, juce::dontSendNotification);
        followBtn.setToggleState (ctx.follow, juce::dontSendNotification);
        modes.setSelected ((int) ctx.proc.apvts.getRawParameterValue ("mode")->load());
        repaint(); pads.repaint(); steps.repaint(); strip.repaint(); chainStrip.repaint();
    }

    void tick() override
    {
        pads.tick();
        swing.syncFromParam(); tempo.syncFromParam();
        // the chain plays other scenes than the one shown: show the one that sounds
        const int playing = ctx.proc.playScene.load();
        if (ctx.P().song.chainOn && ctx.proc.playStep.load() >= 0 && ctx.follow && playing != ctx.scene()) { ctx.proc.pattern.edit.store (playing); ctx.viewBar = 0; if (ctx.refreshAll) ctx.refreshAll(); }
        // a scene chosen by the host (automation) or reached on the bar line
        const int wanted = (int) std::lround (ctx.proc.apvts.getRawParameterValue ("scene")->load());
        if (! ctx.P().song.chainOn && wanted != ctx.scene()) { ctx.proc.pattern.edit.store (juce::jlimit (0, 3, wanted)); if (ctx.refreshAll) ctx.refreshAll(); }
        const int s = ctx.proc.playStep.load();
        if (ctx.follow && s >= 0 && playing == ctx.scene())
        {
            const int page = (s / 16) - (s / 16) % ctx.viewBars;
            if (page != ctx.viewBar) { ctx.viewBar = page; if (ctx.refreshAll) ctx.refreshAll(); }
        }
        const auto v = ctx.proc.pattern.version.load();
        const int q = ctx.proc.queuedScene.load(), link = ctx.proc.playLink.load();
        if (s != lastStep || v != lastVersion) { if (s / 16 != lastStep / 16 || v != lastVersion) strip.repaint(); lastStep = s; lastVersion = v; steps.repaint(); }
        if (q != lastQueued || link != lastLink) { lastQueued = q; lastLink = link; repaint(); chainStrip.repaint(); }
        // a pattern loaded from elsewhere (the host's program list) shows here
        if (ctx.proc.currentPreset.load() != seenPreset) { seenPreset = ctx.proc.currentPreset.load(); userLoaded = -1; presets.setCurrent (presetIndex()); }
    }

private:
    Ctx& ctx;
    PadGrid pads;
    StepGrid steps;
    BarStrip strip;
    ChainStrip chainStrip;
    juce::ComboBox kitBox, barsBox;
    bc::PresetBar presets;
    juce::StringArray presetNames;
    int userLoaded = -1, seenPreset = -1;      // a MINE pattern loaded through the bar, and the factory one last seen
    int presetIndex() const { return userLoaded >= 0 ? nb::numPatterns() + userLoaded : ctx.proc.currentPreset.load(); }
    Pill save { "SAVE" }, clear { "CLEAR" }, chain { "CHAIN" }, followBtn { "FOLLOW" };
    Segmented scenes, zoom, modes;
    Knob swing, tempo;
    int lastStep = -2, lastQueued = -2, lastLink = -2;
    uint32_t lastVersion = 0;
};

std::unique_ptr<juce::Component> makeKitRoom (Ctx& c) { return std::make_unique<KitRoom> (c); }
} // namespace ui
