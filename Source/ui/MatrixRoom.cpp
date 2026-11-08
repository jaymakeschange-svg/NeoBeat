// NeoBeat 2.0 — the MATRIXXXER room: Sequencer (draw with sound), One Shot (an effect on a hit),
// Sample (effect boxes on a sample's spectrogram).
#include "UI.h"
#include <juce_dsp/juce_dsp.h>

namespace ui
{
// ------------------------------------------------------------------ the brush: an effect family, then an effect
class Brush final : public juce::Component
{
public:
    explicit Brush (Ctx& c) : ctx (c) {}
    static const juce::StringArray& families() { static juce::StringArray f { "SMOOTH", "SUSTAIN", "EXCITE", "PITCH", "FILTER", "TEXTURE", "PARTIALS", "TIME", "FRICTION", "BOUNCE" }; return f; }
    std::vector<int> members (const juce::String& fam) const
    {
        std::vector<int> v;
        for (int i = 0; i < 26; ++i) if (fxFamily (fxFromIndex (i)) == fam) v.push_back (i);
        return v;
    }
    void paint (juce::Graphics& g) override
    {
        paintWell (g, getLocalBounds().toFloat(), 12.0f);
        famRects.clear(); fxRects.clear();
        const auto cur = fxFamily (fxFromIndex (ctx.brush));
        paintLabel (g, "BRUSH", { 10, 8, 56, 14 }, juce::Justification::centredLeft, 8.0f);
        int x = 66;
        for (const auto& f : families())
        {
            const int w = (int) textW (caps (8.0f), f) + 30;
            juce::Rectangle<int> r (x, 6, w, 22);
            famRects.add (r);
            const bool on = f == cur;
            paintTile (g, r.toFloat(), on ? C::family (f) : juce::Colour (0xffeef2f0), {}, on);
            g.setColour (C::family (f)); g.fillEllipse ((float) r.getX() + 7, (float) r.getCentreY() - 4, 8, 8);
            g.setColour (juce::Colour (0x701f2925)); g.drawEllipse ((float) r.getX() + 7, (float) r.getCentreY() - 4, 8, 8, 0.8f);
            paintLabel (g, f, r.withTrimmedLeft (19), juce::Justification::centredLeft, 7.0f);
            x += w + 4;
        }
        paintLabel (g, cur, { 10, 36, 56, 14 }, juce::Justification::centredLeft, 7.0f, C::dim());
        x = 66;
        for (int i : members (cur))
        {
            const auto name = fxShort (fxFromIndex (i));
            const int w = (int) textW (caps (8.0f), name) + 20;
            juce::Rectangle<int> r (x, 34, w, 22);
            fxRects.push_back ({ r, i });
            const bool on = i == ctx.brush;
            paintTile (g, r.toFloat(), on ? C::fxColour (fxFromIndex (i)) : juce::Colour (0xffeef2f0), name, on);
            x += w + 4;
        }
        g.setColour (C::dim()); g.setFont (font (11.0f));
        g.drawText (hint, juce::Rectangle<int> (x + 8, 34, getWidth() - x - 14, 22), juce::Justification::centredLeft, true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < famRects.size(); ++i)
            if (famRects[i].contains (e.getPosition())) { ctx.brush = members (families()[i]).front(); repaint(); return; }
        for (const auto& fr : fxRects) if (fr.first.contains (e.getPosition())) { ctx.brush = fr.second; repaint(); return; }
    }
    juce::String hint;
private:
    Ctx& ctx;
    juce::Array<juce::Rectangle<int>> famRects;
    std::vector<std::pair<juce::Rectangle<int>, int>> fxRects;
};

// ------------------------------------------------------------------ Sequencer view: the drawing canvas
class SeqCanvas final : public juce::Component
{
public:
    explicit SeqCanvas (Ctx& c) : ctx (c) {}
    static constexpr int kLabel = 34;
    nb::SeqPreset& S() { return ctx.P().matrix.seq; }
    double steps() { return S().bars * 4.0 / nb::gridBeats (S().grid); }
    float stepW() { return (float) (getWidth() - kLabel - 4) / (float) steps(); }
    float rowH() { return (float) (getHeight() - 4) / (float) nb::SeqPreset::kRows; }
    juce::Rectangle<float> markRect (const nb::Mark& m)
    {
        const float x = kLabel + m.start * stepW(), y = 2 + (nb::SeqPreset::kRows - 1 - m.row) * rowH();
        const bool longMark = m.type == nb::MarkType::Dash || m.type == nb::MarkType::Warble || m.type == nb::MarkType::Spray;
        return { x, y, longMark ? std::max (4.0f, m.length * stepW()) : std::max (6.0f, rowH()), rowH() };
    }
    int markAt (juce::Point<float> p)
    {
        auto& ms = S().marks;
        for (int i = (int) ms.size() - 1; i >= 0; --i) if (markRect (ms[(size_t) i]).expanded (2, 1).contains (p)) return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff11251c), r.getCentreX(), 0, juce::Colour (0xff040a07), r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 14.0f);
        auto& P = S();
        const float rh = rowH(), sw = stepW();
        const int stepsPerBar = juce::roundToInt (4.0 / nb::gridBeats (P.grid));
        for (int row = 0; row < nb::SeqPreset::kRows; ++row)
        {
            const float y = 2 + (nb::SeqPreset::kRows - 1 - row) * rh;
            const int note = nb::rowNote (P, row);
            const bool root = (note - P.root) % 12 == 0;
            g.setColour (C::lcdText().withAlpha (root ? 0.13f : 0.04f)); g.drawHorizontalLine ((int) (y + rh), (float) kLabel, r.getRight());
            if (root || rh > 13) { g.setColour (root ? C::lcdText() : C::lcdDim()); g.setFont (font (std::min (10.0f, rh * 0.8f), root)); g.drawText (noteName (note), 2, (int) y, kLabel - 4, (int) rh, juce::Justification::centredRight); }
        }
        for (int s = 0; s <= (int) steps(); ++s)
        {
            const bool bar = s % stepsPerBar == 0, beat = s % std::max (1, stepsPerBar / 4) == 0;
            if (! beat && sw < 5) continue;
            g.setColour (C::lcdText().withAlpha (bar ? 0.3f : beat ? 0.12f : 0.04f));
            g.drawVerticalLine ((int) (kLabel + s * sw), 2.0f, r.getBottom() - 2);
        }
        // the main pattern's hits along the bottom (what Mirror follows)
        nb::Pattern pat; ctx.proc.snapshotPattern (pat);
        const double gb = nb::gridBeats (P.grid);
        for (int s = 0; s < pat.length; ++s)
        {
            bool any = false; for (int t = 0; t < 16 && ! any; ++t) any = pat.steps[(size_t) t][(size_t) s].vel > 0;
            if (! any) continue;
            for (double beat = s * 0.25; beat < P.bars * 4.0; beat += pat.length * 0.25)
            { g.setColour (C::lcdDim().withAlpha (P.mirror ? 0.7f : 0.3f)); g.fillEllipse (kLabel + (float) (beat / gb) * sw - 2, r.getBottom() - 7, 4, 4); }
        }
        // the marks
        for (int i = 0; i < (int) P.marks.size(); ++i)
        {
            const auto& m = P.marks[(size_t) i];
            auto mr = markRect (m);
            const bool sel = ctx.sel.kind == Sel::Mark && ctx.sel.index == i;
            const juce::Colour col = m.type == nb::MarkType::Warble ? juce::Colour (0xffffd1f3) : m.type == nb::MarkType::Spray ? juce::Colour (0xffc7d3ff)
                                   : m.type == nb::MarkType::Tick ? juce::Colour (0xfffff1b3) : C::lcdText();
            g.setColour (col.withAlpha (0.25f + 0.6f * m.level));
            switch (m.type)
            {
                case nb::MarkType::Dot:
                {
                    juce::Path d; const float s = rh * 0.55f, cx = mr.getX() + 3, cy = mr.getCentreY();
                    d.addQuadrilateral (cx, cy - s, cx + s * 0.45f, cy, cx, cy + s, cx - s * 0.45f, cy);
                    g.fillPath (d);
                    break;
                }
                case nb::MarkType::Tick: g.drawLine (mr.getX() + 2, mr.getY() + 1, mr.getX() + 2, mr.getBottom() - 1, 2.0f); break;
                case nb::MarkType::Dash: g.fillRoundedRectangle (mr.reduced (0, rh * 0.28f), rh * 0.2f); break;
                case nb::MarkType::Warble:
                {
                    juce::Path w;
                    for (float x = mr.getX(); x <= mr.getRight(); x += 1.5f)
                    {
                        const float y = mr.getCentreY() + std::sin ((x - mr.getX()) * 0.35f * m.rate / 6.0f) * rh * 0.35f * juce::jlimit (0.2f, 1.0f, m.depth / 60.0f);
                        if (x == mr.getX()) w.startNewSubPath (x, y); else w.lineTo (x, y);
                    }
                    g.strokePath (w, juce::PathStrokeType (1.8f));
                    break;
                }
                case nb::MarkType::Spray:
                {
                    juce::Random rnd ((juce::int64) i * 977 + 3);
                    for (int k = 0; k < (int) (mr.getWidth() * 0.5f); ++k)
                        g.fillEllipse (mr.getX() + rnd.nextFloat() * mr.getWidth(), mr.getCentreY() + (rnd.nextFloat() - 0.5f) * rh * 2.4f, 2.2f, 2.2f);
                    break;
                }
            }
            if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (mr.expanded (2, 2), 3.0f, 1.2f); }
        }
        const double beat = ctx.proc.workstation().seqBeat.load();
        if (beat >= 0.0) { g.setColour (C::glow().withAlpha (0.7f)); g.drawVerticalLine ((int) (kLabel + beat / gb * sw), 2.0f, r.getBottom() - 2); }
        if (P.marks.empty()) { g.setColour (C::lcdDim()); g.setFont (font (14.0f)); g.drawText ("Pick a brush on the left and draw: dots sparkle, dashes drone, warbles wobble, ticks click, sprays scatter.", getLocalBounds().reduced (60), juce::Justification::centred); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& P = S();
        const int hit = markAt (e.position);
        dragIndex = -1;
        if (ctx.markBrush == 5) { erase (e.position); return; }
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu()) { P.marks.erase (P.marks.begin() + hit); ctx.select ({ Sel::None, 0 }); ctx.changed(); return; }
            dragIndex = hit; moving = true; grabStart = P.marks[(size_t) hit].start; grabRow = P.marks[(size_t) hit].row;
            ctx.select ({ Sel::Mark, hit }, ctx.tab == 2 ? 2 : 0);
            return;
        }
        if (ctx.markBrush == 6 || e.x < kLabel) { ctx.select ({ Sel::None, 0 }); return; }
        if (P.marks.size() >= 400) return;
        nb::Mark m;
        m.type = (nb::MarkType) juce::jlimit (0, 4, ctx.markBrush);
        m.start = (float) std::floor ((e.x - kLabel) / stepW());
        m.row = juce::jlimit (0, nb::SeqPreset::kRows - 1, nb::SeqPreset::kRows - 1 - (int) ((e.y - 2) / rowH()));
        m.length = 1.0f;
        if (m.type == nb::MarkType::Dash || m.type == nb::MarkType::Warble) { m.level = 0.5f; m.inertia = 0.1f; }
        if (m.type == nb::MarkType::Spray) { m.depth = 60.0f; m.level = 0.6f; }
        P.marks.push_back (m);
        dragIndex = (int) P.marks.size() - 1; moving = false;
        ctx.select ({ Sel::Mark, dragIndex }, 0);
        ctx.changed (false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& P = S();
        if (ctx.markBrush == 5) { erase (e.position); return; }
        if (dragIndex < 0 || dragIndex >= (int) P.marks.size()) return;
        auto& m = P.marks[(size_t) dragIndex];
        if (moving)
        {
            m.start = (float) juce::jlimit (0.0, steps() - 1.0, (double) grabStart + std::round (e.getDistanceFromDragStartX() / stepW()));
            m.row = juce::jlimit (0, nb::SeqPreset::kRows - 1, grabRow - juce::roundToInt (e.getDistanceFromDragStartY() / rowH()));
        }
        else m.length = (float) juce::jlimit (1.0, steps() - m.start, std::round ((e.x - kLabel) / stepW() - m.start + 0.5));
        ctx.changed (false);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragIndex >= 0) { dragIndex = -1; ctx.changed(); } }

