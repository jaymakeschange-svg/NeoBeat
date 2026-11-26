// MidiJaQ's window.
//
// Seven rooms along the top. CUT, LOOPS and KEYS are Batchcut's, running the same code rather than
// a copy of it. MAP, MOD, RANDOM and the Stereoroom are MidiJaQ's own.

#include "Editor.h"
#include "CutRoom.h"
#include "MapRoom.h"
#include "ModRoom.h"
#include "RandomRoom.h"
#include "StereoRoomUi.h"

using namespace mjqui;

namespace
{
const juce::StringArray kRoomNames { "CUT", "LOOPS", "KEYS", "MAP", "MOD", "RANDOM", "STEREOROOM" };

const char* kRoomTips[] = {
    "Recordings in, and cut into sounds.",
    "Those sounds as loops of chops.",
    "Those sounds as an instrument across the keys.",
    "One grid where loops and keys play together.",
    "The Orb, the shapes, and where they go.",
    "Rolls the whole instrument while you play.",
    "Where every chop stands, and how it moves."
};
}

struct MjqEditor::Parts
{
    explicit Parts (MjqEditor& e)
        : rooms ({ "CUT", "LOOPS", "KEYS", "MAP", "MOD", "RANDOM", "STEREOROOM" }),
          cut (e), loops (e), keys (e), map (e), mod (e), random (e), stage (e)
    {
        juce::ignoreUnused (e);
    }

    Segmented rooms;

    // the header
    juce::Label title { {}, "MIDIJAQ" };
    juce::Label presetLabel { {}, "PRESET" };
    juce::Label presetName { {}, "(none)" };
    Pill presetPrev { "<" }, presetNext { ">" }, presetBrowse { "BROWSE" };
    juce::Label readout { {}, "" };

    // the rooms themselves
    CutRoom cut;
    bc::LoopsRoom loops;
    bc::KeysRoom keys;
    MapRoom map;
    ModRoom mod;
    RandomRoom random;
    StereoRoomUi stage;
};

MjqEditor::MjqEditor (MjqProcessor& p)
    : juce::AudioProcessorEditor (&p), bc::RoomView (p), proc (p)
{
    setLookAndFeel (&look);
    nsui::setSkin (nsui::lookFor (nsui::skins::chromePearl()));

    parts = std::make_unique<Parts> (*this);
    auto& q = *parts;

    addAndMakeVisible (q.rooms);
    q.rooms.onSelect = [this] (int i) { showRoom (i); };
    for (int i = 0; i < kRoomNames.size(); ++i) q.rooms.setItemTooltip (i, kRoomTips[i]);

    for (auto* l : { &q.title, &q.presetLabel, &q.presetName, &q.readout })
        addAndMakeVisible (l);
    q.title.setJustificationType (juce::Justification::centredLeft);
    q.readout.setJustificationType (juce::Justification::centredRight);

    for (auto* b : { &q.presetPrev, &q.presetNext, &q.presetBrowse }) addAndMakeVisible (b);
    q.presetPrev.setTooltip ("The preset before this one, straight away.");
    q.presetNext.setTooltip ("The next preset, straight away.");
    q.presetBrowse.setTooltip ("Every preset, auditioned as you move over them.");

    for (auto* c : std::initializer_list<juce::Component*> { &q.cut, &q.loops, &q.keys, &q.map, &q.mod, &q.random, &q.stage })
        addChildComponent (c);

    // what the rooms call when something they changed needs to show
    repaintAll = [this] { repaint(); };
    refreshPanels = [this] { refresh(); };
    roomChanged = [this] { showRoom (room); };

    proc.addChangeListener (this);
    showRoom (1);                       // LOOPS: where there is usually something to hear
    setSize (1280, 800);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MjqEditor::~MjqEditor()
{
    proc.removeChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void MjqEditor::showRoom (int which)
{
    room = juce::jlimit (0, kRoomNames.size() - 1, which);
    auto& q = *parts;
    q.rooms.setSelected (room);

    juce::Component* all[] = { &q.cut, &q.loops, &q.keys, &q.map, &q.mod, &q.random, &q.stage };
    for (int i = 0; i < 7; ++i) all[i]->setVisible (i == room);

    // a key held in one room is let go on leaving it, or it rings on in the next
    q.keys.letGo();
    resized();
    refresh();
}

void MjqEditor::refresh()
{
    auto& q = *parts;

    const auto& s = proc.session;
    q.presetName.setText (! proc.keys.name.empty() ? juce::String (proc.keys.name)
                                                   : juce::String (s.files.empty() ? "(nothing loaded)" : "(unsaved)"),
                          juce::dontSendNotification);

    juce::String r;
    if (proc.lastMessage.isNotEmpty()) r = proc.lastMessage;
    else r << (int) proc.kit.pool.size() << " sounds";
    q.readout.setText (r, juce::dontSendNotification);

    switch (room)
    {
        case 0: q.cut.refresh(); break;
        case 1: q.loops.refresh(); break;
        case 2: q.keys.refresh(); break;
        case 3: q.map.refresh(); break;
        case 4: q.mod.refresh(); break;
        case 5: q.random.refresh(); break;
        case 6: q.stage.refresh(); break;
        default: break;
    }
    repaint();
}

void MjqEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void MjqEditor::timerCallback()
{
    auto& q = *parts;
    switch (room)
    {
        case 3: q.map.tick(); break;
        case 6: q.stage.tick(); break;
        default: break;
    }
}

void MjqEditor::paint (juce::Graphics& g)
{
    paintChassis (g, getLocalBounds().toFloat());
    auto header = getLocalBounds().removeFromTop (64);
    paintPlate (g, header.toFloat().reduced (1.0f));
}

void MjqEditor::resized()
{
    auto& q = *parts;
    auto r = getLocalBounds();

    auto header = r.removeFromTop (64).reduced (16, 0);
    q.title.setBounds (header.removeFromLeft (120).withSizeKeepingCentre (120, 26));
    header.removeFromLeft (8);
    q.presetLabel.setBounds (header.removeFromLeft (52).withSizeKeepingCentre (52, 20));
    q.presetName.setBounds (header.removeFromLeft (230).withSizeKeepingCentre (230, 26));
    header.removeFromLeft (6);
    q.presetPrev.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 26));
    header.removeFromLeft (3);
    q.presetNext.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 26));
    header.removeFromLeft (6);
    q.presetBrowse.setBounds (header.removeFromLeft (84).withSizeKeepingCentre (84, 26));
    q.readout.setBounds (header.withSizeKeepingCentre (header.getWidth(), 22));

    auto strip = r.removeFromTop (44).reduced (16, 7);
    q.rooms.setBounds (strip.removeFromLeft (660));

    const auto body = r.reduced (10);
    for (auto* c : std::initializer_list<juce::Component*> { &q.cut, &q.loops, &q.keys, &q.map, &q.mod, &q.random, &q.stage })
        c->setBounds (body);
}

