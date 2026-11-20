// NeoBeat 3.0 — the interface. One shell in every room:
//   left       the tree: where you are
//   middle     the room (SEQUENCER, MATRIXXXER, LAYERMAX, KEY MAP): drawing and playing only
//
// The words, everywhere:
//   Room    one of the four tabs.            Lane   one of the 16 rows of the sequencer, and its sound.
//   Node    a box in a sequencer that holds a hit. A node can be sent to an effect; until it is, it is Init.
//   Send    to give a node, a lane or a row an effect.     Scene  one of the four patterns, A to D.
//   right      the inspector: whatever is selected (SOUND / FX / MOD)
//   bottom     the mod dock: drag a source onto any knob
// The Friction Stretch and Bounce Delay windows open over the shell.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../plugin/PluginProcessor.h"
#include "../shared/Ui.h"

namespace ui
{
// ------------------------------------------------------------------ colours and drawing
//
// The look itself lives in Source/shared/Ui.h, which Batchcut and MidiJaQ draw with too: one copy of the
// drawing code, with every colour a named token so a skin is a table rather than a rewrite. NeoBeat's own
// copy had drifted - a heavier plate shadow, a wider corner, a cooler grey, a shade across the chassis -
// so those values are written down as the "Chrome Pearl Deep" skin and installed at startup, and nothing
// about NeoBeat looks any different for the merge.
//
// What stays here is what is NeoBeat's alone: the colours of its effect families and mod sources, and its
// own Knob, which is a mod destination as well as a control.
using nsui::Skin;
using nsui::skin;
using nsui::setSkin;
using nsui::font;
using nsui::caps;
using nsui::textW;
using nsui::paintChassis;
using nsui::paintPlate;
using nsui::paintWell;
using nsui::paintPearl;
using nsui::paintLcd;
using nsui::paintGem;
using nsui::paintLabel;
using nsui::paintTile;
using nsui::Look;
using nsui::Pill;
using nsui::Segmented;

namespace C
{
    using nsui::C::text;
    using nsui::C::label;
    using nsui::C::dim;
    using nsui::C::teal;
    using nsui::C::glow;
    using nsui::C::lcdText;
    using nsui::C::lcdDim;
    using nsui::C::amber;
    juce::Colour family (const juce::String& fam);
    juce::Colour fxColour (const nb::CellFx& fx);
    juce::Colour mod (nb::ModSrc s);
}

juce::String fxName (const nb::CellFx& fx);
juce::String fxShort (const nb::CellFx& fx);
juce::String fxFamily (const nb::CellFx& fx);
nb::CellFx fxFromIndex (int i);   // 0..23 library, 24 Friction Stretch, 25 Bounce Delay
int fxIndex (const nb::CellFx& fx);
juce::StringArray fxMenuNames();
juce::String noteName (int note);

// ------------------------------------------------------------------ shared state
struct Sel
{
    enum Kind { None, Pad, Cell, SampleOp, Mark, Zone, Grid, Node };   // Node: index = lane * 512 + step, in the scene shown
    Kind kind = Pad;
    int index = 0;
    bool operator== (const Sel& o) const { return kind == o.kind && index == o.index; }
};

struct Ctx
{
    explicit Ctx (NeoBeatProcessor& p) : proc (p) {}
    NeoBeatProcessor& proc;
    nb::Project& P() { return proc.project; }
    int room = 0;            // 0 SEQUENCER, 1 MATRIXXXER, 2 LAYERMAX, 3 KEY MAP
    int gridRow = 0;         // the Layermax row last touched (the inspector shows what it follows)
    int scene() const { return proc.scene(); }
    // the part of the scene the step grids show: `viewBars` bars from bar `viewBar`
    int viewBar = 0, viewBars = 2;
    bool follow = true;      // the view turns its pages with the playhead
    int firstStep() const { return viewBar * 16; }
    int shownSteps() const { return std::max (1, std::min (viewBars * 16, proc.pattern.length() - viewBar * 16)); }
    // overlays: 0 none, 1 export, 2 MIDI out, 3 import a folder, 4 "make this a default sequence?"
    int overlay = 0;
    juce::String overlayFile;
    // a node's effect, copied
    nb::CellFx clipFx; nb::Step clipStep; int clipSpan = 2; float clipHeight = 0.5f; bool clipHas = false;
    // something worth saying once (a notice over the shell)
    juce::StringArray noticesShown; juce::String noticeTitle, noticeText;
    void notify (const juce::String& key, const juce::String& title, const juce::String& text)
    {
        if (noticesShown.contains (key)) return;
        noticesShown.add (key); noticeTitle = title; noticeText = text;
        if (refreshAll) refreshAll();
    }
    // what changed since the last call becomes one undo step in this room
    void commit() { proc.undoCommit (room); }
    int pad = 0;             // the pad the kit room and the inspector follow
    Sel sel;
    int tab = 0;             // inspector tab: 0 SOUND, 1 FX, 2 MOD
    nb::ModSrc dockSrc = nb::ModSrc::Lfo1;
    int fsSlot = 0, bdSlot = 0;
    bool fsOpen = false, bdOpen = false, listOpen = false;
    int winCell = -1;        // the windows edit this One Shot cell's effect instead of a slot
    int brush = 3;           // One Shot / Sample brush: an fxIndex
    int markBrush = 0;       // Sequencer view: 0 dot, 1 dash, 2 warble, 3 tick, 4 spray, 5 erase, 6 select
    // Rebuild the inspector, tree and rooms once the current event is over. A button or knob that
    // calls this is usually inside the panel being rebuilt, so the rebuild never deletes it mid-callback;
    // several requests in one event make one rebuild.
    std::function<void()> refreshAll;
    std::function<void()> refreshNow;      // the same, immediately (tools that draw right after a change)
    void changed (bool rebuild = true) { proc.projectChanged(); if (rebuild && refreshAll) refreshAll(); }
    void select (Sel s, int newTab = -1) { sel = s; if (s.kind == Sel::Pad) { pad = s.index; proc.selectedPad.store (s.index); } if (newTab >= 0) tab = newTab; if (refreshAll) refreshAll(); }
    void openWindow (bool fs, int slot, int cell = -1);
};

// ------------------------------------------------------------------ widgets
// A knob with its label and value, a mod destination (rings for the routes that reach it, and a drop
// target for the dock's sources). Values are plain (not normalised); `toText` formats them.
class Knob final : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    Knob (Ctx& c, juce::String label, float lo, float hi, float def, bool logScale = false);
    std::function<void (float)> onChange;
    std::function<juce::String (float)> toText;
    std::function<void()> onDragEnd;
    void setValue (float v, bool notify = false);
    float getValue() const noexcept { return value; }
    void setDest (const juce::String& d) { dest = d; repaint(); }
    void setMain (bool m) { main = m; repaint(); }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setRange (float lo, float hi, bool logScale) { minV = lo; maxV = hi; isLog = logScale; repaint(); }
    void attach (juce::RangedAudioParameter* p);      // follow a host parameter
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    void syncFromParam();

private:
    float toNorm (float v) const;
    float fromNorm (float n) const;
    Ctx& ctx;
    juce::String label, dest;
    float minV, maxV, defV, value;
    bool isLog = false, main = false, dropHover = false, gesture = false;
    float dragStartNorm = 0.0f;
    juce::RangedAudioParameter* param = nullptr;
};

