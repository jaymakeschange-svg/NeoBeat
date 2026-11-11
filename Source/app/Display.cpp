// BatchCut - the display: the spectrogram (layers in colour, the edit tools), the event lane, the ruler, the overview.
#include "Editor.h"

using namespace bcui;

namespace
{
const juce::Colour kBg (0xff0a1611);

// grid byte -> amplitude (the grid holds dB: 0 = -120 .. 255 = 0)
const float* ampTable()
{
    static float t[256];
    static bool done = false;
    if (! done) { for (int u = 0; u < 256; ++u) t[u] = u == 0 ? 0.0f : std::pow (10.0f, ((float) u / 2.125f - 120.0f) / 20.0f); done = true; }
    return t;
}

juce::String timeText (double t, double step)
{
    const int m = (int) (t / 60.0);
    const double s = t - 60.0 * m;
    const int dec = step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
    return juce::String (m) + ":" + juce::String (s, dec).paddedLeft ('0', dec > 0 ? 3 + dec : 2);
}

int groupColour (const bc::Doc& d, const bc::Event& e) { return e.group >= 0 && e.group < (int) d.groups.size() ? d.groups[(size_t) e.group].colour : -1; }
bool usable (const bc::Doc& d, const bc::Event& e) { return e.enabled && (e.group < 0 || e.group >= (int) d.groups.size() || d.groups[(size_t) e.group].enabled); }

void playhead (juce::Graphics& g, float x, float h)
{
    g.setColour (juce::Colours::white.withAlpha (0.9f)); g.fillRect (x - 0.5f, 0.0f, 1.5f, h);
    g.setColour (C::glow().withAlpha (0.35f)); g.fillRect (x - 3.0f, 0.0f, 6.0f, h);
}
}

// ---------------------------------------------------------------- the spectrogram

void SpecView::rebuild()
{
    const auto& d = *view.doc;
    const auto& an = *d.an;
    const auto& L = d.layers;
    const int W = std::max (1, getWidth());
    img = juce::Image (juce::Image::RGB, W, bc::kRows, false);
    lev.assign ((size_t) W * bc::kRows, -120.0f);
    std::vector<double> tc ((size_t) W);
    for (int x = 0; x < W; ++x) tc[(size_t) x] = view.t0 + ((double) x + 0.5) / W * (view.t1 - view.t0);
    std::vector<float> yc (bc::kRows);
    for (int r = 0; r < bc::kRows; ++r) yc[(size_t) r] = ((float) r + 0.5f) / bc::kRows;
    bc::Coefs co;
    bc::applyOps (L, tc.data(), W, yc.data(), bc::kRows, co);
    const int nl = (int) L.layers.size();
    std::vector<float> gd ((size_t) nl), cr ((size_t) nl), cg ((size_t) nl), cb ((size_t) nl);
    for (int k = 0; k < nl; ++k)
    {
        const float lin = std::pow (10.0f, L.layers[(size_t) k].gainDb / 20.0f);
        float g = L.gainOf (k) > 0.0f ? L.gainOf (k) : 0.16f * lin;   // a muted layer is shown faintly, not hidden
        if (view.target >= 0 && k != view.target) g *= 0.3f;          // the layer being edited stands out
        gd[(size_t) k] = g;
        const auto c = C::layer (k);
        cr[(size_t) k] = c.getFloatRed(); cg[(size_t) k] = c.getFloatGreen(); cb[(size_t) k] = c.getFloatBlue();
    }
    const float* amp = ampTable();
    const float top = an.dispTop + 6.0f, range = 72.0f;
    const double fps = an.sr / bc::kHop / an.pool;
    uint8_t mx[bc::kAuto][bc::kRows];
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < W; ++x)
    {
        const double ta = view.t0 + (double) x / W * (view.t1 - view.t0), tb = view.t0 + (double) (x + 1) / W * (view.t1 - view.t0);
        int c0 = (int) std::floor (ta * fps), c1 = std::max (c0 + 1, (int) std::floor (tb * fps));
        c0 = std::max (0, c0); c1 = std::min (an.cols, c1);
        if (c0 >= c1 || tb < 0.0)
        {
            for (int r = 0; r < bc::kRows; ++r) bd.setPixelColour (x, r, kBg);
            continue;
        }
        std::memset (mx, 0, sizeof mx);
        for (int c = c0; c < c1; ++c)
            for (int l = 0; l < bc::kAuto; ++l)
            {
                const uint8_t* src = &an.grid[l][(size_t) c * bc::kRows];
                for (int r = 0; r < bc::kRows; ++r) mx[l][r] = std::max (mx[l][r], src[r]);
            }
        for (int r = 0; r < bc::kRows; ++r)
        {
            const size_t idx = (size_t) x * bc::kRows + (size_t) r;
            float a[bc::kAuto]; for (int l = 0; l < bc::kAuto; ++l) a[l] = amp[mx[l][r]];
            float R = 0, G = 0, B = 0, tot = 0, tgt = 0;
            for (int k = 0; k < nl; ++k)
            {
                float v = 0; for (int l = 0; l < bc::kAuto; ++l) if (a[l] > 0) v += co.get (k, l, idx) * a[l];
                if (v <= 0.0f) continue;
                if (k == view.target) tgt = v;
                v *= gd[(size_t) k];
                tot += v; R += v * cr[(size_t) k]; G += v * cg[(size_t) k]; B += v * cb[(size_t) k];
            }
            const float db = 20.0f * std::log10 (tot + 1.0e-9f);
            lev[idx] = 20.0f * std::log10 ((view.target >= 0 ? tgt : tot) + 1.0e-9f);
            float bright = juce::jlimit (0.0f, 1.0f, (db - (top - range)) / range);
            bright = bright * bright * (3.0f - 2.0f * bright);
            const float inv = tot > 0.0f ? 1.0f / tot : 0.0f;
            const float k = 1.15f * bright;
            bd.setPixelColour (x, bc::kRows - 1 - r, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, kBg.getFloatRed() + R * inv * k),
                                                                                   juce::jlimit (0.0f, 1.0f, kBg.getFloatGreen() + G * inv * k),
                                                                                   juce::jlimit (0.0f, 1.0f, kBg.getFloatBlue() + B * inv * k), 1.0f));
        }
    }
    builtFor = view.doc.get(); builtT0 = view.t0; builtT1 = view.t1; builtW = W; builtTarget = view.target;
    dirty = false;
}

