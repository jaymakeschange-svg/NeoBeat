// BatchCut - the editor, and the actions every part of it shares (View).
#include "Editor.h"

using namespace bcui;

// ---------------------------------------------------------------- View

int View::indexOf (int64_t onset) const
{
    if (! doc || onset < 0) return -1;
    int best = -1; int64_t bd = (int64_t) (0.005 * sr()) + 1;
    for (int i = 0; i < (int) doc->events.size(); ++i) { const auto d = std::llabs (doc->events[(size_t) i].onset - onset); if (d < bd) { bd = d; best = i; } }
    return best;
}

int View::selIndex() const { return indexOf (selOnset); }

bc::PartEdits* View::edits()
{
    if (! doc || doc->file < 0 || doc->file >= (int) proc.session.files.size()) return nullptr;
    return &proc.session.edits (doc->file, doc->part);
}

void View::setView (double a, double b)
{
    const double len = std::max (0.05, length());
    double w = juce::jlimit (std::min (0.01, len), std::max (0.01, len), b - a);
    if (! std::isfinite (w)) w = len;
    a = juce::jlimit (0.0, std::max (0.0, len - w), a);
    t0 = a; t1 = a + w;
    proc.viewStart = t0; proc.viewEnd = t1;
    if (repaintAll) repaintAll();
}

void View::zoom (double factor, double around)
{
    const double w = (t1 - t0) * factor;
    setView (around - (around - t0) * factor, around - (around - t0) * factor + w);
}

void View::select (const bc::Shape& s, int mode)
{
    ++selVersion;
    if (mode == 0 || ! hasSel)
    {
        if (mode == 2 || s.empty()) { clearSelection(); return; }
        sel = s; hasSel = true;
        if (repaintAll) repaintAll();
        if (refreshPanels) refreshPanels();
        return;
    }
    // add: the union of the two boxes; take away: the old box. Both sampled on a fine grid.
    const double T0 = mode == 1 ? std::min (sel.t0, s.t0) : sel.t0, T1 = mode == 1 ? std::max (sel.t1, s.t1) : sel.t1;
    const float Y0 = mode == 1 ? std::min (sel.y0, s.y0) : sel.y0, Y1 = mode == 1 ? std::max (sel.y1, s.y1) : sel.y1;
    bc::Shape out; out.t0 = T0; out.t1 = T1; out.y0 = Y0; out.y1 = Y1;
    out.w = juce::jlimit (4, 2048, (int) ((T1 - T0) / std::max (1e-6, t1 - t0) * 1400.0));
    out.h = juce::jlimit (4, 512, (int) ((Y1 - Y0) * 480.0f));
    out.a.assign ((size_t) (out.w * out.h), 0);
    for (int j = 0; j < out.h; ++j)
    {
        const float y = Y0 + ((float) j + 0.5f) / (float) out.h * (Y1 - Y0);
        for (int i = 0; i < out.w; ++i)
        {
            const double t = T0 + ((double) i + 0.5) / out.w * (T1 - T0);
            const float a = sel.at (t, y), b = s.at (t, y);
            const float v = mode == 1 ? std::max (a, b) : a * (1.0f - b);
            out.a[(size_t) (j * out.w + i)] = (uint8_t) std::lround (juce::jlimit (0.0f, 1.0f, v) * 255.0f);
        }
    }
    if (out.empty()) { clearSelection(); return; }
    sel = std::move (out);
    if (repaintAll) repaintAll();
    if (refreshPanels) refreshPanels();
}

void View::clearSelection()
{
    ++selVersion;
    hasSel = false; sel = {};
    if (repaintAll) repaintAll();
    if (refreshPanels) refreshPanels();
}

View::Undo View::snapshot (bool layers)
{
    Undo u; u.layers = layers;
    if (doc && doc->file >= 0 && doc->file < (int) proc.session.files.size())
    {
        u.path = proc.session.files[(size_t) doc->file].info.path; u.part = doc->part;
        const auto& ed = proc.session.edits (doc->file, doc->part);
        u.layerCount = (int) ed.layers.layers.size();
        if (! layers) { u.keep = ed.keep; u.deleted = ed.deleted; u.off = ed.off; u.groupsOff = ed.groupsOff; }
    }
    return u;
}

