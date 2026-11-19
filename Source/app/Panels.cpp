// BatchCut - the panels: the file list, the inspector's four tabs, the header.
#include "Editor.h"

using namespace bcui;

namespace
{
juce::String mmss (double t)
{
    if (! std::isfinite (t) || t < 0) return "-";
    const int h = (int) (t / 3600.0), m = (int) std::fmod (t / 60.0, 60.0), s = (int) std::fmod (t, 60.0);
    return (h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m)) + ":" + juce::String (s).paddedLeft ('0', 2);
}

void lcdText (juce::Graphics& g, juce::Rectangle<int> r, const juce::StringArray& lines, float size = 11.5f)
{
    paintLcd (g, r.toFloat(), 6.0f);
    auto t = r.reduced (8, 5);
    const int lh = (int) (size + 4.0f);
    for (int i = 0; i < lines.size() && t.getHeight() >= lh - 2; ++i)
    {
        g.setFont (font (size, i == 0)); g.setColour (i == 0 ? C::lcdText() : C::lcdDim());
        g.drawFittedText (lines[i], t.removeFromTop (lh), juce::Justification::centredLeft, 1, 0.85f);
    }
}

juce::String plural (int n, const char* word) { return juce::String (n) + " " + word + (n == 1 ? "" : "s"); }

const char* kAudioWildcard = "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.ogg;*.mp3";
}

// ---------------------------------------------------------------- the file list

FileList::FileList (View& v) : view (v)
{
    list.setModel (this);
    list.setRowHeight (38);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setMultipleSelectionEnabled (true);
    addAndMakeVisible (list);
    for (auto* b : { &addFiles, &addFolder, &remove, &clearAll }) addAndMakeVisible (b);
    addFiles.setTooltip ("Add audio files (or drop them anywhere on Batchcut)");
    addFolder.setTooltip ("Add every audio file in a folder and the folders inside it");
    remove.setTooltip ("Take the selected files off the list (Shift or Ctrl-click selects several). The files themselves are not touched.");
    clearAll.setTooltip ("Take every file off the list. The files themselves are not touched, and the kit pool keeps its sounds.");
    addFiles.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add audio files", juce::File::getSpecialLocation (juce::File::userMusicDirectory), kAudioWildcard);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  juce::StringArray paths; for (const auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  if (! paths.isEmpty()) view.proc.addPaths (paths);
                                  view.refreshPanels();
                              });
    };
    addFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of recordings", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File()) { const int n = view.proc.addPaths ({ fc.getResult().getFullPathName() }); view.proc.lastMessage = "Added " + juce::String (n) + " files"; }
                                  view.refreshPanels();
                              });
    };
    remove.onClick = [this] { removeSelected(); };
    clearAll.onClick = [this] { confirmClearAll(); };
}

void FileList::removeSelected()
{
    std::vector<int> rows;
    const auto sel = list.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i) rows.push_back (sel[i]);
    list.deselectAllRows();
    view.proc.removeFiles (rows);
    view.refreshPanels();
}

void FileList::confirmClearAll()
{
    const int n = getNumRows();
    if (n == 0) return;
    auto go = [safe = juce::Component::SafePointer<FileList> (this)] (bool yes)
    {
        if (safe == nullptr || ! yes) return;
        safe->list.deselectAllRows();
        safe->view.proc.clearFiles();
        safe->view.refreshPanels();
    };
    if (ask) { ask (go); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Clear all")
                                      .withMessage ("Take all " + juce::String (n) + (n == 1 ? " file" : " files") + " off the list?\n\nTheir cuts and edits go with them. The files themselves stay where they are, and the kit pool keeps its sounds.")
                                      .withButton ("Clear all").withButton ("Cancel").withAssociatedComponent (this),
                                  [go] (int r) { go (r == 1); });
}

void FileList::paint (juce::Graphics& g)
{
    paintPlate (g, getLocalBounds().toFloat(), 12.0f);
    paintLabel (g, "Files (" + juce::String ((int) view.proc.session.files.size()) + ")", { 14, 8, getWidth() - 28, 14 }, juce::Justification::centredLeft, 8.5f);
    paintWell (g, list.getBounds().toFloat().expanded (3.0f), 8.0f);
    if (view.proc.session.files.empty())
    {
        g.setFont (font (11.5f)); g.setColour (C::dim());
        g.drawFittedText ("No files yet.\n\nDrop recordings or folders onto Batchcut, or press ADD FILES.", list.getBounds().reduced (10), juce::Justification::centred, 6);
    }
}

void FileList::resized()
{
    list.setBounds (12, 30, getWidth() - 24, getHeight() - 30 - 82);
    const int y = getHeight() - 76, half = (getWidth() - 24) / 2;
    addFiles.setBounds (10, y, half, 30);
    addFolder.setBounds (14 + half, y, half, 30);
    remove.setBounds (10, y + 36, half, 28);
    clearAll.setBounds (14 + half, y + 36, half, 28);
}

void FileList::refresh()
{
    list.updateContent();
    const int open = view.doc ? view.doc->file : view.proc.openFile;
    // several files picked for Remove stay picked; otherwise the open file is the one selected
    bool keep = list.getNumSelectedRows() > 1;
    for (int i = 0; keep && i < list.getNumSelectedRows(); ++i) keep = list.getSelectedRow (i) < getNumRows();
    if (! keep) { if (open >= 0 && open < getNumRows()) list.selectRow (open, true, true); else list.deselectAllRows(); }
    remove.setEnabled (list.getNumSelectedRows() > 0 && getNumRows() > 0);
    clearAll.setEnabled (getNumRows() > 0);
    list.repaint();
    repaint();
}

int FileList::getNumRows() { return (int) view.proc.session.files.size(); }