void SpecView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    if (! view.doc || ! view.doc->an || ! view.doc->audio)
    {
        paintLcd (g, b, 6.0f);
        const auto st = view.proc.session.status();
        g.setColour (C::lcdText()); g.setFont (font (15.0f, true));
        g.drawText (st.busy ? juce::String (st.what) : juce::String ("Drop audio files or folders here"), b.withTrimmedBottom (b.getHeight() * 0.5f - 10.0f), juce::Justification::centredBottom);
        g.setColour (C::lcdDim()); g.setFont (font (12.0f));
        if (! st.busy) g.drawText ("or use ADD FILES. Long recordings open in parts of 8 minutes.", b.withTrimmedTop (b.getHeight() * 0.5f + 10.0f), juce::Justification::centredTop);
        return;
    }
    const auto& d = *view.doc;
    if (stale()) rebuild();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, b, juce::RectanglePlacement::stretchToFit);
    // frequencies
    g.setFont (font (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        if (f > d.an->tab.fMax) break;
        const float y = heightToY (d.an->tab.yOf (f));
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawHorizontalLine ((int) y, 0.0f, b.getWidth());
        g.setColour (C::lcdDim().withAlpha (0.8f));
        g.drawText (f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String ((int) f), 4, (int) y - 12, 40, 11, juce::Justification::left);
    }
    // where events start; the selected one shaded
    const double sr = view.sr();
    const int si = view.selIndex();
    for (int i = 0; i < (int) d.events.size(); ++i)
    {
        const auto& e = d.events[(size_t) i];
        const float x = timeToX ((double) e.onset / sr);
        if (x < -2.0f || x > b.getWidth() + 2.0f) continue;
        const auto col = C::group (groupColour (d, e));
        if (i == si)
        {
            const float x1 = timeToX ((double) e.end / sr), x0 = timeToX ((double) e.start / sr);
            g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRect (x0, 0.0f, x1 - x0, b.getHeight());
            g.setColour (col.withAlpha (0.9f)); g.fillRect (x - 0.5f, 0.0f, 1.5f, b.getHeight());
        }
        else { g.setColour (col.withAlpha (usable (d, e) ? 0.28f : 0.12f)); g.drawVerticalLine ((int) x, 0.0f, b.getHeight()); }
    }
    // the selection
    if (view.hasSel)
    {
        const auto& s = view.sel;
        if (selBuilt != view.selVersion)
        {
            selImg = juce::Image (juce::Image::ARGB, std::max (1, s.w), std::max (1, s.h), true);
            juce::Image::BitmapData sd (selImg, juce::Image::BitmapData::writeOnly);
            for (int j = 0; j < s.h; ++j) for (int i = 0; i < s.w; ++i)
            {
                const uint8_t v = s.a.size() == (size_t) (s.w * s.h) ? s.a[(size_t) (j * s.w + i)] : 255;
                sd.setPixelColour (i, s.h - 1 - j, juce::Colours::white.withAlpha ((float) v / 255.0f * 0.32f));
            }
            selBuilt = view.selVersion;
        }
        const juce::Rectangle<float> r (timeToX (s.t0), heightToY (s.y1), timeToX (s.t1) - timeToX (s.t0), heightToY (s.y0) - heightToY (s.y1));
        g.drawImage (selImg, r, juce::RectanglePlacement::stretchToFit);
        juce::Path outline; outline.addRectangle (r);
        juce::Path dashed; const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillPath (dashed);
    }
    // what is being drawn
    if (dragging)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        if (view.tool == 0) g.drawRect (juce::Rectangle<float> (down, lastPos), 1.0f);
        else if (view.tool == 1) { g.setColour (juce::Colours::white.withAlpha (0.25f)); g.strokePath (stroke, juce::PathStrokeType (2.0f * view.brushPx, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        else g.strokePath (stroke, juce::PathStrokeType (1.2f));
    }
    else if (view.tool == 1 && hovering)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawEllipse (juce::Rectangle<float> (2.0f * view.brushPx, 2.0f * view.brushPx).withCentre (lastPos), 1.0f);
    }
    // where PLAY starts, and the playhead
    const float cx = timeToX (view.cursor);
    if (cx >= 0.0f && cx <= b.getWidth()) { g.setColour (C::amber().withAlpha (0.6f)); g.fillRect (cx - 0.5f, 0.0f, 1.0f, b.getHeight()); }
    const int64_t ph = view.proc.playhead();
    if (ph >= 0) playhead (g, timeToX ((double) ph / sr), b.getHeight());
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRect (b, 1.0f);
}

bc::Shape SpecView::rasterize (const juce::Path& p, bool isStroke) const
{
    auto area = (isStroke ? p.getBounds().expanded (view.brushPx + 1.0f) : p.getBounds()).getIntersection (getLocalBounds().toFloat());
    const auto box = area.getSmallestIntegerContainer();
    if (box.getWidth() < 1 || box.getHeight() < 1) return {};
    const int w = juce::jlimit (1, 2048, box.getWidth()), h = juce::jlimit (1, 1024, box.getHeight());
    juce::Image m (juce::Image::SingleChannel, w, h, true);
    {
        juce::Graphics g (m);
        g.addTransform (juce::AffineTransform::translation ((float) -box.getX(), (float) -box.getY()).scaled ((float) w / (float) box.getWidth(), (float) h / (float) box.getHeight()));
        g.setColour (juce::Colours::white);
        if (isStroke) g.strokePath (p, juce::PathStrokeType (2.0f * view.brushPx, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        else g.fillPath (p);
    }
    bc::Shape s;
    s.t0 = xToTime ((float) box.getX()); s.t1 = xToTime ((float) box.getRight());
    s.y0 = yToHeight ((float) box.getBottom()); s.y1 = yToHeight ((float) box.getY());
    s.w = w; s.h = h; s.a.assign ((size_t) (w * h), 0);
    const juce::Image::BitmapData bd (m, juce::Image::BitmapData::readOnly);
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) s.a[(size_t) (j * w + i)] = *bd.getPixelPointer (i, h - 1 - j);   // row 0: the lowest frequency
    return s;
}

bool SpecView::stale() const noexcept
{
    return dirty || builtFor != view.doc.get() || builtT0 != view.t0 || builtT1 != view.t1 || builtW != getWidth() || builtTarget != view.target;
}

void SpecView::wand (juce::Point<float> p, int selMode)
{
    if (! view.doc || ! view.doc->an || getWidth() <= 0) return;
    if (stale()) rebuild();   // the levels must be the ones of what is on screen now
    const int W = builtW;
    if (lev.size() != (size_t) W * bc::kRows || W <= 0) return;
    const int x = juce::jlimit (0, W - 1, (int) (p.x / std::max (1, getWidth()) * W));
    const int r = juce::jlimit (0, bc::kRows - 1, (int) (yToHeight (p.y) * bc::kRows));
    const float seed = lev[(size_t) x * bc::kRows + (size_t) r];
    if (seed < -110.0f) return;
    // each frequency's background on screen (its median): the wand never floods into it, so a click on a sound
    // takes the sound and stops where the noise begins. A click on the noise itself takes noise of that level.
    std::vector<float> floor (bc::kRows), col ((size_t) W);
    for (int k = 0; k < bc::kRows; ++k)
    {
        for (int i = 0; i < W; ++i) col[(size_t) i] = lev[(size_t) i * bc::kRows + (size_t) k];
        std::nth_element (col.begin(), col.begin() + W / 2, col.end());
        floor[(size_t) k] = col[(size_t) (W / 2)];
    }
    const bool onNoise = seed < floor[(size_t) r] + 6.0f;
    auto inside = [&] (size_t cell, int row)
    {
        const float v = lev[cell];
        if (onNoise) return std::fabs (v - seed) <= 0.5f * view.tolDb;
        return v >= seed - view.tolDb && v >= floor[(size_t) row] + 6.0f;
    };
    std::vector<uint8_t> in ((size_t) W * bc::kRows, 0);
    std::vector<int> stack { x * bc::kRows + r };
    in[(size_t) stack[0]] = 1;
    int x0 = x, x1 = x, r0 = r, r1 = r, count = 0;
    while (! stack.empty() && count < 400000)
    {
        const int c = stack.back(); stack.pop_back(); ++count;
        const int cx = c / bc::kRows, cr = c % bc::kRows;
        x0 = std::min (x0, cx); x1 = std::max (x1, cx); r0 = std::min (r0, cr); r1 = std::max (r1, cr);
        const int nb[4][2] = { { cx - 1, cr }, { cx + 1, cr }, { cx, cr - 1 }, { cx, cr + 1 } };
        for (const auto& n : nb)
        {
            if (n[0] < 0 || n[0] >= W || n[1] < 0 || n[1] >= bc::kRows) continue;
            const size_t k = (size_t) n[0] * bc::kRows + (size_t) n[1];
            if (in[k] || ! inside (k, n[1])) continue;
            in[k] = 1; stack.push_back ((int) k);
        }
    }
    bc::Shape s;
    s.t0 = builtT0 + (double) x0 / W * (builtT1 - builtT0); s.t1 = builtT0 + (double) (x1 + 1) / W * (builtT1 - builtT0);
    s.y0 = (float) r0 / bc::kRows; s.y1 = (float) (r1 + 1) / bc::kRows;
    s.w = x1 - x0 + 1; s.h = r1 - r0 + 1; s.a.assign ((size_t) (s.w * s.h), 0);
    for (int j = 0; j < s.h; ++j) for (int i = 0; i < s.w; ++i) if (in[(size_t) (x0 + i) * bc::kRows + (size_t) (r0 + j)]) s.a[(size_t) (j * s.w + i)] = 255;
    view.select (s, selMode);
}

void SpecView::mouseDown (const juce::MouseEvent& e)
{
    if (! view.doc || ! view.doc->an) return;
    grabKeyboardFocusOnEditor();
    if (e.mods.isPopupMenu()) { menuAt = xToTime (e.position.x); showMenu(); return; }
    mode = e.mods.isShiftDown() ? 1 : (e.mods.isAltDown() ? 2 : 0);
    down = lastPos = e.position;
    stroke.clear(); stroke.startNewSubPath (down);
    if (view.tool == 1) stroke.lineTo (down.translated (0.01f, 0.0f));
    if (view.tool == 3) { dragging = false; wand (e.position, mode); return; }
    dragging = true;
}

void SpecView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    lastPos = e.position.withX (juce::jlimit (0.0f, (float) getWidth(), e.position.x)).withY (juce::jlimit (0.0f, (float) getHeight(), e.position.y));
    if (view.tool == 1 || view.tool == 2) stroke.lineTo (lastPos);
    repaint();
}

void SpecView::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    if (e.getDistanceFromDragStart() < 3 && view.tool != 1)
    {
        // a click: PLAY starts here
        view.cursor = juce::jlimit (0.0, view.length(), xToTime (down.x));
        if (mode == 0 && view.hasSel) view.clearSelection(); else if (view.repaintAll) view.repaintAll();
        return;
    }
    bc::Shape s;
    if (view.tool == 0)
    {
        const auto r = juce::Rectangle<float> (down, lastPos).getIntersection (getLocalBounds().toFloat());
        if (r.getWidth() >= 1.0f && r.getHeight() >= 1.0f) s = bc::Shape::rect (xToTime (r.getX()), xToTime (r.getRight()), yToHeight (r.getBottom()), yToHeight (r.getY()));
    }
    else if (view.tool == 1) s = rasterize (stroke, true);
    else { stroke.closeSubPath(); s = rasterize (stroke, false); }
    stroke.clear();
    view.select (s, mode);
    repaint();
}

