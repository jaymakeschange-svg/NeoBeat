#include "PresetBar.h"

// The dropdown. A plain list, but it loads on hover rather than on click, which is the whole
// reason it is not a juce::PopupMenu: a popup gives no usable hover hook, and the audition is the
// feature.
class bc::PresetBar::List final : public juce::Component
{
public:
    List (PresetBar& o, const juce::Array<juce::File>& f, int selected)
        : owner (o), files (f), current (selected)
    {
        setSize (kWidth, std::min (kMaxRows, files.size()) * kRowH + 8);
        setWantsKeyboardFocus (true);
    }

    static constexpr int kWidth = 320, kRowH = 24, kMaxRows = 14;

    int rowAt (juce::Point<int> p) const
    {
        const int r = (p.y - 4) / kRowH + scroll;
        return juce::isPositiveAndBelow (r, files.size()) && p.y >= 4 ? r : -1;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        nsui::paintPlate (g, b, 8.0f);
        const int rows = std::min (kMaxRows, files.size());
        for (int i = 0; i < rows; ++i)
        {
            const int f = i + scroll;
            if (! juce::isPositiveAndBelow (f, files.size())) break;
            juce::Rectangle<int> r (4, 4 + i * kRowH, getWidth() - 8, kRowH);
            const bool isHover = f == hover, isCur = f == current;
            if (isHover) { g.setColour (nsui::C::teal().withAlpha (0.22f)); g.fillRoundedRectangle (r.toFloat(), 4.0f); }
            else if (isCur) { g.setColour (nsui::C::teal().withAlpha (0.10f)); g.fillRoundedRectangle (r.toFloat(), 4.0f); }
            g.setColour (isCur ? nsui::C::teal() : nsui::C::text());
            g.setFont (nsui::font (12.0f, isCur));
            g.drawText (files[f].getFileNameWithoutExtension(), r.reduced (8, 0), juce::Justification::centredLeft, true);
        }
        if (files.size() > kMaxRows)
        {
            g.setColour (nsui::C::dim());
            g.setFont (nsui::font (9.0f));
            g.drawText (juce::String (scroll + 1) + "-" + juce::String (std::min (files.size(), scroll + kMaxRows))
                        + " of " + juce::String (files.size()),
                        getLocalBounds().removeFromBottom (12).reduced (8, 0), juce::Justification::centredRight, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.getPosition());
        if (r == hover) return;
        hover = r;
        owner.hoverRow (r);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; owner.hoverRow (-1); repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.getPosition());
        if (r < 0) { owner.closeList (false); return; }
        current = r;
        owner.hoverRow (r);          // make sure the one clicked is the one loaded
        owner.closeList (true);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (files.size() <= kMaxRows) return;
        scroll = juce::jlimit (0, files.size() - kMaxRows, scroll - (int) std::round (w.deltaY * 4.0f));
        repaint();
    }

    PresetBar& owner;
    juce::Array<juce::File> files;
    int current = -1, hover = -1, scroll = 0;
};

// ------------------------------------------------------------------

bc::PresetBar::PresetBar()
{
    for (auto* b : { &prev, &next })
    {
        addAndMakeVisible (*b);
        b->setTooltip ("The previous or next preset in the folder, loaded as you step");
    }
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (+1); };
}

bc::PresetBar::~PresetBar() { closeList (false); }

void bc::PresetBar::setFolder (const juce::File& dir, const juce::String& wildcard)
{
    folder = dir;
    pattern = wildcard;
    rescan();
}

void bc::PresetBar::rescan()
{
    const auto was = current >= 0 && current < files.size() ? files[current] : juce::File();
    files.clear();
    if (folder.isDirectory())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, pattern))
            files.add (f);
    // by name, so stepping is the order you see in the list and in the folder
    files.sort();
    current = was != juce::File() ? files.indexOf (was) : -1;
    repaint();
}

juce::String bc::PresetBar::currentName() const
{
    if (message.isNotEmpty()) return message;
    if (current < 0 || current >= files.size()) return files.isEmpty() ? "no presets in the folder" : "none loaded";
    return files[current].getFileNameWithoutExtension();
}

bool bc::PresetBar::loadIndex (int i)
{
    if (! juce::isPositiveAndBelow (i, files.size())) return false;
    if (onLoad == nullptr) return false;
    const bool ok = onLoad (files[i]);
    if (ok) { current = i; message.clear(); }
    else      message = files[i].getFileNameWithoutExtension() + " would not open";
    repaint();
    return ok;
}

void bc::PresetBar::step (int by)
{
    if (files.isEmpty()) { message = "no presets in the folder"; repaint(); return; }
    // Stepping is a commitment, not a browse, so whatever a hover left behind is finished with.
    before.reset();
    const int n = files.size();
    int at = current < 0 ? (by > 0 ? -1 : 0) : current;
    at = ((at + by) % n + n) % n;      // wraps, so holding an arrow goes round the folder
    if (loadIndex (at) && onAudition != nullptr) { onAudition(); auditioning = true; startTimer (1200); }
}

void bc::PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (! prev.getBounds().contains (e.getPosition()) && ! next.getBounds().contains (e.getPosition()))
        list == nullptr ? openList() : closeList (false);
}

void bc::PresetBar::openList()
{
    rescan();
    if (files.isEmpty()) { message = "no presets in the folder"; repaint(); return; }

    // What the room holds right now, so moving the mouse away puts it back. Without a snapshot the
    // bar still works; it just does not preview, because previewing would be a way to lose work.
    before = onSnapshot != nullptr ? onSnapshot() : nullptr;

    list = std::make_unique<List> (*this, files, current);
    auto& box = juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (list.get()),
                                                        getScreenBounds(), nullptr);
    box.setDismissalMouseClicksAreAlwaysConsumed (false);
}

void bc::PresetBar::closeList (bool commit)
{
    if (list == nullptr) return;
    auto* l = list.release();          // the call-out box owns it
    if (auto* box = l->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();

    hovered = -1;
    if (auditioning && onAuditionStop != nullptr) onAuditionStop();
    auditioning = false;
    stopTimer();

    if (! commit && before != nullptr && onRestore != nullptr)
    {
        onRestore (*before);           // browsing cost nothing
        repaint();
    }
    before.reset();
}

void bc::PresetBar::hoverRow (int row)
{
    if (row == hovered) return;
    hovered = row;
    if (row < 0) return;               // off the list: leave what was last previewed until it closes
    if (loadIndex (row) && onAudition != nullptr)
    {
        if (auditioning && onAuditionStop != nullptr) onAuditionStop();
        onAudition();
        auditioning = true;
        startTimer (1200);
    }
}

void bc::PresetBar::timerCallback()
{
    stopTimer();
    if (auditioning && onAuditionStop != nullptr) onAuditionStop();
    auditioning = false;
}

void bc::PresetBar::resized()
{
    auto r = getLocalBounds();
    prev.setBounds (r.removeFromLeft (24).reduced (0, 2));
    next.setBounds (r.removeFromRight (24).reduced (0, 2));
}

void bc::PresetBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (26.0f, 0.0f);
    nsui::paintLcd (g, r, 6.0f);
    g.setColour (message.isNotEmpty() ? nsui::C::amber() : nsui::C::lcdText());
    g.setFont (nsui::font (12.0f));
    g.drawText (currentName(), r.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

    if (files.size() > 1 && current >= 0 && message.isEmpty())
    {
        g.setColour (nsui::C::lcdDim());
        g.setFont (nsui::font (9.0f));
        g.drawText (juce::String (current + 1) + "/" + juce::String (files.size()),
                    r.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, false);
    }
}
