// Batchcut 3.0 - the KEYS room: where the sounds come from (the pool) and the sounds the instrument holds on
// the left, its map of keys and velocities and the keys to play it in the middle, how it plays on the right.
#include "Editor.h"
#include "../neobeat/gen/Scale.h"
#include "../neobeat/shared/NjqInstrument.h"
#include <set>

using namespace bcui;

namespace
{
juce::Colour zoneColour (int i)
{
    static const juce::uint32 c[] = { 0xff4fd9c8, 0xfff5b25a, 0xffe58ad8, 0xff8fb8ff, 0xffc6f27a, 0xffff8f7a, 0xfff2ee7a, 0xffb59cff };
    return juce::Colour (c[(size_t) ((i % 8) + 8) % 8]);
}
juce::String centsText (float root) { const int c = (int) std::lround ((root - std::round (root)) * 100.0f); return juce::String (gen::noteName ((int) std::lround (root))) + (c == 0 ? juce::String() : (c > 0 ? " +" : " ") + juce::String (c) + " ct"); }

// ---------------------------------------------------------------- where the sounds come from
class KeysSources final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit KeysSources (View& v) : view (v)
    {
        list.setModel (this); list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }
    std::function<void (const bc::LoopSource&)> onPick;
    void rebuild()
    {
        rows.clear();
        const auto& pool = view.proc.kit.pool;
        auto add = [&] (const bc::LoopSource& src)
        {
            Row r; r.src = src;
            for (const auto& it : pool) if (src.takes (*it)) { ++r.count; r.tuned += bc::keysdetail::pitched (*it) ? 1 : 0; }
            if (r.count > 0 || src.kind == bc::LoopSource::Everything) rows.push_back (r);
        };
        add (bc::LoopSource());
        for (const auto& s : view.proc.loopsSources()) { bc::LoopSource src; src.kind = bc::LoopSource::From; src.from = s; add (src); }
        for (int f = 0; f < 12; ++f) { bc::LoopSource src; src.kind = bc::LoopSource::Family; src.family = f; add (src); }
        list.updateContent(); list.repaint();
    }
    void resized() override { list.setBounds (getLocalBounds().reduced (3)); }
    void paint (juce::Graphics& g) override
    {
        paintWell (g, getLocalBounds().toFloat(), 8.0f);
        if (view.proc.kit.pool.empty())
        {
            g.setFont (font (11.5f)); g.setColour (C::dim());
            g.drawFittedText ("The pool is empty.\n\nTHIS FILE, ALL FILES or FOLDER brings sounds in (the Kit room and LOOPS share it).",
                              getLocalBounds().reduced (16), juce::Justification::centred, 8);
        }
    }
private:
    struct Row { bc::LoopSource src; int count = 0, tuned = 0; };
    int getNumRows() override { return view.proc.kit.pool.empty() ? 0 : (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        const auto& r = rows[(size_t) row];
        const bool current = view.proc.keysSource == r.src;
        if (current) { g.setColour (C::glow().withAlpha (0.45f)); g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 5.0f); }
        g.setFont (font (11.5f, current)); g.setColour (C::text());
        g.drawText (r.src.label(), 10, 0, w - 104, h, juce::Justification::centredLeft, true);
        g.setFont (font (9.5f)); g.setColour (C::dim());
        g.drawText (juce::String (r.count) + ",  " + juce::String (r.tuned) + " tuned", w - 96, 0, 90, h, juce::Justification::centredRight);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int) rows.size() && onPick) onPick (rows[(size_t) row].src); list.repaint(); }
    View& view;
    juce::ListBox list;
    std::vector<Row> rows;
};

// ---------------------------------------------------------------- the sounds the instrument holds
class ZoneList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ZoneList (View& v) : view (v)
    {
        list.setModel (this); list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);
    }
    std::function<void()> onChange;
    std::function<void (int key, float vel)> onAudition;
    void rebuild() { list.updateContent(); list.repaint(); }
    void resized() override { list.setBounds (getLocalBounds().reduced (3)); }
    void paint (juce::Graphics& g) override { paintWell (g, getLocalBounds().toFloat(), 8.0f); }