void FileList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto& f = view.proc.session.files[(size_t) row];
    const bool open = view.doc && view.doc->file == row;
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2, 1).toFloat();
    if (selected || open) { g.setColour (C::glow().withAlpha (open ? 0.45f : 0.25f)); g.fillRoundedRectangle (r, 5.0f); }
    g.setColour (juce::Colours::black.withAlpha (0.08f)); g.drawHorizontalLine (h - 1, 6.0f, (float) w - 6.0f);
    g.setFont (font (12.0f, true)); g.setColour (C::text());
    g.drawFittedText (juce::String::fromUTF8 (f.info.name.c_str()), 8, 3, w - 14, 16, juce::Justification::centredLeft, 1, 0.8f);
    juce::String info;
    if (! f.error.empty()) info = juce::String::fromUTF8 (f.error.c_str());
    else if (f.info.sr <= 0) info = "reading...";
    else
    {
        const int parts = bc::partCount (f.info);
        int events = 0; bool known = false; for (int c : f.counts) if (c >= 0) { events += c; known = true; }
        info << juce::String (f.info.sr / 1000.0, f.info.sr == 44100.0 ? 1 : 0) << " kHz  " << (f.info.channels > 1 ? "stereo" : "mono") << "  " << mmss ((double) f.info.length / f.info.sr);
        if (parts > 1) info << "  " << parts << " parts";
        if (known) info << "  " << events << (events == 1 ? " event" : " events");
    }
    g.setFont (font (10.5f)); g.setColour (f.error.empty() ? C::dim() : C::red());
    g.drawFittedText (info, 8, 19, w - 14, 15, juce::Justification::centredLeft, 1, 0.8f);
}

void FileList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= getNumRows()) return;
    remove.setEnabled (true);
    if (e.mods.isPopupMenu())
    {
        const int picked = list.isRowSelected (row) ? list.getNumSelectedRows() : 1;
        juce::PopupMenu m; m.addItem (1, "Open"); m.addItem (2, "Show in folder");
        m.addItem (3, picked > 1 ? "Take the " + juce::String (picked) + " selected files off the list" : juce::String ("Take off the list"));
        m.addSeparator(); m.addItem (4, "Clear all (every file off the list)...");
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, row, picked] (int r)
        {
            if (row >= getNumRows()) return;
            if (r == 1) view.proc.open (row, 0);
            else if (r == 2) juce::File (juce::String::fromUTF8 (view.proc.session.files[(size_t) row].info.path.c_str())).revealToUser();
            else if (r == 3) { if (picked > 1) removeSelected(); else { view.proc.removeFiles ({ row }); view.refreshPanels(); } }
            else if (r == 4) confirmClearAll();
        });
        return;
    }
    // Shift and Ctrl (Cmd) pick several files for Remove; a plain click opens the file
    if (e.mods.isShiftDown() || e.mods.isCommandDown()) { remove.setEnabled (list.getNumSelectedRows() > 0); return; }
    if (! view.doc || view.doc->file != row) view.proc.open (row, 0);
    view.refreshPanels();
}

void FileList::deleteKeyPressed (int row)
{
    if (list.getNumSelectedRows() > 1) { removeSelected(); return; }
    if (row < 0 || row >= getNumRows()) return;
    view.proc.removeFiles ({ row });
    view.refreshPanels();
}

juce::String FileList::getTooltipForRow (int row)
{
    return row >= 0 && row < getNumRows() ? juce::String::fromUTF8 (view.proc.session.files[(size_t) row].info.path.c_str()) : juce::String();
}

// ---------------------------------------------------------------- DETECT

DetectPanel::DetectPanel (View& v) : Panel (v)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &sens, &gap, &grouping, &preRoll, &tail, &maxLen, &endMode, &cutAtNext, &field, &dense, &loops })
        addAndMakeVisible (c);
    sens.main = true;
    sens.toText = [] (float x) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; };
    gap.toText = [] (float x) { return juce::String (juce::roundToInt (x)) + " ms"; };
    grouping.toText = [] (float x) { return x < 0.34f ? juce::String ("broad") : x < 0.67f ? juce::String ("medium") : juce::String ("fine"); };
    preRoll.toText = [] (float x) { return juce::String (x, 1) + " ms"; };
    tail.toText = [] (float x) { return juce::String (juce::roundToInt (x)) + " ms"; };
    maxLen.toText = [] (float x) { return juce::String (x, x < 1.0f ? 2 : 1) + " s"; };
    sens.setTooltip ("How quiet an event may be and still be found. Lower finds only the obvious ones.");
    gap.setTooltip ("Two starts closer than this are one event. Lower it (under 45 ms) to split fast bounces and flams.");
    grouping.setTooltip ("How finely events are sorted into groups of alike sounds.");
    preRoll.setTooltip ("Kept before each start, so the attack is never cut.");
    tail.setTooltip ("Kept after an event has sunk into the background.");
    maxLen.setTooltip ("No one-shot is longer than this.");
    endMode.setItemTooltip (0, "Ends where the sound sinks into the background");
    endMode.setItemTooltip (1, "Runs on to the next event (full length)");
    endMode.setItemTooltip (2, "Ends at the quietest moment just before the next event (clean cuts in loops)");
    endMode.setItemTooltip (3, "Ends where it has fallen 40 dB below its peak");
    cutAtNext.setTooltip ("An event never runs into the next one");
    field.setTooltip ("Field recordings: sparse events in a noisy background");
    dense.setTooltip ("Busy recordings: many events close together");
    loops.setTooltip ("Drum loops and rhythmic material: cuts at the next hit");
    auto& d = settings().detect;
    sens.onChange = [this, &d] (float x) { d.sensitivity = x; changed(); };
    gap.onChange = [this, &d] (float x) { d.minGapMs = x; changed(); };
    grouping.onChange = [this, &d] (float x) { d.grouping = x; changed(); };
    preRoll.onChange = [this, &d] (float x) { d.preRollMs = x; changed(); };
    tail.onChange = [this, &d] (float x) { d.tailMs = x; changed(); };
    maxLen.onChange = [this, &d] (float x) { d.maxLenS = x; changed(); };
    endMode.onSelect = [this, &d] (int i) { d.endMode = i; changed(); repaint(); };
    cutAtNext.onClick = [this, &d] { d.cutAtNext = cutAtNext.getToggleState(); changed(); };
    field.onClick = [this] { preset (0); };
    dense.onClick = [this] { preset (1); };
    loops.onClick = [this] { preset (2); };
    refresh();
}

void DetectPanel::preset (int which)
{
    auto& d = settings().detect;
    const bc::DetectParams def;
    d = def;
    if (which == 0) { d.sensitivity = 0.5f; d.minGapMs = 80.0f; d.preRollMs = 5.0f; d.tailMs = 40.0f; d.maxLenS = 4.0f; d.endMode = 0; d.grouping = 0.5f; }
    if (which == 1) { d.sensitivity = 0.6f; d.minGapMs = 30.0f; d.preRollMs = 2.0f; d.tailMs = 10.0f; d.maxLenS = 2.0f; d.endMode = 2; d.grouping = 0.5f; }
    if (which == 2) { d.sensitivity = 0.55f; d.minGapMs = 40.0f; d.preRollMs = 1.0f; d.tailMs = 5.0f; d.maxLenS = 1.5f; d.endMode = 1; d.grouping = 0.6f; }
    refresh();
    changed();
}