bc::PartEdits* View::editsOf (const Undo& u)
{
    for (int i = 0; i < (int) proc.session.files.size(); ++i)
        if (proc.session.files[(size_t) i].info.path == u.path) return &proc.session.edits (i, u.part);
    return nullptr;
}

bool View::isOpen (const Undo& u) const
{
    return doc && doc->file >= 0 && doc->file < (int) proc.session.files.size() && proc.session.files[(size_t) doc->file].info.path == u.path && doc->part == u.part;
}

void View::edit (bc::EditOp::Kind kind, float gain, int toLayer)
{
    auto* ed = edits();
    if (ed == nullptr || ! hasSel || sel.empty()) return;
    Undo u = snapshot (true);
    int tgt = -1;
    if (kind != bc::EditOp::Gain)
    {
        if (toLayer == -2)
        {
            if ((int) ed->layers.layers.size() >= bc::kMaxLayers) { proc.lastMessage = "There are 8 layers already"; if (refreshPanels) refreshPanels(); return; }
            tgt = ed->layers.addLayer();
        }
        else tgt = toLayer;
        if (tgt < 0 || tgt >= (int) ed->layers.layers.size() || tgt == target) return;
    }
    bc::EditOp op; op.kind = kind; op.layer = target; op.target = tgt; op.gain = gain; op.shape = sel;
    ed->layers.apply (std::move (op));
    undos.push_back (std::move (u)); redos.clear();
    layersChanged();
}

void View::layersChanged()
{
    proc.session.layersChanged();
    doc = proc.session.doc();
    proc.stateChanged();
    if (proc.isPlaying() && ! proc.playingEvent) play();   // what plays follows the edits
    if (repaintAll) repaintAll();
    if (refreshPanels) refreshPanels();
}

static void trimLayers (bc::LayerState& L, int count)
{
    // layers added by an edit that was undone go too, if nothing else uses them
    while ((int) L.layers.size() > std::max (count, bc::kAuto))
    {
        const int last = (int) L.layers.size() - 1;
        bool used = false; for (const auto& o : L.ops) used = used || o.target == last || o.layer == last;
        if (used) break;
        L.layers.pop_back();
    }
}

void View::undo()
{
    if (undos.empty()) return;
    Undo u = std::move (undos.back()); undos.pop_back();
    auto* ed = editsOf (u);
    if (ed == nullptr) return;
    if (u.layers)
    {
        Undo r = u; r.layerCount = (int) ed->layers.layers.size();
        if (! ed->layers.undo()) return;
        trimLayers (ed->layers, u.layerCount);
        redos.push_back (std::move (r));
        if (isOpen (u)) layersChanged();
    }
    else
    {
        Undo r = u; r.keep = ed->keep; r.deleted = ed->deleted; r.off = ed->off; r.groupsOff = ed->groupsOff;
        ed->keep = u.keep; ed->deleted = u.deleted; ed->off = u.off; ed->groupsOff = u.groupsOff;
        redos.push_back (std::move (r));
        if (isOpen (u)) eventsChanged();
    }
}

void View::redo()
{
    if (redos.empty()) return;
    Undo r = std::move (redos.back()); redos.pop_back();
    auto* ed = editsOf (r);
    if (ed == nullptr) return;
    if (r.layers)
    {
        Undo u = r; u.layerCount = (int) ed->layers.layers.size();
        if (! ed->layers.undone.empty())
        {
            const auto& op = ed->layers.undone.back();
            while (op.target >= (int) ed->layers.layers.size() && (int) ed->layers.layers.size() < bc::kMaxLayers) ed->layers.addLayer();
        }
        if (! ed->layers.redo()) return;
        undos.push_back (std::move (u));
        if (isOpen (r)) layersChanged();
    }
    else
    {
        Undo u = r; u.keep = ed->keep; u.deleted = ed->deleted; u.off = ed->off; u.groupsOff = ed->groupsOff;
        ed->keep = r.keep; ed->deleted = r.deleted; ed->off = r.off; ed->groupsOff = r.groupsOff;
        undos.push_back (std::move (u));
        if (isOpen (r)) eventsChanged();
    }
}