private:
    int getNumRows() override { return (int) view.proc.keys.zones.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        const auto& Z = view.proc.keys.zones;
        if (row < 0 || row >= (int) Z.size() || ! Z[(size_t) row].sound) return;
        const auto& z = Z[(size_t) row];
        if (row == view.keysZone) { g.setColour (C::glow().withAlpha (0.45f)); g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 5.0f); }
        g.setColour (zoneColour (row)); g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 6.0f, 6.0f, (float) h - 12.0f), 2.0f);
        g.setFont (font (11.0f)); g.setColour (C::text());
        g.drawText (juce::String::fromUTF8 (z.sound->name.c_str()), 18, 0, w - 120, h, juce::Justification::centredLeft, true);
        g.setFont (font (9.5f, true)); g.setColour (C::dim());
        const bool tuned = view.proc.keys.mode == bc::KeysMode::Tuned;
        g.drawText (tuned ? centsText (z.root) : view.proc.keys.mode == bc::KeysMode::Chops ? juce::String (gen::noteName (z.lowKey)) : juce::String ("exciter"), w - 100, 0, 94, h, juce::Justification::centredRight, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        view.keysZone = row;
        const auto& Z = view.proc.keys.zones;
        if (row >= 0 && row < (int) Z.size())
        {
            const auto& z = Z[(size_t) row];
            const int key = view.proc.keys.mode == bc::KeysMode::Tuned ? juce::jlimit (z.lowKey, z.highKey, (int) std::lround (z.root)) : view.proc.keys.mode == bc::KeysMode::Chops ? z.lowKey : 60;
            const float vel = (float) (z.lowVel + z.highVel) * 0.5f / 127.0f;
            if (onAudition) onAudition (key, vel);
        }
        if (onChange) onChange();
    }
    View& view;
    juce::ListBox list;
};

