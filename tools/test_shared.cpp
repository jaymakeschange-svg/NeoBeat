// Neosystem groundwork, without any framework: the .NJQ manifest at version 2 in every tree, categories that
// survive a round trip, version 1 files that still open, and the four assigned folders — which are remembered,
// and which a save somewhere else must never move.
#include "../Source/neobeat/shared/NjqInstrument.h"
#include <cstdio>
#include <cstdlib>

// JUCE reads environment variables but does not set them
static void putEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
   #else
    setenv (name, value.toRawUTF8(), 1);
   #endif
}

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

// a kit with enough in it to be worth saving
static njq::Kit madeUpKit()
{
    njq::Kit k;
    k.name = "Oxblood";
    k.author = "Marbelite";
    k.bpm = 138.0;
    k.swing = 0.58f;
    auto& ln = k.lanes[0];
    ln.set = true; ln.name = "Kick"; ln.role = 0; ln.stock = "nb.kick.1";
    return k;
}

int main()
{
    // the tests get their own shared folder, so the real settings on this computer are left alone
    const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neosystem-test-shared");
    sandbox.deleteRecursively();
    sandbox.createDirectory();
    putEnv ("MARBELITE_SHARED_DIR", sandbox.getFullPathName());
    CHECK (shared::dir() == sandbox, "the tests keep away from the real settings");

    const auto tmp = sandbox.getChildFile ("files");
    tmp.createDirectory();

    std::printf ("\nthe manifest\n");
    {
        auto k = madeUpKit();
        k.category = "  Clubby \t ";          // typed carelessly
        const auto f = tmp.getChildFile ("kit.njq");
        juce::String err;
        const bool saved = njq::save (f, k, err);
        CHECK (saved, "a kit saves (%s)", err.toRawUTF8());

        njq::Kit back;
        const bool opened = njq::load (f, back, err);
        CHECK (opened, "it opens again (%s)", err.toRawUTF8());
        CHECK (back.category == "Clubby", "the category comes back tidied: '%s'", back.category.toRawUTF8());
        CHECK (back.kind == "kit", "a kit is a kit");
        CHECK (back.name == k.name && (int) back.bpm == 138, "the rest of it survives");

        // what the file itself says, so the format is checked and not just the round trip
        juce::ZipFile zip (f);
        const auto* me = zip.getEntry ("manifest.xml");
        CHECK (me != nullptr, "there is a manifest");
        if (me != nullptr)
        {
            std::unique_ptr<juce::InputStream> ms (zip.createStreamForEntry (*me));
            const auto xml = juce::XmlDocument::parse (ms->readEntireStreamAsString());
            CHECK (xml != nullptr && xml->getIntAttribute ("version") == 2, "it is written at version 2");
            CHECK (xml != nullptr && xml->getStringAttribute ("category") == "Clubby", "the category is an attribute of its own");
        }
    }
    {
        // no category: nothing is written, and nothing comes back
        auto k = madeUpKit();
        const auto f = tmp.getChildFile ("plain.njq");
        juce::String err;
        njq::save (f, k, err);
        njq::Kit back;
        njq::load (f, back, err);
        CHECK (back.category.isEmpty(), "a kit with no category stays uncategorised");

        juce::ZipFile zip (f);
        const auto* me = zip.getEntry ("manifest.xml");
        std::unique_ptr<juce::InputStream> ms (zip.createStreamForEntry (*me));
        const auto xml = juce::XmlDocument::parse (ms->readEntireStreamAsString());
        CHECK (xml != nullptr && ! xml->hasAttribute ("category"), "and writes no empty attribute");
    }
    {
        // a version 1 file, as Batchcut 2.x and NeoBeat 2.0 wrote them: no kind, no category
        auto k = madeUpKit();
        const auto f = tmp.getChildFile ("old.njq");
        juce::String err;
        njq::save (f, k, err);
        juce::ZipFile zip (f);
        const auto* me = zip.getEntry ("manifest.xml");
        std::unique_ptr<juce::InputStream> ms (zip.createStreamForEntry (*me));
        auto xml = juce::XmlDocument::parse (ms->readEntireStreamAsString());
        xml->setAttribute ("version", 1);
        xml->removeAttribute ("kind");
        xml->removeAttribute ("category");
        const auto v1 = tmp.getChildFile ("v1.njq");
        juce::ZipFile::Builder zb;
        const auto s = xml->toString();
        zb.addEntry (new juce::MemoryInputStream (s.toRawUTF8(), s.getNumBytesAsUTF8(), true), 9, "manifest.xml", juce::Time::getCurrentTime());
        { juce::FileOutputStream os (v1); double p = 0.0; zb.writeToStream (os, &p); }

        njq::Kit back;
        const bool opened = njq::load (v1, back, err);
        CHECK (opened, "a version 1 file still opens (%s)", err.toRawUTF8());
        CHECK (back.kind == "kit" && back.category.isEmpty(), "and reads as an uncategorised kit");
        CHECK (back.name == k.name, "with everything it did carry");
    }

    std::printf ("\ncategories\n");
    {
        CHECK (njq::tidyCategory ("  Found sound  ") == "Found sound", "the ends are trimmed");
        CHECK (njq::tidyCategory ("Found    sound") == "Found sound", "runs of space collapse");
        CHECK (njq::tidyCategory ("Found\tsound") == "Found sound", "a tab is a space");
        CHECK (njq::tidyCategory ("clubby") == "clubby", "the capitalisation is left as typed");
        CHECK (njq::categoryKey ("Clubby") == njq::categoryKey (" clubby "), "a browser groups the two together");
        CHECK (njq::categoryKey ("Clubby") != njq::categoryKey ("Texture"), "and keeps different ones apart");
        CHECK (njq::suggestedCategories().contains ("Texture"), "the suggestions are there to start from");
        CHECK (njq::tidyCategory (juce::String::fromUTF8 ("Ritmico à la Milano")) == juce::String::fromUTF8 ("Ritmico à la Milano"),
               "an accent is not stripped");
    }

    std::printf ("\nthe assigned folders\n");
    {
        shared::Settings s;
        // nothing assigned: each kind has its own default, and they are four different places
        CHECK (s.folder (shared::Folder::Njq) == shared::Settings::defaultFolder (shared::Folder::Njq), ".NJQ starts at its default");
        CHECK (s.instruments() != s.presets(), "instruments are not in with the presets");
        CHECK (s.midis() != s.wavs(), "MIDI is not in with the WAVs");

        const auto mine = sandbox.getChildFile ("my njq folder");
        s.assign (shared::Folder::Njq, mine);
        s.save();

        // the point of the whole thing: it is still there next time something asks
        const auto reloaded = shared::Settings::load();
        CHECK (reloaded.presets() == mine, "an assigned folder is remembered");
        CHECK (reloaded.instruments() == shared::Settings::defaultFolder (shared::Folder::Njqi), "assigning one kind leaves the others alone");

        // a save that goes somewhere else is not an assignment
        const auto elsewhere = sandbox.getChildFile ("somewhere else");
        elsewhere.createDirectory();
        auto k = madeUpKit();
        juce::String err;
        njq::save (elsewhere.getChildFile ("strayed.njq"), k, err);
        CHECK (shared::Settings::load().presets() == mine, "saving one file elsewhere does not move the folder");

        // all four, remembered together
        auto t = shared::Settings::load();
        t.assign (shared::Folder::Wav, sandbox.getChildFile ("w"));
        t.assign (shared::Folder::Midi, sandbox.getChildFile ("m"));
        t.assign (shared::Folder::Njqi, sandbox.getChildFile ("i"));
        t.save();
        const auto all = shared::Settings::load();
        CHECK (all.wavs().getFileName() == "w" && all.midis().getFileName() == "m"
               && all.instruments().getFileName() == "i" && all.presets() == mine, "all four are remembered at once");
        CHECK (shared::instrumentsFolder() == all.instruments(), "instrumentsFolder() is the assigned one");

        // an assigned folder that has gone away must not fail a save
        const auto gone = sandbox.getChildFile ("unplugged");
        auto u = shared::Settings::load();
        u.assign (shared::Folder::Wav, gone.getChildFile ("deep").getChildFile ("deeper"));
        u.save();
        const auto fallback = shared::Settings::load().wavs();
        CHECK (fallback.isDirectory(), "a folder that is not there is made, or the default is used instead");

        // clearing an assignment goes back to the default, it does not empty the path
        auto c = shared::Settings::load();
        c.assigned (shared::Folder::Njq) = {};
        c.save();
        CHECK (shared::Settings::load().presets() == shared::Settings::defaultFolder (shared::Folder::Njq), "clearing one returns it to its default");
    }

    std::printf ("\nthe three trees carry the same headers\n");
    {
        // the shared headers are copied, not included across trees, so a drift is a real risk: the names below
        // are the ones every tree must agree on. If this stops compiling, a tree has been edited on its own.
        CHECK (sizeof (njq::Kit) > 0 && njq::Kit().kind == "kit", "njq::Kit has kind");
        CHECK (njq::Kit().category.isEmpty(), "njq::Kit has category");
        CHECK (std::string (shared::Settings::name (shared::Folder::Njqi)) == ".NJQi", "the four kinds are named");
    }

    sandbox.deleteRecursively();
    std::printf ("\n%s\n", failures == 0 ? "all ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