void View::selectEvent (int index, bool playIt)
{
    if (! doc || index < 0 || index >= (int) doc->events.size()) return;
    const auto& e = doc->events[(size_t) index];
    selOnset = e.onset;
    // keep it in sight
    const double ts = (double) e.start / sr(), te = (double) e.end / sr();
    if (ts < t0 || te > t1) { const double w = t1 - t0; if (te - ts < w) setView (ts - 0.25 * w, ts + 0.75 * w); }
    if (playIt) proc.playEvent (e);
    if (repaintAll) repaintAll();
    if (refreshPanels) refreshPanels();
}

void View::step (int dir)
{
    if (! doc || doc->events.empty()) return;
    int i = selIndex();
    if (i < 0) { i = 0; for (int k = 0; k < (int) doc->events.size(); ++k) if ((double) doc->events[(size_t) k].onset / sr() >= t0) { i = k; break; } }
    else i = juce::jlimit (0, (int) doc->events.size() - 1, i + dir);
    selectEvent (i, true);
}

static void eraseNear (std::vector<bc::Event>& keep, int64_t onset, int64_t gap)
{
    keep.erase (std::remove_if (keep.begin(), keep.end(), [&] (const bc::Event& k) { return std::llabs (k.onset - onset) < gap; }), keep.end());
}

int64_t View::gapSamples() const { return std::max<int64_t> (1, (int64_t) (proc.session.settings.detect.minGapMs * 0.001 * sr())); }

void View::deleteEvent (int index)
{
    auto* ed = edits();
    if (ed == nullptr || index < 0 || index >= (int) doc->events.size()) return;
    beforeEventChange();
    const auto e = doc->events[(size_t) index];
    eraseNear (ed->keep, e.onset, gapSamples());
    ed->deleted.push_back (e.onset);
    // the next one is selected, so DELETE can be pressed again
    selOnset = index + 1 < (int) doc->events.size() ? doc->events[(size_t) index + 1].onset : -1;
    eventsChanged();
}

void View::toggleEvent (int index)
{
    auto* ed = edits();
    if (ed == nullptr || index < 0 || index >= (int) doc->events.size()) return;
    beforeEventChange();
    const auto onset = doc->events[(size_t) index].onset;
    const auto gap = gapSamples();
    const auto it = std::find_if (ed->off.begin(), ed->off.end(), [&] (int64_t o) { return std::llabs (o - onset) < gap; });
    if (it != ed->off.end()) ed->off.erase (it); else ed->off.push_back (onset);
    eventsChanged();
}

void View::addEventAt (double seconds)
{
    auto* ed = edits();
    if (ed == nullptr || ! doc->audio || ! doc->an) return;
    beforeEventChange();
    auto e = bc::makeManual (*doc->audio, *doc->an, proc.session.settings.detect, seconds);
    eraseNear (ed->keep, e.onset, gapSamples());
    ed->keep.push_back (e);
    selOnset = e.onset;
    eventsChanged();
}

void View::moveStart (int index, double seconds)
{
    auto* ed = edits();
    if (ed == nullptr || index < 0 || index >= (int) doc->events.size()) return;
    beforeEventChange();
    const auto e = doc->events[(size_t) index];
    eraseNear (ed->keep, e.onset, gapSamples());
    if (! e.manual) ed->deleted.push_back (e.onset);
    auto m = e;
    m.onset = juce::jlimit<int64_t> (0, std::max<int64_t> (0, e.end - 64), (int64_t) std::llround (seconds * sr()));
    m.start = m.onset; m.manual = true; m.found = false; m.reattack = false;
    m.frame = juce::jlimit (0, doc->an->frames - 1, (int) ((m.onset + bc::kHop / 2) / bc::kHop));
    m.rise = {};
    ed->keep.push_back (m);
    selOnset = m.onset;
    eventsChanged();
}

