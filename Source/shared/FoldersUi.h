// The assigned folders, as a panel. One row per kind of file the Neosystem writes: WAV, MIDI, .NJQ, .NJQi.
//
// A folder is chosen here and remembered from then on. Saving a single file somewhere else is not an
// assignment: a save dialog opens in the assigned folder, and wherever that one file ends up going, the folder
// itself does not move. Importing is deliberately not on this panel — it stays loose, and only remembers the
// last place it was pointed at.
//
// Kept byte-identical in all three source trees. Batchcut, NeoBeat and MidiJaQ all show the same panel and
// write the same settings file, so a folder assigned in one is the folder the others use.
#pragma once

#include "Njq.h"
#include "Ui.h"

namespace nsui
{
class FoldersPanel final : public juce::Component
{
public:
    FoldersPanel()
    {
        for (auto k : { shared::Folder::Wav, shared::Folder::Midi, shared::Folder::Njq, shared::Folder::Njqi })
        {
            auto* row = rows.add (new Row (k));
            addAndMakeVisible (row->choose);
            addAndMakeVisible (row->reset);
            row->choose.onClick = [this, row] { pick (*row); };
            row->reset.onClick = [this, row]
            {
                auto s = shared::Settings::load();
                s.assigned (row->kind) = {};          // back to the default for that kind
                s.save();
                refresh();
                if (onChange) onChange();
            };
            row->choose.setTooltip ("Choose the folder " + juce::String (shared::Settings::name (k)) + " files are written to");
            row->reset.setTooltip ("Back to " + shared::Settings::defaultFolder (k).getFullPathName());
        }
        refresh();
    }

    std::function<void()> onChange;   // after an assignment, so whatever shows a path can redraw

    void refresh()
    {
        const auto s = shared::Settings::load();
        for (auto* row : rows)
        {
            const auto& a = s.assigned (row->kind);
            row->path = a.isNotEmpty() ? a : shared::Settings::defaultFolder (row->kind).getFullPathName();
            row->isDefault = a.isEmpty();
            row->reset.setEnabled (! row->isDefault);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        paintPlate (g, getLocalBounds().toFloat());
        auto b = getLocalBounds().reduced (14, 12);
        paintLabel (g, "Assigned folders", b.removeFromTop (14), juce::Justification::centredLeft, 10.0f, C::text());
        b.removeFromTop (2);
        g.setFont (font (10.5f));
        g.setColour (C::dim());
        g.drawFittedText ("Chosen once and remembered. Saving one file somewhere else does not move the folder. "
                          "Importing stays loose.", b.removeFromTop (26), juce::Justification::topLeft, 2);
        b.removeFromTop (6);
        for (auto* row : rows)
        {
            auto r = b.removeFromTop (kRowH);
            auto top = r.removeFromTop (kHeadH);
            // the kind, in its own capitalisation: ".NJQi" is not ".NJQI"
            g.setFont (caps (9.0f));
            g.setColour (col (skin().labelShadow));
            g.drawText (shared::Settings::name (row->kind), top.translated (0, 1), juce::Justification::centredLeft, false);
            g.setColour (C::label());
            g.drawText (shared::Settings::name (row->kind), top, juce::Justification::centredLeft, false);

            auto lcd = r.removeFromTop (kPathH).toFloat();
            paintLcd (g, lcd, 5.0f);
            g.setFont (font (11.0f));
            g.setColour (row->isDefault ? C::lcdDim() : C::lcdText());
            // the tail of a long path is the part that says where it is
            g.drawText (fitPath (row->path, g.getCurrentFont(), lcd.getWidth() - 16.0f),
                        lcd.toNearestInt().reduced (8, 0), juce::Justification::centredLeft, false);
            b.removeFromTop (kRowGap);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (14, 12);
        b.removeFromTop (14 + 2 + 26 + 6);
        for (auto* row : rows)
        {
            auto r = b.removeFromTop (kRowH);
            // the two buttons sit on the kind's line, at the right; the path gets the whole width below them
            auto buttons = r.removeFromTop (kHeadH).removeFromRight (kButtonsW);
            row->reset.setBounds (buttons.removeFromRight (64));
            buttons.removeFromRight (4);
            row->choose.setBounds (buttons);
            b.removeFromTop (kRowGap);
        }
    }

    // the height this panel wants
    static int wantedHeight() { return 12 + 14 + 2 + 26 + 6 + 4 * (kRowH + kRowGap) + 12; }

private:
    static constexpr int kHeadH = 20, kPathH = 22, kRowH = kHeadH + kPathH, kRowGap = 8, kButtonsW = 148;

    struct Row
    {
        explicit Row (shared::Folder k) : kind (k) {}
        shared::Folder kind;
        juce::String path;
        bool isDefault = true;
        Pill choose { "Choose" }, reset { "Default" };
    };

    void pick (Row& row)
    {
        const auto start = shared::Settings::load().folder (row.kind);
        chooser = std::make_unique<juce::FileChooser> ("Where " + juce::String (shared::Settings::name (row.kind)) + " files go", start);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, kind = row.kind] (const juce::FileChooser& fc)
        {
            const auto d = fc.getResult();
            if (! d.isDirectory()) return;
            auto s = shared::Settings::load();
            s.assign (kind, d);
            s.save();
            refresh();
            if (onChange) onChange();
        });
    }

    // A path too long for its box loses its middle: the last folders are the part that says where it is, so
    // they stay, and what is cut is marked with one ellipsis. Never two separators in a row.
    static juce::String fitPath (const juce::String& p, const juce::Font& f, float width)
    {
        if (textW (f, p) <= width) return p;
        auto parts = juce::StringArray::fromTokens (p, "/\\", {});
        parts.removeEmptyStrings();
        if (parts.isEmpty()) return p;
        for (int keep = parts.size() - 1; keep >= 1; --keep)
        {
            juce::StringArray tail;
            for (int i = parts.size() - keep; i < parts.size(); ++i) tail.add (parts[i]);
            const auto s = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6/")) + tail.joinIntoString ("/");
            if (textW (f, s) <= width) return s;
        }
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6/")) + parts[parts.size() - 1];
    }

    juce::OwnedArray<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace nsui
