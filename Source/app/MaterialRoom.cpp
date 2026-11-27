#include "MaterialRoom.h"

// The list of materials found in the folder.
struct MaterialRoom::Rows final : public juce::ListBoxModel
{
    explicit Rows (MaterialRoom& o) : owner (o) {}

    int getNumRows() override { return owner.files.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (! juce::isPositiveAndBelow (row, owner.files.size())) return;
        if (sel || row == owner.current) { g.setColour (nsui::C::glow().withAlpha (row == owner.current ? 0.45f : 0.25f)); g.fillRoundedRectangle (juce::Rectangle<int> (2, 1, w - 4, h - 2).toFloat(), 4.0f); }
        g.setColour (nsui::C::text());
        g.setFont (nsui::font (13.0f, row == owner.current));
        g.drawText (owner.files[row].getFileNameWithoutExtension(), 10, 0, w - 16, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner.loadRow (row); }

    MaterialRoom& owner;
};

// The generation: one tile per child, with its kind, how close it came, its waveform and what was done
// to make it. Click a tile to hear it; Keep (bottom right of the tile) puts it in the pool and makes it
// a parent of the next generation.
struct MaterialRoom::Strip final : public juce::Component
{
    explicit Strip (MaterialRoom& o) : owner (o) { setComponentID ("breed strip"); }