void View::moveEnd (int index, double seconds)
{
    auto* ed = edits();
    if (ed == nullptr || index < 0 || index >= (int) doc->events.size()) return;
    beforeEventChange();
    const auto e = doc->events[(size_t) index];
    eraseNear (ed->keep, e.onset, gapSamples());
    auto m = e;
    m.end = juce::jlimit<int64_t> (e.onset + 64, doc->audio->size(), (int64_t) std::llround (seconds * sr()));
    m.userEnd = true; m.manual = true;
    ed->keep.push_back (m);
    eventsChanged();
}

void View::resetEnd (int index)
{
    auto* ed = edits();
    if (ed == nullptr || index < 0 || index >= (int) doc->events.size()) return;
    const auto onset = doc->events[(size_t) index].onset;
    for (auto& k : ed->keep) if (std::llabs (k.onset - onset) < gapSamples() && k.userEnd)
    {
        beforeEventChange();
        for (auto& k2 : ed->keep) if (std::llabs (k2.onset - onset) < gapSamples()) k2.userEnd = false;
        eventsChanged();
        return;
    }
}

void View::toggleGroup (int g)
{
    auto* ed = edits();
    if (ed == nullptr || g < 0 || g >= (int) doc->groups.size()) return;
    beforeEventChange();
    const auto& c = doc->groups[(size_t) g].centroid;
    const auto it = std::find_if (ed->groupsOff.begin(), ed->groupsOff.end(), [&] (const std::array<float, bc::kFeat>& x) { return bc::featDist (x, c) < 0.5f; });
    if (it != ed->groupsOff.end()) ed->groupsOff.erase (it); else ed->groupsOff.push_back (c);
    eventsChanged();
}

void View::findMore (int index)
{
    if (! doc || ! doc->an || index < 0 || index >= (int) doc->events.size()) return;
    auto& st = proc.session.settings;
    const auto& e = doc->events[(size_t) index];
    st.like = bc::makeTemplate (*doc->an, e);
    const double t = (double) (e.onset + doc->offset) / sr();
    const auto name = doc->file >= 0 && doc->file < (int) proc.session.files.size() ? proc.session.files[(size_t) doc->file].info.name : std::string();
    st.likeName = name + " at " + juce::String ((int) (t / 60.0)).toStdString() + ":" + juce::String (std::fmod (t, 60.0), 2).paddedLeft ('0', 5).toStdString();
    proc.session.redetect();
    proc.stateChanged();
    if (refreshPanels) refreshPanels();
}

void View::clearFindMore()
{
    auto& st = proc.session.settings;
    st.like = {}; st.likeName.clear();
    if (st.pick.mode == bc::Pick::Like) st.pick.mode = bc::Pick::All;
    proc.session.redetect();
    proc.stateChanged();
    if (refreshPanels) refreshPanels();
}

void View::eventsChanged()
{
    proc.session.redetect();
    proc.stateChanged();
    if (repaintAll) repaintAll();
    if (refreshPanels) refreshPanels();
}

void View::play()
{
    if (proc.isPlaying()) { proc.stopPlayback(); if (refreshPanels) refreshPanels(); return; }
    if (! doc || ! doc->audio) return;
    const double c = cursor >= t0 && cursor < t1 ? cursor : t0;
    const auto from = (int64_t) (c * sr());
    const auto to = loop ? (int64_t) (t1 * sr()) : doc->audio->size();
    proc.playRange (from, to, loop);
    if (refreshPanels) refreshPanels();
}

// ---------------------------------------------------------------- the content (1280 x 800)

