// NeoBeat 2.0 — the interface. One shell in every room:
//   left       the tree: where you are
//   middle     the room (KIT, MATRIXXXER, GRID, KEY MAP): drawing and playing only
//   right      the inspector: whatever is selected (SOUND / FX / MOD)
//   bottom     the mod dock: drag a source onto any knob
// The Friction Stretch and Bounce Delay windows open over the shell.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../plugin/PluginProcessor.h"

namespace ui
{
// ------------------------------------------------------------------ colours and drawing
namespace C
{
    inline juce::Colour text()  { return juce::Colour (0xff1f2925); }
    inline juce::Colour label() { return juce::Colour (0xff2b3631); }
    inline juce::Colour dim()   { return juce::Colour (0xff4b5852); }
    inline juce::Colour teal()  { return juce::Colour (0xff1f9e8c); }
    inline juce::Colour glow()  { return juce::Colour (0xff5fd8c4); }
    inline juce::Colour lcdText() { return juce::Colour (0xffaaf6d0); }
    inline juce::Colour lcdDim()  { return juce::Colour (0xff5fae8a); }
    inline juce::Colour amber() { return juce::Colour (0xfff5b25a); }
    juce::Colour family (const juce::String& fam);
    juce::Colour fxColour (const nb::CellFx& fx);
    juce::Colour mod (nb::ModSrc s);
}

juce::Font font (float size, bool bold = false);
juce::Font caps (float size);      // the engraved label face: bold caps, tracked
float textW (const juce::Font& f, const juce::String& t);

void paintChassis (juce::Graphics&, juce::Rectangle<float>);
void paintPlate (juce::Graphics&, juce::Rectangle<float>, float radius = 14.0f);
void paintWell (juce::Graphics&, juce::Rectangle<float>, float radius = 12.0f);
void paintPearl (juce::Graphics&, juce::Rectangle<float>, float radius = 16.0f, bool glow = false);
void paintLcd (juce::Graphics&, juce::Rectangle<float>, float radius = 8.0f);
void paintGem (juce::Graphics&, juce::Rectangle<float>, bool on = true);
void paintLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Justification = juce::Justification::centredLeft, float size = 9.0f, juce::Colour = C::label());
void paintTile (juce::Graphics&, juce::Rectangle<float>, juce::Colour fill, const juce::String& text, bool selected, juce::Colour ring = {});
juce::String fxName (const nb::CellFx& fx);
juce::String fxShort (const nb::CellFx& fx);
juce::String fxFamily (const nb::CellFx& fx);
nb::CellFx fxFromIndex (int i);   // 0..23 library, 24 Friction Stretch, 25 Bounce Delay
int fxIndex (const nb::CellFx& fx);
juce::StringArray fxMenuNames();
juce::String noteName (int note);

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return caps (std::min (9.5f, h * 0.36f)); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
};

// ------------------------------------------------------------------ shared state
struct Sel
{
    enum Kind { None, Pad, Cell, SampleOp, Mark, Zone, Grid };
    Kind kind = Pad;
    int index = 0;
    bool operator== (const Sel& o) const { return kind == o.kind && index == o.index; }
};

struct Ctx
{
    explicit Ctx (NeoBeatProcessor& p) : proc (p) {}
    NeoBeatProcessor& proc;
    nb::Project& P() { return proc.project; }
    int room = 0;            // 0 KIT, 1 MATRIXXXER, 2 GRID, 3 KEY MAP
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

class Pill final : public juce::TextButton
{
public:
    explicit Pill (const juce::String& t = {}, bool toggle = false) : juce::TextButton (t) { setClickingTogglesState (toggle); }
};

class LcdBox final : public juce::Component
{
public:
    juce::String line1, line2;
    void set (const juce::String& a, const juce::String& b = {}) { if (a != line1 || b != line2) { line1 = a; line2 = b; repaint(); } }
    void paint (juce::Graphics&) override;
};

// A row of mutually exclusive pills.
class Segmented final : public juce::Component
{
public:
    Segmented (juce::StringArray items);
    std::function<void (int)> onSelect;
    void setSelected (int i);
    int getSelected() const noexcept { return selected; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<Pill> buttons;
    int selected = 0;
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
    std::unique_ptr<juce::Component> header, tree, inspector, dock, routeList, fsWindow, bdWindow;
    std::array<std::unique_ptr<juce::Component>, 4> rooms;
    std::vector<Part*> parts;
    int shownRoom = -1;
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

// shared helpers for the parts
void addRoute (Ctx&, nb::ModSrc src, const juce::String& dest, float depth = 0.3f);
juce::String destLabel (const juce::String& dest);
juce::String sourceLabel (const nb::Project&, const nb::SlotSource&);
juce::PopupMenu sourceMenu (const nb::Project&, int idBase);   // ids: base + kind * 1000 + index
nb::SlotSource sourceFromMenu (int result, int idBase);
} // namespace ui
