// BatchCut - the editor: the file list, the spectrogram with its layers and edit tools, the events, and the
// inspector (detection, events, layers, export). Laid out at 1280 x 800 and scaled with the window.
#pragma once

#include "Processor.h"
#include "../neobeat/shared/rooms/RoomView.h"
#include "MaterialRoom.h"
#include "Ui.h"
#include "../neobeat/shared/FoldersUi.h"

// What the editor shows and works on, shared by all its parts, and the actions they take.
struct View : public bc::RoomView
{
    explicit View (BcProcessor& p) : bc::RoomView (p), proc (p) {}
    BcProcessor& proc;
    double t0 = 0.0, t1 = 1.0;            // the visible stretch (s, in the part)
    double cursor = 0.0;                  // where PLAY starts
    int64_t selOnset = -1;                // the selected event, by its start (indexes change with every detection)
    int tool = 0;                         // 0 box, 1 brush, 2 lasso, 3 magic wand
    int target = -1;                      // the layer the tools work on (-1: all)
    float brushPx = 14.0f, tolDb = 12.0f;
    bc::Shape sel; bool hasSel = false;   // the selection on the spectrogram
    int selVersion = 0;                   // changes with the selection (for its overlay)
    bool loop = false;
    juce::String hover;
    std::function<void()> hoverChanged;   // the readout under the display changed

    double length() const noexcept { return doc && doc->audio ? (double) doc->audio->size() / doc->audio->sr : 0.0; }
    double sr() const noexcept { return doc && doc->audio ? doc->audio->sr : 48000.0; }
    int selIndex() const;
    int indexOf (int64_t onset) const;    // the event starting there (within 5 ms), or -1
    int64_t gapSamples() const;
    const bc::Event* selEvent() const { const int i = selIndex(); return i >= 0 ? &doc->events[(size_t) i] : nullptr; }
    bc::PartEdits* edits();
    void setView (double a, double b);
    void fit() { setView (0.0, std::max (0.5, length())); }
    void zoom (double factor, double around);
    void scroll (double seconds) { setView (t0 + seconds, t1 + seconds); }

    // the selection: combined with what is there (0 replace, 1 add, 2 take away)
    void select (const bc::Shape& s, int mode);
    void clearSelection();

    // edits on the selection, all undoable
    void edit (bc::EditOp::Kind kind, float gain, int toLayer);   // toLayer -2: a new layer
    void layersChanged();
    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undos.empty(); }
    bool canRedo() const noexcept { return ! redos.empty(); }

    // the events (each change can be undone; detection runs again)
    void selectEvent (int index, bool play);
    void step (int dir);
    void deleteEvent (int index);
    void toggleEvent (int index);
    void addEventAt (double seconds);
    void moveStart (int index, double seconds);
    void moveEnd (int index, double seconds);
    void resetEnd (int index);
    void toggleGroup (int g);
    void findMore (int index);
    void clearFindMore();
    void eventsChanged();

    void play();
    void stop() { proc.stopPlayback(); }

private:
    struct Undo
    {
        bool layers = true;
        std::string path; int part = 0;   // the file by its path: indexes change when files are removed
        int layerCount = bc::kAuto;
        std::vector<bc::Event> keep; std::vector<int64_t> deleted, off; std::vector<std::array<float, bc::kFeat>> groupsOff;
    };
    std::vector<Undo> undos, redos;
    Undo snapshot (bool layers);
    bc::PartEdits* editsOf (const Undo& u);
    bool isOpen (const Undo& u) const;
    void beforeEventChange() { undos.push_back (snapshot (false)); redos.clear(); }
};

// ---------------------------------------------------------------- the display

class SpecView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SpecView (View& v) : view (v) {}
    void paint (juce::Graphics&) override;
    void resized() override { dirty = true; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void invalidate() { dirty = true; repaint(); }
    void showMenu();

    double xToTime (float x) const noexcept { return view.t0 + (double) x / std::max (1, getWidth()) * (view.t1 - view.t0); }
    float timeToX (double t) const noexcept { return (float) ((t - view.t0) / std::max (1e-9, view.t1 - view.t0) * getWidth()); }
    float yToHeight (float y) const noexcept { return 1.0f - y / (float) std::max (1, getHeight()); }   // 0 bottom .. 1 top
    float heightToY (float h) const noexcept { return (1.0f - h) * (float) getHeight(); }

private:
    void rebuild();
    bool stale() const noexcept;
    void wand (juce::Point<float> p, int mode);
    bc::Shape rasterize (const juce::Path& p, bool stroke) const;
    void grabKeyboardFocusOnEditor();
    View& view;
    juce::Image img, selImg;
    std::vector<float> lev;            // dB per cell (x * kRows + row): the target layer, or all of them
    bool dirty = true;
    const void* builtFor = nullptr; double builtT0 = -1, builtT1 = -1; int builtW = 0, builtTarget = -2;
    int selBuilt = -1;
    juce::Point<float> down, lastPos; juce::Path stroke; bool dragging = false, hovering = false; int mode = 0;
    double menuAt = 0.0;
};