BcEditor::Content::Content (View& v)
    : view (v), header (v), files (v), spec (v), lane (v), ruler (v), overview (v), inspector (v), kitRoom (v), loopsRoom (v), keysRoom (v)
{
    addChildComponent (kitRoom);
    addChildComponent (loopsRoom);
    addChildComponent (keysRoom);
    view.roomChanged = [this] { showRoom(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &header, &files, &spec, &lane, &ruler, &overview, &inspector, &tools, &layerBox,
                                                                          &erase, &down, &up, &toLayer, &undo, &redo, &partPrev, &partNext, &zoomOut, &zoomIn, &zoomFit })
        addAndMakeVisible (c);
    tools.onSelect = [this] (int i) { view.tool = i; spec.repaint(); view.hover = toolHint(); if (view.hoverChanged) view.hoverChanged(); };
    tools.setItemTooltip (0, "Box: drag a rectangle (1). Shift adds to the selection, Alt takes away.");
    tools.setItemTooltip (1, "Brush: paint over what to select (2). Alt + wheel sets the size.");
    tools.setItemTooltip (2, "Lasso: draw around what to select (3).");
    tools.setItemTooltip (3, "Magic wand: click a sound to select all of it that is connected (4). Alt + wheel sets the tolerance.");
    layerBox.setTooltip ("The layer the tools work on");
    layerBox.onChange = [this] { view.target = layerBox.getSelectedId() - 2; spec.repaint(); view.refreshPanels(); };
    erase.onClick = [this] { view.edit (bc::EditOp::Gain, 0.0f, -1); };
    down.onClick = [this] { view.edit (bc::EditOp::Gain, 0.5f, -1); };
    up.onClick = [this] { view.edit (bc::EditOp::Gain, 2.0f, -1); };
    erase.setTooltip ("Remove what is selected (Delete)");
    down.setTooltip ("Make what is selected 6 dB quieter");
    up.setTooltip ("Make what is selected 6 dB louder");
    toLayer.setTooltip ("Move or copy what is selected to a layer of its own");
    toLayer.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Move to a new layer");
        m.addItem (2, "Copy to a new layer");
        if (auto* ed = view.edits())
        {
            juce::PopupMenu to;
            for (int d = 0; d < (int) ed->layers.layers.size(); ++d) if (d != view.target) to.addItem (100 + d, juce::String::fromUTF8 (ed->layers.layers[(size_t) d].name.c_str()));
            m.addSubMenu ("Move to", to);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toLayer), [this] (int r)
        {
            if (r == 1) view.edit (bc::EditOp::Move, 1.0f, -2);
            else if (r == 2) view.edit (bc::EditOp::Copy, 1.0f, -2);
            else if (r >= 100) view.edit (bc::EditOp::Move, 1.0f, r - 100);
        });
    };
    undo.onClick = [this] { view.undo(); };
    redo.onClick = [this] { view.redo(); };
    undo.setTooltip ("Undo (Ctrl+Z)"); redo.setTooltip ("Redo (Ctrl+Shift+Z)");
    partPrev.onClick = [this] { if (view.doc) view.proc.open (view.doc->file, view.doc->part - 1); };
    partNext.onClick = [this] { if (view.doc) view.proc.open (view.doc->file, view.doc->part + 1); };
    partPrev.setTooltip ("The part before (long recordings open in parts of 8 minutes)");
    partNext.setTooltip ("The next part");
    zoomOut.onClick = [this] { view.zoom (1.6, 0.5 * (view.t0 + view.t1)); };
    zoomIn.onClick = [this] { view.zoom (1.0 / 1.6, 0.5 * (view.t0 + view.t1)); };
    zoomFit.onClick = [this] { view.fit(); };
    refreshToolbar();
}

juce::String BcEditor::Content::toolHint() const
{
    switch (view.tool)
    {
        case 1: return "Brush " + juce::String ((int) view.brushPx) + " px: paint to select. Shift adds, Alt takes away.";
        case 2: return "Lasso: draw around a sound. Shift adds, Alt takes away.";
        case 3: return "Wand, tolerance " + juce::String ((int) view.tolDb) + " dB: click a sound. Shift adds, Alt takes away.";
        default: return "Box: drag over a sound. Shift adds, Alt takes away. Right-click for more.";
    }
}