    int count() const { return (int) owner.proc.breedChildren.size(); }
    juce::Rectangle<int> tile (int i) const
    {
        const float w = (float) getWidth() / 8.0f;
        return juce::Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (3.0f, 2.0f).toNearestInt();
    }
    juce::Rectangle<int> keep (int i) const { auto t = tile (i).reduced (5); return t.removeFromBottom (18).removeFromRight (44); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < count(); ++i)
        {
            if (keep (i).contains (e.getPosition()))
            {
                juce::String error;
                if (owner.proc.breedKeep (i, error)) owner.say (owner.proc.lastMessage); else owner.say (error);
                if (owner.onChanged) owner.onChanged();
                owner.refresh();
                return;
            }
            if (tile (i).contains (e.getPosition())) { owner.proc.breedPlay (i); playing = i; repaint(); return; }
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& kids = owner.proc.breedChildren;
        if (kids.empty())
        {
            g.setColour (nsui::C::dim());
            g.setFont (nsui::font (11.5f));
            g.drawFittedText (owner.proc.breedBusy() ? "Breeding..." :
                              "Wander makes eight variants of the loaded material and the sound it was fitted to; "
                              "Grow climbs toward the sound from what is loaded or kept. They appear here.",
                              getLocalBounds().reduced (12, 4), juce::Justification::centred, 2);
            return;
        }
        for (int i = 0; i < (int) kids.size() && i < 8; ++i)
        {
            const auto& c = kids[(size_t) i];
            const bool kept = i < (int) owner.proc.breedKeptFlags.size() && owner.proc.breedKeptFlags[(size_t) i];
            auto t = tile (i).toFloat();
            g.setColour (nsui::C::glow().withAlpha (i == playing ? 0.40f : 0.14f));
            g.fillRoundedRectangle (t, 5.0f);
            if (kept) { g.setColour (nsui::C::teal()); g.drawRoundedRectangle (t.reduced (0.5f), 5.0f, 1.6f); }
            auto r = t.reduced (6.0f, 4.0f);
            auto top = r.removeFromTop (14.0f);
            g.setColour (nsui::C::text());
            g.setFont (nsui::font (11.0f, true));
            g.drawText (juce::String (i + 1) + " " + ml::kindName (c.kind), top, juce::Justification::centredLeft, false);
            if (std::isfinite (c.loss))
            {
                g.setColour (nsui::C::label());
                g.setFont (nsui::font (10.5f));
                g.drawText (juce::String (c.loss, 2), top, juce::Justification::centredRight, false);
            }
            // the waveform, its envelope on a log scale so a long ring shows
            auto wave = r.removeFromTop (r.getHeight() - 34.0f).reduced (0.0f, 2.0f);
            const int cols = std::max (1, (int) wave.getWidth());
            float pk = 1.0e-9f; for (float v : c.sound) pk = std::max (pk, std::fabs (v));
            g.setColour (nsui::C::teal().withAlpha (0.8f));
            const size_t per = std::max<size_t> (1, c.sound.size() / (size_t) cols);
            for (int x = 0; x < cols; ++x)
            {
                float m = 0.0f;
                for (size_t k = (size_t) x * per; k < std::min (c.sound.size(), (size_t) (x + 1) * per); ++k) m = std::max (m, std::fabs (c.sound[k]));
                const float h = juce::jlimit (0.0f, 1.0f, 1.0f + std::log10 (m / pk + 1e-6f) / 3.0f) * wave.getHeight() * 0.5f;
                g.fillRect (wave.getX() + (float) x, wave.getCentreY() - h, 1.0f, std::max (0.5f, 2.0f * h));
            }
            g.setColour (nsui::C::label());
            g.setFont (nsui::font (10.0f));
            g.drawFittedText (c.how, r.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            const auto kb = keep (i).toFloat();
            g.setColour (kept ? nsui::C::teal() : nsui::C::dim().withAlpha (0.35f));
            g.fillRoundedRectangle (kb, 8.0f);
            g.setColour (kept ? juce::Colours::white : nsui::C::text());
            g.setFont (nsui::font (10.5f, true));
            g.drawText (kept ? "Kept" : "Keep", kb, juce::Justification::centred, false);
        }
    }

    MaterialRoom& owner;
    int playing = -1;
};

// ------------------------------------------------------------------

MaterialRoom::MaterialRoom (BcProcessor& p, std::function<void()> changed)
    : proc (p), onChanged (std::move (changed))
{
    strip = std::make_unique<Strip> (*this);
    addAndMakeVisible (*strip);
    for (auto* b : { &wander, &grow }) addAndMakeVisible (*b);
    addAndMakeVisible (reachKnob);
    reachLabel.setText ("Reach", juce::dontSendNotification);
    reachLabel.setFont (nsui::font (11.0f));
    reachLabel.setColour (juce::Label::textColourId, nsui::C::dim());
    addAndMakeVisible (reachLabel);
    reachKnob.setRange (0.0, 1.0, 0.01);
    reachKnob.setValue (proc.breedReach, juce::dontSendNotification);
    reachKnob.onValueChange = [this] { proc.breedReach = (float) reachKnob.getValue(); };
    reachKnob.setTooltip ("How far the children go from their parents: low for variants you would mistake for the parent, high for wild ones");
    wander.setTooltip ("Make eight variants of the loaded material (or what you kept) and of the sound it was fitted to. "
                       "Click one to hear it; Keep the ones to breed from next. Only kept ones go into the pool");
    grow.setTooltip ("Climb toward the fitted sound from the loaded material (or what you kept), twelve generations on its own, "
                     "keeping the closest each time. Then the eight closest it made appear below. Takes a minute or so");
    wander.onClick = [this] { juce::String e; if (proc.breedStart (false, e)) say (proc.lastMessage); else say (e); refresh(); };
    grow.onClick = [this] { juce::String e; if (proc.breedStart (true, e)) say (proc.lastMessage); else say (e); refresh(); };

    rows = std::make_unique<Rows> (*this);
    list.setModel (rows.get());
    list.setRowHeight (24);
    // on a plate of its own, as the CUT room's file list is: the default dark list hid the dark row text
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    for (auto* b : { &folderBtn, &strike, &render, &through, &prev, &next, &fit }) addAndMakeVisible (*b);
    addChildComponent (cancel);
    seenGeneration = proc.fitGeneration;
    addAndMakeVisible (poolBox);
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (message);

    folderBtn.setTooltip ("The folder holding the learner's material files (the .json files it writes)");
    strike.setTooltip ("Hit the object. Hit it again while it is ringing and the energy adds, the way a real body behaves");
    render.setTooltip ("Strike it eight times at different strengths and put every hit in the pool - no two the same");
    through.setTooltip ("Send the chosen pool sound through this material and add the result to the pool");
    fit.setTooltip ("Learn the object that made the chosen pool sound. Batchcut fits it four times, each a little different, "
                    "and puts every result in the pool as its own material, the closest first. About a minute; you can keep working. "
                    "You can also drop any audio file here to fit it");
    cancel.setTooltip ("Stop fitting. Nothing is added from a fit that was stopped");

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

    fit.onClick = [this]
    {
        juce::String error;
        if (proc.fitStartPool (poolBox.getSelectedId() - 1, error)) say (proc.lastMessage);
        else                                                        say (error);
        refresh();
    };

    cancel.onClick = [this] { proc.fitCancel(); proc.breedCancel(); say ("Stopping..."); };
    seenBreed = proc.breedGeneration;

    startTimerHz (8);
}

MaterialRoom::~MaterialRoom() { list.setModel (nullptr); }

juce::Component& MaterialRoom::getStrip() noexcept { return *strip; }

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
    const bool busy = proc.fitBusy() || proc.breedBusy();
    fit.setEnabled (! proc.kit.pool.empty() && ! busy);
    cancel.setVisible (busy);
    wander.setEnabled (! busy && (proc.loadedBodyValid || proc.breedHasTarget || ! proc.breedKept.empty()));
    grow.setEnabled (! busy && proc.breedHasTarget && (proc.loadedBodyValid || ! proc.breedKept.empty()));
    strip->repaint();
    repaint();
}

// A generation ended: show it (the processor has its children; nothing went into the pool).
void MaterialRoom::breedFinished()
{
    strip->playing = -1;
    say (proc.lastMessage);
    if (onChanged) onChanged();
}

void MaterialRoom::timerCallback()
{
    if (isShowing()) proc.materialLive.store (true);
    if (proc.fitGeneration != seenGeneration)
    {
        seenGeneration = proc.fitGeneration;
        if (! proc.fitBusy()) fitFinished();
        refresh();
    }
    if (proc.breedGeneration != seenBreed)
    {
        seenBreed = proc.breedGeneration;
        if (! proc.breedBusy()) breedFinished();
        refresh();
    }
    auto timeLeft = [] (double left)
    {
        return left >= 0.0 ? ", about " + (left < 90.0 ? juce::String (juce::jmax (1, juce::roundToInt (left))) + " s"
                                                       : juce::String (juce::roundToInt (left / 60.0)) + " min") + " left" : juce::String();
    };
    if (proc.fitBusy())
    {
        say ("Fitting " + proc.fitSource + " - " + juce::String (juce::roundToInt (100.0 * proc.fitProgress())) + "%" + timeLeft (proc.fitSecondsLeft()));
        repaint (message.getBounds().expanded (0, 4));
    }
    else if (proc.breedBusy())
    {
        say ((proc.breedGrowing ? "Growing toward " + proc.breedTargetName : juce::String ("Wandering")) + " - "
             + juce::String (juce::roundToInt (100.0 * proc.breedProgress())) + "%" + timeLeft (proc.breedSecondsLeft()));
        repaint (message.getBounds().expanded (0, 4));
    }
}

// A job ended (the processor has already put its sounds in the pool): show its materials, the
// closest one loaded, so the next press of Strike is the new object.
void MaterialRoom::fitFinished()
{
    const auto msg = proc.lastMessage;
    if (proc.fitFolder.isDirectory())
    {
        openFolder (proc.fitFolder);
        const int row = files.indexOf (proc.fitBest);
        if (row >= 0) loadRow (row);
    }
    proc.lastMessage = msg;           // the fit's summary, not the "Opened" line loading it wrote
    say (msg);
    if (onChanged) onChanged();
}

bool MaterialRoom::isAudio (const juce::File& f) const
{
    return f.existsAsFile() && proc.formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

bool MaterialRoom::isInterestedInFileDrag (const juce::StringArray& files_)
{
    for (const auto& f : files_)
    {
        const juce::File file (f);
        if (file.isDirectory() || file.hasFileExtension ("json") || isAudio (file)) return true;
    }
    return false;
}

void MaterialRoom::filesDropped (const juce::StringArray& files_, int, int)
{
    dropping = false;
    if (files_.isEmpty()) { repaint(); return; }
    const juce::File f (files_[0]);
    if (isAudio (f))
    {
        // any sound: fit it (the first, if several were dropped - one fit at a time)
        juce::String error;
        if (proc.fitStartFile (f, error)) say (proc.lastMessage + (files_.size() > 1 ? " (one sound at a time: the first of those dropped)" : ""));
        else                              say (error);
        refresh();
        return;
    }
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
    {
        auto face = r.withTrimmedBottom (114);
        strip->setBounds (face.removeFromBottom (104).reduced (8, 6));
    }
    auto bottom = r.removeFromBottom (92);
    auto line = r.removeFromBottom (22);
    cancel.setBounds (line.removeFromRight (80).reduced (0, 1));
    line.removeFromRight (8);
    message.setBounds (line);

    auto row1 = bottom.removeFromTop (32);
    strike.setBounds (row1.removeFromLeft (120).reduced (0, 2));
    row1.removeFromLeft (8);
    render.setBounds (row1.removeFromLeft (200).reduced (0, 2));
    grow.setBounds (row1.removeFromRight (80).reduced (0, 2));
    row1.removeFromRight (8);
    wander.setBounds (row1.removeFromRight (90).reduced (0, 2));
    row1.removeFromRight (10);
    reachKnob.setBounds (row1.removeFromRight (30));
    reachLabel.setBounds (row1.removeFromRight (46));

    bottom.removeFromTop (8);
    auto row2 = bottom.removeFromTop (34);
    through.setBounds (row2.removeFromLeft (130).reduced (0, 2));
    row2.removeFromLeft (8);
    mixKnob.setBounds (row2.removeFromLeft (34));
    row2.removeFromLeft (8);
    fit.setBounds (row2.removeFromRight (80).reduced (0, 2));
    row2.removeFromRight (8);
    poolBox.setBounds (row2.reduced (0, 4));
}

void MaterialRoom::paint (juce::Graphics& g)
{
    nsui::paintPlate (g, list.getBounds().toFloat(), 8.0f);
    auto r = getLocalBounds().reduced (12);
    r.removeFromLeft (274);
    auto face = r.removeFromTop (r.getHeight() - 114).toFloat();
    nsui::paintPlate (g, face, 8.0f);

    if (! proc.material.valid())
    {
        g.setColour (nsui::C::dim());
        g.setFont (nsui::font (14.0f));
        const auto above = face.withTrimmedBottom (104.0f).toNearestInt();
        g.drawText ("Open a folder of material files, or drop one here",
                    above.translated (0, -10), juce::Justification::centred, false);
        g.setFont (nsui::font (12.0f));
        g.drawText ("or drop any sound to fit it into a material",
                    above.translated (0, 12), juce::Justification::centred, false);
    }
    else
    {
        // The object, drawn as what it is: its modes. Height is how loud each one rings, and the
        // colour how long it rings for - so a bright tall forest is a bell and a dark short one
        // is a thud. This is the same table the sound reads from, not a picture of a waveform.
        const auto& m = proc.material;
        auto plot = face.withTrimmedBottom (104.0f).reduced (18.0f, 26.0f);

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

    {
        const auto sb = strip->getBounds().toFloat();
        g.setColour (nsui::C::dim().withAlpha (0.25f));
        g.drawHorizontalLine ((int) sb.getY() - 3, sb.getX() + 4.0f, sb.getRight() - 4.0f);
    }

    // the fit's (or a generation's) progress, under the message line
    if (proc.fitBusy() || proc.breedBusy())
    {
        const auto m = message.getBounds().toFloat();
        const auto bar = juce::Rectangle<float> (m.getX(), m.getBottom() - 3.0f, m.getWidth(), 3.0f);
        g.setColour (nsui::C::dim().withAlpha (0.25f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (nsui::C::teal());
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, proc.fitBusy() ? proc.fitProgress() : proc.breedProgress())), 1.5f);
    }

    if (dropping)
    {
        g.setColour (nsui::C::teal().withAlpha (0.16f));
        g.fillRect (getLocalBounds());
        g.setColour (nsui::C::teal());
        g.drawRect (getLocalBounds(), 3);
    }
}