void DetectPanel::timerCallback()
{
    stopTimer();
    view.proc.session.redetect();
    view.proc.stateChanged();
}

void DetectPanel::refresh()
{
    const auto& d = settings().detect;
    sens.setValue (d.sensitivity); gap.setValue (d.minGapMs); grouping.setValue (d.grouping);
    preRoll.setValue (d.preRollMs); tail.setValue (d.tailMs); maxLen.setValue (d.maxLenS);
    endMode.setSelected (juce::jlimit (0, 3, d.endMode));
    cutAtNext.setToggleState (d.cutAtNext, juce::dontSendNotification);
    cutAtNext.setEnabled (d.endMode == 0 || d.endMode == 3);
    repaint();
}

void DetectPanel::paint (juce::Graphics& g)
{
    caption (g, "Presets", 6);
    caption (g, "Finding events", 66);
    caption (g, "Where an event ends", 268);
    juce::StringArray lines;
    if (view.doc)
    {
        int on = 0; for (const auto& e : view.doc->events) on += e.enabled ? 1 : 0;
        lines.add (plural ((int) view.doc->events.size(), "event") + " in " + plural ((int) view.doc->groups.size(), "group"));
        lines.add (view.doc->parts > 1 ? "in part " + juce::String (view.doc->part + 1) + " of " + juce::String (view.doc->parts) + " of this file" : "in this file");
        if (on < (int) view.doc->events.size()) lines.add (juce::String ((int) view.doc->events.size() - on) + " switched off");
    }
    else lines.add ("Open a file to see its events");
    if (view.proc.session.status().busy) lines.set (0, juce::String (view.proc.session.status().what));
    lcdText (g, { 14, 360, getWidth() - 28, 64 }, lines);
    g.setFont (font (11.0f)); g.setColour (C::dim());
    g.drawFittedText ("Detection looks at each frequency band against its own background, so a quiet hit in "
                      "a noisy recording is found where its sound stands out. Every file uses these settings.",
                      { 14, 436, getWidth() - 28, 80 }, juce::Justification::topLeft, 6);
}

void DetectPanel::resized()
{
    const int w = (getWidth() - 28) / 3;
    field.setBounds (14, 22, w - 4, 30); dense.setBounds (14 + w, 22, w - 4, 30); loops.setBounds (14 + 2 * w, 22, w - 4, 30);
    sens.setBounds (14, 84, w, 86); gap.setBounds (14 + w, 84, w, 86); grouping.setBounds (14 + 2 * w, 84, w, 86);
    preRoll.setBounds (14, 176, w, 86); tail.setBounds (14 + w, 176, w, 86); maxLen.setBounds (14 + 2 * w, 176, w, 86);
    endMode.setBounds (14, 286, getWidth() - 28, 30);
    cutAtNext.setBounds (14, 324, getWidth() - 28, 24);
}

// ---------------------------------------------------------------- EVENTS

EventsPanel::EventsPanel (View& v) : Panel (v)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &play, &onOff, &del, &prev, &next, &findMore, &clearMore, &similarity })
        addAndMakeVisible (c);
    similarity.toText = [] (float x) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; };
    similarity.setTooltip ("How alike a sound must be to count as more of the same (for FIND MORE and for exporting LIKE)");
    play.onClick = [this] { if (auto* e = view.selEvent()) view.proc.playEvent (*e); };
    onOff.onClick = [this] { view.toggleEvent (view.selIndex()); };
    del.onClick = [this] { view.deleteEvent (view.selIndex()); };
    prev.onClick = [this] { view.step (-1); };
    next.onClick = [this] { view.step (1); };
    play.setTooltip ("Hear it as it will be exported (Enter)");
    onOff.setTooltip ("Export it or not (E)");
    del.setTooltip ("Delete it (Delete). Undo brings it back.");
    prev.setTooltip ("The event before (left arrow)"); next.setTooltip ("The next event (right arrow)");
    findMore.setTooltip ("Search every file for more sounds like the selected one, quiet ones included (F)");
    clearMore.setTooltip ("Stop searching for more like it");
    findMore.onClick = [this] { view.findMore (view.selIndex()); };
    clearMore.onClick = [this] { view.clearFindMore(); };
    similarity.onChange = [this] (float x) { settings().pick.similarity = x; repaint(); };
    similarity.onCommit = [this] { if (settings().like.valid) view.proc.session.redetect(); view.proc.stateChanged(); };
}

int EventsPanel::groupsShown() const { return view.doc ? std::min (10, (int) view.doc->groups.size()) : 0; }

void EventsPanel::refresh()
{
    const bool has = view.selEvent() != nullptr;
    for (auto* b : { &play, &onOff, &del, &findMore }) b->setEnabled (has);
    prev.setEnabled (view.doc && ! view.doc->events.empty()); next.setEnabled (prev.isEnabled());
    clearMore.setEnabled (settings().like.valid);
    similarity.setValue (settings().pick.similarity);
    repaint();
}