class LcdBox final : public juce::Component
{
public:
    juce::String line1, line2;
    void set (const juce::String& a, const juce::String& b = {}) { if (a != line1 || b != line2) { line1 = a; line2 = b; repaint(); } }
    void paint (juce::Graphics&) override;
};

// ------------------------------------------------------------------ the shell's parts
// every part is a Component and a Part (the shell refreshes and ticks them)
struct Part
{
    virtual ~Part() = default;
    virtual void refresh() {}
    virtual void tick() {}
};

class Shell final : public juce::Component, public juce::DragAndDropContainer, private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit Shell (NeoBeatProcessor&);
    ~Shell() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    Ctx ctx;

private:
    void timerCallback() override;
    void handleAsyncUpdate() override { refresh(); }
    Look look;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;      // (every child's: the end of a gesture is an undo step)
    std::unique_ptr<juce::Component> header, tree, inspector, dock, routeList, fsWindow, bdWindow;
    std::array<std::unique_ptr<juce::Component>, 4> rooms;
    std::array<std::unique_ptr<juce::Component>, 5> overlays;   // export, MIDI, import, default prompt, notice
    std::vector<Part*> parts;
    int shownRoom = -1, sinceCommit = 0;
    uint32_t lateSeen = 0;
    bool shownFs = false, shownBd = false, shownList = false;
};

std::unique_ptr<juce::Component> makeHeader (Ctx&);
std::unique_ptr<juce::Component> makeTree (Ctx&);
std::unique_ptr<juce::Component> makeInspector (Ctx&);
std::unique_ptr<juce::Component> makeDock (Ctx&);
std::unique_ptr<juce::Component> makeKitRoom (Ctx&);
std::unique_ptr<juce::Component> makeMatrixRoom (Ctx&);
std::unique_ptr<juce::Component> makeGridRoom (Ctx&);
std::unique_ptr<juce::Component> makeKeyMapRoom (Ctx&);
std::unique_ptr<juce::Component> makeFrictionWindow (Ctx&);
std::unique_ptr<juce::Component> makeBounceWindow (Ctx&);
std::unique_ptr<juce::Component> makeRouteList (Ctx&);
std::unique_ptr<juce::Component> makeExportPanel (Ctx&);
std::unique_ptr<juce::Component> makeMidiPanel (Ctx&);
std::unique_ptr<juce::Component> makeImportPanel (Ctx&);
std::unique_ptr<juce::Component> makeDefaultPrompt (Ctx&);
std::unique_ptr<juce::Component> makeNotice (Ctx&);

// nodes: a hit in the scene shown, and the effect it is sent to
int nodeCell (Ctx&, int lane, int step);                       // the index of its effect cell, or -1 (Init)
void sendNode (Ctx&, int lane, int step, int fxIdx);           // fxIdx: an fxIndex, or -1 for Init (no effect)
void addSendMenu (juce::PopupMenu&, int idBase, int current);  // "Send to": ids idBase (Init) and idBase + 1 + fxIndex
void copyNode (Ctx&, int lane, int step);
void pasteNode (Ctx&, int lane, int step);

// shared helpers for the parts
void addRoute (Ctx&, nb::ModSrc src, const juce::String& dest, float depth = 0.3f);
juce::String destLabel (const juce::String& dest);
juce::String sourceLabel (const nb::Project&, const nb::SlotSource&);
juce::PopupMenu sourceMenu (const nb::Project&, int idBase);   // ids: base + kind * 1000 + index
nb::SlotSource sourceFromMenu (int result, int idBase);
} // namespace ui
