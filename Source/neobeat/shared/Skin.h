#pragma once

// A skin pack: a .nsk archive dresses the plugin without being allowed to rearrange it.
//
// The one rule the format rests on is that a skin never specifies layout. It supplies pixels and
// colours; the plugin owns every rectangle. That is not a limitation we are putting up with, it is
// the reason the idea works at all: a skinner who also has to lay out an interface is doing an
// engineer's job and mostly does not finish, which is why bespoke-layout skin formats have a
// handful of skins each and Winamp's fixed-layout one has a hundred thousand.
//
// Everything is optional. Every draw call returns false when the sheet it needs is absent, and the
// caller draws the element itself as it always has. A skin containing nothing but colors.json is
// valid and useful - it recolours the whole plugin without a single bitmap. That graceful floor is
// what lets a wrapper convert skins from other formats without ever failing: map what maps,
// synthesise what does not, and let the rest fall through to here.
//
// Sheet geometry is fixed by the format and is not negotiable per skin. Only metrics that cannot
// be inferred - the chassis inset, the knob frame count - come from skin.json.

#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>

namespace nsr
{

// ------------------------------------------------------------------ what is not a bitmap
// The data displays are drawn by the plugin and tinted from here rather than being skinned as
// images. A sixteen-band readout has to stay legible however dark the chassis is, and a skinner
// cannot be trusted with that - nor should they have to think about it. Winamp drew the same line
// with VISCOLOR.TXT and PLEDIT.TXT for exactly this reason.
struct SkinColours
{
    juce::Colour ink      { 0xffbfe9f6 };   // text and values
    juce::Colour dim      { 0xff48788c };   // labels, inactive
    juce::Colour accent   { 0xff45f0d0 };   // active, on-steps
    juce::Colour accent2  { 0xff2aa8e0 };   // secondary, negative modulation
    juce::Colour well     { 0xaa000000 };   // recessed surfaces behind data
    juce::Colour grid     { 0x14ffffff };   // beat lines

    juce::Array<juce::Colour> meter;        // low to high
    juce::Array<juce::Colour> lanes;        // per-lane tints

    SkinColours()
    {
        meter = { juce::Colour (0xff2aa8e0), juce::Colour (0xff45f0d0),
                  juce::Colour (0xffffcf5a), juce::Colour (0xffff5a5a) };
        lanes = { juce::Colour (0xff45f0d0), juce::Colour (0xff2aa8e0),
                  juce::Colour (0xff8f7cff), juce::Colour (0xffff8ac4) };
    }
};

class SkinPack
{
public:
    enum class Sheet { chassis = 0, tabs, buttons, knob, slider, cells, meter, font, count };
    enum class BtnState { normal = 0, hover, pressed, disabled };
    enum class CellState { off = 0, on, accent, ghost };

    SkinPack() = default;

    //==============================================================================================
    bool load (const juce::File& nsk, juce::String& error)
    {
        unload();

        if (! nsk.existsAsFile()) { error = "no such file: " + nsk.getFullPathName(); return false; }

        juce::ZipFile zip (nsk);
        if (zip.getNumEntries() == 0) { error = "not a readable archive"; return false; }

        // skin.json is the only required member. Without it we do not know the chassis inset, and
        // guessing it would put a seam through somebody's artwork.
        auto manifestText = readEntry (zip, "skin.json");
        if (manifestText.isEmpty()) { error = "skin.json missing"; return false; }

        auto manifest = juce::JSON::parse (manifestText);
        if (! manifest.isObject()) { error = "skin.json is not an object"; return false; }

        skinName = manifest.getProperty ("name", "Untitled").toString();
        skinAuthor = manifest.getProperty ("author", "").toString();

        if (auto* ch = manifest.getProperty ("chassis", {}).getDynamicObject())
        {
            auto ins = ch->getProperty ("inset");
            if (auto* a = ins.getArray())
                for (int i = 0; i < juce::jmin (4, a->size()); ++i)
                    inset[i] = juce::jmax (0, (int) a->getUnchecked (i));
        }
        if (auto* kn = manifest.getProperty ("knob", {}).getDynamicObject())
        {
            knobFrames = juce::jmax (1, (int) kn->getProperty ("frames"));
            knobSize   = juce::jmax (1, (int) kn->getProperty ("size"));
        }

        // colors.json: present or not, we start from the defaults and overwrite what it names.
        if (auto colText = readEntry (zip, "colors.json"); colText.isNotEmpty())
            readColours (juce::JSON::parse (colText));

        static const char* files[] = { "chassis.png", "tabs.png", "buttons.png", "knob.png",
                                       "slider.png", "cells.png", "meter.png", "font.png" };
        for (int i = 0; i < (int) Sheet::count; ++i)
            sheets[i] = loadImage (zip, files[i]);

        loadedOk = true;
        return true;
    }

