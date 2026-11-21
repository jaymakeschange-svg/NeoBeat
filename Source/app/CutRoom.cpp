#include "CutRoom.h"
#include "../neobeat/shared/Ui.h"

using namespace nsui;

namespace
{
juce::String shortName (const std::string& path)
{
    return juce::File (juce::String (path)).getFileName();
}
}

// ---------------------------------------------------------------- the file list
class FileList final : public juce::Component
{
public:
    explicit FileList (bc::RoomView& v) : view (v) {}

    std::function<void()> onPick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        paintWell (g, r);

        const auto& files = view.rooms.session.files;
        if (files.empty())
        {
            paintLabel (g, "Drop recordings here, or use ADD", getLocalBounds().reduced (12),
                        juce::Justification::centredTop, 11.0f, C::dim());
            return;
        }

        const auto d = view.rooms.session.doc();
        const int openRow = d ? d->file : -1;

        auto body = getLocalBounds().reduced (5);
        for (int i = 0; i < (int) files.size(); ++i)
        {
            if (body.getHeight() < kRow) break;
            auto row = body.removeFromTop (kRow);
            const auto& f = files[(size_t) i];

            if (i == openRow)
            {
                g.setColour (C::teal().withAlpha (0.18f));
                g.fillRoundedRectangle (row.toFloat().reduced (1.0f), 3.0f);
            }

            auto text = row.reduced (7, 0);
            auto right = text.removeFromRight (74);

            g.setColour (i == openRow ? C::text() : C::label());
            g.setFont (font (11.0f));
            g.drawText (shortName (f.info.path), text, juce::Justification::centredLeft, true);

            juce::String note;
            juce::Colour noteColour = C::dim();
            if (! f.error.empty()) { note = "failed"; noteColour = C::red(); }
            else
            {
                int events = 0; bool analysed = false;
                for (int c : f.counts) if (c >= 0) { events += c; analysed = true; }
                note = analysed ? (juce::String (events) + " sounds") : juce::String ("...");
                if (analysed) noteColour = C::label();
            }
            g.setColour (noteColour);
            g.setFont (font (10.0f));
            g.drawText (note, right, juce::Justification::centredRight, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i < 0 || i >= (int) view.rooms.session.files.size()) return;
        view.rooms.open (i, 0);
        if (onPick) onPick();
    }

    int rowAt (int y) const
    {
        const int i = (y - 5) / kRow;
        return i >= 0 ? i : -1;
    }

    int selected() const
    {
        const auto d = view.rooms.session.doc();
        return d ? d->file : -1;
    }

private:
    static constexpr int kRow = 24;
    bc::RoomView& view;
};

// ---------------------------------------------------------------- the pool, as the other rooms will see it
class PoolStrip final : public juce::Component
{
public:
    explicit PoolStrip (bc::RoomView& v) : view (v) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        paintWell (g, r.toFloat());

        const auto& pool = view.rooms.kit.pool;
        if (pool.empty())
        {
            paintLabel (g, "Nothing cut yet. Open a recording and the sounds in it land here.",
                        r.reduced (12), juce::Justification::centredLeft, 11.0f, C::dim());
            return;
        }

        auto body = r.reduced (6);
        const int n = (int) pool.size();
        const int cols = juce::jmax (1, body.getWidth() / 86);
        const int rows = juce::jmax (1, body.getHeight() / 30);
        const int shown = juce::jmin (n, cols * rows);

        for (int i = 0; i < shown; ++i)
        {
            const int cx = i % cols, cy = i / cols;
            juce::Rectangle<int> cell (body.getX() + cx * 86, body.getY() + cy * 30, 82, 26);
            const auto& it = pool[(size_t) i];
            paintTile (g, cell.toFloat(), C::teal().withAlpha (0.22f),
                       juce::String (it->name).substring (0, 10), false);
        }

        if (shown < n)
            paintLabel (g, "+ " + juce::String (n - shown) + " more",
                        r.removeFromBottom (18).reduced (8, 0), juce::Justification::centredRight, 10.0f, C::dim());
    }

private:
    bc::RoomView& view;
};

// ---------------------------------------------------------------- the room
struct CutRoom::Parts
{
    explicit Parts (bc::RoomView& v) : files (v), pool (v) {}

    FileList files;
    PoolStrip pool;

    Pill add { "ADD" }, folder { "FOLDER" }, remove { "REMOVE" }, clearAll { "CLEAR ALL" };
    Pill gather { "CUT THEM ALL INTO THE POOL" };
    juce::Label status { {}, "" };
    std::unique_ptr<juce::FileChooser> chooser;
};