void BcEditor::Content::showRoom()
{
    const bool cut = view.room == 0, kit = view.room == 1, loops = view.room == 2;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &files, &spec, &lane, &ruler, &overview, &inspector, &tools, &layerBox,
                                                                          &erase, &down, &up, &toLayer, &undo, &redo, &partPrev, &partNext, &zoomOut, &zoomIn, &zoomFit })
        c->setVisible (cut);
    if (! cut) view.proc.stopPlayback();
    if (! kit) view.proc.kitPlayer.playing.store (false);
    const bool keys = view.room == 3;
    if (! loops) view.proc.loopPlayer.playing.store (false);
    if (! keys) keysRoom.letGo();
    if (loops) view.proc.midiTarget.store (2);
    if (keys) view.proc.midiTarget.store (3);
    kitRoom.setVisible (kit);
    loopsRoom.setVisible (loops);
    keysRoom.setVisible (keys);
    if (kit) kitRoom.refresh();
    if (loops) loopsRoom.refresh();
    if (keys) keysRoom.refresh();
    header.refresh(); header.resized(); header.repaint();
    repaint();
}

void BcEditor::Content::refreshToolbar()
{
    auto* ed = view.edits();
    layerBox.clear (juce::dontSendNotification);
    layerBox.addItem ("All layers", 1);
    if (ed != nullptr) for (int d = 0; d < (int) ed->layers.layers.size(); ++d) layerBox.addItem (juce::String::fromUTF8 (ed->layers.layers[(size_t) d].name.c_str()), d + 2);
    if (ed == nullptr || view.target >= (int) ed->layers.layers.size()) view.target = -1;
    layerBox.setSelectedId (view.target + 2, juce::dontSendNotification);
    const bool can = view.hasSel && view.doc != nullptr;
    for (auto* b : { &erase, &down, &up, &toLayer }) b->setEnabled (can);
    undo.setEnabled (view.canUndo()); redo.setEnabled (view.canRedo());
    const int parts = view.doc ? view.doc->parts : 1;
    partPrev.setEnabled (view.doc && view.doc->part > 0);
    partNext.setEnabled (view.doc && view.doc->part + 1 < parts);
    tools.setSelected (view.tool);
    repaint (partLabel); repaint (hoverLabel);
}

void BcEditor::Content::paint (juce::Graphics& g)
{
    paintChassis (g, getLocalBounds().toFloat());
    if (view.room != 0) return;
    paintPlate (g, { 222.0f, 68.0f, 736.0f, 724.0f }, 12.0f);
    // the part, and the readout under the pointer
    juce::String part = "Nothing open";
    if (view.doc && view.doc->audio)
    {
        const double s0 = (double) view.doc->offset / view.sr(), s1 = s0 + view.length();
        auto mmss = [] (double t) { return juce::String ((int) (t / 60.0)) + ":" + juce::String ((int) std::fmod (t, 60.0)).paddedLeft ('0', 2); };
        part = view.doc->parts > 1 ? "Part " + juce::String (view.doc->part + 1) + " of " + juce::String (view.doc->parts) + "  " + mmss (s0) + "-" + mmss (s1)
                                    : "Whole file  " + mmss (s1);
    }
    paintLabel (g, part, partLabel, juce::Justification::centred, 8.0f);
    g.setFont (font (11.0f)); g.setColour (C::dim());
    g.drawText (view.hover.isNotEmpty() ? view.hover : toolHint(), hoverLabel, juce::Justification::centredLeft, true);
}

