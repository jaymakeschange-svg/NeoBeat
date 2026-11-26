// What a skin may and may not do to the plugin.
//
// The format's whole claim is that a skin changes how things look and can never change where they
// are, and that anything missing falls through to the plugin's own drawing rather than breaking.
// Both of those are testable, so they are tested here rather than asserted in a comment.

#include <juce_audio_utils/juce_audio_utils.h>
#include "neobeat/shared/Skin.h"
#include "neobeat/shared/Ui.h"

namespace
{
int failures = 0;

void ok (bool cond, const juce::String& what)
{
    std::cout << (cond ? "  ok:   " : "  FAIL: ") << what << "\n";
    if (! cond) ++failures;
}

void heading (const juce::String& s) { std::cout << "\n" << s << "\n"; }

juce::MemoryBlock encodePng (const juce::Image& img)
{
    juce::MemoryOutputStream mos;
    juce::PNGImageFormat png;
    png.writeImageToStream (img, mos);
    return mos.getMemoryBlock();
}

// A chassis whose nine regions are nine distinct colours, so a smeared corner is visible as a
// wrong pixel rather than as something that merely looks off.
juce::Image makeNineSlice (int size, int inset)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const int xs[4] = { 0, inset, size - inset, size };
    const juce::Colour c[3][3] = {
        { juce::Colour (0xffff0000), juce::Colour (0xff00ff00), juce::Colour (0xff0000ff) },
        { juce::Colour (0xffffff00), juce::Colour (0xff808080), juce::Colour (0xff00ffff) },
        { juce::Colour (0xffff00ff), juce::Colour (0xff202020), juce::Colour (0xffffffff) }
    };
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
        {
            g.setColour (c[r][k]);
            g.fillRect (xs[k], xs[r], xs[k + 1] - xs[k], xs[r + 1] - xs[r]);
        }
    return img;
}

// Filmstrip where every frame is a different grey, so the chosen frame is readable from one pixel.
juce::Image makeFilmstrip (int frames, int cell)
{
    juce::Image img (juce::Image::ARGB, cell, frames * cell, true);
    juce::Graphics g (img);
    for (int f = 0; f < frames; ++f)
    {
        const auto v = (juce::uint8) juce::jlimit (0, 255, (f * 255) / juce::jmax (1, frames - 1));
        g.setColour (juce::Colour (v, v, v));
        g.fillRect (0, f * cell, cell, cell);
    }
    return img;
}

void addPng (juce::ZipFile::Builder& b, const juce::String& name, const juce::Image& img,
             juce::OwnedArray<juce::MemoryBlock>& keep)
{
    auto* mb = keep.add (new juce::MemoryBlock (encodePng (img)));
    b.addEntry (new juce::MemoryInputStream (mb->getData(), mb->getSize(), false),
                9, name, juce::Time::getCurrentTime());
}

void addText (juce::ZipFile::Builder& b, const juce::String& name, const juce::String& text,
              juce::OwnedArray<juce::MemoryBlock>& keep)
{
    auto* mb = keep.add (new juce::MemoryBlock (text.toRawUTF8(), (size_t) text.getNumBytesAsUTF8()));
    b.addEntry (new juce::MemoryInputStream (mb->getData(), mb->getSize(), false),
                9, name, juce::Time::getCurrentTime());
}

bool writeZip (juce::ZipFile::Builder& b, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.failedToOpen()) return false;
    return b.writeToStream (out, nullptr);
}

juce::Colour pixelAt (const juce::Image& img, int x, int y) { return img.getPixelAt (x, y); }

