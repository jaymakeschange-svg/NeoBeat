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
    // scene B: eight bars; a chain; a Layermax row that follows the kick
    {
        auto b = std::make_unique<nb::Pattern> (nb::patternPreset (7).pattern);
        const int base = b->length;
        b->length = 128;
        for (int t = 0; t < 16; ++t) for (int st = base; st < 128; ++st) b->steps[(size_t) t][(size_t) st] = b->steps[(size_t) t][(size_t) (st % base)];
        proc->pattern.loadScene (1, *b);
        P.song.chainOn = false; P.song.numLinks = 3; P.song.chain[0] = { 0, 2 }; P.song.chain[1] = { 1, 1 }; P.song.chain[2] = { 0, 1 };
        P.grid.follow[4].on = true; P.grid.follow[4].lane = 0;
        nb::OneShotCell c; c.scene = 1; c.pad = 2; c.step = 20; c.fx = nb::CellFx::library (nb::MatrixFx::Shimmer); P.matrix.oneShot.cells.push_back (c);
    }
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
        { "11_routes", [&] { ctx.bdOpen = false; ctx.listOpen = true; ctx.refreshNow(); } },
        { "12_scene_b_8bars", [&] { ctx.listOpen = false; ctx.room = 0; proc->setScene (1); ctx.viewBars = 4; ctx.viewBar = 0; ctx.select ({ ui::Sel::Node, 4 * nb::Pattern::kMaxSteps + 6 }, 0); } },
        { "13_layermax_follow", [&] { proc->setScene (0); ctx.viewBars = 2; ctx.room = 2; ctx.gridRow = 4; ctx.select ({ ui::Sel::Grid, 0 }, 1); } },
        { "14_export", [&] { ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 0); ctx.overlay = 1; } },
        { "15_midi_out", [&] { ctx.overlay = 2; } },
        { "16_import_folder", [&] { ctx.overlay = 3; ctx.overlayFile = "D:\\Samples\\Field recordings\\Kitchen"; } },
        { "17_default_sequence", [&] { ctx.overlay = 4; ctx.overlayFile = "C:\\Users\\you\\Documents\\Marbelite\\NJQ\\Kitchen kit.NJQ"; } },
        { "18_notice", [&] { ctx.overlay = 0; ui::sendNode (ctx, 2, 4, 24); } },
        { "19_jack", [&] { ctx.noticeText = {}; ctx.bandsOpen = false; ctx.room = 4; ctx.select ({ ui::Sel::Pad, 0 }, 0); } },
        { "20_jack_held", [&] { auto& t = proc->chaos.allTargets(); for (int q : { 3, 7, 11 }) t[(size_t) q].on = false;
                                proc->jack.selfRandomise = 0.5f; proc->jackApply(); proc->jackSetHeld (true); } },
        { "21_jack_roll", [&] { proc->jackSetHeld (false); proc->roll(); ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 0); } },
        { "22_bands_lane", [&] { proc->setParam (proc->paramId (2, "bands"), 0.7f); proc->setParam (proc->paramId (0, "bands"), 1.0f); ctx.select ({ ui::Sel::Pad, 2 }, 0); } },
        { "23_bands_window", [&] { ctx.bandsOpen = true; } },
        { "24_bands_all", [&] { proc->setParam ("bands_all", 0.5f); } },
        { "25_preset_bar", [&] { ctx.bandsOpen = false; proc->setParam ("bands_all", 0.0f); ctx.room = 0; ctx.select ({ ui::Sel::Pad, 2 }, 0); } },
    };
    for (auto& s : shots)
    {
        s.set();
        ctx.refreshNow();
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
