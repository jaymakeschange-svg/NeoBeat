#include "MaterialRoom.h"

// The list of materials found in the folder.
struct MaterialRoom::Rows final : public juce::ListBoxModel
{
    explicit Rows (MaterialRoom& o) : owner (o) {}

    int getNumRows() override { return owner.files.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (! juce::isPositiveAndBelow (row, owner.files.size())) return;
        if (sel) { g.setColour (nsui::C::teal().withAlpha (0.18f)); g.fillRoundedRectangle (juce::Rectangle<int> (2, 1, w - 4, h - 2).toFloat(), 4.0f); }
        g.setColour (row == owner.current ? nsui::C::teal() : nsui::C::text());
        g.setFont (nsui::font (13.0f, row == owner.current));
        g.drawText (owner.files[row].getFileNameWithoutExtension(), 10, 0, w - 16, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner.loadRow (row); }

    MaterialRoom& owner;
};

// ------------------------------------------------------------------

MaterialRoom::MaterialRoom (BcProcessor& p, std::function<void()> changed)
    : proc (p), onChanged (std::move (changed))
{
    rows = std::make_unique<Rows> (*this);
    list.setModel (rows.get());
    list.setRowHeight (24);
    addAndMakeVisible (list);

    for (auto* b : { &folderBtn, &strike, &render, &through, &prev, &next }) addAndMakeVisible (*b);
    addAndMakeVisible (poolBox);
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (message);

    folderBtn.setTooltip ("The folder holding the learner's material files (the .json files it writes)");
    strike.setTooltip ("Hit the object. Hit it again while it is ringing and the energy adds, the way a real body behaves");
    render.setTooltip ("Strike it eight times at different strengths and put every hit in the pool - no two the same");
    through.setTooltip ("Send the chosen pool sound through this material and add the result to the pool");

    mixKnob.setRange (0.0, 1.0, 0.01);
    mixKnob.setValue (1.0, juce::dontSendNotification);
    mixKnob.setTooltip ("How much of the material you hear against the original sound");

    message.setJustificationType (juce::Justification::centredLeft);
    message.setFont (nsui::font (12.0f));
    message.setColour (juce::Label::textColourId, nsui::C::dim());

    folderBtn.onClick = [this]
    {
        auto start = folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        auto chooser = std::make_shared<juce::FileChooser> ("The folder with your material files", start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto d = fc.getResult();
                                  if (d.isDirectory()) openFolder (d);
                              });
    };

    prev.onClick = [this] { if (! files.isEmpty()) loadRow (current <= 0 ? files.size() - 1 : current - 1); };
    next.onClick = [this] { if (! files.isEmpty()) loadRow (current >= files.size() - 1 ? 0 : current + 1); };

    strike.onClick = [this]
    {
        if (! proc.material.valid()) { say ("Open a material first."); return; }
        proc.materialPending.store (0.55f + 0.4f * juce::Random::getSystemRandom().nextFloat());
    };

    render.onClick = [this]
    {
        juce::String error;
        const int n = proc.materialRender (8, error);
        say (n > 0 ? proc.lastMessage : error);
        if (onChanged) onChanged();
        refresh();
    };

    through.onClick = [this]
    {
        const int idx = poolBox.getSelectedId() - 1;
        juce::String error;
        if (proc.materialThrough (idx, (float) mixKnob.getValue(), error)) say (proc.lastMessage);
        else                                                              say (error);
        if (onChanged) onChanged();
        refresh();
    };

    startTimerHz (8);
}

MaterialRoom::~MaterialRoom() { list.setModel (nullptr); }

void MaterialRoom::say (const juce::String& s) { message.setText (s, juce::dontSendNotification); }

void MaterialRoom::openFolder (const juce::File& dir)
{
    folder = dir;
    files.clear();
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.json"))
        files.add (f);
    files.sort();
    current = -1;
    list.updateContent();
    say (files.isEmpty() ? "No material files in that folder."
                         : juce::String (files.size()) + (files.size() == 1 ? " material" : " materials") + " - click one to load it");
    refresh();
}

void MaterialRoom::loadRow (int row)
{
    if (! juce::isPositiveAndBelow (row, files.size())) return;
    juce::String error;
    if (proc.materialOpen (files[row], error)) { current = row; say (proc.lastMessage); }
    else                                         say (error);
    list.updateContent();
    refresh();          // the buttons only come alive once something is loaded - refresh, not repaint
}

void MaterialRoom::refresh()
{
    const int keep = poolBox.getSelectedId();
    poolBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) proc.kit.pool.size(); ++i)
        poolBox.addItem (juce::String (proc.kit.pool[(size_t) i]->name), i + 1);
    if (keep > 0 && keep <= (int) proc.kit.pool.size()) poolBox.setSelectedId (keep, juce::dontSendNotification);
    else if (! proc.kit.pool.empty())                   poolBox.setSelectedId (1, juce::dontSendNotification);

    const bool has = proc.material.valid();
    strike.setEnabled (has);
    render.setEnabled (has);
    through.setEnabled (has && ! proc.kit.pool.empty());
    repaint();
}