void SpecView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! view.doc || view.tool == 3) return;
    view.cursor = juce::jlimit (0.0, view.length(), xToTime (e.position.x));
    view.proc.playRange ((int64_t) (view.cursor * view.sr()), view.doc->audio->size(), false);
    view.refreshPanels();
}

void SpecView::mouseMove (const juce::MouseEvent& e)
{
    lastPos = e.position; hovering = true;
    if (! view.doc || ! view.doc->an) return;
    const double t = xToTime (e.position.x) + (double) view.doc->offset / view.sr();
    const float f = view.doc->an->tab.freqOf (yToHeight (e.position.y));
    juce::String s = timeText (t, 0.001) + "   " + (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String ((int) f) + " Hz");
    if (stale() && getWidth() > 0) rebuild();
    const int x = juce::jlimit (0, std::max (0, builtW - 1), (int) e.position.x);
    const int r = juce::jlimit (0, bc::kRows - 1, (int) (yToHeight (e.position.y) * bc::kRows));
    if (lev.size() == (size_t) builtW * bc::kRows && builtW > 0) { const float v = lev[(size_t) x * bc::kRows + (size_t) r]; if (v > -110.0f) s << "   " << juce::String (juce::roundToInt (v)) << " dB"; }
    if (view.target >= 0) if (auto* ed = view.edits()) if (view.target < (int) ed->layers.layers.size()) s << "   editing " << juce::String::fromUTF8 (ed->layers.layers[(size_t) view.target].name.c_str());
    view.hover = s;
    if (view.hoverChanged) view.hoverChanged();
    if (view.tool == 1) repaint();
}