void EventsPanel::paint (juce::Graphics& g)
{
    const int ng = view.doc ? (int) view.doc->groups.size() : 0;
    caption (g, "Groups of alike sounds" + juce::String (ng > 0 ? " (" + juce::String (ng) + ")" : ""), 6);
    g.setFont (font (10.0f)); g.setColour (C::dim());
    g.drawText ("click to export a group or not", 14, 22, getWidth() - 28, 14, juce::Justification::centredLeft);
    if (view.doc)
    {
        const auto& d = *view.doc;
        for (int i = 0; i < groupsShown(); ++i)
        {
            const auto& gr = d.groups[(size_t) i];
            const auto r = groupRow (i);
            paintGem (g, juce::Rectangle<float> ((float) r.getX() + 2.0f, (float) r.getCentreY() - 6.0f, 12.0f, 12.0f), gr.enabled, C::group (gr.colour));
            g.setFont (font (12.0f, true)); g.setColour (gr.enabled ? C::text() : C::dim());
            g.drawText (juce::String::fromUTF8 (gr.name.c_str()), r.withTrimmedLeft (22), juce::Justification::centredLeft, true);
            g.setFont (font (11.0f)); g.setColour (C::dim());
            g.drawText (juce::String (gr.count) + (gr.count == 1 ? " event" : " events"), r, juce::Justification::centredRight, true);
        }
        if (ng > groupsShown()) { g.setFont (font (10.5f)); g.drawText ("and " + juce::String (ng - groupsShown()) + " more groups", groupRow (groupsShown()), juce::Justification::centredLeft); }
    }
    caption (g, "The selected event", 270);
    juce::StringArray lines;
    if (auto* e = view.selEvent())
    {
        const auto& d = *view.doc;
        const double sr = view.sr();
        const double t = (double) (e->onset + d.offset) / sr;
        lines.add ((e->group >= 0 && e->group < (int) d.groups.size() ? juce::String::fromUTF8 (d.groups[(size_t) e->group].name.c_str()) : juce::String ("event"))
                   + "  at " + mmss (t) + juce::String (std::fmod (t, 1.0), 3).substring (1));
        lines.add (juce::String ((double) (e->end - e->start) / sr, 3) + " s long,  " + juce::String (juce::roundToInt (e->snrDb)) + " dB over the background"
                   + (e->enabled ? "" : ",  not exported"));
        juce::String how = e->manual ? "placed or changed by hand" : e->found ? "found by FIND MORE" : e->reattack ? "a strike inside the one before" : "detected";
        lines.add (how);
    }
    else lines.add ("Click an event to hear and select it");
    lcdText (g, { 14, 286, getWidth() - 28, 62 }, lines, 11.0f);
    caption (g, "Find more like it", 396);
    juce::StringArray more;
    if (settings().like.valid) { more.add ("Searching every file for:"); more.add (juce::String::fromUTF8 (settings().likeName.c_str())); }
    else { more.add ("Not searching"); more.add ("Select an event, then FIND MORE"); }
    lcdText (g, { 14, 412, getWidth() - 28 - 96, 46 }, more, 11.0f);
    caption (g, "Drag out", dragPad().getY() - 18);
    const auto pad = dragPad();
    paintWell (g, pad.toFloat(), 10.0f);
    g.setFont (font (12.5f, true)); g.setColour (view.selEvent() ? C::text() : C::dim());
    g.drawFittedText (view.selEvent() ? "Drag the selected one-shot from here into your DAW or a folder" : "Select an event to drag it out", pad.reduced (12), juce::Justification::centred, 3);
}

void EventsPanel::resized()
{
    const int w = getWidth() - 28;
    prev.setBounds (14, 354, 30, 30);
    play.setBounds (48, 354, (w - 68) / 3, 30);
    onOff.setBounds (50 + (w - 68) / 3, 354, (w - 68) / 3, 30);
    del.setBounds (52 + 2 * (w - 68) / 3, 354, (w - 68) / 3, 30);
    next.setBounds (getWidth() - 14 - 30, 354, 30, 30);
    similarity.setBounds (getWidth() - 14 - 90, 392, 90, 86);
    findMore.setBounds (14, 464, w - 96 - 70, 30);
    clearMore.setBounds (14 + w - 96 - 66, 464, 62, 30);
}

void EventsPanel::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (! view.doc) return;
    for (int i = 0; i < groupsShown(); ++i) if (groupRow (i).contains (e.getPosition())) { view.toggleGroup (i); return; }
}

void EventsPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || ! dragPad().contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6) return;
    auto* ev = view.selEvent();
    if (ev == nullptr) return;
    dragStarted = true;
    const auto f = view.proc.renderToTemp (*ev);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

// ---------------------------------------------------------------- LAYERS

LayersPanel::LayersPanel (View& v) : Panel (v)
{
    for (auto* b : { &addLayer, &delLayer, &undo, &redo, &reset }) addAndMakeVisible (b);
    addLayer.setTooltip ("A new, empty layer: move sound into it with TO LAYER");
    delLayer.setTooltip ("Delete the selected layer of your own (its sound goes back where it came from)");
    reset.setTooltip ("Undo every edit on this part and remove your layers");
    addLayer.onClick = [this]
    {
        if (auto* ed = view.edits()) { if ((int) ed->layers.layers.size() < bc::kMaxLayers) { view.target = ed->layers.addLayer(); view.layersChanged(); } }
    };
    delLayer.onClick = [this]
    {
        auto* ed = view.edits();
        const int d = view.target;
        if (ed == nullptr || d < bc::kAuto || d >= (int) ed->layers.layers.size()) return;
        auto& L = ed->layers;
        L.ops.erase (std::remove_if (L.ops.begin(), L.ops.end(), [d] (const bc::EditOp& o) { return o.target == d || o.layer == d; }), L.ops.end());
        for (auto& o : L.ops) { if (o.target > d) --o.target; if (o.layer > d) --o.layer; }
        L.undone.clear();
        L.layers.erase (L.layers.begin() + d);
        view.target = -1;
        view.layersChanged();
    };
    undo.onClick = [this] { view.undo(); };
    redo.onClick = [this] { view.redo(); };
    reset.onClick = [this]
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Reset the layers")
                                          .withMessage ("Undo every edit on this part and remove your own layers?").withButton ("Reset").withButton ("Cancel").withAssociatedComponent (this),
                                      [this] (int r)
                                      {
                                          if (r != 1) return;
                                          if (auto* ed = view.edits()) { ed->layers = bc::LayerState(); view.target = -1; view.layersChanged(); }
                                      });
    };
}