void BcEditor::Content::resized()
{
    header.setBounds (0, 0, 1280, 64);
    files.setBounds (12, 72, 204, 716);
    tools.setBounds (230, 74, 236, 30);
    layerBox.setBounds (472, 76, 122, 26);
    erase.setBounds (600, 74, 56, 30);
    down.setBounds (658, 74, 52, 30);
    up.setBounds (712, 74, 52, 30);
    toLayer.setBounds (766, 74, 70, 30);
    undo.setBounds (840, 74, 52, 30);
    redo.setBounds (894, 74, 52, 30);
    lane.setBounds (230, 110, 720, 28);
    spec.setBounds (230, 140, 720, 514);
    ruler.setBounds (230, 654, 720, 20);
    overview.setBounds (230, 678, 720, 50);
    partPrev.setBounds (230, 736, 28, 28);
    partLabel = { 260, 736, 170, 28 };
    partNext.setBounds (432, 736, 28, 28);
    hoverLabel = { 470, 736, 350, 28 };
    zoomOut.setBounds (826, 736, 30, 28);
    zoomIn.setBounds (858, 736, 30, 28);
    zoomFit.setBounds (890, 736, 60, 28);
    inspector.setBounds (964, 72, 304, 716);
    kitRoom.setBounds (0, 64, 1280, 736);
    loopsRoom.setBounds (0, 64, 1280, 736);
    keysRoom.setBounds (0, 64, 1280, 736);
}

// ---------------------------------------------------------------- the editor

BcEditor::BcEditor (BcProcessor& p) : juce::AudioProcessorEditor (p), proc (p), view (p), content (view)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    view.repaintAll = [this] { content.spec.repaint(); content.lane.repaint(); content.ruler.repaint(); content.overview.repaint(); content.refreshToolbar(); };
    view.refreshPanels = [this] { content.inspector.refresh(); content.header.refresh(); content.files.refresh(); content.refreshToolbar(); if (view.room == 1) content.kitRoom.refresh(); if (view.room == 2) content.loopsRoom.refresh(); if (view.room == 3) content.keysRoom.refresh(); };
    view.hoverChanged = [this] { content.repaint (content.hoverLabel); };
    setResizable (true, true);
    setResizeLimits (800, 500, 2560, 1600);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1280.0 / 800.0);
    setWantsKeyboardFocus (true);
    proc.addChangeListener (this);
    setSize (1152, 720);
    docChanged();
    startTimerHz (30);
}

BcEditor::~BcEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void BcEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffc8d1cc));
}

void BcEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dropping) return;
    g.setColour (C::glow().withAlpha (0.18f)); g.fillRect (getLocalBounds());
    g.setColour (C::teal()); g.drawRect (getLocalBounds(), 3);
    g.setFont (caps (18.0f * (float) getWidth() / 1280.0f)); g.setColour (C::text());
    g.drawText ("DROP AUDIO FILES OR FOLDERS", getLocalBounds(), juce::Justification::centred);
}

void BcEditor::resized()
{
    content.setBounds (0, 0, 1280, 800);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / 1280.0f, (float) getHeight() / 800.0f));
}

void BcEditor::changeListenerCallback (juce::ChangeBroadcaster*) { docChanged(); }

void BcEditor::docChanged()
{
    auto d = proc.session.doc();
    if (d != view.doc)
    {
        const bool newPart = ! d || ! view.doc || d->file != view.doc->file || d->part != view.doc->part || d->audio != view.doc->audio;
        view.doc = d;
        if (newPart)
        {
            view.hasSel = false; view.sel = {}; ++view.selVersion; view.selOnset = -1;
            if (d && proc.viewEnd > proc.viewStart) view.setView (proc.viewStart, proc.viewEnd); else view.fit();
            view.cursor = view.t0;
        }
    }
    view.refreshPanels();
    view.repaintAll();
}

void BcEditor::timerCallback()
{
    const int64_t ph = proc.playhead();
    if (ph != lastPlayhead)
    {
        lastPlayhead = ph;
        // follow the playhead when it runs off the screen
        if (ph >= 0 && ! proc.playingEvent && view.doc)
        {
            const double t = (double) ph / view.sr();
            if (t > view.t1 || t < view.t0) { const double w = view.t1 - view.t0; view.setView (t - 0.05 * w, t + 0.95 * w); }
        }
        content.spec.repaint(); content.lane.repaint(); content.overview.repaint();
    }
    if (++ticks % 3 == 0) content.header.refresh();
    const bool busy = proc.session.status().busy;
    if (busy != wasBusy) { wasBusy = busy; content.inspector.refresh(); }
}