void SpecView::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    view.hover = {};
    if (view.hoverChanged) view.hoverChanged();
    repaint();
}

void SpecView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! view.doc) return;
    const float dy = w.isReversed ? -w.deltaY : w.deltaY, dx = w.isReversed ? -w.deltaX : w.deltaX;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { if (dy != 0.0f) view.zoom (dy > 0 ? 0.8 : 1.25, xToTime (e.position.x)); return; }
    if (e.mods.isAltDown())
    {
        if (view.tool == 3) view.tolDb = juce::jlimit (1.0f, 40.0f, view.tolDb + (dy > 0 ? 1.0f : -1.0f));
        else view.brushPx = juce::jlimit (2.0f, 80.0f, view.brushPx * (dy > 0 ? 1.15f : 1.0f / 1.15f));
        repaint();
        return;
    }
    const double span = view.t1 - view.t0;
    const float d = std::fabs (dx) > std::fabs (dy) ? -dx : -dy;
    view.scroll (d * span * 0.35);
}

void SpecView::grabKeyboardFocusOnEditor()
{
    for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<juce::AudioProcessorEditor*> (p) != nullptr) { p->grabKeyboardFocus(); return; }
}

void SpecView::showMenu()
{
    juce::PopupMenu m;
    const bool sel = view.hasSel;
    m.addItem (1, "Erase the selection", sel);
    m.addItem (2, "Selection 6 dB quieter", sel);
    m.addItem (3, "Selection 6 dB louder", sel);
    m.addItem (4, "Move the selection to a new layer", sel);
    m.addItem (5, "Copy the selection to a new layer", sel);
    m.addItem (6, "Clear the selection", sel);
    m.addSeparator();
    m.addItem (7, "Play from here");
    m.addItem (8, "Add an event here");
    const double at = menuAt;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, at] (int r)
    {
        switch (r)
        {
            case 1: view.edit (bc::EditOp::Gain, 0.0f, -1); break;
            case 2: view.edit (bc::EditOp::Gain, 0.5f, -1); break;
            case 3: view.edit (bc::EditOp::Gain, 2.0f, -1); break;
            case 4: view.edit (bc::EditOp::Move, 1.0f, -2); break;
            case 5: view.edit (bc::EditOp::Copy, 1.0f, -2); break;
            case 6: view.clearSelection(); break;
            case 7: if (view.doc) { view.cursor = at; view.proc.playRange ((int64_t) (at * view.sr()), view.doc->audio->size(), false); view.refreshPanels(); } break;
            case 8: view.addEventAt (at); break;
            default: break;
        }
    });
}