    void unload()
    {
        for (auto& s : sheets) s = {};
        cols = {};
        skinName = skinAuthor = {};
        inset[0] = inset[1] = inset[2] = inset[3] = 64;
        knobFrames = 64; knobSize = 64;
        loadedOk = false;
    }

    bool isLoaded() const noexcept { return loadedOk; }
    bool has (Sheet s) const noexcept { return sheets[(int) s].isValid(); }
    const SkinColours& colours() const noexcept { return cols; }
    juce::String getName() const { return skinName; }
    juce::String getAuthor() const { return skinAuthor; }
    int getKnobFrames() const noexcept { return knobFrames; }

    //==============================================================================================
    // The chassis stretches to whatever size the editor is. Corners are blitted at their own size
    // so they never smear, edges stretch along one axis only, and the centre fills what is left.
    // Winamp could not do this - its window was 275x116 forever - and it is the one place the
    // format has to depart from the original rather than copy it.
    bool drawChassis (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& img = sheets[(int) Sheet::chassis];
        if (! img.isValid()) return false;

        const int sw = img.getWidth(), sh = img.getHeight();
        const int l = juce::jmin (inset[0], sw / 2), t = juce::jmin (inset[1], sh / 2);
        const int rr = juce::jmin (inset[2], sw / 2), b = juce::jmin (inset[3], sh / 2);

        const int sx[4] = { 0, l, sw - rr, sw };
        const int sy[4] = { 0, t, sh - b,  sh };

        // Degenerate target: if the destination is smaller than the corners, the corners win and
        // the stretched bands collapse. Better a cramped frame than overlapping artwork.
        const float dl = juce::jmin ((float) l, r.getWidth()  * 0.5f);
        const float dt = juce::jmin ((float) t, r.getHeight() * 0.5f);
        const float dr = juce::jmin ((float) rr, r.getWidth()  * 0.5f);
        const float db = juce::jmin ((float) b,  r.getHeight() * 0.5f);

        const float tx[4] = { r.getX(), r.getX() + dl, r.getRight()  - dr, r.getRight() };
        const float ty[4] = { r.getY(), r.getY() + dt, r.getBottom() - db, r.getBottom() };

        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int  s_w = sx[col + 1] - sx[col], s_h = sy[row + 1] - sy[row];
                const float d_w = tx[col + 1] - tx[col], d_h = ty[row + 1] - ty[row];
                if (s_w <= 0 || s_h <= 0 || d_w <= 0.0f || d_h <= 0.0f) continue;

                g.drawImage (img, tx[col], ty[row], d_w, d_h,
                             sx[col], sy[row], s_w, s_h);
            }
        return true;
    }

    // tabs.png: four across, active row over inactive row.
    bool drawTab (juce::Graphics& g, juce::Rectangle<float> r, int index, bool active) const
    {
        return blitCell (g, r, Sheet::tabs, 4, 2, juce::jlimit (0, 3, index), active ? 0 : 1);
    }

    // buttons.png: glyph columns, state rows.
    bool drawButton (juce::Graphics& g, juce::Rectangle<float> r, int glyph, BtnState s) const
    {
        return blitCell (g, r, Sheet::buttons, 8, 4, juce::jlimit (0, 7, glyph), (int) s);
    }

    // cells.png: off, on, accent, ghost.
    bool drawCell (juce::Graphics& g, juce::Rectangle<float> r, CellState s) const
    {
        return blitCell (g, r, Sheet::cells, 4, 1, (int) s, 0);
    }