void LayersPanel::refresh()
{
    auto* ed = view.edits();
    const int n = ed ? (int) ed->layers.layers.size() : 0;
    if (n != shownLayers)
    {
        rows.clear();
        for (int d = 0; d < n; ++d)
        {
            Row r;
            r.solo = std::make_unique<Pill> ("S", true); r.mute = std::make_unique<Pill> ("M", true);
            r.solo->setColour (juce::TextButton::buttonOnColourId, C::amber());
            r.mute->setColour (juce::TextButton::buttonOnColourId, C::red());
            r.solo->setTooltip ("Hear this layer alone (with any other soloed ones)");
            r.mute->setTooltip ("Silence this layer (it is still shown, faintly)");
            r.gain = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            r.gain->setRange (-60.0, 12.0, 0.1); r.gain->setDoubleClickReturnValue (true, 0.0); r.gain->setSkewFactorFromMidPoint (-12.0);
            r.gain->setColour (juce::Slider::trackColourId, C::layer (d)); r.gain->setColour (juce::Slider::thumbColourId, juce::Colour (0xfff2f6f4));
            r.gain->setColour (juce::Slider::backgroundColourId, juce::Colour (0x40000000));
            r.gain->setTooltip ("The layer's level (double-click: 0 dB)");
            r.solo->onClick = [this, d] { if (auto* e2 = view.edits()) if (d < (int) e2->layers.layers.size()) { e2->layers.layers[(size_t) d].solo = rows[(size_t) d].solo->getToggleState(); view.layersChanged(); } };
            r.mute->onClick = [this, d] { if (auto* e2 = view.edits()) if (d < (int) e2->layers.layers.size()) { e2->layers.layers[(size_t) d].mute = rows[(size_t) d].mute->getToggleState(); view.layersChanged(); } };
            r.gain->onDragEnd = [this] { view.proc.stateChanged(); };
            r.gain->onValueChange = [this, d] { if (auto* e2 = view.edits()) if (d < (int) e2->layers.layers.size()) { e2->layers.layers[(size_t) d].gainDb = (float) rows[(size_t) d].gain->getValue(); view.layersChanged(); } };
            addAndMakeVisible (*r.solo); addAndMakeVisible (*r.mute); addAndMakeVisible (*r.gain);
            rows.push_back (std::move (r));
        }
        shownLayers = n;
        resized();
    }
    for (int d = 0; d < n; ++d)
    {
        const auto& l = ed->layers.layers[(size_t) d];
        rows[(size_t) d].solo->setToggleState (l.solo, juce::dontSendNotification);
        rows[(size_t) d].mute->setToggleState (l.mute, juce::dontSendNotification);
        rows[(size_t) d].gain->setValue (l.gainDb, juce::dontSendNotification);
    }
    addLayer.setEnabled (ed != nullptr && n < bc::kMaxLayers);
    delLayer.setEnabled (ed != nullptr && view.target >= bc::kAuto);
    bool touched = false;
    if (ed != nullptr) { touched = ! ed->layers.ops.empty() || (int) ed->layers.layers.size() > bc::kAuto; for (const auto& l : ed->layers.layers) touched = touched || l.mute || l.solo || l.gainDb != 0.0f; }
    reset.setEnabled (touched);
    undo.setEnabled (view.canUndo()); redo.setEnabled (view.canRedo());
    repaint();
}