void MaterialRoom::timerCallback()
{
    if (isShowing()) proc.materialLive.store (true);
}

bool MaterialRoom::isInterestedInFileDrag (const juce::StringArray& files_)
{
    for (const auto& f : files_)
    {
        const juce::File file (f);
        if (file.isDirectory() || file.hasFileExtension ("json")) return true;
    }
    return false;
}

void MaterialRoom::filesDropped (const juce::StringArray& files_, int, int)
{
    dropping = false;
    if (files_.isEmpty()) { repaint(); return; }
    const juce::File f (files_[0]);
    openFolder (f.isDirectory() ? f : f.getParentDirectory());
    if (! f.isDirectory())
    {
        const int row = files.indexOf (f);
        if (row >= 0) loadRow (row);
    }
}

// ------------------------------------------------------------------ drawing

void MaterialRoom::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto left = r.removeFromLeft (260);
    auto head = left.removeFromTop (30);
    folderBtn.setBounds (head.removeFromLeft (100).reduced (0, 2));
    next.setBounds (head.removeFromRight (30).reduced (0, 2));
    prev.setBounds (head.removeFromRight (30).reduced (2, 2));
    left.removeFromTop (6);
    list.setBounds (left);

    r.removeFromLeft (14);
    auto bottom = r.removeFromBottom (92);
    message.setBounds (r.removeFromBottom (22));

    auto row1 = bottom.removeFromTop (32);
    strike.setBounds (row1.removeFromLeft (120).reduced (0, 2));
    row1.removeFromLeft (8);
    render.setBounds (row1.removeFromLeft (200).reduced (0, 2));

    bottom.removeFromTop (8);
    auto row2 = bottom.removeFromTop (34);
    through.setBounds (row2.removeFromLeft (130).reduced (0, 2));
    row2.removeFromLeft (8);
    mixKnob.setBounds (row2.removeFromLeft (34));
    row2.removeFromLeft (8);
    poolBox.setBounds (row2.reduced (0, 4));
}

void MaterialRoom::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromLeft (274);
    auto face = r.removeFromTop (r.getHeight() - 114).toFloat();
    nsui::paintPlate (g, face, 8.0f);

    if (! proc.material.valid())
    {
        g.setColour (nsui::C::dim());
        g.setFont (nsui::font (14.0f));
        g.drawText ("Open a folder of material files, or drop one here",
                    face.toNearestInt(), juce::Justification::centred, false);
    }
    else
    {
        // The object, drawn as what it is: its modes. Height is how loud each one rings, and the
        // colour how long it rings for - so a bright tall forest is a bell and a dark short one
        // is a thud. This is the same table the sound reads from, not a picture of a waveform.
        const auto& m = proc.material;
        auto plot = face.reduced (18.0f, 26.0f);

        g.setColour (nsui::C::text());
        g.setFont (nsui::font (15.0f, true));
        g.drawText (m.name, face.reduced (14.0f, 6.0f).removeFromTop (20.0f).toNearestInt(),
                    juce::Justification::topLeft, false);
        g.setColour (nsui::C::dim());
        g.setFont (nsui::font (11.0f));
        g.drawText (juce::String (m.numModes) + " modes, rings " + juce::String (m.ringSeconds(), 2) + " s",
                    face.reduced (14.0f, 6.0f).removeFromTop (20.0f).toNearestInt(),
                    juce::Justification::topRight, false);

        float loudest = 1.0e-6f, slowest = 1.0e-6f;
        for (int i = 0; i < m.numModes; ++i)
        {
            loudest = std::max (loudest, m.a[(size_t) i]);
            slowest = std::max (slowest, 1.0f / std::max (0.01f, m.sigmaOf (i)));
        }

        for (int i = 0; i < m.numModes; ++i)
        {
            const float hz = m.f[(size_t) i];
            if (hz < 20.0f || hz > 20000.0f) continue;
            // log frequency across, so the low end is readable
            const float x = plot.getX() + plot.getWidth()
                          * (std::log (hz / 20.0f) / std::log (1000.0f));
            const float h = plot.getHeight() * std::sqrt (m.a[(size_t) i] / loudest);
            const float ring = (1.0f / std::max (0.01f, m.sigmaOf (i))) / slowest;
            g.setColour (nsui::C::teal().withAlpha (0.25f + 0.65f * std::sqrt (juce::jlimit (0.0f, 1.0f, ring))));
            g.fillRect (juce::Rectangle<float> (x, plot.getBottom() - h, 1.6f, h));
        }
    }

    if (dropping)
    {
        g.setColour (nsui::C::teal().withAlpha (0.16f));
        g.fillRect (getLocalBounds());
        g.setColour (nsui::C::teal());
        g.drawRect (getLocalBounds(), 3);
    }
}