private:
    void erase (juce::Point<float> p)
    {
        const int i = markAt (p);
        if (i >= 0) { S().marks.erase (S().marks.begin() + i); ctx.sel = {}; ctx.changed (false); repaint(); }
    }
    Ctx& ctx;
    int dragIndex = -1, grabRow = 0;
    float grabStart = 0.0f;
    bool moving = false;
};

// ------------------------------------------------------------------ One Shot view: cells on the hits
class OneShotLanes final : public juce::Component
{
public:
    explicit OneShotLanes (Ctx& c) : ctx (c) {}
    static constexpr int kLabel = 116;
    int length() const { return ctx.proc.pattern.length.load(); }
    float stepW() const { return (float) (getWidth() - kLabel - 4) / (float) length(); }
    float laneH() const { return (float) (getHeight() - 16) / 16.0f; }
    juce::Rectangle<float> cellRect (const nb::OneShotCell& c) const
    {
        const float y = 16 + c.pad * laneH();
        return { kLabel + c.step * stepW() + 1, y + 2, std::max (6.0f, std::min ((float) c.span, (float) (length() - c.step)) * stepW() - 2), laneH() * 0.62f };
    }
    int cellAt (juce::Point<float> p) const
    {
        auto& cs = ctx.P().matrix.oneShot.cells;
        for (int i = (int) cs.size() - 1; i >= 0; --i) if (cs[(size_t) i].step < length() && cellRect (cs[(size_t) i]).contains (p)) return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const int len = length();
        const float sw = stepW(), lh = laneH();
        for (int s = 0; s < len; s += 4)
        {
            if ((s / 4) % 2 == 0) { g.setColour (juce::Colour (0x10304840)); g.fillRect ((float) kLabel + s * sw, 16.0f, sw * 4, (float) getHeight() - 16); }
            paintLabel (g, juce::String (s / 16 + 1) + "." + juce::String ((s / 4) % 4 + 1), juce::Rectangle<int> (kLabel + (int) (s * sw) + 2, 0, 40, 14), juce::Justification::centredLeft, 7.0f, C::dim());
        }
        auto& cells = ctx.P().matrix.oneShot.cells;
        const int play = ctx.proc.playStep.load();
        for (int t = 0; t < 16; ++t)
        {
            const float y = 16 + t * lh;
            g.setColour (C::text()); g.setFont (font (11.0f, t == ctx.pad));
            g.drawText (juce::String (t + 1).paddedLeft ('0', 2) + " " + ctx.proc.padName (t), juce::Rectangle<int> (4, (int) y, kLabel - 8, (int) lh), juce::Justification::centredLeft, true);
            for (int s = 0; s < len; ++s)
            {
                const auto st = ctx.proc.pattern.get (t, s);
                const float cx = kLabel + (s + 0.5f) * sw, cy = y + lh * 0.82f;
                if (st.vel == 0) { g.setColour (juce::Colour (0x30586560)); g.fillEllipse (cx - 1.5f, cy - 1.5f, 3, 3); continue; }
                const float sz = std::min (sw, lh) * (st.vel > 110 ? 0.42f : 0.32f);
                g.setColour (st.vel > 110 ? C::teal() : juce::Colour (0xff6d7a74)); g.fillEllipse (cx - sz * 0.5f, cy - sz * 0.5f, sz, sz);
            }
        }
        for (int i = 0; i < (int) cells.size(); ++i)
        {
            const auto& c = cells[(size_t) i];
            if (c.step >= len) continue;
            auto r = cellRect (c);
            const bool hasHit = ctx.proc.pattern.get (c.pad, c.step).vel > 0;
            const bool sel = ctx.sel.kind == Sel::Cell && ctx.sel.index == i;
            juce::Colour ring;
            for (const auto& rt : ctx.P().mod.routes) if (juce::String (rt.dest) == "cell/" + juce::String (i) + "/main") ring = C::mod (rt.src);
            paintTile (g, r, C::fxColour (c.fx).withAlpha (hasHit ? 1.0f : 0.45f), fxShort (c.fx), sel, ring);
            // the height (main control) as a meter at the tile's left
            g.setColour (C::text().withAlpha (0.55f));
            g.fillRoundedRectangle (r.getX() + 2, r.getBottom() - 2 - (r.getHeight() - 4) * c.height, 3.0f, (r.getHeight() - 4) * c.height, 1.5f);
        }
        if (play >= 0 && play < len) { g.setColour (C::teal().withAlpha (0.5f)); g.drawRect (kLabel + play * sw, 16.0f, sw, (float) getHeight() - 16, 1.5f); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& cs = ctx.P().matrix.oneShot.cells;
        const int hit = cellAt (e.position);
        drag = -1;
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu()) { cs.erase (cs.begin() + hit); ctx.select ({ Sel::None, 0 }); ctx.changed(); return; }
            drag = hit;
            const auto r = cellRect (cs[(size_t) hit]);
            mode = e.position.x > r.getRight() - 7 ? 1 : 0;
            grabStep = cs[(size_t) hit].step; grabSpan = cs[(size_t) hit].span; grabHeight = cs[(size_t) hit].height;
            ctx.select ({ Sel::Cell, hit }, ctx.tab == 0 ? 1 : ctx.tab);
            return;
        }
        if (e.x < kLabel || e.y < 16) return;
        const int t = juce::jlimit (0, 15, (int) ((e.y - 16) / laneH()));
        const int s = juce::jlimit (0, length() - 1, (int) ((e.x - kLabel) / stepW()));
        if (e.mods.isPopupMenu() || cs.size() >= 256) return;
        nb::OneShotCell c; c.pad = t; c.step = s; c.fx = fxFromIndex (ctx.brush); c.span = c.fx.kind == nb::CellFx::FrictionStretchFx ? 6 : 3;
        cs.push_back (c);
        drag = (int) cs.size() - 1; mode = 1; grabStep = s; grabSpan = c.span; grabHeight = c.height;
        ctx.select ({ Sel::Cell, drag }, 1);
        ctx.changed();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& cs = ctx.P().matrix.oneShot.cells;
        if (drag < 0 || drag >= (int) cs.size()) return;
        auto& c = cs[(size_t) drag];
        if (mode == 1) c.span = juce::jlimit (1, 64, grabSpan + juce::roundToInt (e.getDistanceFromDragStartX() / stepW()));
        else
        {
            if (std::abs (e.getDistanceFromDragStartY()) > std::abs (e.getDistanceFromDragStartX()))
                c.height = juce::jlimit (0.0f, 1.0f, grabHeight - e.getDistanceFromDragStartY() / (laneH() * 3.0f));
            else c.step = juce::jlimit (0, length() - 1, grabStep + juce::roundToInt (e.getDistanceFromDragStartX() / stepW()));
        }
        ctx.changed (false);
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override { if (drag >= 0 && e.mouseWasDraggedSinceMouseDown()) ctx.changed(); drag = -1; }

private:
    Ctx& ctx;
    int drag = -1, mode = 0, grabStep = 0, grabSpan = 1;
    float grabHeight = 0.5f;
};

