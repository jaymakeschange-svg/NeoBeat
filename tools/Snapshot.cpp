// Renders the NeoBeat editor to PNGs, one per room and window, while a preset plays (Linux UI
// check under Xvfb). Usage: NeoBeatSnapshot outdir
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"

static void pump (NeoBeatProcessor& proc, int ms)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        for (int b = 0; b < 3; ++b) { proc.processBlock (buf, midi); midi.clear(); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "shots"));
    outDir.createDirectory();
    auto proc = std::make_unique<NeoBeatProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->loadPreset (0);
    if (auto* run = proc->apvts.getParameter ("run")) run->setValueNotifyingHost (1.0f);

    // some content in every room
    auto& P = proc->project;
    nb::Pattern pat; proc->snapshotPattern (pat);
    P.matrix.oneShot = nb::makeOneShot (32, pat); P.matrix.oneShotIndex = 32;
    P.matrix.seq = nb::makeSeq (1); P.matrix.seqIndex = 1;
    P.matrix.sample = nb::makeSample (31); P.matrix.sampleIndex = 31;
    P.fs[0].on = true; P.fs[0].src = { nb::SourceKind::Pad, 2 }; P.fs[0].p.lines = { nb::StretchLine { true, 200, 800, 0.3f, 0.0f, 0.0f, 0, 1, 0 }, nb::StretchLine { true, 3000, 9000, 0.3f, 0.12f, 0.05f, 0.2f, 1, 0 } };
    P.bd[0].on = true; P.bd[0].src = { nb::SourceKind::Pad, 0 };
    P.keymap = nb::makeKeyMap (1);
    P.padFx[2].inserts[0] = nb::CellFx::library (nb::MatrixFx::CombFilterbank);
    P.mod.routes.push_back ({ nb::ModSrc::Lfo1, "pad/2/tone", 0.4f });
    P.mod.routes.push_back ({ nb::ModSrc::Velocity, "pad/2/level", -0.2f });
    P.mod.routes.push_back ({ nb::ModSrc::Lfo2, "fs/0/stretch", 0.3f });
    auto& g = P.grid.patterns[0];
    for (int col = 0; col < 32; col += 2) g.cells[(size_t) (4 * 64 + col)] = nb::GridPattern::pack (150 + col * 3, 60 + col * 4, 128, nb::GridPattern::kOn);
    for (int col : { 0, 6, 10, 16, 22, 26 }) g.cells[(size_t) col] = nb::GridPattern::pack (230, 128, 128, nb::GridPattern::kOn);
    for (int col : { 4, 12, 20, 28 }) g.cells[(size_t) (2 * 64 + col)] = nb::GridPattern::pack (220, 128, 90, nb::GridPattern::kOn | nb::GridPattern::kBd);
    g.length = 32;
    proc->projectChanged();

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (1440, 900);
    auto* shell = dynamic_cast<ui::Shell*> (ed->getChildComponent (0));
    if (shell == nullptr) { std::printf ("no shell\n"); return 1; }
    auto& ctx = shell->ctx;
    pump (*proc, 6000);   // let the renders land

    struct Shot { const char* name; std::function<void()> set; };
    std::vector<Shot> shots {
        { "01_kit", [&] { ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 0); } },
        { "02_kit_fx", [&] { ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 1); } },
        { "03_kit_mod", [&] { ctx.room = 0; ctx.dockSrc = nb::ModSrc::Lfo2; ctx.select ({ ui::Sel::Pad, 2 }, 2); } },
        { "04_matrix_seq", [&] { ctx.room = 1; P.matrix.view = 0; ctx.select ({ ui::Sel::None, 0 }, 0); } },
        { "05_matrix_oneshot", [&] { ctx.room = 1; P.matrix.view = 1; ctx.select ({ ui::Sel::Cell, 1 }, 1); } },
        { "06_matrix_sample", [&] { ctx.room = 1; P.matrix.view = 2; ctx.select ({ ui::Sel::SampleOp, 1 }, 1); } },
        { "07_grid", [&] { ctx.room = 2; ctx.select ({ ui::Sel::Grid, 0 }, 0); } },
        { "08_keymap", [&] { ctx.room = 3; ctx.select ({ ui::Sel::Zone, 16 }, 0); } },
        { "09_friction", [&] { ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 0); ctx.openWindow (true, 0); } },
        { "10_bounce", [&] { ctx.fsOpen = false; ctx.openWindow (false, 0); } },
        { "11_routes", [&] { ctx.bdOpen = false; ctx.listOpen = true; ctx.refreshAll(); } },
    };
    for (auto& s : shots)
    {
        s.set();
        pump (*proc, 1500);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile (juce::String (s.name) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    ed.reset();
    return 0;
}