// ---------------------------------------------------------------- the event lane

int EventLane::hit (float x) const
{
    if (! view.doc) return -1;
    const auto& ev = view.doc->events;
    const double sr = view.sr();
    int best = -1; float bd = 6.0f;
    for (int i = 0; i < (int) ev.size(); ++i) { const float d = std::fabs (timeToX ((double) ev[(size_t) i].onset / sr) - x); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) return best;
    const double t = xToTime (x);
    for (int i = (int) ev.size() - 1; i >= 0; --i) if (t >= (double) ev[(size_t) i].start / sr && t <= (double) ev[(size_t) i].end / sr) return i;
    return -1;
}

void EventLane::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    paintLcd (g, b, 5.0f);
    if (! view.doc) return;
    const auto& d = *view.doc;
    const double sr = view.sr();
    const int si = view.selIndex();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (getLocalBounds().reduced (1));
    for (int i = 0; i < (int) d.events.size(); ++i)
    {
        const auto& e = d.events[(size_t) i];
        const float x0 = timeToX ((double) e.start / sr), x1 = std::max (x0 + 2.0f, timeToX ((double) e.end / sr)), xo = timeToX ((double) e.onset / sr);
        if (x1 < 0.0f || x0 > b.getWidth()) continue;
        const auto col = C::group (groupColour (d, e));
        const bool on = usable (d, e);
        const juce::Rectangle<float> r (x0, 5.0f, x1 - x0, b.getHeight() - 10.0f);
        g.setColour (col.withAlpha (on ? 0.42f : 0.1f)); g.fillRoundedRectangle (r, 2.0f);
        if (! on) { g.setColour (col.withAlpha (0.4f)); g.drawRoundedRectangle (r, 2.0f, 0.8f); }
        g.setColour (col.withAlpha (on ? 1.0f : 0.45f)); g.fillRect (xo - 0.5f, 3.0f, 1.6f, b.getHeight() - 6.0f);
        if (e.found) { juce::Path p; p.addTriangle (xo - 3.5f, 2.0f, xo + 3.5f, 2.0f, xo, 7.0f); g.fillPath (p); }
        if (e.reattack) { g.setColour (col.withAlpha (0.8f)); g.fillEllipse (xo - 2.0f, b.getHeight() - 7.0f, 4.0f, 4.0f); }
        if (i == si)
        {
            g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.expanded (0.5f), 2.0f, 1.4f);
            g.fillRect (x0 - 1.5f, 3.0f, 3.0f, b.getHeight() - 6.0f);
            g.fillRect (x1 - 1.5f, 3.0f, 3.0f, b.getHeight() - 6.0f);
        }
    }
    if (dragKind != 0) { g.setColour (C::amber()); g.fillRect (dragX - 1.0f, 0.0f, 2.0f, b.getHeight()); }
    const int64_t ph = view.proc.playhead();
    if (ph >= 0) playhead (g, timeToX ((double) ph / sr), b.getHeight());
}