// ------------------------------------------------------------------ Sample view: boxes on the spectrogram
class SampleCanvas final : public juce::Component
{
public:
    explicit SampleCanvas (Ctx& c) : ctx (c) {}
    static constexpr float kLo = 30.0f, kHi = 16000.0f;
    float yOf (float f) const { return (float) getHeight() - 4 - std::log (juce::jlimit (kLo, kHi, f) / kLo) / std::log (kHi / kLo) * (getHeight() - 8); }
    float fOf (float y) const { return kLo * std::pow (kHi / kLo, juce::jlimit (0.0f, 1.0f, (getHeight() - 4 - y) / (float) (getHeight() - 8))); }
    float xOf (int cell) const { return 4 + cell * (getWidth() - 8) / 32.0f; }
    int cellOf (float x) const { return juce::jlimit (0, 32, juce::roundToInt ((x - 4) / ((getWidth() - 8) / 32.0f))); }
    juce::Rectangle<float> opRect (const nb::SampleOp& o) const
    {
        return juce::Rectangle<float>::leftTopRightBottom (xOf (std::min (o.c0, o.c1)), yOf (std::max (o.fLo, o.fHi)), xOf (std::max (o.c0, o.c1)), yOf (std::min (o.fLo, o.fHi)));
    }
    int opAt (juce::Point<float> p) const
    {
        auto& os = ctx.P().matrix.sample.ops;
        for (int i = (int) os.size() - 1; i >= 0; --i) if (opRect (os[(size_t) i]).contains (p)) return i;
        return -1;
    }