bool near (juce::Colour a, juce::Colour b, int tol = 6)
{
    return std::abs ((int) a.getRed()   - (int) b.getRed())   <= tol
        && std::abs ((int) a.getGreen() - (int) b.getGreen()) <= tol
        && std::abs ((int) a.getBlue()  - (int) b.getBlue())  <= tol;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("nsk_test");
    tmp.createDirectory();

    //==============================================================================================
    heading ("a file that is not a skin is refused, and says why");
    {
        auto junk = tmp.getChildFile ("junk.nsk");
        junk.replaceWithText ("this is not a zip");
        nsr::SkinPack s; juce::String err;
        ok (! s.load (junk, err), "a text file is not loaded as a skin");
        ok (err.isNotEmpty(), "the failure carries a reason (" + err + ")");
        ok (! s.isLoaded(), "nothing is left half-loaded");
    }

    //==============================================================================================
    heading ("skin.json is the one required member");
    {
        juce::OwnedArray<juce::MemoryBlock> keep;
        juce::ZipFile::Builder b;
        addText (b, "colors.json", "{\"accent\":\"#ff0000\"}", keep);
        auto f = tmp.getChildFile ("no_manifest.nsk");
        writeZip (b, f);

        nsr::SkinPack s; juce::String err;
        ok (! s.load (f, err), "an archive without skin.json is refused");
    }

    //==============================================================================================
    heading ("a skin of colours alone is valid, and every draw falls through");
    {
        juce::OwnedArray<juce::MemoryBlock> keep;
        juce::ZipFile::Builder b;
        addText (b, "skin.json", "{\"format\":1,\"name\":\"Colours only\"}", keep);
        addText (b, "colors.json",
                 "{\"accent\":\"#00FF00\",\"ink\":\"#FFFFFF\",\"well\":\"#102030C0\","
                 "\"meter\":[\"#111111\",\"#222222\"]}", keep);
        auto f = tmp.getChildFile ("colours_only.nsk");
        writeZip (b, f);

        nsr::SkinPack s; juce::String err;
        ok (s.load (f, err), "it loads");
        ok (s.getName() == "Colours only", "the name comes through");
        ok (s.colours().accent == juce::Colour (0xff00ff00), "#00FF00 parses opaque green");
        ok (s.colours().well == juce::Colour (0xc0102030), "#RRGGBBAA keeps its alpha");
        ok (s.colours().meter.size() == 2, "a colour list replaces the default wholesale");

        juce::Image canvas (juce::Image::ARGB, 64, 64, true);
        juce::Graphics g (canvas);
        ok (! s.drawChassis (g, { 0.0f, 0.0f, 64.0f, 64.0f }), "no chassis sheet, no chassis drawn");
        ok (! s.drawKnob (g, { 0.0f, 0.0f, 32.0f, 32.0f }, 0.5f), "no knob sheet, no knob drawn");
        ok (! s.drawCell (g, { 0.0f, 0.0f, 8.0f, 8.0f }, nsr::SkinPack::CellState::on),
            "no cells sheet, no cell drawn");
        ok (canvas.getPixelAt (32, 32).isTransparent(), "and nothing was put on the canvas");
    }

    //==============================================================================================
    heading ("the chassis stretches without smearing its corners");
    {
        juce::OwnedArray<juce::MemoryBlock> keep;
        juce::ZipFile::Builder b;
        addText (b, "skin.json",
                 "{\"format\":1,\"name\":\"Slice\",\"chassis\":{\"inset\":[16,16,16,16]}}", keep);
        addPng (b, "chassis.png", makeNineSlice (48, 16), keep);
        auto f = tmp.getChildFile ("slice.nsk");
        writeZip (b, f);

        nsr::SkinPack s; juce::String err;
        ok (s.load (f, err), "it loads");
        ok (s.has (nsr::SkinPack::Sheet::chassis), "the chassis sheet is present");

        juce::Image canvas (juce::Image::ARGB, 400, 300, true);
        juce::Graphics g (canvas);
        ok (s.drawChassis (g, { 0.0f, 0.0f, 400.0f, 300.0f }), "it draws at a size it was not authored at");

        ok (near (pixelAt (canvas, 4, 4),       juce::Colour (0xffff0000)), "top left corner is its own colour");
        ok (near (pixelAt (canvas, 396, 4),     juce::Colour (0xff0000ff)), "top right corner is its own colour");
        ok (near (pixelAt (canvas, 4, 296),     juce::Colour (0xffff00ff)), "bottom left corner is its own colour");
        ok (near (pixelAt (canvas, 396, 296),   juce::Colour (0xffffffff)), "bottom right corner is its own colour");
        ok (near (pixelAt (canvas, 200, 150),   juce::Colour (0xff808080)), "the centre fills with the centre");
        ok (near (pixelAt (canvas, 200, 4),     juce::Colour (0xff00ff00)), "the top edge stretched along one axis");
        ok (near (pixelAt (canvas, 4, 150),     juce::Colour (0xffffff00)), "the left edge stretched along the other");

        // Corners must stay square: at 400x300 with a 16px inset the corner is still 16px, so a
        // pixel just inside it belongs to the edge band, not to the corner.
        ok (! near (pixelAt (canvas, 30, 4), juce::Colour (0xffff0000)),
            "the corner did not grow with the target");
    }

    //==============================================================================================
    heading ("a knob is a filmstrip, and the frame follows the value");
    {
        juce::OwnedArray<juce::MemoryBlock> keep;
        juce::ZipFile::Builder b;
        addText (b, "skin.json",
                 "{\"format\":1,\"name\":\"Strip\",\"knob\":{\"frames\":16,\"size\":8}}", keep);
        addPng (b, "knob.png", makeFilmstrip (16, 8), keep);
        auto f = tmp.getChildFile ("strip.nsk");
        writeZip (b, f);

        nsr::SkinPack s; juce::String err;
        ok (s.load (f, err), "it loads");
        ok (s.getKnobFrames() == 16, "the frame count comes from skin.json, not from the image");

        auto frameColourAt = [&s] (float v)
        {
            juce::Image c (juce::Image::ARGB, 20, 20, true);
            juce::Graphics g (c);
            s.drawKnob (g, { 0.0f, 0.0f, 20.0f, 20.0f }, v);
            return c.getPixelAt (10, 10);
        };
        ok (frameColourAt (0.0f).getRed() < 20,   "value 0 draws the first frame");
        ok (frameColourAt (1.0f).getRed() > 235,  "value 1 draws the last frame");
        ok (frameColourAt (0.5f).getRed() > 100
            && frameColourAt (0.5f).getRed() < 155, "value 0.5 lands in the middle of the strip");
        ok (frameColourAt (-3.0f).getRed() < 20,  "a value below range is clamped, not wrapped");
        ok (frameColourAt (9.0f).getRed() > 235,  "a value above range is clamped too");
    }

    //==============================================================================================
    heading ("a meter shows as much of its ramp as the level asks for");
    {
        juce::OwnedArray<juce::MemoryBlock> keep;
        juce::ZipFile::Builder b;
        addText (b, "skin.json", "{\"format\":1,\"name\":\"Meter\"}", keep);
        juce::Image m (juce::Image::ARGB, 4, 64, true);
        { juce::Graphics g (m); g.setColour (juce::Colours::white); g.fillAll(); }
        addPng (b, "meter.png", m, keep);
        auto f = tmp.getChildFile ("meter.nsk");
        writeZip (b, f);

        nsr::SkinPack s; juce::String err;
        ok (s.load (f, err), "it loads");

        juce::Image canvas (juce::Image::ARGB, 10, 100, true);
        { juce::Graphics g (canvas); ok (s.drawMeter (g, { 0.0f, 0.0f, 10.0f, 100.0f }, 0.0f),
                                         "a silent meter still reports that it handled the draw"); }
        ok (canvas.getPixelAt (5, 95).isTransparent(), "but it put nothing on the canvas");

        juce::Image canvas2 (juce::Image::ARGB, 10, 100, true);
        { juce::Graphics g (canvas2); s.drawMeter (g, { 0.0f, 0.0f, 10.0f, 100.0f }, 0.5f); }
        ok (! canvas2.getPixelAt (5, 95).isTransparent(), "at half level the bottom is filled");
        ok (canvas2.getPixelAt (5, 5).isTransparent(),    "and the top is not");
    }

    //==============================================================================================
    heading ("colour shorthand");
    {
        ok (nsr::SkinPack::parseColour ("#abc", juce::Colours::black)
                == juce::Colour (0xffaabbcc), "#abc expands to #aabbcc");
        ok (nsr::SkinPack::parseColour ("00FF00", juce::Colours::black)
                == juce::Colour (0xff00ff00), "a missing hash is tolerated");
        ok (nsr::SkinPack::parseColour ("nonsense", juce::Colour (0xff123456))
                == juce::Colour (0xff123456), "something unparseable keeps the fallback");
    }

    //==============================================================================================
    // If a real wrapped skin was handed in, check the thing the wrapper claimed about it.
    if (argc > 1)
    {
        heading ("a wrapped Winamp skin, if one was supplied");
        nsr::SkinPack s; juce::String err;
        juce::File real { juce::String (argv[1]) };
        if (s.load (real, err))
        {
            ok (true, "it loads: " + s.getName());
            ok (s.has (nsr::SkinPack::Sheet::chassis), "it brought a chassis across");
            ok (s.has (nsr::SkinPack::Sheet::buttons), "it brought buttons across");
            ok (s.has (nsr::SkinPack::Sheet::knob),    "it has a knob sheet, synthesised or not");
            ok (s.colours().accent == juce::Colour (0xff00ff00),
                "its accent is the Winamp green out of PLEDIT.TXT");
            juce::Image canvas (juce::Image::ARGB, 600, 400, true);
            juce::Graphics g (canvas);
            ok (s.drawChassis (g, { 0.0f, 0.0f, 600.0f, 400.0f }), "its chassis draws at editor size");
            ok (! canvas.getPixelAt (300, 200).isTransparent(), "and it covered the middle");
        }
        else ok (false, "could not load " + real.getFullPathName() + ": " + err);
    }

    //==============================================================================================
    // A pack that loads but does not reach the drawing is the failure mode that looks like success:
    // the name appears, nothing changes on screen. So check the tokens the drawing actually reads.
    if (argc > 1)
    {
        heading ("applying a pack changes the tokens the drawing reads");
        nsui::setSkin (nsui::skins::chromePearl());
        const auto before = nsui::skin();

        juce::String err;
        const bool applied = nsui::applyPack (juce::File { juce::String (argv[1]) }, err);
        ok (applied, "the pack applies" + (applied ? juce::String() : " (" + err + ")"));

        const auto after = nsui::skin();
        ok (after.name == "Winamp Classic 2.91", "the name took: " + after.name);
        ok (after.accent == 0xff00ff00u, "accent is the Winamp green");
        ok (after.accent != before.accent, "the accent actually moved");
        ok (after.chassisTop != before.chassisTop, "the chassis colour moved");
        ok (juce::Colour (after.chassisTop).getPerceivedBrightness()
                < juce::Colour (before.chassisTop).getPerceivedBrightness(),
            "a dark pack darkened the chassis rather than only its readouts");
        ok (after.lcdInk == 0xff00ff00u, "the readout ink took the accent");

        nsui::clearPack();
        ok (nsui::skin().chassisTop == before.chassisTop, "clearing puts the base back");
    }

    //==============================================================================================
    // A shelf of skins, and switching between them live. This is the part that has to be cheap:
    // somebody flicking through a menu is going to do it twenty times in a row.
    if (argc > 2)
    {
        heading ("a folder of skins becomes a menu");
        juce::File folder { juce::String (argv[2]) };

        const int found = nsui::library().rescan (folder);
        ok (found >= 4, "the folder scans: " + juce::String (found) + " skins");
        ok (nsui::library().names().joinIntoString (", ").isNotEmpty(),
            "they have names: " + nsui::library().names().joinIntoString (", "));

        // sorted, so a menu does not reorder itself between runs
        bool sorted = true;
        for (int i = 1; i < nsui::library().size(); ++i)
            if (nsui::library()[i - 1].name.compareIgnoreCase (nsui::library()[i].name) > 0)
                sorted = false;
        ok (sorted, "the shelf is in a stable order");

        heading ("switching, repeatedly, live");
        const auto base = nsui::skins::chromePearl();
        juce::String err;
        juce::Array<juce::uint32> seen;

        for (const auto& n : nsui::library().names())
        {
            const bool applied = nsui::applyNamed (n, err, base);
            ok (applied, "switch to " + n + (applied ? juce::String() : " (" + err + ")"));
            if (applied)
            {
                ok (nsui::currentPackName() == n, "  it reports itself as " + n);
                seen.add (nsui::skin().accent);
            }
        }
        // Every skin in this shelf was given a different accent, so a repeat means a switch that
        // silently did nothing - which is exactly the bug that hid behind a correct console line
        // earlier today.
        bool allDistinct = true;
        for (int i = 0; i < seen.size(); ++i)
            for (int k = i + 1; k < seen.size(); ++k)
                if (seen[i] == seen[k]) allDistinct = false;
        ok (allDistinct, "each switch actually changed the look");

        ok (! nsui::applyNamed ("Does Not Exist", err, base), "a skin that is not there is refused");
        ok (nsui::currentPackName().isNotEmpty(), "and the one that was showing is left alone");

        heading ("state round-trips, and a missing skin is not fatal");
        nsui::applyNamed ("Oxide", err, base);
        const auto saved = nsui::currentPackName();
        nsui::clearPack (base);
        ok (nsui::currentPackName().isEmpty() || nsui::skin().accent == base.accent,
            "cleared back to the plugin's own look");

        nsui::restorePackFromState (saved, base);
        ok (nsui::currentPackName() == "Oxide", "the saved name restores the skin");

        nsui::restorePackFromState ("A Skin This Machine Does Not Have", base);
        ok (nsui::skin().accent == base.accent,
            "a session naming a skin that is not installed falls back rather than failing");
        ok (nsui::currentPackName().isEmpty(), "and stops claiming to be wearing it");
    }

    std::cout << "\n" << (failures == 0 ? "PASS" : "FAILED") << ": " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