void EventLane::mouseDown (const juce::MouseEvent& e)
{
    if (! view.doc) return;
    dragKind = 0; dragIndex = -1;
    const int si = view.selIndex();
    const double sr = view.sr();
    if (si >= 0 && ! e.mods.isPopupMenu())
    {
        const auto& s = view.doc->events[(size_t) si];
        const float x0 = timeToX ((double) s.start / sr), x1 = std::max (x0 + 2.0f, timeToX ((double) s.end / sr));
        if (std::fabs (e.position.x - x1) <= 5.0f) { dragKind = 2; dragIndex = si; dragX = e.position.x; return; }
        if (std::fabs (e.position.x - x0) <= 5.0f) { dragKind = 1; dragIndex = si; dragX = e.position.x; return; }
    }
    const int i = hit (e.position.x);
    if (e.mods.isPopupMenu())
    {
        if (i >= 0) { view.selectEvent (i, false); eventMenu (view, i, this); }
        else
        {
            const double t = xToTime (e.position.x);
            juce::PopupMenu m; m.addItem (1, "Add an event here");
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, t] (int r) { if (r == 1) view.addEventAt (t); });
        }
        return;
    }
    if (i >= 0) view.selectEvent (i, true);
    else { view.selOnset = -1; view.cursor = juce::jlimit (0.0, view.length(), xToTime (e.position.x)); view.repaintAll(); view.refreshPanels(); }
}

void EventLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKind == 0) return;
    dragX = juce::jlimit (0.0f, (float) getWidth(), e.position.x);
    repaint();
}

void EventLane::mouseUp (const juce::MouseEvent& e)
{
    if (dragKind == 0) return;
    const int kind = dragKind; dragKind = 0;
    if (e.getDistanceFromDragStart() < 2) { repaint(); return; }
    if (kind == 1) view.moveStart (dragIndex, xToTime (dragX));
    else view.moveEnd (dragIndex, xToTime (dragX));
}

void EventLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! view.doc || hit (e.position.x) >= 0) return;
    view.addEventAt (xToTime (e.position.x));
}

void EventLane::mouseMove (const juce::MouseEvent& e)
{
    if (! view.doc) return;
    const int si = view.selIndex();
    const double sr = view.sr();
    bool edge = false;
    if (si >= 0)
    {
        const auto& s = view.doc->events[(size_t) si];
        const float x0 = timeToX ((double) s.start / sr), x1 = std::max (x0 + 2.0f, timeToX ((double) s.end / sr));
        edge = std::fabs (e.position.x - x0) <= 5.0f || std::fabs (e.position.x - x1) <= 5.0f;
    }
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    const int i = hit (e.position.x);
    juce::String tip;
    if (i >= 0)
    {
        const auto& ev = view.doc->events[(size_t) i];
        const auto& d = *view.doc;
        tip << (ev.group >= 0 && ev.group < (int) d.groups.size() ? juce::String::fromUTF8 (d.groups[(size_t) ev.group].name.c_str()) : juce::String ("event"))
            << "  at " << timeText ((double) (ev.onset + d.offset) / sr, 0.001) << ", " << juce::String ((double) (ev.end - ev.start) / sr, 3) << " s, "
            << juce::String (juce::roundToInt (ev.snrDb)) << " dB over the background" << (ev.found ? " (found by FIND MORE)" : "") << (ev.manual ? " (placed by hand)" : "")
            << (usable (d, ev) ? "" : " (not exported)");
    }
    else tip = "Click an event to hear it; double-click to add one; drag the selected one's edges";
    setTooltip (tip);
}