bool MjqEditor::keyPressed (const juce::KeyPress& k)
{
    auto& q = *parts;

    // the rooms, by number
    for (int i = 0; i < kRoomNames.size(); ++i)
        if (k.isKeyCode ('1' + i)) { showRoom (i); return true; }

    switch (room)
    {
        case 0: return q.cut.key (k);
        case 1: return q.loops.key (k);
        case 2: return q.keys.key (k);
        case 3: return q.map.key (k);
        case 5: return q.random.key (k);
        case 6: return q.stage.key (k);
        default: break;
    }
    return false;
}

static_assert (std::is_convertible<MjqEditor*, juce::FileDragAndDropTarget*>::value,
               "MjqEditor must inherit FileDragAndDropTarget publicly or JUCE cannot see it");

bool MjqEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory() || MjqProcessor::isAudioFile (file)) return true;
        // the things MidiJaQ opens whole, so dropping one of those works too
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".njq" || ext == ".njqi" || ext == ".njqloop") return true;
    }
    return false;
}

void MjqEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    repaint();

    // A preset dropped on its own goes straight in, whichever room is showing: that is what
    // dropping one means, and there is nothing else it could mean.
    if (files.size() == 1)
    {
        const juce::File one (files[0]);
        const auto ext = one.getFileExtension().toLowerCase();
        juce::String error;
        if (ext == ".njqloop")
        {
            if (proc.loopsOpenPreset (one, error)) { proc.lastMessage = "Opened " + one.getFileName(); showRoom (1); }
            else                                    proc.lastMessage = error;
            refresh();
            return;
        }
        if (ext == ".njqi")
        {
            if (proc.keysOpenNjq (one, error)) { proc.lastMessage = "Opened " + one.getFileName(); showRoom (2); }
            else                                proc.lastMessage = error;
            refresh();
            return;
        }
    }

    // Audio, and where it should land. In CUT the drop is the raw file list - that is the room's
    // whole job. Everywhere else the sounds go into the pool, ready to be rolled into a loop or
    // built into an instrument, which is what you came to those rooms to do.
    if (room == 0)
    {
        const int n = proc.addPaths (files);
        proc.lastMessage = n > 0 ? "Added " + juce::String (n) + (n == 1 ? " file" : " files")
                                 : "Nothing new to add (no audio files, or already in the list)";
        refresh();
        return;
    }

    int added = 0;
    juce::StringArray seen;
    for (const auto& f : files)
    {
        const juce::File file (f);
        const juce::File dir = file.isDirectory() ? file : file.getParentDirectory();
        if (seen.contains (dir.getFullPathName())) continue;
        seen.add (dir.getFullPathName());
        added += proc.kitAddFolder (dir);
    }
    proc.lastMessage = added > 0 ? "Added " + juce::String (added) + (added == 1 ? " sound" : " sounds") + " to the pool"
                                 : "Nothing new to add (no audio files, or already in the pool)";
    refresh();
}

void MjqEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dropping) return;
    g.setColour (nsui::C::teal().withAlpha (0.16f));
    g.fillRect (getLocalBounds());
    g.setColour (nsui::C::teal());
    g.drawRect (getLocalBounds(), 3);
    g.setFont (nsui::font (18.0f * (float) getWidth() / 1280.0f, true));
    g.setColour (nsui::C::text());
    g.drawText (room == 0 ? "DROP AUDIO FILES OR FOLDERS" : "DROP SOUNDS, FOLDERS OR A PRESET",
                getLocalBounds(), juce::Justification::centred);
}

bool MjqEditor::keyStateChanged (bool isKeyDown)
{
    return room == 2 ? parts->keys.keyState (isKeyDown) : false;
}