    // knob.png: a filmstrip, frame 0 at minimum. This is VOLUME.BMP's structure unchanged, and it
    // is still how plugin knobs are drawn thirty years later.
    bool drawKnob (juce::Graphics& g, juce::Rectangle<float> r, float value01) const
    {
        const auto& img = sheets[(int) Sheet::knob];
        if (! img.isValid() || knobFrames <= 0) return false;

        const int frame = juce::jlimit (0, knobFrames - 1,
                                        (int) std::lround (juce::jlimit (0.0f, 1.0f, value01)
                                                           * (float) (knobFrames - 1)));
        const int fh = img.getHeight() / knobFrames;
        if (fh <= 0) return false;

        g.drawImage (img, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                     0, frame * fh, img.getWidth(), fh);
        return true;
    }

    bool drawSliderTrack (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& img = sheets[(int) Sheet::slider];
        if (! img.isValid()) return false;
        g.drawImage (img, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                     0, 0, juce::jmin (256, img.getWidth()), juce::jmin (12, img.getHeight()));
        return true;
    }

    bool drawSliderThumb (juce::Graphics& g, juce::Rectangle<float> r, bool pressed) const
    {
        const auto& img = sheets[(int) Sheet::slider];
        if (! img.isValid() || img.getHeight() < 52) return false;
        g.drawImage (img, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                     pressed ? 20 : 0, 32, 16, 20);
        return true;
    }

    // meter.png is a vertical ramp sampled top-down: a level of 1 shows all of it, a level of 0
    // none. The strip is read from the bottom up so the hot end stays at the top where it belongs.
    bool drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level01) const
    {
        const auto& img = sheets[(int) Sheet::meter];
        if (! img.isValid()) return false;

        const float lv = juce::jlimit (0.0f, 1.0f, level01);
        if (lv <= 0.0f) return true;                       // nothing to draw, but the sheet exists

        const int sh = juce::jmax (1, (int) std::lround ((float) img.getHeight() * lv));
        const int sy = img.getHeight() - sh;
        const float dh = r.getHeight() * lv;

        g.drawImage (img, r.getX(), r.getBottom() - dh, r.getWidth(), dh,
                     0, sy, img.getWidth(), sh);
        return true;
    }

    //==============================================================================================
    // Colour parsing kept here rather than leaning on juce::Colour::fromString, which expects
    // aarrggbb. Skins are authored by people writing CSS-shaped hex, and #RRGGBB has to mean opaque.
    static juce::Colour parseColour (const juce::String& s, juce::Colour fallback)
    {
        auto t = s.trim().removeCharacters ("#");

        // Check it is hex before trusting the length. getHexValue skips what it cannot read, so
        // an eight-letter word parses as a perfectly plausible colour if you let it - which is how
        // a typo in colors.json turns into a mystery tint rather than into the default.
        if (t.isEmpty()) return fallback;
        for (int i = 0; i < t.length(); ++i)
        {
            const auto c = t[i];
            const bool hex = (c >= '0' && c <= '9')
                          || (c >= 'a' && c <= 'f')
                          || (c >= 'A' && c <= 'F');
            if (! hex) return fallback;
        }

        if (t.length() == 3)                               // #abc
        {
            juce::String e;
            for (int i = 0; i < 3; ++i) e << t[i] << t[i];
            t = e;
        }
        if (t.length() == 6) return juce::Colour ((juce::uint32) (0xff000000u | t.getHexValue32()));
        if (t.length() == 8)
        {
            // authored as rrggbbaa, stored as aarrggbb
            const auto v = (juce::uint32) t.getHexValue64();
            return juce::Colour ((juce::uint32) (((v & 0xffu) << 24) | (v >> 8)));
        }
        return fallback;
    }