void EventLane::eventMenu (View& view, int index, juce::Component* target)
{
    if (! view.doc || index < 0 || index >= (int) view.doc->events.size()) return;
    const auto& e = view.doc->events[(size_t) index];
    const int64_t onset = e.onset;
    const int g = e.group;
    juce::PopupMenu m;
    m.addItem (1, "Play");
    m.addItem (2, "Export it", true, e.enabled);
    m.addItem (3, "Delete");
    m.addItem (4, "Find more like this (in every file)");
    m.addSeparator();
    m.addItem (5, "Back to the detected end", e.userEnd);
    if (g >= 0 && g < (int) view.doc->groups.size())
    {
        const auto& gr = view.doc->groups[(size_t) g];
        m.addItem (6, "Export its group, \"" + juce::String::fromUTF8 (gr.name.c_str()) + "\"", true, gr.enabled);
    }
    juce::ignoreUnused (target);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [&view, onset, g] (int r)
    {
        const int i = view.indexOf (onset);   // detection may have run since: found again by its start
        if (i < 0 && r != 6) return;
        switch (r)
        {
            case 1: view.proc.playEvent (view.doc->events[(size_t) i]); break;
            case 2: view.toggleEvent (i); break;
            case 3: view.deleteEvent (i); break;
            case 4: view.findMore (i); break;
            case 5: view.resetEnd (i); break;
            case 6: view.toggleGroup (g); break;
            default: break;
        }
    });
}

// ---------------------------------------------------------------- the ruler

void Ruler::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff13261e)); g.fillRect (b);
    if (! view.doc) return;
    const double span = view.t1 - view.t0, pxPerS = b.getWidth() / std::max (1e-9, span);
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    double step = 600; for (double s : steps) if (s * pxPerS >= 80.0) { step = s; break; }
    const double off = (double) view.doc->offset / view.sr();
    g.setFont (font (9.5f));
    for (double t = std::ceil ((view.t0 + off) / step) * step; t <= view.t1 + off; t += step)
    {
        const float x = (float) ((t - off - view.t0) * pxPerS);
        g.setColour (C::lcdDim()); g.drawVerticalLine ((int) x, 0.0f, 5.0f);
        g.setColour (C::lcdText().withAlpha (0.8f)); g.drawText (timeText (t, step), (int) x + 3, 2, 80, (int) b.getHeight() - 2, juce::Justification::topLeft);
        for (int k = 1; k < 5; ++k) { const float xs = x + (float) (k * step / 5.0 * pxPerS); g.setColour (C::lcdDim().withAlpha (0.5f)); g.drawVerticalLine ((int) xs, 0.0f, 3.0f); }
    }
}

// ---------------------------------------------------------------- the overview

void Overview::centreAt (float x)
{
    if (! view.doc) return;
    const double t = (double) x / std::max (1, getWidth()) * view.length(), w = view.t1 - view.t0;
    view.setView (t - 0.5 * w, t + 0.5 * w);
}

void Overview::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    paintLcd (g, b, 6.0f);
    if (! view.doc || ! view.doc->an) return;
    const auto& d = *view.doc;
    const auto& mn = d.an->peakMin; const auto& mx = d.an->peakMax;
    const int W = getWidth(); const float mid = b.getCentreY(), half = b.getHeight() * 0.42f;
    float top = 1.0e-6f; for (size_t i = 0; i < mx.size(); ++i) top = std::max (top, std::max (mx[i], -mn[i]));
    g.setColour (C::lcdDim());
    if (! mx.empty())
        for (int x = 0; x < W; ++x)
        {
            const size_t k0 = (size_t) x * mx.size() / (size_t) W, k1 = std::max (k0 + 1, (size_t) (x + 1) * mx.size() / (size_t) W);
            float lo = 0, hi = 0; for (size_t k = k0; k < k1 && k < mx.size(); ++k) { lo = std::min (lo, mn[k]); hi = std::max (hi, mx[k]); }
            g.drawVerticalLine (x, mid - hi / top * half, mid - lo / top * half + 1.0f);
        }
    const double len = std::max (1e-9, view.length()), sr = view.sr();
    for (const auto& e : d.events)
    {
        const float x = (float) ((double) e.onset / sr / len * W);
        g.setColour (C::group (groupColour (d, e)).withAlpha (usable (d, e) ? 0.9f : 0.3f));
        g.fillRect (x, b.getBottom() - 6.0f, 1.0f, 4.0f);
    }
    const float v0 = (float) (view.t0 / len * W), v1 = (float) (view.t1 / len * W);
    const juce::Rectangle<float> win (v0, 1.0f, std::max (3.0f, v1 - v0), b.getHeight() - 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRect (win);
    g.setColour (C::glow().withAlpha (0.8f)); g.drawRect (win, 1.0f);
    const int64_t ph = view.proc.playhead();
    if (ph >= 0) playhead (g, (float) ((double) ph / sr / len * W), b.getHeight());
}