// ---------------------------------------------------------------- the map: keys across, velocity up
class KeyMap final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KeyMap (View& v) : view (v) { setTooltip ("Each sound's keys (across) and velocities (up). Click a sound to hear it and choose it; drag its root to retune it."); }
    std::function<void()> onChange;
    int lowKey = 24, highKey = 108;           // C1 .. C8, as shown

    float xOf (float key) const { return 8.0f + ((float) getWidth() - 16.0f) * (key - (float) lowKey) / (float) (highKey - lowKey + 1); }
    float yOf (int vel) const { return 22.0f + ((float) getHeight() - 30.0f) * (1.0f - (float) vel / 127.0f); }

    void paint (juce::Graphics& g) override
    {
        paintLcd (g, getLocalBounds().toFloat(), 8.0f);
        const auto& ins = view.proc.keys;
        // octaves
        for (int k = lowKey; k <= highKey; ++k)
        {
            if (k % 12 != 0) continue;
            const float x = xOf ((float) k);
            g.setColour (C::lcdDim().withAlpha (0.25f)); g.drawVerticalLine ((int) x, 20.0f, (float) getHeight() - 6.0f);
            g.setFont (font (9.0f, true)); g.setColour (C::lcdDim()); g.drawText (gen::noteName (k), (int) x + 2, 4, 40, 14, juce::Justification::centredLeft);
        }
        if (ins.zones.empty())
        {
            g.setFont (font (12.0f)); g.setColour (C::lcdDim());
            g.drawFittedText ("No instrument yet.\n\nChoose where its sounds come from on the left, the way it plays on the right, then BUILD.", getLocalBounds().reduced (30), juce::Justification::centred, 5);
            return;
        }
        for (int i = 0; i < (int) ins.zones.size(); ++i)
        {
            const auto& z = ins.zones[(size_t) i];
            const float x0 = xOf ((float) std::max (lowKey, z.lowKey)), x1 = xOf ((float) std::min (highKey, z.highKey) + 1.0f);
            if (x1 <= x0) continue;
            // round robins share a cell: each takes a slice of its height
            const float y0 = yOf (z.highVel), y1 = yOf (z.lowVel - 1), sh = (y1 - y0) / (float) std::max (1, z.rrCount);
            auto r = juce::Rectangle<float> (x0 + 0.5f, y0 + sh * (float) z.rr + 0.5f, x1 - x0 - 1.0f, sh - 1.0f);
            const bool sel = i == view.keysZone;
            g.setColour (zoneColour (i).withAlpha (sel ? 0.9f : 0.55f)); g.fillRoundedRectangle (r, 3.0f);
            if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 3.0f, 1.5f); }
            if (ins.mode == bc::KeysMode::Tuned && z.root >= (float) lowKey && z.root <= (float) highKey + 1)
            {
                const float rx = xOf (z.root + 0.5f);
                g.setColour (juce::Colours::black.withAlpha (0.65f)); g.fillRect (rx - 1.0f, r.getY(), 2.0f, r.getHeight());
            }
        }
        // the keys sounding
        for (int k = lowKey; k <= highKey; ++k)
            if (view.proc.keyboard.isNoteOnForChannels (0xffff, k))
            {
                g.setColour (C::amber().withAlpha (0.8f)); g.fillRect (xOf ((float) k), (float) getHeight() - 8.0f, xOf ((float) k + 1.0f) - xOf ((float) k), 4.0f);
            }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int z = zoneAt (e.position);
        view.keysZone = z;
        dragging = z >= 0 && view.proc.keys.mode == bc::KeysMode::Tuned;
        if (z >= 0)
        {
            const auto& zz = view.proc.keys.zones[(size_t) z];
            const int key = juce::jlimit (zz.lowKey, zz.highKey, keyAt (e.position.x));
            const float vel = juce::jlimit (0.05f, 1.0f, (float) velAt (e.position.y) / 127.0f);
            view.proc.keyboard.noteOn (1, key, vel); held = key;
            rootAtDown = zz.root; xDown = e.position.x;
        }
        if (onChange) onChange();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        // with Alt: the zone's root follows the mouse in cents (retunes it)
        if (! dragging || ! e.mods.isAltDown() || view.keysZone < 0) return;
        auto& z = view.proc.keys.zones[(size_t) view.keysZone];
        z.root = juce::jlimit (-24.0f, 151.0f, rootAtDown + (e.position.x - xDown) / 400.0f);
        view.proc.keysChanged(); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (held >= 0) { view.proc.keyboard.noteOff (1, held, 0.0f); held = -1; }
        if (dragging) { view.proc.keysMark(); if (onChange) onChange(); }
        dragging = false;
    }
private:
    int keyAt (float x) const { return juce::jlimit (0, 127, lowKey + (int) std::floor ((x - 8.0f) / std::max (1.0f, (float) getWidth() - 16.0f) * (float) (highKey - lowKey + 1))); }
    int velAt (float y) const { return juce::jlimit (1, 127, (int) std::lround (127.0f * (1.0f - (y - 22.0f) / std::max (1.0f, (float) getHeight() - 30.0f)))); }
    int zoneAt (juce::Point<float> p) const
    {
        const auto& Z = view.proc.keys.zones;
        const int k = keyAt (p.x), v = velAt (p.y);
        for (int i = 0; i < (int) Z.size(); ++i)
        {
            const auto& z = Z[(size_t) i];
            if (k < z.lowKey || k > z.highKey || v < z.lowVel || v > z.highVel) continue;
            const float y0 = yOf (z.highVel), y1 = yOf (z.lowVel - 1), sh = (y1 - y0) / (float) std::max (1, z.rrCount);
            if (p.y >= y0 + sh * (float) z.rr && p.y < y0 + sh * (float) (z.rr + 1)) return i;
        }
        return -1;
    }
    View& view;
    bool dragging = false;
    int held = -1;
    float rootAtDown = 60.0f, xDown = 0.0f;
};
} // namespace