CutRoom::CutRoom (bc::RoomView& v) : view (v)
{
    parts = std::make_unique<Parts> (v);
    auto& q = *parts;

    addAndMakeVisible (q.files);
    addAndMakeVisible (q.pool);
    addAndMakeVisible (q.status);
    for (auto* b : { &q.add, &q.folder, &q.remove, &q.clearAll, &q.gather }) addAndMakeVisible (b);

    q.files.onPick = [this] { if (view.refreshPanels) view.refreshPanels(); repaint(); };

    q.add.setTooltip ("Recordings to cut. You can also drop them anywhere in this room.");
    q.add.onClick = [this]
    {
        auto& p = *parts;
        p.chooser = std::make_unique<juce::FileChooser> ("Recordings to cut", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        // canSelectFiles is not optional. Without it - and without canSelectDirectories - JUCE's
        // browser has nothing it is allowed to show, so the list comes up empty in a folder full
        // of wavs and the button looks broken. It was missing here and nowhere else: every other
        // chooser in the plugin came from Batchcut and had it.
        p.chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this] (const juce::FileChooser& fc)
        {
            juce::StringArray paths;
            for (const auto& f : fc.getResults()) paths.add (f.getFullPathName());
            if (! paths.isEmpty()) view.rooms.addPaths (paths);
            refresh();
        });
    };

    q.folder.setTooltip ("Every recording in a folder.");
    q.folder.onClick = [this]
    {
        auto& p = *parts;
        p.chooser = std::make_unique<juce::FileChooser> ("A folder of recordings", juce::File());
        p.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.isDirectory()) view.rooms.addPaths ({ f.getFullPathName() });
            refresh();
        });
    };

    q.remove.setTooltip ("Takes the open recording off the list. The sounds already cut from it stay.");
    q.remove.onClick = [this]
    {
        const int i = parts->files.selected();
        if (i >= 0) view.rooms.removeFiles ({ i });
        refresh();
    };

    // Clear all belongs where the samples are listed, not beside the preset controls
    q.clearAll.setTooltip ("Every recording off the list, and the pool emptied. Sounds already in LOOPS or KEYS stay.");
    q.clearAll.onClick = [this]
    {
        view.rooms.clearFiles();
        view.rooms.kitClearPool();
        refresh();
    };

    q.gather.setTooltip ("Cuts every recording on the list and puts the sounds in the pool, for LOOPS and KEYS to use.");
    q.gather.onClick = [this]
    {
        view.rooms.kitGather (true);
        refresh();
    };

    q.status.setJustificationType (juce::Justification::centredLeft);
    startTimerHz (8);
}

CutRoom::~CutRoom() { stopTimer(); }

void CutRoom::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintPlate (g, r);

    auto body = getLocalBounds().reduced (10);
    body.removeFromTop (30);                     // the buttons
    auto left = body.removeFromLeft (330);
    paintLabel (g, "RECORDINGS", left.removeFromTop (16), juce::Justification::centredLeft);
    body.removeFromLeft (10);
    paintLabel (g, "THE POOL", body.removeFromTop (16), juce::Justification::centredLeft);
}

void CutRoom::resized()
{
    auto& q = *parts;
    auto r = getLocalBounds().reduced (10);

    auto bar = r.removeFromTop (26);
    q.add.setBounds (bar.removeFromLeft (64));      bar.removeFromLeft (4);
    q.folder.setBounds (bar.removeFromLeft (74));   bar.removeFromLeft (4);
    q.remove.setBounds (bar.removeFromLeft (80));   bar.removeFromLeft (4);
    q.clearAll.setBounds (bar.removeFromLeft (92));
    bar.removeFromLeft (14);
    q.status.setBounds (bar);
    r.removeFromTop (4);

    auto left = r.removeFromLeft (330);
    left.removeFromTop (16);
    q.files.setBounds (left);

    r.removeFromLeft (10);
    r.removeFromTop (16);
    q.gather.setBounds (r.removeFromBottom (30).removeFromLeft (260));
    r.removeFromBottom (6);
    q.pool.setBounds (r);
}

void CutRoom::refresh()
{
    auto& q = *parts;
    const auto st = view.rooms.session.status();
    q.status.setText (st.busy ? juce::String (st.what) : juce::String (view.rooms.lastMessage),
                      juce::dontSendNotification);
    q.remove.setEnabled (q.files.selected() >= 0);
    q.clearAll.setEnabled (! view.rooms.session.files.empty() || ! view.rooms.kit.pool.empty());
    q.gather.setEnabled (! view.rooms.session.files.empty() && ! st.busy);
    repaint();
}

void CutRoom::timerCallback()
{
    const auto st = view.rooms.session.status();
    const int files = (int) view.rooms.session.files.size();
    const int pool = (int) view.rooms.kit.pool.size();
    if (files != seenFiles || pool != seenPool || st.busy != wasBusy)
    {
        seenFiles = files; seenPool = pool; wasBusy = st.busy;
        refresh();
    }
}

bool CutRoom::key (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        const int i = parts->files.selected();
        if (i >= 0) { view.rooms.removeFiles ({ i }); refresh(); return true; }
    }
    return false;
}

// The guard against that mistake returning, here and in every other drop target: this is the same
// conversion JUCE's dynamic_cast needs, so if the inheritance is ever made private again the build
// stops instead of the feature.
static_assert (std::is_convertible<CutRoom*, juce::FileDragAndDropTarget*>::value,
               "CutRoom must inherit FileDragAndDropTarget publicly or JUCE cannot see it");

bool CutRoom::isInterestedInFileDrag (const juce::StringArray&) { return true; }

void CutRoom::filesDropped (const juce::StringArray& files, int, int)
{
    view.rooms.addPaths (files);
    refresh();
}