private:
    static juce::String readEntry (juce::ZipFile& zip, const juce::String& name)
    {
        const int idx = zip.getIndexOfFileName (name, true);
        if (idx < 0) return {};
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (idx));
        return s != nullptr ? s->readEntireStreamAsString() : juce::String();
    }

    static juce::Image loadImage (juce::ZipFile& zip, const juce::String& name)
    {
        const int idx = zip.getIndexOfFileName (name, true);
        if (idx < 0) return {};
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (idx));
        if (s == nullptr) return {};
        return juce::ImageFileFormat::loadFrom (*s);
    }

    void readColours (const juce::var& v)
    {
        if (! v.isObject()) return;
        auto one = [&v] (const char* key, juce::Colour& dest)
        {
            auto p = v.getProperty (key, juce::var());
            if (p.isString()) dest = parseColour (p.toString(), dest);
        };
        one ("ink", cols.ink);       one ("dim", cols.dim);
        one ("accent", cols.accent); one ("accent2", cols.accent2);
        one ("well", cols.well);     one ("grid", cols.grid);

        auto list = [&v] (const char* key, juce::Array<juce::Colour>& dest)
        {
            auto p = v.getProperty (key, juce::var());
            if (auto* a = p.getArray())
            {
                juce::Array<juce::Colour> out;
                for (auto& e : *a)
                    if (e.isString())
                        out.add (parseColour (e.toString(), juce::Colours::transparentBlack));
                if (! out.isEmpty()) dest = out;
            }
        };
        list ("meter", cols.meter);
        list ("lanes", cols.lanes);
    }

    bool blitCell (juce::Graphics& g, juce::Rectangle<float> r, Sheet sheet,
                   int cols_, int rows_, int col, int row) const
    {
        const auto& img = sheets[(int) sheet];
        if (! img.isValid() || cols_ <= 0 || rows_ <= 0) return false;

        const int cw = img.getWidth() / cols_, chh = img.getHeight() / rows_;
        if (cw <= 0 || chh <= 0) return false;

        g.drawImage (img, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                     col * cw, row * chh, cw, chh);
        return true;
    }

    juce::Image sheets[(int) Sheet::count];
    SkinColours cols;
    juce::String skinName, skinAuthor;
    int inset[4] { 64, 64, 64, 64 };
    int knobFrames = 64, knobSize = 64;
    bool loadedOk = false;
};

// ------------------------------------------------------------------ the shelf
// What turns one loadable skin into a wardrobe. A folder of .nsk files, scanned, sorted, and
// addressable by name - because the name is what gets written into plugin state, and a name
// survives a file being moved or a library being rebuilt in a way an index never does.
//
// Everything here is deliberately cheap. Scanning reads skin.json out of each archive and nothing
// else, so a shelf of fifty skins costs fifty small reads rather than fifty sets of bitmaps; the
// sheets are only decoded when a skin is actually chosen.
class SkinLibrary
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name;      // from skin.json, which is what a menu shows and state stores
        juce::String author;
    };

    // Where skins live. One folder, beside the user's own material rather than inside the app,
    // so a skin someone downloads can simply be dropped in.
    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Neosystem").getChildFile ("Skins");
    }

    int rescan (const juce::File& folder = defaultFolder())
    {
        entries.clear();
        root = folder;
        if (! folder.isDirectory()) return 0;

        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.nsk"))
        {
            juce::ZipFile zip (f);
            if (zip.getNumEntries() == 0) continue;

            const int idx = zip.getIndexOfFileName ("skin.json", true);
            if (idx < 0) continue;                       // not a skin, whatever it is called

            juce::String name, author;
            if (std::unique_ptr<juce::InputStream> s { zip.createStreamForEntry (idx) })
            {
                const auto v = juce::JSON::parse (s->readEntireStreamAsString());
                name = v.getProperty ("name", {}).toString();
                author = v.getProperty ("author", {}).toString();
            }
            if (name.isEmpty()) name = f.getFileNameWithoutExtension();
            entries.add ({ f, name, author });
        }

        std::sort (entries.begin(), entries.end(),
                   [] (const Entry& a, const Entry& b)
                   { return a.name.compareIgnoreCase (b.name) < 0; });
        return entries.size();
    }

    int size() const noexcept { return entries.size(); }
    const juce::File& folder() const noexcept { return root; }
    const Entry& operator[] (int i) const { return entries.getReference (i); }

    int indexOf (const juce::String& name) const
    {
        for (int i = 0; i < entries.size(); ++i)
            if (entries.getReference (i).name.equalsIgnoreCase (name))
                return i;
        return -1;
    }

    juce::StringArray names() const
    {
        juce::StringArray out;
        for (const auto& e : entries) out.add (e.name);
        return out;
    }

private:
    juce::Array<Entry> entries;
    juce::File root;
};

} // namespace nsr