class EventLane final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EventLane (View& v) : view (v) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    static void eventMenu (View& view, int index, juce::Component* target);
private:
    double xToTime (float x) const noexcept { return view.t0 + (double) x / std::max (1, getWidth()) * (view.t1 - view.t0); }
    float timeToX (double t) const noexcept { return (float) ((t - view.t0) / std::max (1e-9, view.t1 - view.t0) * getWidth()); }
    int hit (float x) const;
    View& view;
    int dragKind = 0;      // 1 start, 2 end
    int dragIndex = -1;
    float dragX = 0.0f;
};

class Ruler final : public juce::Component
{
public:
    explicit Ruler (View& v) : view (v) {}
    void paint (juce::Graphics&) override;
private:
    View& view;
};

class Overview final : public juce::Component
{
public:
    explicit Overview (View& v) : view (v) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { centreAt (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { centreAt (e.position.x); }
    void mouseDoubleClick (const juce::MouseEvent&) override { view.fit(); }
private:
    void centreAt (float x);
    View& view;
};

// ---------------------------------------------------------------- the panels

class FileList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FileList (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;
    void removeSelected();
public:
    void confirmClearAll();                  // Clear all, after asking (the host test answers for itself)
    std::function<void (std::function<void (bool)>)> ask;   // set by tests: replaces the question
private:
    View& view;
    juce::ListBox list;
    bcui::Pill addFiles { "Add files" }, addFolder { "Add folder" }, remove { "Remove" }, clearAll { "Clear all" };
    std::unique_ptr<juce::FileChooser> chooser;
};

class Panel : public juce::Component
{
public:
    explicit Panel (View& v) : view (v) {}
    virtual void refresh() = 0;
protected:
    View& view;
    bc::Settings& settings() { return view.proc.session.settings; }
    void caption (juce::Graphics& g, const juce::String& t, int y) { bcui::paintLabel (g, t, { 14, y, getWidth() - 28, 14 }, juce::Justification::centredLeft, 8.5f); }
};

class DetectPanel final : public Panel, private juce::Timer
{
public:
    explicit DetectPanel (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
private:
    void timerCallback() override;
    void changed() { startTimer (120); }   // detection runs again shortly after the knobs stop moving
    void preset (int which);
    bcui::Knob sens { "Sensitivity", 0.0f, 1.0f, 0.5f }, gap { "Min gap", 5.0f, 500.0f, 50.0f, true }, grouping { "Grouping", 0.0f, 1.0f, 0.5f },
               preRoll { "Pre-roll", 0.0f, 50.0f, 3.0f }, tail { "Tail", 0.0f, 500.0f, 20.0f }, maxLen { "Max length", 0.1f, 20.0f, 4.0f, true };
    bcui::Segmented endMode { { "Floor", "Next", "Gap", "-40 dB" } };
    bcui::Gem cutAtNext { "Cut at the next start" };
    bcui::Pill field { "Field" }, dense { "Dense" }, loops { "Loops" };
};

class EventsPanel final : public Panel
{
public:
    explicit EventsPanel (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    juce::Rectangle<int> groupRow (int i) const { return { 14, 44 + i * 22, getWidth() - 28, 20 }; }
    juce::Rectangle<int> dragPad() const { return { 14, getHeight() - 92, getWidth() - 28, 78 }; }
    int groupsShown() const;
    bcui::Pill play { "Play" }, onOff { "On / off" }, del { "Delete" }, prev { "<" }, next { ">" };
    bcui::Pill findMore { "Find more like this" }, clearMore { "Clear" };
    bcui::Knob similarity { "Similarity", 0.0f, 1.0f, 0.5f };
    bool dragStarted = false;
public:
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }
};

class LayersPanel final : public Panel
{
public:
    explicit LayersPanel (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    struct Row { std::unique_ptr<bcui::Pill> solo, mute; std::unique_ptr<juce::Slider> gain; };
    juce::Rectangle<int> rowArea (int i) const { return { 14, 42 + i * 34, getWidth() - 28, 30 }; }
    std::vector<Row> rows;
    bcui::Pill addLayer { "New layer" }, delLayer { "Delete layer" }, undo { "Undo" }, redo { "Redo" }, reset { "Reset" };
    int shownLayers = -1;
};

class ExportPanel final : public Panel
{
public:
    explicit ExportPanel (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() override;
private:
    void changed() { view.proc.stateChanged(); refresh(); }
    bcui::Segmented which { { "All", "Best", "Like" } }, levelMode { { "Off", "Peak", "Loudness" } }, bits { { "16", "24", "32f" } };
    bcui::Knob best { "Best of each", 1.0f, 16.0f, 1.0f }, target { "Loudness", -40.0f, -6.0f, -16.0f }, ceiling { "Ceiling", -12.0f, 0.0f, -1.0f },
               fadeIn { "Fade in", 0.0f, 50.0f, 1.0f }, fadeOut { "Fade out", 0.0f, 500.0f, 10.0f };
    bcui::Gem matchAll { "Match all" }, separate { "A file per layer" }, perFile { "A folder per file" };
    bcui::Pill choose { "Folder..." }, openDir { "Open" }, exportOne { "Export this file" }, exportAll { "Export all files" };
    std::unique_ptr<juce::FileChooser> chooser;
};

// FOLDERS: the four assigned folders, shared with NeoBeat and MidiJaQ. The panel itself is the shared one
// (Source/neobeat/shared/FoldersUi.h); this only puts it in a tab.
class FoldersTab final : public Panel
{
public:
    explicit FoldersTab (View& v);
    void refresh() override { panel.refresh(); }
    void resized() override;
private:
    nsui::FoldersPanel panel;
};

class Inspector final : public juce::Component
{
public:
    explicit Inspector (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void show (int tab);
    int current() const noexcept { return tabs.getSelected(); }
private:
    static constexpr int kNumTabs = 5;
    bcui::Segmented tabs { { "Detect", "Events", "Layers", "Export", "Folders" } };
    DetectPanel detect; EventsPanel events; LayersPanel layers; ExportPanel exportP; FoldersTab folders;
    Panel* panels[kNumTabs];
};

class Header final : public juce::Component
{
public:
    explicit Header (View& v);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    View& view;
    bcui::Segmented rooms { { "Cut", "Kit", "Loops", "Keys", "Material" } };
    bcui::Pill play { "Play" }, rec { "Record" }, exp { "Export" }, expAll { "Export all" };
    bcui::Gem loop { "Loop" };
    juce::Rectangle<int> lcd;
};

// ---------------------------------------------------------------- the kit room (bc::KitRoom.cpp)
// ---------------------------------------------------------------- the editor

class BcEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::ChangeListener, private juce::Timer
{
public:
    explicit BcEditor (BcProcessor&);
    ~BcEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }

    // for the tests: the parts by name
    View& getView() noexcept { return view; }
    Inspector& getInspector() noexcept { return content.inspector; }
    bc::KitRoom& getKitRoom() noexcept { return content.kitRoom; }
    bc::LoopsRoom& getLoopsRoom() noexcept { return content.loopsRoom; }
    bc::KeysRoom& getKeysRoom() noexcept { return content.keysRoom; }
    MaterialRoom& getMaterialRoom() noexcept { return content.materialRoom; }
    FileList& getFiles() noexcept { return content.files; }
    void showRoom (int room) { view.room = room; if (view.roomChanged) view.roomChanged(); }
    SpecView& getSpec() noexcept { return content.spec; }
    EventLane& getLane() noexcept { return content.lane; }
    Overview& getOverview() noexcept { return content.overview; }
    juce::Component& getContent() noexcept { return content; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void docChanged();

    struct Content final : public juce::Component
    {
        explicit Content (View& v);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refreshToolbar();
        juce::String toolHint() const;
        View& view;
        void showRoom();
        Header header; FileList files; SpecView spec; EventLane lane; Ruler ruler; Overview overview; Inspector inspector; bc::KitRoom kitRoom; bc::LoopsRoom loopsRoom; bc::KeysRoom keysRoom; MaterialRoom materialRoom;
        bcui::Segmented tools { { "Box", "Brush", "Lasso", "Wand" } };
        juce::ComboBox layerBox;
        bcui::Pill erase { "Erase" }, down { "-6 dB" }, up { "+6 dB" }, toLayer { "To layer" }, undo { "Undo" }, redo { "Redo" };
        bcui::Pill partPrev { "<" }, partNext { ">" }, zoomOut { "-" }, zoomIn { "+" }, zoomFit { "Fit" };
        juce::Rectangle<int> partLabel, hoverLabel;
    };

    BcProcessor& proc;
    bcui::Look look;
    View view;
    Content content;
    juce::TooltipWindow tips { this, 600 };
    bool dropping = false, wasBusy = false;
    int64_t lastPlayhead = -2;
    int ticks = 0;
};