    void updateImage (const nb::RenderedPtr& src)
    {
        if (src.get() == shownSrc && image.isValid() && image.getWidth() == getWidth()) return;
        shownSrc = src.get();
        image = juce::Image (juce::Image::RGB, std::max (8, getWidth()), std::max (8, getHeight()), true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff07120d));
        if (! src || src->size() < 256) return;
        const int W = image.getWidth(), H = image.getHeight();
        constexpr int N = 2048;
        juce::dsp::FFT fft (11);
        std::vector<float> buf (2 * N);
        const int len = src->size();
        for (int x = 0; x < W; ++x)
        {
            const int start = (int) ((double) x / W * std::max (1, len - N));
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N && start + i < len; ++i) buf[(size_t) i] = 0.5f * (src->l[(size_t) (start + i)] + src->r[(size_t) (start + i)]) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int y = 0; y < H; ++y)
            {
                const float f = fOf ((float) y);
                const int bin = juce::jlimit (1, N / 2 - 1, (int) (f / src->sampleRate * N));
                const float db = 20.0f * std::log10 (buf[(size_t) bin] + 1.0e-6f);
                const float v = juce::jlimit (0.0f, 1.0f, (db + 50.0f) / 60.0f);
                image.setPixelAt (x, y, juce::Colour::fromFloatRGBA (0.25f * v + 0.05f * v * v, 0.95f * v, 0.65f * v, 1.0f));
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto d = ctx.proc.renderer().display();
        updateImage (d ? d->sampleIn : nullptr);
        auto r = getLocalBounds().toFloat();
        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (r, 14.0f); g.reduceClipRegion (clip);
        g.drawImageAt (image, 0, 0);
        g.restoreState();
        for (int c = 0; c <= 32; ++c) { g.setColour (juce::Colours::white.withAlpha (c % 4 == 0 ? 0.25f : 0.08f)); g.drawVerticalLine ((int) xOf (c), 4.0f, r.getBottom() - 4); }
        for (float f : { 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawHorizontalLine ((int) yOf (f), 4.0f, r.getRight() - 4);
            g.setColour (C::lcdDim()); g.setFont (font (10.0f)); g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String (f, 0), 8, (int) yOf (f) - 13, 40, 12, juce::Justification::centredLeft);
        }
        auto& ops = ctx.P().matrix.sample.ops;
        for (int i = 0; i < (int) ops.size(); ++i)
        {
            auto o = opRect (ops[(size_t) i]);
            const bool sel = ctx.sel.kind == Sel::SampleOp && ctx.sel.index == i;
            const auto col = C::fxColour (ops[(size_t) i].fx);
            g.setColour (col.withAlpha (0.22f)); g.fillRoundedRectangle (o, 6.0f);
            g.setColour (sel ? juce::Colours::white : col); g.drawRoundedRectangle (o, 6.0f, sel ? 2.4f : 1.6f);
            auto tag = o.reduced (4).removeFromTop (16).withWidth (std::min (o.getWidth() - 8, 140.0f));
            g.setColour (juce::Colour (0xcc0a1611)); g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (col); g.setFont (caps (7.5f)); g.drawText (fxShort (ops[(size_t) i].fx), tag.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
        if (drawing) { g.setColour (C::fxColour (fxFromIndex (ctx.brush))); g.drawRoundedRectangle (juce::Rectangle<float> (dragA, dragB), 6.0f, 1.5f); }
        if (! d || ! d->sampleIn) { g.setColour (C::lcdDim()); g.setFont (font (14.0f)); g.drawText ("Rendering the sample...", getLocalBounds(), juce::Justification::centred); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& ops = ctx.P().matrix.sample.ops;
        const int hit = opAt (e.position);
        drawing = false; drag = -1;
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu()) { ops.erase (ops.begin() + hit); ctx.select ({ Sel::None, 0 }); ctx.changed(); return; }
            drag = hit; grab = ops[(size_t) hit];
            const auto r = opRect (grab);
            mode = e.position.x > r.getRight() - 8 ? 1 : e.position.y < r.getY() + 8 ? 2 : e.position.y > r.getBottom() - 8 ? 3 : 0;
            ctx.select ({ Sel::SampleOp, hit }, 1);
            return;
        }
        if (e.mods.isPopupMenu()) return;
        drawing = true; dragA = dragB = e.position;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drawing) { dragB = e.position; repaint(); return; }
        auto& ops = ctx.P().matrix.sample.ops;
        if (drag < 0 || drag >= (int) ops.size()) return;
        auto& o = ops[(size_t) drag];
        const float dx = (float) e.getDistanceFromDragStartX(), dy = (float) e.getDistanceFromDragStartY();
        const int dc = juce::roundToInt (dx / ((getWidth() - 8) / 32.0f));
        if (mode == 0)
        {
            const int w = std::abs (grab.c1 - grab.c0);
            const int a = juce::jlimit (0, 32 - w, std::min (grab.c0, grab.c1) + dc);
            o.c0 = a; o.c1 = a + w;
            const float ratio = fOf (yOf (grab.fLo) + dy) / grab.fLo;
            o.fLo = juce::jlimit (kLo, kHi, grab.fLo * ratio); o.fHi = juce::jlimit (kLo, kHi, grab.fHi * ratio);
        }
        else if (mode == 1) o.c1 = juce::jlimit (std::min (grab.c0, grab.c1) + 1, 32, std::max (grab.c0, grab.c1) + dc);
        else if (mode == 2) o.fHi = juce::jlimit (std::min (grab.fLo, grab.fHi) * 1.2f, kHi, fOf (yOf (std::max (grab.fLo, grab.fHi)) + dy));
        else o.fLo = juce::jlimit (kLo, std::max (grab.fLo, grab.fHi) / 1.2f, fOf (yOf (std::min (grab.fLo, grab.fHi)) + dy));
        ctx.changed (false);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        auto& ops = ctx.P().matrix.sample.ops;
        if (drawing)
        {
            drawing = false;
            const int c0 = cellOf (std::min (dragA.x, dragB.x)), c1 = std::max (c0 + 1, cellOf (std::max (dragA.x, dragB.x)));
            float hi = fOf (std::min (dragA.y, dragB.y)), lo = fOf (std::max (dragA.y, dragB.y));
            if (hi / lo < 1.5f) { lo = lo / 1.6f; hi = hi * 1.6f; }
            if (ops.size() < 64)
            {
                nb::SampleOp o; o.c0 = c0; o.c1 = c1; o.fLo = juce::jlimit (kLo, kHi, lo); o.fHi = juce::jlimit (kLo, kHi, hi); o.fx = fxFromIndex (ctx.brush);
                ops.push_back (o);
                ctx.select ({ Sel::SampleOp, (int) ops.size() - 1 }, 1);
            }
            ctx.changed();
            return;
        }
        if (drag >= 0) { drag = -1; ctx.changed(); }
    }