void LayersPanel::paint (juce::Graphics& g)
{
    caption (g, "Layers", 6);
    g.setFont (font (10.0f)); g.setColour (C::dim());
    g.drawText ("click one: the tools then edit only it", 14, 22, getWidth() - 28, 14, juce::Justification::centredLeft);
    auto* ed = view.edits();
    const int n = ed ? (int) ed->layers.layers.size() : 0;
    for (int i = 0; i <= n; ++i)
    {
        const auto r = rowArea (i);
        const int d = i - 1;
        const bool sel = view.target == d;
        if (sel) { g.setColour (C::glow().withAlpha (0.4f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); }
        g.setColour (juce::Colours::black.withAlpha (0.06f)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        if (d >= 0)
        {
            g.setColour (C::layer (d)); g.fillRoundedRectangle ((float) r.getX() + 6.0f, (float) r.getCentreY() - 6.0f, 12.0f, 12.0f, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f)); g.drawRoundedRectangle ((float) r.getX() + 6.0f, (float) r.getCentreY() - 6.0f, 12.0f, 12.0f, 3.0f, 0.8f);
        }
        g.setFont (font (12.0f, true)); g.setColour (C::text());
        g.drawText (d < 0 ? juce::String ("All layers") : juce::String::fromUTF8 (ed->layers.layers[(size_t) d].name.c_str()), r.withTrimmedLeft (24).withWidth (96), juce::Justification::centredLeft, true);
    }
    juce::StringArray lines;
    if (ed)
    {
        lines.add (juce::String ((int) ed->layers.ops.size()) + (ed->layers.ops.size() == 1 ? " edit" : " edits") + " on this part");
        lines.add ("Background: the steady noise. Tone: what rings.");
        lines.add ("Transient: the attacks. Noise: the rest of the hit.");
    }
    else lines.add ("Open a file to work on its layers");
    lcdText (g, { 14, getHeight() - 126, getWidth() - 28, 66 }, lines, 10.5f);
    g.setFont (font (10.5f)); g.setColour (C::dim());
    g.drawFittedText ("Every export and preview plays the layers as set here: mute the background to export clean hits.",
                      { 14, getHeight() - 54, getWidth() - 28, 44 }, juce::Justification::topLeft, 3);
}

void LayersPanel::resized()
{
    for (int d = 0; d < (int) rows.size(); ++d)
    {
        const auto r = rowArea (d + 1);
        rows[(size_t) d].gain->setBounds (r.getX() + 120, r.getY() + 5, r.getWidth() - 120 - 64, 20);
        rows[(size_t) d].solo->setBounds (r.getRight() - 60, r.getY() + 3, 28, 24);
        rows[(size_t) d].mute->setBounds (r.getRight() - 30, r.getY() + 3, 28, 24);
    }
    const int y = rowArea (bc::kMaxLayers + 1).getY() + 4, w = (getWidth() - 28) / 2;
    addLayer.setBounds (14, y, w - 2, 28); delLayer.setBounds (16 + w, y, w - 2, 28);
    const int w3 = (getWidth() - 28) / 3;
    undo.setBounds (14, y + 34, w3 - 2, 28); redo.setBounds (16 + w3, y + 34, w3 - 2, 28); reset.setBounds (18 + 2 * w3, y + 34, w3 - 2, 28);
}

void LayersPanel::mouseDown (const juce::MouseEvent& e)
{
    auto* ed = view.edits();
    const int n = ed ? (int) ed->layers.layers.size() : 0;
    for (int i = 0; i <= n; ++i)
        if (rowArea (i).contains (e.getPosition())) { view.target = i - 1; view.refreshPanels(); view.repaintAll(); return; }
}

// ---------------------------------------------------------------- EXPORT

ExportPanel::ExportPanel (View& v) : Panel (v)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &which, &levelMode, &bits, &best, &target, &ceiling, &fadeIn, &fadeOut,
                                                                          &matchAll, &separate, &perFile, &choose, &openDir, &exportOne, &exportAll })
        addAndMakeVisible (c);
    auto& st = settings();
    best.step = 1.0f;
    best.toText = [] (float x) { return juce::String (juce::roundToInt (x)); };
    target.toText = [] (float x) { return juce::String (x, 1) + " LUFS"; };
    ceiling.toText = [] (float x) { return juce::String (x, 1) + " dB"; };
    fadeIn.toText = [] (float x) { return juce::String (x, 1) + " ms"; };
    fadeOut.toText = [] (float x) { return juce::String (juce::roundToInt (x)) + " ms"; };
    which.setItemTooltip (0, "Every event that is switched on");
    which.setItemTooltip (1, "The best few of each group: clear of the background, typical of the group");
    which.setItemTooltip (2, "Only the ones like the FIND MORE sound (EVENTS tab)");
    levelMode.setItemTooltip (0, "As recorded");
    levelMode.setItemTooltip (1, "Every peak at the same level");
    levelMode.setItemTooltip (2, "Every one-shot equally loud (ITU-R BS.1770 loudness, the loudest 400 ms)");
    target.setTooltip ("How loud every one-shot is made (LUFS)");
    ceiling.setTooltip ("PEAK: where every peak goes. LOUDNESS: no peak goes above this.");
    matchAll.setTooltip ("If some one-shots cannot reach the loudness without passing the ceiling, the others are brought down to them, so all match");
    separate.setTooltip ("Also write each audible layer of each one-shot as a file of its own (they share one level)");
    perFile.setTooltip ("Put each recording's one-shots in a folder of its own");
    which.onSelect = [this, &st] (int i) { st.pick.mode = (bc::Pick) i; changed(); };
    best.onChange = [this, &st] (float x) { st.pick.bestN = juce::roundToInt (x); changed(); };
    levelMode.onSelect = [this, &st] (int i) { st.exp.normMode = i; changed(); };
    target.onChange = [this, &st] (float x) { st.exp.loudDb = x; view.proc.stateChanged(); };
    ceiling.onChange = [this, &st] (float x) { st.exp.normDb = x; view.proc.stateChanged(); };
    fadeIn.onChange = [this, &st] (float x) { st.exp.fadeInMs = x; view.proc.stateChanged(); };
    fadeOut.onChange = [this, &st] (float x) { st.exp.fadeOutMs = x; view.proc.stateChanged(); };
    matchAll.onClick = [this, &st] { st.exp.matchAll = matchAll.getToggleState(); changed(); };
    separate.onClick = [this, &st] { st.exp.separate = separate.getToggleState(); changed(); };
    perFile.onClick = [this, &st] { st.folderPerFile = perFile.getToggleState(); changed(); };
    bits.onSelect = [this, &st] (int i) { st.exp.bits = i == 0 ? 16 : i == 1 ? 24 : 32; changed(); };
    choose.onClick = [this, &st]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where the one-shots go", juce::File (juce::String::fromUTF8 (st.outDir.c_str())));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, &st] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) { st.outDir = fc.getResult().getFullPathName().toStdString(); changed(); }
        });
    };
    openDir.onClick = [this, &st]
    {
        const auto last = view.proc.session.status().lastExportDir;
        juce::File d (juce::String::fromUTF8 ((last.empty() ? st.outDir : last).c_str()));
        d.createDirectory();
        d.startAsProcess();
    };
    exportOne.onClick = [this] { view.proc.session.exportFiles (false); view.refreshPanels(); };
    exportAll.onClick = [this] { view.proc.session.exportFiles (true); view.refreshPanels(); };
    exportOne.setTooltip ("Cut and write the one-shots of the open file (every part of it)");
    exportAll.setTooltip ("Cut and write the one-shots of every file in the list");
    refresh();
}

void ExportPanel::refresh()
{
    const auto& st = settings();
    which.setSelected ((int) st.pick.mode);
    best.setValue ((float) st.pick.bestN); best.setEnabled (st.pick.mode == bc::Pick::Best);
    levelMode.setSelected (juce::jlimit (0, 2, st.exp.normMode));
    target.setValue (st.exp.loudDb); target.setEnabled (st.exp.normMode == 2);
    ceiling.setValue (st.exp.normDb); ceiling.setEnabled (st.exp.normMode != 0);
    matchAll.setToggleState (st.exp.matchAll, juce::dontSendNotification); matchAll.setEnabled (st.exp.normMode == 2);
    fadeIn.setValue (st.exp.fadeInMs); fadeOut.setValue (st.exp.fadeOutMs);
    separate.setToggleState (st.exp.separate, juce::dontSendNotification);
    perFile.setToggleState (st.folderPerFile, juce::dontSendNotification);
    bits.setSelected (st.exp.bits == 16 ? 0 : st.exp.bits == 24 ? 1 : 2);
    const bool busy = view.proc.session.status().busy;
    exportOne.setEnabled (view.doc != nullptr && ! busy);
    exportAll.setEnabled (! view.proc.session.files.empty() && ! busy);
    repaint();
}