// ---------------------------------------------------------------- the room
struct KeysRoom::Parts
{
    explicit Parts (View& v) : sources (v), zones (v), map (v), keys (v.proc.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {}
    KeysSources sources; ZoneList zones; KeyMap map;
    juce::MidiKeyboardComponent keys;
    Pill fromFile { "This file" }, fromAll { "All files" }, folder { "Folder" };
    juce::TextEditor name;
    Pill build { "Build" }, undo { "Undo" }, redo { "Redo" }, open { "Open .NJQ" }, save { "Save .NJQ" };
    juce::ComboBox chordBox;
    Segmented mode { { "Tuned", "Resonator", "Chops" } }, sustain { { "As recorded", "Loop", "Hold" } };
    Knob attack { "ATTACK", 1.0f, 2000.0f, 2.0f, true }, release { "RELEASE", 10.0f, 8000.0f, 300.0f, true }, glide { "GLIDE", 0.0f, 1000.0f, 0.0f };
    Knob resonance { "RING", 0.0f, 1.0f, 0.7f }, brightness { "BRIGHT", 0.0f, 1.0f, 0.5f };
    Gem mono { "Mono" };
    Knob tune { "TUNE", -50.0f, 50.0f, 0.0f }, gain { "LEVEL", -24.0f, 12.0f, 0.0f };
    Pill leaveOut { "Leave out" };
    std::unique_ptr<juce::FileChooser> chooser;
    int auditionKey = -1; juce::uint32 auditionOff = 0;
    juce::Rectangle<int> srcPlate, mainPlate, insPlate, zoneBox, zoneTitle;
};

KeysRoom::KeysRoom (View& v) : view (v), parts (std::make_unique<Parts> (v))
{
    auto& p = *parts;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.sources, &p.zones, &p.map, &p.keys, &p.fromFile, &p.fromAll, &p.folder, &p.name, &p.build, &p.undo, &p.redo,
            &p.open, &p.save, &p.chordBox, &p.mode, &p.sustain, &p.attack, &p.release, &p.glide, &p.resonance, &p.brightness, &p.mono, &p.tune, &p.gain, &p.leaveOut })
        addAndMakeVisible (c);
    auto done = [this] { view.proc.keysMark(); refresh(); };
    auto rebuild = [this] { view.proc.keysBuild(); view.keysZone = -1; };

    // ---- the sounds
    p.fromFile.onClick = [this] { view.proc.kitGather (false); refresh(); };
    p.fromAll.onClick = [this] { view.proc.kitGather (true); refresh(); };
    p.folder.onClick = [this]
    {
        auto& pr = *parts;
        const auto settings = shared::Settings::load();
        pr.chooser = std::make_unique<juce::FileChooser> ("A folder of sounds", settings.importFolder.isNotEmpty() ? juce::File (settings.importFolder) : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult(); if (! dir.isDirectory()) return;
            auto s = shared::Settings::load(); s.importFolder = dir.getFullPathName(); s.save();
            view.proc.kitAddFolder (dir); refresh();
        });
    };
    p.fromFile.setTooltip ("Gather the one-shots of the open file into the pool"); p.fromAll.setTooltip ("Gather the one-shots of every file in the list"); p.folder.setTooltip ("Add a folder of sounds to the pool");
    p.sources.onPick = [this, rebuild] (const bc::LoopSource& src) { view.proc.keysSource = src; view.proc.keysLeftOut.clear(); rebuild(); refresh(); };
    p.zones.onChange = [this] { refresh(); };
    p.zones.onAudition = [this] (int key, float vel)
    {
        auto& pr = *parts;
        if (pr.auditionKey >= 0) view.proc.keyboard.noteOff (1, pr.auditionKey, 0.0f);
        view.proc.keyboard.noteOn (1, key, vel);
        pr.auditionKey = key; pr.auditionOff = juce::Time::getMillisecondCounter() + 450;
    };

    // ---- the instrument
    p.name.setTooltip ("The instrument's name: its file is called this");
    p.name.setFont (font (13.0f, true)); p.name.setJustification (juce::Justification::centredLeft);
    p.name.onTextChange = [this] { view.proc.keys.name = parts->name.getText().trim().toStdString(); view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    p.name.onReturnKey = [this] { unfocusAllComponents(); };
    p.build.setTooltip ("Build the instrument from the chosen sounds (B)");
    p.build.onClick = [this, rebuild] { rebuild(); refresh(); };
    p.undo.onClick = [this] { view.proc.keysUndo(); refresh(); }; p.redo.onClick = [this] { view.proc.keysRedo(); refresh(); };
    p.undo.setTooltip ("Undo (Ctrl+Z)"); p.redo.setTooltip ("Redo (Ctrl+Shift+Z)");
    p.save.setTooltip ("Save the instrument as a .NJQ file for MidiJaQ (in the Instruments folder beside NeoBeat's)");
    p.open.setTooltip ("Open a .NJQ instrument to play or change it");
    p.save.onClick = [this]
    {
        auto& pr = *parts;
        auto nm = juce::File::createLegalFileName (juce::String (view.proc.keys.name).trim()); if (nm.isEmpty()) nm = "My keys";
        pr.chooser = std::make_unique<juce::FileChooser> ("Save the instrument for MidiJaQ", shared::instrumentsFolder().getChildFile (nm + ".njq"), "*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            juce::String err; if (! view.proc.keysSaveNjq (f.withFileExtension ("njq"), err)) view.proc.keysMessage = "Not saved: " + err;
            refresh();
        });
    };
    p.open.onClick = [this]
    {
        auto& pr = *parts;
        pr.chooser = std::make_unique<juce::FileChooser> ("Open an instrument", shared::instrumentsFolder(), "*.njq");
        pr.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (! f.existsAsFile()) return;
            juce::String err; if (! view.proc.keysOpenNjq (f, err)) view.proc.keysMessage = "Not opened: " + err;
            view.keysZone = -1; refresh();
        });
    };
    for (int k = 0; k < BcProcessor::numChords(); ++k) p.chordBox.addItem (BcProcessor::chordName (k), k + 1);
    p.chordBox.setTooltip ("Each key plays a chord (the on-screen keys and a MIDI keyboard). In key: three notes of LOOPS' key, a third apart.");
    p.chordBox.onChange = [this] { view.proc.keysChord.store (juce::jlimit (0, BcProcessor::numChords() - 1, parts->chordBox.getSelectedId() - 1)); view.proc.stateChanged(); };
    p.keys.setAvailableRange (24, 108);
    p.keys.setLowestVisibleKey (36);
    p.keys.setKeyPressBaseOctave (5);
    p.keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C::glow().withAlpha (0.8f));
    p.keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::glow().withAlpha (0.3f));

    // ---- how it plays
    p.mode.setItemTooltip (0, "Tuned: pitched sounds spread over the keys, each covering the keys nearest its pitch");
    p.mode.setItemTooltip (1, "Resonator: any sound (a drill, a screech) struck into a resonator tuned to the key");
    p.mode.setItemTooltip (2, "Chops: a different sound on each key from C2 up, as it is");
    p.mode.onSelect = [this, rebuild] (int i) { view.proc.keys.mode = (bc::KeysMode) i; rebuild(); refresh(); };
    p.sustain.setItemTooltip (0, "As recorded: each sound plays out once");
    p.sustain.setItemTooltip (1, "Loop: a loop found in the sound's body holds it while the key is down");
    p.sustain.setItemTooltip (2, "Hold: the sound's body frozen while the key is down, as Friction Stretch holds a hit");
    p.sustain.onSelect = [this, done] (int i) { view.proc.keys.sustain = (bc::KeysSustain) i; view.proc.keysChanged(); done(); };
    auto ms = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String ((int) std::round (v)) + " ms"; };
    p.attack.toText = ms; p.release.toText = ms; p.glide.toText = ms;
    p.resonance.toText = [] (float v) { const double t = bc::resonatorRing (v); return t >= 1.0 ? juce::String (t, 1) + " s" : juce::String ((int) std::round (t * 1000.0)) + " ms"; };
    p.brightness.toText = [] (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    p.attack.onChange = [this] (float v) { view.proc.keys.attackMs = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    p.release.onChange = [this] (float v) { view.proc.keys.releaseMs = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    p.glide.onChange = [this] (float v) { view.proc.keys.glideMs = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    p.resonance.onChange = [this] (float v) { view.proc.keys.resonance = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    p.brightness.onChange = [this] (float v) { view.proc.keys.brightness = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; };
    for (auto* k : { &p.attack, &p.release, &p.glide, &p.resonance, &p.brightness }) k->onCommit = done;
    p.glide.setTooltip ("How long a new key takes to slide from the last one (most at home with MONO)");
    p.resonance.setTooltip ("Resonator: how long a note rings, high or low"); p.brightness.setTooltip ("Resonator: how bright it rings (dark: few overtones, and high notes ring a little shorter)");
    p.mono.setTooltip ("One note at a time; a new key glides from the held one");
    p.mono.onClick = [this, done] { view.proc.keys.mono = parts->mono.getToggleState(); view.proc.keysChanged(); done(); };

    // ---- the chosen sound
    auto zone = [this] () -> bc::KeyZone* { auto& Z = view.proc.keys.zones; return view.keysZone >= 0 && view.keysZone < (int) Z.size() ? &Z[(size_t) view.keysZone] : nullptr; };
    p.tune.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String ((int) std::round (v)) + " ct"; };
    p.gain.toText = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    p.tune.setTooltip ("Retune this sound, in cents (its pitch as heard is kept with the instrument)");
    p.tune.onChange = [this, zone] (float v) { if (auto* z = zone()) { z->root = std::round (z->root) - v / 100.0f; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; } };
    p.gain.onChange = [this, zone] (float v) { if (auto* z = zone()) { z->gainDb = v; view.proc.keysChanged(); seenVersion = view.proc.keysVersion; } };
    p.tune.onCommit = done; p.gain.onCommit = done;
    p.leaveOut.setTooltip ("Build the instrument again without this sound");
    p.leaveOut.onClick = [this, zone, rebuild] { if (auto* z = zone()) { view.proc.keysLeftOut.insert (z->sound); rebuild(); } refresh(); };
    p.map.onChange = [this] { refresh(); };
    startTimerHz (20);
}

KeysRoom::~KeysRoom() { stopTimer(); if (parts->auditionKey >= 0) view.proc.keyboard.noteOff (1, parts->auditionKey, 0.0f); }
juce::Component& KeysRoom::map() { return parts->map; }

void KeysRoom::refresh()
{
    auto& p = *parts; auto& proc = view.proc; const auto& ins = proc.keys;
    if (view.keysZone >= (int) ins.zones.size()) view.keysZone = -1;
    if (! p.name.hasKeyboardFocus (true)) p.name.setText (juce::String (ins.name), false);
    p.mode.setSelected ((int) ins.mode); p.sustain.setSelected ((int) ins.sustain);
    p.attack.setValue (ins.attackMs); p.release.setValue (ins.releaseMs); p.glide.setValue (ins.glideMs);
    p.resonance.setValue (ins.resonance); p.brightness.setValue (ins.brightness);
    p.resonance.setEnabled (ins.mode == bc::KeysMode::Resonator); p.brightness.setEnabled (ins.mode == bc::KeysMode::Resonator);
    p.mono.setToggleState (ins.mono, juce::dontSendNotification);
    p.chordBox.setSelectedId (proc.keysChord.load() + 1, juce::dontSendNotification);
    p.undo.setEnabled (proc.keysCanUndo()); p.redo.setEnabled (proc.keysCanRedo());
    p.save.setEnabled (! ins.zones.empty()); p.build.setEnabled (! proc.kit.pool.empty());
    const bool busy = proc.session.status().busy;
    p.fromFile.setEnabled (view.doc != nullptr && ! busy); p.fromAll.setEnabled (! proc.session.files.empty() && ! busy);
    const bc::KeyZone* z = view.keysZone >= 0 ? &ins.zones[(size_t) view.keysZone] : nullptr;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &p.tune, &p.gain, &p.leaveOut }) c->setEnabled (z != nullptr);
    p.tune.setEnabled (z != nullptr && ins.mode == bc::KeysMode::Tuned);
    if (z != nullptr) { p.tune.setValue ((std::round (z->root) - z->root) * 100.0f); p.gain.setValue (z->gainDb); }
    if (seenPool != (int) proc.kit.pool.size() || seenVersion != proc.keysVersion) { p.sources.rebuild(); p.zones.rebuild(); }
    seenPool = (int) proc.kit.pool.size(); seenVersion = proc.keysVersion;
    p.map.repaint();
    repaint();
}

void KeysRoom::timerCallback()
{
    auto& p = *parts;
    if (p.auditionKey >= 0 && juce::Time::getMillisecondCounter() >= p.auditionOff) { view.proc.keyboard.noteOff (1, p.auditionKey, 0.0f); p.auditionKey = -1; }
    if (! isVisible()) return;
    parts->map.repaint();
    if (view.proc.keysVersion != seenVersion || (int) view.proc.kit.pool.size() != seenPool) refresh();
}

bool KeysRoom::key (const juce::KeyPress& key)
{
    auto& proc = view.proc;
    const auto c = key.getKeyCode();
    const auto letter = c < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c) : 0;
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && letter == 'z') { if (mods.isShiftDown()) proc.keysRedo(); else proc.keysUndo(); refresh(); return true; }
    if (mods.isCommandDown() && letter == 'y') { proc.keysRedo(); refresh(); return true; }
    if (letter == 'b' && ! mods.isCommandDown()) { parts->build.triggerClick(); return true; }
    const int n = (int) proc.keys.zones.size();
    if ((c == juce::KeyPress::upKey || c == juce::KeyPress::downKey) && n > 0)
    {
        view.keysZone = view.keysZone < 0 ? 0 : (view.keysZone + (c == juce::KeyPress::downKey ? 1 : n - 1)) % n;
        refresh(); return true;
    }
    // the computer keyboard plays the on-screen keys
    return parts->keys.keyPressed (key);
}