private:
    Ctx& ctx;
    juce::Image image;
    const void* shownSrc = nullptr;
    bool drawing = false;
    juce::Point<float> dragA, dragB;
    int drag = -1, mode = 0;
    nb::SampleOp grab;
};

// ------------------------------------------------------------------ the room
class MatrixRoom final : public juce::Component, public Part
{
public:
    explicit MatrixRoom (Ctx& c) : ctx (c), views ({ "SEQUENCER", "ONE SHOT", "SAMPLE" }), seq (c), lanes (c), sample (c), brush (c)
    {
        addAndMakeVisible (views);
        views.onSelect = [this] (int i) { ctx.P().matrix.view = i; ctx.select ({ Sel::None, 0 }); };
        addAndMakeVisible (presets);
        presets.onChange = [this] { pick (presets.getSelectedId()); };
        for (auto* b : { &prev, &next, &save, &init, &onOff, &play }) addAndMakeVisible (b);
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        save.onClick = [this] { saveUser(); };
        init.onClick = [this] { pick (1); };
        onOff.setClickingTogglesState (true);
        onOff.onClick = [this]
        {
            auto& M = ctx.P().matrix;
            bool& on = M.view == 0 ? M.seqOn : M.view == 1 ? M.oneShotOn : M.sampleOn;
            on = onOff.getToggleState(); ctx.changed();
        };
        play.onClick = [this] { ctx.proc.audition (ctx.P().matrix.sample.pad, 0.9f); };
        addChildComponent (seq); addChildComponent (lanes); addChildComponent (sample); addChildComponent (brush);
        const char* tools[] = { "DOT", "DASH", "WARBLE", "TICK", "SPRAY", "ERASE", "SELECT" };
        for (int i = 0; i < 7; ++i)
        {
            auto* b = markTools.add (new Pill (tools[i]));
            b->onClick = [this, i] { ctx.markBrush = i; for (int k = 0; k < markTools.size(); ++k) markTools[k]->setToggleState (k == i, juce::dontSendNotification); };
            addChildComponent (b);
        }
        refresh();
    }

