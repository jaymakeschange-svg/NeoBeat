// Pictures of MidiJaQ's seven rooms, for looking over the layout. Run under Xvfb:
//   xvfb-run -a MidiJaQSnapshot <folder>
//
// The plugin is loaded with enough in it that each room has something real to draw: recordings cut
// into a pool, a loop made of chops, an instrument across the keys, and a phrase on the map. A room
// drawn empty tells you nothing about whether it works.
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "app/Editor.h"
#include "neobeat/shared/rooms/Rooms.h"

static void shot (juce::Component& c, const juce::File& dir, const juce::String& name)
{
    juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    { juce::Graphics g (img); c.paintEntireComponent (g, true); }
    juce::PNGImageFormat png;
    auto f = dir.getChildFile (name + ".png");
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
    if (out != nullptr) png.writeImageToStream (img, *out);
    std::printf ("  %s\n", f.getFullPathName().toRawUTF8());
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// a handful of sounds, so the rooms have something to show
static void fillPool (MjqProcessor& p)
{
    for (int i = 0; i < 12; ++i)
    {
        auto it = std::make_shared<bc::PoolItem>();
        it->name = (juce::String ("sound ") + juce::String (i + 1)).toStdString();
        it->source = "snapshot";
        it->sr = 48000.0;
        const int n = 12000;
        it->l.assign ((size_t) n, 0.0f);
        const double hz = 60.0 * std::pow (1.32, (double) i);
        for (int k = 0; k < n; ++k)
        {
            const double t = (double) k / it->sr;
            const double env = std::exp (-t * (4.0 + 0.8 * i));
            it->l[(size_t) k] = (float) (0.6 * env * std::sin (6.283185307179586 * hz * t));
        }
        it->r = it->l;
        // a sound the rooms will not use until it has been analysed, which is correct of them
        bc::analyseItem (*it, nbkstore::learner());
        // the first few are low, so the Stereoroom locks them as it would a kick
        it->f.low = i < 3 ? 0.8f : 0.1f;
        p.kit.pool.push_back (it);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto dir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String ("."));
    dir.createDirectory();

    // Optional second argument: a .nsk to dress the plugin in before anything is drawn. Applied
    // here rather than after the editor exists, so every component is built already wearing it.
    if (argc > 2)
    {
        juce::String err;
        if (nsui::applyPack (juce::File (juce::String (argv[2])), err))
            std::printf ("  skin: %s\n", nsui::skin().name.toRawUTF8());
        else
            std::printf ("  skin failed to load: %s\n", err.toRawUTF8());
    }

    auto proc = std::make_unique<MjqProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    fillPool (*proc);

    // a loop of chops, so LOOPS and the Stereoroom have nodes
    proc->loops.numLayers = 2;
    for (int layer = 0; layer < 2; ++layer)
    {
        auto& L = proc->loops.layers[(size_t) layer];
        L.bars = 1; L.perBar = 16;
        L.hits.clear();
        for (int s = 0; s < 16; s += (layer == 0 ? 4 : 3))
        {
            bc::LoopHit h;
            h.step = s;
            h.sound = proc->kit.pool[(size_t) ((s / 2 + layer * 3) % proc->kit.pool.size())];
            h.vel = 0.7f + 0.3f * (float) ((s % 4) == 0);
            L.hits.push_back (h);
        }
    }
    proc->loopsChanged();

    // an instrument across the keys
    proc->keysSource = {};
    proc->keys.mode = bc::KeysMode::Resonator;        // tunes whatever is in the pool to the keys
    proc->keysBuild();
    std::printf ("  instrument: %d zones\n", (int) proc->keys.zones.size());

    // a phrase on the map
    proc->map.steps = 16;
    proc->mapWrite();

    std::unique_ptr<MjqEditor> ed (dynamic_cast<MjqEditor*> (proc->createEditor()));
    ed->setBounds (0, 0, 1280, 800);
    ed->refresh();
    pump (150);

    const char* names[] = { "1_cut", "2_loops", "3_keys", "4_map", "5_mod", "6_random", "7_stereoroom" };
    for (int i = 0; i < 7; ++i)
    {
        ed->showRoom (i);
        ed->refresh();
        pump (120);
        shot (*ed, dir, names[i]);
    }

    // the Stereoroom again with everything spread out and one chop travelling, which is the thing
    // worth looking at: locked low elements still in the middle while the rest move
    {
        ed->showRoom (6);
        for (size_t i = 0; i < proc->places.size(); ++i)
        {
            auto& pl = proc->places[i];
            if (pl.locked) continue;
            pl.on = true;
            const float t = proc->places.size() > 1 ? (float) i / (float) (proc->places.size() - 1) : 0.5f;
            pl.x = -0.85f + 1.7f * t;
            pl.y = 0.1f + 0.5f * std::fabs (t - 0.5f) * 2.0f;
            pl.width = 0.4f;
        }
        if (proc->places.size() > 3)
        {
            auto& pl = proc->places[3];
            if (! pl.locked) { pl.travel = nsr::Travel::Sway; pl.bars = 0.25f; pl.depth = 0.9f; }
        }
        proc->stage.on = true;
        proc->stereoChanged();
        ed->refresh();
        pump (120);
        shot (*ed, dir, "8_stereoroom_spread");
    }

    std::printf ("done\n");
    return 0;
}