bool KeysRoom::keyState (bool isKeyDown) { return parts->keys.keyStateChanged (isKeyDown); }
void KeysRoom::letGo()
{
    // the computer keys and the on-screen keys up; voices still sounding (from a MIDI keyboard too) let go
    parts->keys.focusLost (juce::Component::focusChangedDirectly);
    view.proc.keyboard.allNotesOff (0);
    view.proc.keysPlayer.letGo.store (true);
}

void KeysRoom::paint (juce::Graphics& g)
{
    auto& p = *parts; const auto& ins = view.proc.keys;
    for (auto r : { p.srcPlate, p.mainPlate, p.insPlate }) paintPlate (g, r.toFloat(), 12.0f);
    paintLabel (g, "Sounds", { p.srcPlate.getX() + 14, p.srcPlate.getY() + 8, 140, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "In the instrument", { p.zones.getX(), p.zones.getY() - 18, 200, 14 }, juce::Justification::centredLeft, 8.5f);
    paintLabel (g, "Keys", { p.mainPlate.getX() + 14, p.mainPlate.getY() + 10, 50, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "Chords", { p.chordBox.getX(), p.chordBox.getY() - 15, 80, 13 }, juce::Justification::centredLeft, 8.0f);
    paintLabel (g, "Instrument", { p.insPlate.getX() + 14, p.insPlate.getY() + 10, 140, 16 }, juce::Justification::centredLeft, 9.5f);
    paintLabel (g, "Sustain", { p.sustain.getX(), p.sustain.getY() - 15, 80, 13 }, juce::Justification::centredLeft, 8.0f);
    paintWell (g, p.zoneBox.toFloat(), 10.0f);
    const bc::KeyZone* z = view.keysZone >= 0 && view.keysZone < (int) ins.zones.size() ? &ins.zones[(size_t) view.keysZone] : nullptr;
    paintLabel (g, "Chosen sound", p.zoneTitle, juce::Justification::centredLeft, 8.5f);
    g.setFont (font (12.0f, true)); g.setColour (z != nullptr ? C::text() : C::dim());
    g.drawText (z != nullptr && z->sound ? juce::String::fromUTF8 (z->sound->name.c_str()) : juce::String ("Click a sound in the map or the list"), p.zoneTitle.translated (0, 18).withHeight (16), juce::Justification::centredLeft, true);
    if (z != nullptr && z->sound)
    {
        g.setFont (font (10.0f)); g.setColour (C::dim());
        juce::String a = ins.mode == bc::KeysMode::Tuned ? "pitch " + centsText (z->root) + ",  keys " + gen::noteName (z->lowKey) + " to " + gen::noteName (z->highKey)
                       : ins.mode == bc::KeysMode::Chops ? juce::String ("on ") + gen::noteName (z->lowKey) : juce::String ("an exciter for every key");
        juce::String b = "velocity " + juce::String (z->lowVel) + " to " + juce::String (z->highVel) + (z->rrCount > 1 ? ",  round robin " + juce::String (z->rr + 1) + " of " + juce::String (z->rrCount) : juce::String())
                       + (z->loopEnd > z->loopStart ? ",  loop " + juce::String (juce::roundToInt ((z->loopEnd - z->loopStart) * 1000.0 / z->sound->sr)) + " ms" : juce::String());
        g.drawText (a, p.zoneTitle.translated (0, 36).withHeight (14), juce::Justification::centredLeft, true);
        g.drawText (b, p.zoneTitle.translated (0, 50).withHeight (14), juce::Justification::centredLeft, true);
    }
    g.setFont (font (10.0f)); g.setColour (C::dim());
    g.drawFittedText ("Play from a MIDI keyboard, the keys below or your computer keyboard (A to K). SAVE .NJQ puts the instrument where MidiJaQ finds it.",
                      { p.insPlate.getX() + 14, p.zoneBox.getBottom() + 10, p.insPlate.getWidth() - 28, 60 }, juce::Justification::topLeft, 4);
}

void KeysRoom::resized()
{
    auto& p = *parts;
    p.srcPlate = { 12, 8, 300, 720 }; p.mainPlate = { 322, 8, 652, 720 }; p.insPlate = { 984, 8, 284, 720 };
    { const int x = p.srcPlate.getX() + 10, y = 34, w = (p.srcPlate.getWidth() - 20 - 6) / 3;
      p.fromFile.setBounds (x, y, w, 28); p.fromAll.setBounds (x + w + 3, y, w, 28); p.folder.setBounds (x + 2 * (w + 3), y, w, 28);
      p.sources.setBounds (x, y + 36, p.srcPlate.getWidth() - 20, 290);
      p.zones.setBounds (x, y + 352, p.srcPlate.getWidth() - 20, 330); }
    { const int x = p.mainPlate.getX();
      p.name.setBounds (x + 52, 12, 210, 26);
      p.build.setBounds (x + 272, 11, 74, 28); p.open.setBounds (x + 352, 11, 92, 28); p.save.setBounds (x + 450, 11, 92, 28);
      p.undo.setBounds (x + 550, 11, 46, 28); p.redo.setBounds (x + 600, 11, 46, 28);
      p.map.setBounds (x + 12, 48, p.mainPlate.getWidth() - 24, 470);
      p.chordBox.setBounds (x + 12, 548, 180, 28);
      p.keys.setBounds (x + 12, 588, p.mainPlate.getWidth() - 24, 128); }
    { const int x = p.insPlate.getX() + 14, w = p.insPlate.getWidth() - 28, k3 = w / 3;
      p.mode.setBounds (x, 36, w, 30);
      p.sustain.setBounds (x, 92, w, 30);
      p.attack.setBounds (x, 132, k3, 84); p.release.setBounds (x + k3, 132, k3, 84); p.glide.setBounds (x + 2 * k3, 132, k3, 84);
      p.resonance.setBounds (x, 222, k3, 84); p.brightness.setBounds (x + k3, 222, k3, 84); p.mono.setBounds (x + 2 * k3 + 6, 254, k3 - 6, 24);
      p.zoneBox = { x - 4, 318, w + 8, 236 };
      p.zoneTitle = { x + 8, 326, w - 16, 16 };
      p.tune.setBounds (x + w / 2 - 76, 400, 72, 84); p.gain.setBounds (x + w / 2 + 4, 400, 72, 84);
      p.leaveOut.setBounds (x + 8, 504, w - 16, 28); }
}