    juce::String section() const { const int v = ctx.P().matrix.view; return v == 0 ? "Sequencer" : v == 1 ? "OneShot" : "Sample"; }
    std::vector<std::string> factoryNames() const { const int v = ctx.P().matrix.view; return v == 0 ? nb::seqNames() : v == 1 ? nb::oneShotNames() : nb::sampleNames(); }

    void pick (int id)
    {
        auto& M = ctx.P().matrix;
        if (id >= 1 && id <= 33)
        {
            const int i = id - 1;
            if (M.view == 0) { M.seq = nb::makeSeq (i); M.seqIndex = i; }
            else if (M.view == 1) { nb::Pattern pat; ctx.proc.snapshotPattern (pat); M.oneShot = nb::makeOneShot (i, pat); M.oneShotIndex = i; }
            else { const int pad = M.sample.pad; const auto file = M.sample.file; M.sample = nb::makeSample (i); M.sampleIndex = i; if (i == 0) { M.sample.pad = pad; M.sample.file = file; } }
            ctx.select ({ Sel::None, 0 });
            ctx.changed();
        }
        else if (id > 1000)
        {
            const auto names = ctx.proc.userPresets (section());
            if (id - 1001 < names.size()) { ctx.proc.loadUserPreset (section(), names[id - 1001]); ctx.select ({ Sel::None, 0 }); ctx.changed(); }
        }
    }
    void step (int d)
    {
        auto& M = ctx.P().matrix;
        const int cur = M.view == 0 ? M.seqIndex : M.view == 1 ? M.oneShotIndex : M.sampleIndex;
        pick ((cur + d + 33) % 33 + 1);
    }
    void saveUser()
    {
        auto* w = new juce::AlertWindow ("Save this " + section(), "A name (kept in your NeoBeat presets, with its samples):", juce::MessageBoxIconType::NoIcon);
        auto& M = ctx.P().matrix;
        w->addTextEditor ("name", juce::String (M.view == 0 ? M.seq.name : M.view == 1 ? M.oneShot.name : M.sample.name) + " (mine)");
        w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            const auto name = w->getTextEditorContents ("name");
            auto& M2 = ctx.P().matrix;
            (M2.view == 0 ? M2.seq.name : M2.view == 1 ? M2.oneShot.name : M2.sample.name) = name.toStdString();
            ctx.proc.saveUserPreset (section(), name);
            ctx.changed();
        }), true);
    }

    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat(), 18.0f);
        if (ctx.P().matrix.view == 0) paintLabel (g, "BRUSH", { 14, 58, 90, 12 }, juce::Justification::centredLeft, 7.5f, C::dim());
        if (ctx.P().matrix.view == 2)
        {
            auto& S = ctx.P().matrix.sample;
            paintLabel (g, "TIME | 32 CELLS  >  |  BAND (HEIGHT = MAIN CONTROL)  ^  |  PLAYS ON PAD " + juce::String (S.pad + 1) + (S.file.empty() ? " | ITS OWN HIT" : " | " + juce::File (S.file).getFileName()),
                        { 14, 54, 700, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        }
    }

    void resized() override
    {
        auto top = juce::Rectangle<int> (12, 10, getWidth() - 24, 36);
        views.setBounds (top.removeFromLeft (300));
        presets.setBounds (top.removeFromLeft (230).reduced (6, 3));
        prev.setBounds (top.removeFromLeft (32).reduced (1, 3)); next.setBounds (top.removeFromLeft (32).reduced (1, 3));
        save.setBounds (top.removeFromLeft (58).reduced (2, 3)); init.setBounds (top.removeFromLeft (52).reduced (2, 3));
        onOff.setBounds (top.reduced (2, 3));
        const int v = ctx.P().matrix.view;
        if (v == 0)
        {
            for (int i = 0; i < markTools.size(); ++i) markTools[i]->setBounds (12, 74 + i * 38, 92, 32);
            seq.setBounds (112, 56, getWidth() - 124, getHeight() - 68);
        }
        lanes.setBounds (8, 54, getWidth() - 16, getHeight() - 54 - 74);
        brush.setBounds (8, getHeight() - 70, getWidth() - 16, 62);
        sample.setBounds (12, 70, getWidth() - 24, getHeight() - 70 - 120);
        play.setBounds (getWidth() - 140, getHeight() - 112, 124, 32);
    }

    void refresh() override
    {
        auto& M = ctx.P().matrix;
        views.setSelected (M.view);
        presets.clear (juce::dontSendNotification);
        const auto names = factoryNames();
        for (int i = 0; i < (int) names.size() && i < 33; ++i) presets.addItem ((i == 0 ? juce::String ("INIT") : juce::String (i).paddedLeft ('0', 2) + "  " + names[(size_t) i]), i + 1);
        const auto user = ctx.proc.userPresets (section());
        if (! user.isEmpty()) { presets.addSeparator(); for (int i = 0; i < user.size(); ++i) presets.addItem ("MINE | " + user[i], 1001 + i); }
        const int idx = M.view == 0 ? M.seqIndex : M.view == 1 ? M.oneShotIndex : M.sampleIndex;
        presets.setText (juce::String (M.view == 0 ? M.seq.name : M.view == 1 ? M.oneShot.name : M.sample.name), juce::dontSendNotification);
        juce::ignoreUnused (idx);
        const bool on = M.view == 0 ? M.seqOn : M.view == 1 ? M.oneShotOn : M.sampleOn;
        onOff.setToggleState (on, juce::dontSendNotification);
        onOff.setButtonText (on ? "ON" : "OFF");
        seq.setVisible (M.view == 0);
        for (int i = 0; i < markTools.size(); ++i) { markTools[i]->setVisible (M.view == 0); markTools[i]->setToggleState (i == ctx.markBrush, juce::dontSendNotification); }
        lanes.setVisible (M.view == 1);
        sample.setVisible (M.view == 2);
        play.setVisible (M.view == 2);
        play.setButtonText ("PLAY PAD " + juce::String (M.sample.pad + 1));
        brush.setVisible (M.view != 0);
        brush.hint = M.view == 1 ? "click a hit to drop a cell" : "drag on the spectrogram to draw a box";
        resized();
        repaint(); seq.repaint(); lanes.repaint(); sample.repaint(); brush.repaint();
    }

    void tick() override
    {
        const int v = ctx.P().matrix.view;
        if (v == 0) seq.repaint();
        if (v == 1) { const int s = ctx.proc.playStep.load(); if (s != lastStep) { lastStep = s; lanes.repaint(); } }
        if (v == 2)
        {
            auto d = ctx.proc.renderer().display();
            const void* now = d ? (const void*) d->sampleIn.get() : nullptr;
            if (now != lastSample) { lastSample = now; sample.repaint(); }
        }
    }

private:
    Ctx& ctx;
    Segmented views;
    juce::ComboBox presets;
    Pill prev { "<" }, next { ">" }, save { "SAVE" }, init { "INIT" }, onOff { "ON" }, play { "PLAY" };
    juce::OwnedArray<Pill> markTools;
    SeqCanvas seq;
    OneShotLanes lanes;
    SampleCanvas sample;
    Brush brush;
    int lastStep = -2;
    const void* lastSample = nullptr;
};

std::unique_ptr<juce::Component> makeMatrixRoom (Ctx& c) { return std::make_unique<MatrixRoom> (c); }
} // namespace ui