bool BcEditor::keyStateChanged (bool isKeyDown) { return view.room == 3 && content.keysRoom.keyState (isKeyDown); }
void BcEditor::focusLost (FocusChangeType) { if (view.room == 3) content.keysRoom.letGo(); }

bool BcEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = key.getKeyCode();
    const auto ch = key.getTextCharacter();
    const auto letter = c < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c) : 0;   // JUCE gives letters in either case
    const auto mods = key.getModifiers();
    if (view.room == 1) return content.kitRoom.key (key);
    if (view.room == 2) return content.loopsRoom.key (key);
    if (view.room == 3) return content.keysRoom.key (key);
    if (key == juce::KeyPress::spaceKey) { view.play(); return true; }
    if (c == juce::KeyPress::escapeKey) { view.clearSelection(); return true; }
    if (mods.isCommandDown() && letter == 'z') { if (mods.isShiftDown()) view.redo(); else view.undo(); return true; }
    if (mods.isCommandDown() && letter == 'y') { view.redo(); return true; }
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey)
    {
        if (view.hasSel) view.edit (bc::EditOp::Gain, 0.0f, -1);
        else if (view.selIndex() >= 0) view.deleteEvent (view.selIndex());
        return true;
    }
    if (c == juce::KeyPress::leftKey) { view.step (-1); return true; }
    if (c == juce::KeyPress::rightKey) { view.step (1); return true; }
    if (c == juce::KeyPress::returnKey) { if (auto* e = view.selEvent()) proc.playEvent (*e); return true; }
    if (c == juce::KeyPress::homeKey) { view.fit(); return true; }
    if (mods.isCommandDown()) return false;
    if (ch >= '1' && ch <= '4') { view.tool = ch - '1'; content.tools.setSelected (view.tool); content.tools.onSelect (view.tool); return true; }
    if (ch == '[' || ch == ']')
    {
        const bool up = ch == ']';
        if (view.tool == 3) view.tolDb = juce::jlimit (1.0f, 40.0f, view.tolDb + (up ? 1.0f : -1.0f));
        else view.brushPx = juce::jlimit (2.0f, 80.0f, view.brushPx * (up ? 1.2f : 1.0f / 1.2f));
        view.hover = content.toolHint(); view.hoverChanged();
        return true;
    }
    if (ch == '+' || ch == '=') { view.zoom (1.0 / 1.6, 0.5 * (view.t0 + view.t1)); return true; }
    if (ch == '-') { view.zoom (1.6, 0.5 * (view.t0 + view.t1)); return true; }
    if (ch == 'e' || ch == 'E') { view.toggleEvent (view.selIndex()); return true; }
    if (ch == 'f' || ch == 'F') { view.findMore (view.selIndex()); return true; }
    if (ch == 'l' || ch == 'L') { view.loop = ! view.loop; content.header.refresh(); return true; }
    return false;
}

bool BcEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) { const juce::File file (f); if (file.isDirectory() || BcProcessor::isAudioFile (file)) return true; }
    return false;
}

void BcEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false; repaint();
    if (view.room >= 1)
    {
        // in the kit room and LOOPS, what is dropped goes to the pool: folders whole, files with their folder
        int added = 0; juce::StringArray seen;
        for (const auto& f : files)
        {
            const juce::File file (f);
            const juce::File dir = file.isDirectory() ? file : file.getParentDirectory();
            if (seen.contains (dir.getFullPathName())) continue;
            seen.add (dir.getFullPathName());
            added += proc.kitAddFolder (dir);
        }
        view.refreshPanels();
        return;
    }
    const int n = proc.addPaths (files);
    proc.lastMessage = n > 0 ? "Added " + juce::String (n) + (n == 1 ? " file" : " files") : "Nothing new to add (no audio files, or already in the list)";
    view.refreshPanels();
}