void ExportPanel::paint (juce::Graphics& g)
{
    const auto& st = settings();
    caption (g, "Which one-shots", 6);
    juce::StringArray count;
    if (view.doc)
    {
        bc::PickParams pk = st.pick;
        if (pk.mode == bc::Pick::Like) { pk.ref = st.like.feat; pk.hasRef = st.like.valid; pk.refRise = st.like.w; pk.hasRise = st.like.valid; }
        const auto chosen = bc::pick (view.doc->events, view.doc->groups, pk);
        count.add (juce::String ((int) chosen.size()) + " of " + juce::String ((int) view.doc->events.size()) + " events");
        count.add (view.doc->parts > 1 ? "in this part of the file" : "in this file");
        if (pk.mode == bc::Pick::Like && ! st.like.valid) count.set (1, "FIND MORE first (EVENTS)");
    }
    else count.add ("Open a file");
    lcdText (g, { 108, 70, getWidth() - 122, 46 }, count, 11.0f);
    caption (g, "Level", 136);
    caption (g, "Files", 278);
    caption (g, "Where they go", 456);
    const auto dir = juce::String::fromUTF8 (st.outDir.c_str());
    paintLcd (g, juce::Rectangle<float> (14.0f, 472.0f, (float) getWidth() - 28.0f - 132.0f, 30.0f), 6.0f);
    g.setFont (font (11.0f)); g.setColour (C::lcdText());
    g.drawFittedText (dir.isEmpty() ? juce::String ("(choose a folder)") : dir, { 22, 472, getWidth() - 28 - 132 - 16, 30 }, juce::Justification::centredRight, 1, 0.8f);
    // the last export
    const auto s = view.proc.session.status();
    juce::String head, body;
    if (s.busy) { head = juce::String::fromUTF8 (s.what.c_str()); body = juce::String (juce::roundToInt (s.progress * 100.0f)) + " %"; }
    else if (s.exported > 0 || s.failed > 0)
    {
        head = "Exported " + plural (s.exported, "file") + (s.failed > 0 ? ", " + juce::String (s.failed) + " failed" : "");
        body = juce::String::fromUTF8 (s.note.c_str());
        if (! s.lastError.empty()) body << (body.isEmpty() ? "" : "\n") << juce::String::fromUTF8 (s.lastError.c_str());
    }
    else { head = "Nothing exported yet"; body = "One-shots are named after the recording, their group and a number (yard_A001.wav)"; }
    const juce::Rectangle<int> box (14, 572, getWidth() - 28, getHeight() - 572 - 6);
    paintLcd (g, box.toFloat(), 6.0f);
    auto t = box.reduced (8, 5);
    g.setFont (font (11.5f, true)); g.setColour (C::lcdText()); g.drawFittedText (head, t.removeFromTop (16), juce::Justification::centredLeft, 1, 0.85f);
    g.setFont (font (10.5f)); g.setColour (C::lcdDim()); g.drawFittedText (body, t, juce::Justification::topLeft, 5, 0.9f);
}

void ExportPanel::resized()
{
    const int W = getWidth();
    which.setBounds (14, 24, W - 28, 30);
    best.setBounds (14, 60, 90, 70);
    levelMode.setBounds (14, 154, W - 28, 30);
    target.setBounds (14, 190, 90, 80);
    ceiling.setBounds (106, 190, 90, 80);
    matchAll.setBounds (204, 216, W - 214, 24);
    fadeIn.setBounds (14, 294, 90, 80);
    fadeOut.setBounds (106, 294, 90, 80);
    bits.setBounds (204, 304, W - 218, 28);
    separate.setBounds (14, 382, W - 28, 22);
    perFile.setBounds (14, 406, W - 28, 22);
    choose.setBounds (W - 14 - 128, 472, 72, 30);
    openDir.setBounds (W - 14 - 54, 472, 54, 30);
    exportOne.setBounds (14, 518, (W - 32) / 2, 36);
    exportAll.setBounds (18 + (W - 32) / 2, 518, (W - 32) / 2, 36);
}

// ---------------------------------------------------------------- the inspector

// ---------------------------------------------------------------- FOLDERS

FoldersTab::FoldersTab (View& v) : Panel (v)
{
    addAndMakeVisible (panel);
    // a new folder changes where the next save opens, and the EXPORT tab shows a path
    panel.onChange = [this] { if (view.refreshPanels) view.refreshPanels(); };
}

void FoldersTab::resized()
{
    panel.setBounds (getLocalBounds().withHeight (std::min (getHeight(), nsui::FoldersPanel::wantedHeight())));
}

Inspector::Inspector (View& v) : detect (v), events (v), layers (v), exportP (v), folders (v)
{
    panels[0] = &detect; panels[1] = &events; panels[2] = &layers; panels[3] = &exportP; panels[4] = &folders;
    addAndMakeVisible (tabs);
    for (auto* p : panels) addChildComponent (p);
    tabs.setItemTooltip (4, "Where each kind of file goes: WAV, MIDI, .NJQ, .NJQi. NeoBeat and MidiJaQ use the same four.");
    tabs.onSelect = [this] (int i) { show (i); };
    show (0);
}

void Inspector::show (int tab)
{
    tabs.setSelected (tab);
    for (int i = 0; i < kNumTabs; ++i) panels[i]->setVisible (i == tab);
    panels[tab]->refresh();
}

void Inspector::refresh() { for (auto* p : panels) p->refresh(); }

void Inspector::paint (juce::Graphics& g) { paintPlate (g, getLocalBounds().toFloat(), 12.0f); }

void Inspector::resized()
{
    tabs.setBounds (10, 10, getWidth() - 20, 32);
    for (auto* p : panels) p->setBounds (0, 50, getWidth(), getHeight() - 56);
}

// ---------------------------------------------------------------- the header

Header::Header (View& v) : view (v)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &rooms, &play, &rec, &exp, &expAll, &loop }) addAndMakeVisible (c);
    rooms.setItemTooltip (0, "The cut room: find the events in your recordings and cut the one-shots");
    rooms.setItemTooltip (1, "The kit room: build a NeoBeat kit out of the one-shots and save it as a .NJQ file");
    rooms.setItemTooltip (2, "LOOPS: quick loops of up to 8 bars, layered from the sounds in the pool, for your productions");
    rooms.setItemTooltip (3, "KEYS: pitched sounds made into instruments that MidiJaQ plays");
    rooms.onSelect = [this] (int i) { view.room = i; if (view.roomChanged) view.roomChanged(); };
    rec.setColour (juce::TextButton::buttonOnColourId, C::red());
    play.setTooltip ("Play the open file from the cursor (Space). Click an event to hear just it.");
    loop.setTooltip ("Play the visible stretch round and round (L)");
    rec.setTooltip ("Record the input (in a DAW: the track Batchcut is on). The recording is saved, added and opened.");
    exp.setTooltip ("Export the one-shots of the open file (settings: EXPORT tab)");
    expAll.setTooltip ("Export the one-shots of every file in the list");
    play.onClick = [this] { view.play(); refresh(); };
    loop.onClick = [this] { view.loop = loop.getToggleState(); };
    rec.onClick = [this]
    {
        if (view.proc.recorder.isRecording()) view.proc.stopRecording(); else view.proc.startRecording();
        view.refreshPanels();
    };
    exp.onClick = [this] { view.proc.session.exportFiles (false); view.refreshPanels(); };
    expAll.onClick = [this] { view.proc.session.exportFiles (true); view.refreshPanels(); };
}

void Header::refresh()
{
    const bool kit = view.room != 0;
    rooms.setSelected (view.room);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &play, &rec, &exp, &expAll, &loop }) c->setVisible (! kit);
    play.setButtonText (view.proc.isPlaying() ? "Stop" : "Play");
    play.setToggleState (view.proc.isPlaying(), juce::dontSendNotification);
    loop.setToggleState (view.loop, juce::dontSendNotification);
    const bool recording = view.proc.recorder.isRecording();
    rec.setToggleState (recording, juce::dontSendNotification);
    rec.setButtonText (recording ? "Stop " + mmss (view.proc.recorder.seconds()) : juce::String ("Record"));
    const bool busy = view.proc.session.status().busy;
    exp.setEnabled (view.doc != nullptr && ! busy);
    expAll.setEnabled (! view.proc.session.files.empty() && ! busy);
    play.setEnabled (view.doc != nullptr);
    repaint (lcd);
}

void Header::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xfff3f7f5), 0.0f, juce::Colour (0xffcdd6d1), b.getBottom()));
    g.fillRect (b);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawHorizontalLine ((int) b.getBottom() - 1, 0.0f, b.getWidth());
    paintGem (g, { 18.0f, 20.0f, 22.0f, 22.0f });
    g.setFont (caps (21.0f)); g.setColour (C::text());
    g.drawText ("BATCHCUT", 50, 12, 180, 26, juce::Justification::centredLeft);
    g.setFont (font (10.5f)); g.setColour (C::dim());
    g.drawText ("find the events, cut the one-shots", 51, 36, 190, 14, juce::Justification::centredLeft);
    // the display: what is happening
    paintLcd (g, lcd.toFloat(), 8.0f);
    auto t = lcd.reduced (12, 6);
    const auto s = view.proc.session.status();
    juce::String l1, l2;
    if (view.proc.recorder.isRecording()) { l1 = "RECORDING  " + mmss (view.proc.recorder.seconds()); l2 = "Press STOP to save it, then it opens here"; }
    else if (s.busy) l1 = juce::String::fromUTF8 (s.what.c_str());
    else if (view.room == 2)
    {
        const auto& lp = view.proc.loops;
        int chops = 0; for (int i = 0; i < lp.numLayers; ++i) chops += (int) lp.layers[(size_t) i].hits.size();
        l1 = view.proc.loopsMessage.isNotEmpty() ? view.proc.loopsMessage : juce::String ("Pick where a layer's sounds come from, then ROLL it");
        l2 = plural (lp.numLayers, "layer") + ",  " + plural (chops, "chop") + ",  " + plural (lp.bars(), "bar") + ",  " + juce::String (lp.bpm, 1) + " BPM,  "
           + gen::pitchName (lp.root) + " " + gen::scale (lp.scale).name + ",  " + plural ((int) view.proc.kit.pool.size(), "sound") + " in the pool";
    }
    else if (view.room == 3)
    {
        const auto& ks = view.proc.keys;
        l1 = view.proc.keysMessage.isNotEmpty() ? view.proc.keysMessage : juce::String ("Pick where the sounds come from, then BUILD the instrument");
        l2 = plural ((int) ks.zones.size(), "sound") + ",  " + bc::keysModeName (ks.mode) + ",  sustain: " + bc::keysSustainName (ks.sustain) + ",  "
           + plural ((int) view.proc.kit.pool.size(), "sound") + " in the pool,  " + juce::String (view.proc.keysPlayer.active.load()) + " playing";
    }
    else if (view.room >= 1)
    {
        const auto& k = view.proc.kit;
        int lanes = 0; for (const auto& ln : k.lanes) lanes += ln.empty() ? 0 : 1;
        l1 = view.proc.kitMessage.isNotEmpty() ? view.proc.kitMessage : juce::String ("Gather one-shots, then AUTO-FILL the kit");
        l2 = plural ((int) k.pool.size(), "sound") + " in the pool,  " + juce::String (lanes) + " of 16 lanes filled,  " + juce::String (k.bpm, 1) + " BPM";
    }
    else if (view.proc.lastMessage.isNotEmpty()) { l1 = view.proc.lastMessage; }
    else if (view.doc && view.doc->file >= 0 && view.doc->file < (int) view.proc.session.files.size())
    {
        const auto& f = view.proc.session.files[(size_t) view.doc->file];
        l1 = juce::String::fromUTF8 (f.info.name.c_str());
        l2 = plural ((int) view.doc->events.size(), "event") + ", " + plural ((int) view.doc->groups.size(), "group")
           + (view.doc->parts > 1 ? ",  part " + juce::String (view.doc->part + 1) + " of " + juce::String (view.doc->parts) : juce::String());
    }
    else l1 = "Drop recordings here to begin";
    if (! s.busy && view.room == 0 && (s.exported > 0 || s.failed > 0) && l2.isEmpty())
        l2 = "Last export: " + juce::String (s.exported) + " files" + (s.failed > 0 ? ", " + juce::String (s.failed) + " failed" : "") + (s.note.empty() ? "" : "  (" + juce::String::fromUTF8 (s.note.c_str()) + ")");
    g.setFont (font (13.0f, true)); g.setColour (view.proc.recorder.isRecording() ? C::red().brighter (0.4f) : C::lcdText());
    g.drawFittedText (l1, t.removeFromTop (18), juce::Justification::centredLeft, 1, 0.8f);
    if (s.busy)
    {
        auto bar = t.withTrimmedTop (4).withHeight (8).toFloat();
        g.setColour (C::lcdDim().withAlpha (0.3f)); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (C::glow()); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, s.progress)), 3.0f);
    }
    else { g.setFont (font (11.0f)); g.setColour (C::lcdDim()); g.drawFittedText (l2, t, juce::Justification::centredLeft, 1, 0.8f); }
}

void Header::resized()
{
    rooms.setBounds (244, 16, 216, 32);
    const bool kit = view.room != 0;
    play.setBounds (468, 16, 70, 32);
    loop.setBounds (544, 20, 64, 24);
    rec.setBounds (612, 16, 98, 32);
    lcd = kit ? juce::Rectangle<int> (472, 10, 796, 44) : juce::Rectangle<int> (718, 10, 288, 44);
    exp.setBounds (1016, 16, 118, 32);
    expAll.setBounds (1142, 16, 126, 32);
}
